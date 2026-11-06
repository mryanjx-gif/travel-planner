#include "planeditor.h"

#include "planpage.h"

#include "appcontroller.h"
#include "datastore.h"
#include "geocoder.h"
#include "mapcontroller.h"
#include "msicons.h"
#include "netclient.h"
#include "plans.h"
#include "roadrouter.h"
#include "roads.h"
#include "translator.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <cmath>

namespace tp::app {

using json::Value;
namespace pe = tp::map::planedit;
namespace roads = tp::map::roads;

namespace {
const QString kId = QStringLiteral("id");
const QString kStops = QStringLiteral("stops");
QString str(const Value &o, const char *k) { return o[QString::fromLatin1(k)].toString(); }
QString planColorOf(const Value &p)
{
    const QString c = str(p, "color");
    if (!c.isEmpty()) return c;
    const QString st = tp::map::plans::statusOf(p);
    return st == QLatin1String("completed") ? QStringLiteral("#2b8a3e") : st == QLatin1String("wish") ? QStringLiteral("#7b61b5") : QStringLiteral("#e8590c");
}
QString base36(quint64 v)
{
    const char *d = "0123456789abcdefghijklmnopqrstuvwxyz";
    QString s;
    do { s.prepend(QLatin1Char(d[v % 36])); v /= 36; } while (v);
    return s;
}
} // namespace

PlanEditor::PlanEditor(AppController *app, data::DataStore *store, map::MapController *mapc, QObject *parent)
    : QObject(parent), m_app(app), m_store(store), m_mapc(mapc)
{
    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(400);
    connect(&m_applyTimer, &QTimer::timeout, this, &PlanEditor::applyNow);
    m_legTimer.setSingleShot(true);
    m_legTimer.setInterval(700);   // Electron scheduleLegRouting(delay = 700)
    connect(&m_legTimer, &QTimer::timeout, this, &PlanEditor::runLegs);
    connect(m_store, &data::DataStore::dirtyChanged, this, &PlanEditor::refreshSaveState);
}

QString PlanEditor::trd(const char *s) const { return tp::tr(s); }

QString PlanEditor::newId(const QString &prefix) const
{
    // Electron uid：prefix + '_' + Date.now().toString(36) + 5 位随机
    QString r;
    for (int i = 0; i < 5; ++i) r += base36(QRandomGenerator::global()->bounded(36));
    return prefix + QLatin1Char('_') + base36(quint64(QDateTime::currentMSecsSinceEpoch())) + r;
}

// ---------------- 打开 / 关闭 ----------------

QString PlanEditor::openNew()
{
    return openWith(pe::blankPlan(newId(QStringLiteral("p")), double(QDateTime::currentMSecsSinceEpoch())), true);
}

QString PlanEditor::openPlan(const QString &planId)
{
    const Value *p = pe::findPlan(m_store->data(), planId);
    if (!p) return trd("找不到这个计划");
    return openWith(*p, false);
}

QString PlanEditor::openWith(const Value &plan, bool isNew)
{
    if (m_open) return trd("请先完成或放弃当前编辑");
    if (m_app->readOnly()) return trd("数据现在只能看、不能改，没法编辑");
    if (m_roads && m_roads->running()) return trd("正在批量计算道路路线，算完再编辑（顶部可以停止）");
    // 之前没存的改动先存成一步，这次编辑在撤销里单独算一步
    if (m_store->isDirty()) m_app->saveFor(true);
    m_app->beginEditGroup();
    m_plan = plan;
    m_original = isNew ? Value() : plan;
    m_isNew = isNew;
    m_inStore = !isNew;
    m_open = true;
    m_autoRegion = tp::map::plans::regionsOf(plan).isEmpty();   // Electron：没有区域标签时跟着站点自动建议
    m_typeAuto = false;
    m_typeNote.clear();
    m_singleAsked = false;
    m_nameTouched = false;
    m_legState.clear();
    ++m_legGen;
    m_active = plan[kStops].asArray().size() == 1 ? 0 : -1;   // Electron：只有一个站点时默认展开
    clearSearch();
    m_mapc->markers()->clearSelection();
    m_mapc->leaveUnlock();   // M-09：在"已解锁"里打开编辑器，先切回上次的在线底图
    m_mapc->setDraft(planId(), m_plan);
    qInfo("编辑器：打开%s「%s」", isNew ? "新计划" : "", qUtf8Printable(str(plan, "name")));
    refreshView();
    refreshSaveState();
    scheduleLegs();
    emit openChanged();
    emit activeStopChanged();
    return {};
}

void PlanEditor::close()
{
    m_open = false;
    m_applyTimer.stop();
    m_legTimer.stop();
    m_legBusy = false;
    ++m_legGen;
    m_legState.clear();
    setPickMode(false);
    clearSearch();
    m_mapc->clearDraft();
    m_active = -1;
    emit openChanged();
    emit activeStopChanged();
}

bool PlanEditor::pristine() const
{
    return m_isNew && !m_inStore && str(m_plan, "name").trimmed().isEmpty() && m_plan[kStops].asArray().empty();
}

QVariantMap PlanEditor::finish()
{
    if (!m_open) return { { QStringLiteral("ok"), true } };
    m_applyTimer.stop();
    if (pristine()) {   // 新建了但什么都没填：直接关
        m_app->endEditGroup();
        close();
        return { { QStringLiteral("ok"), true } };
    }
    const pe::Problems pr = pe::validate(m_plan);
    if (!pr.ok()) {
        m_nameTouched = true;
        refreshView();
        refreshSaveState();
        return { { QStringLiteral("ok"), false }, { QStringLiteral("reason"), m_saveNote } };
    }
    // Electron 保存时：单一目的地但有多个站点 → 问要不要改成路线（改进 g 只管新建时自动改；这里照 Electron 问一次）
    if (!m_singleAsked && str(m_plan, "type") != QLatin1String("route") && m_plan[kStops].asArray().size() > 1) {
        m_singleAsked = true;
        return { { QStringLiteral("ok"), false }, { QStringLiteral("ask"), QStringLiteral("single") } };
    }
    applyNow();
    if (m_store->isDirty()) m_app->saveFor(true);
    m_app->endEditGroup();
    const QString id = planId();
    int failed = 0;
    for (const pe::LegRow &r : pe::legRows(m_plan))
        if (r.road && !r.badCoords && !r.computed) ++failed;
    qInfo("编辑器：完成「%s」", qUtf8Printable(str(m_plan, "name")));
    close();
    emit finished(id);
    if (failed) emit toast(trd("有 %1 段自驾/徒步没算出道路路线，暂按直线显示；可以再打开编辑器重试").arg(failed), true);
    return { { QStringLiteral("ok"), true }, { QStringLiteral("planId"), id } };
}

void PlanEditor::keepSingle() { m_singleAsked = true; }

void PlanEditor::discard()
{
    if (!m_open) return;
    m_applyTimer.stop();
    if (m_inStore) {
        const QString id = planId();
        const Value orig = m_original;
        m_store->modify([&](Value &d) {
            if (orig.isObject()) pe::putPlan(d, orig);
            else pe::removePlan(d, id);
        });
        m_app->saveFor(true);
    }
    m_app->rollbackEditGroup();
    qInfo("编辑器：放弃修改「%s」", qUtf8Printable(str(m_plan, "name")));
    close();
    emit toast(trd("已放弃这次的修改"), false);
}

QString PlanEditor::deletePlan()
{
    if (!m_open) return {};
    const QString name = str(m_plan, "name").trimmed();
    if (!m_inStore) { discard(); return name; }
    m_applyTimer.stop();
    const QString id = planId();
    m_store->modify([&](Value &d) { pe::removePlan(d, id); });
    m_app->saveFor(true);
    m_app->endEditGroup();
    qInfo("编辑器：删除计划「%s」", qUtf8Printable(name));
    close();
    return name;
}

void PlanEditor::fitPlan()
{
    if (!m_open) return;
    QVariantList pts;
    for (const Value &s : m_plan[kStops].asArray()) {
        const double lat = s[QStringLiteral("lat")].jsToNumber(), lng = s[QStringLiteral("lng")].jsToNumber();
        if (std::isfinite(lat) && std::isfinite(lng)) pts << QVariant(QVariantList{ lat, lng });
    }
    if (!pts.isEmpty()) m_mapc->fitPoints(pts, 8);
}

// ---------------- 改动 ----------------

void PlanEditor::changed(bool structural)
{
    if (!m_open) return;
    if (m_autoRegion) pe::setRegions(m_plan, pe::suggestPlanRegions(m_plan));   // Electron autoRegionsFromStops
    if (structural) m_mapc->setDraft(planId(), m_plan);
    refreshView();
    m_applyTimer.start();
    refreshSaveState();
    scheduleLegs();
}

void PlanEditor::setField(const QString &key, const QString &value)
{
    if (!m_open) return;
    if (key == QLatin1String("name")) m_nameTouched = true;
    if (!pe::setField(m_plan, key, value)) return;
    if (key == QLatin1String("type")) m_typeNote.clear();
    // 名字、备注、日期不影响地图上的样子
    changed(key == QLatin1String("type") || key == QLatin1String("status") || key == QLatin1String("markerMode") || key == QLatin1String("name"));
}

void PlanEditor::toggleRegion(const QString &region)
{
    if (!m_open) return;
    QStringList cur = tp::map::plans::regionsOf(m_plan);
    if (cur.contains(region)) cur.removeAll(region);
    else cur << region;
    m_autoRegion = false;   // 手动点过就不再自动改
    pe::setRegions(m_plan, cur);
    changed(false);
}

void PlanEditor::applyRegionSuggestion()
{
    if (!m_open) return;
    pe::setRegions(m_plan, pe::suggestPlanRegions(m_plan));
    changed(false);
}

void PlanEditor::setPlanStyle(const QString &color, const QString &shape, const QString &icon)
{
    if (m_open && pe::setStyle(m_plan, -1, color, shape, icon)) changed();
}

int PlanEditor::addStop(const QString &name, double lat, double lng)
{
    if (!m_open || !std::isfinite(lat) || !std::isfinite(lng)) return -1;
    const int i = pe::appendStop(m_plan, pe::newStop(newId(QStringLiteral("s")), name, lat, lng));
    if (m_isNew && !m_typeAuto && pe::autoRouteOnSecondStop(m_plan)) {   // 改进 g
        m_typeAuto = true;
        m_typeNote = trd("有两个站点了，已改成「路线」；想要几个独立标记，可以改回「单一目的地」");
    }
    m_active = i;   // Electron：新加的站点直接展开
    emit activeStopChanged();
    changed();
    emit toast(trd("已添加站点：%1").arg(name.isEmpty() ? trd("未命名地点") : name), false);
    return i;
}

int PlanEditor::addStopAtCenter()
{
    return addStop(QString(), m_mapc->lat(), m_mapc->lng());
}

void PlanEditor::removeStop(int i)
{
    if (!m_open || !pe::removeStop(m_plan, i)) return;
    if (m_active == i) m_active = -1;
    else if (m_active > i) --m_active;
    emit activeStopChanged();
    changed();
}

void PlanEditor::moveStop(int from, int to)
{
    if (!m_open || !pe::moveStop(m_plan, from, to)) return;
    if (m_active == from) m_active = to;
    else if (from < to && m_active > from && m_active <= to) --m_active;
    else if (from > to && m_active >= to && m_active < from) ++m_active;
    emit activeStopChanged();
    changed();
}

void PlanEditor::setStopField(int i, const QString &key, const QString &value)
{
    if (m_open && pe::setStopField(m_plan, i, key, value)) changed(key == QLatin1String("name"));
}

QString PlanEditor::saveSched(int i, int j, const QString &date, const QString &time, const QString &end, const QString &text)
{
    Value *sv = m_open ? m_plan.mutObject().find(kStops) : nullptr;
    if (!sv || !sv->isArray() || i < 0 || i >= int(sv->asArray().size())) return QStringLiteral("empty");
    const QString err = tp::map::plans::putSchedule(sv->mutArray()[i], j, date, time, end, text);
    if (err.isEmpty()) changed(false);
    return err;
}

bool PlanEditor::deleteSched(int i, int j)
{
    Value *sv = m_open ? m_plan.mutObject().find(kStops) : nullptr;
    if (!sv || !sv->isArray() || i < 0 || i >= int(sv->asArray().size())) return false;
    if (!tp::map::plans::removeSchedule(sv->mutArray()[i], j)) return false;
    changed(false);
    return true;
}

void PlanEditor::setStopLatLng(int i, double lat, double lng)
{
    if (m_open && pe::setStopLatLng(m_plan, i, lat, pe::normLng(lng))) changed();
}

bool PlanEditor::setStopCoord(int i, const QString &lat, const QString &lng)
{
    // Electron：+e.target.value（空 = 0，非数字 = NaN → 坐标无效，校验时标红）
    bool a = true, b = true;
    const double la = lat.trimmed().isEmpty() ? 0 : lat.trimmed().toDouble(&a);
    const double ln = lng.trimmed().isEmpty() ? 0 : lng.trimmed().toDouble(&b);
    if (!a || !b || !m_open) return false;
    json::Array &stops = m_plan.mutObject().find(kStops)->mutArray();
    if (i < 0 || i >= int(stops.size())) return false;
    json::Object &so = stops[i].mutObject();
    // 手动输入时不归一经度（超出范围就标红，Electron 一样）
    so.set(QStringLiteral("lat"), Value(la));
    so.set(QStringLiteral("lng"), Value(ln));
    changed();
    return true;
}

void PlanEditor::setStopStyle(int i, const QString &color, const QString &shape, const QString &icon)
{
    if (m_open && pe::setStyle(m_plan, i, color, shape, icon)) changed();
}

void PlanEditor::resetStopStyle(int i) { setStopStyle(i, {}, {}, {}); }

void PlanEditor::setLegMode(int i, const QString &mode)
{
    if (!m_open) return;
    const json::Array &stops = m_plan[kStops].asArray();
    if (i < 0 || i >= int(stops.size())) return;
    m_legState.remove(str(stops[i], "id"));
    if (pe::setLegMode(m_plan, i, mode)) changed();
}

void PlanEditor::retryLeg(int i)
{
    if (!m_open) return;
    for (const pe::LegRow &r : pe::legRows(m_plan)) {
        if (r.from != i) continue;
        m_legState.remove(str(m_plan[kStops].asArray()[i], "id"));
        if (m_roads && !r.key.isEmpty()) m_roads->forget(r.key);
        refreshView();
        m_legTimer.start(0);
        return;
    }
}

void PlanEditor::flyToStop(int i)
{
    const json::Array &stops = m_plan[kStops].asArray();
    if (!m_open || i < 0 || i >= int(stops.size())) return;
    const double lat = stops[i][QStringLiteral("lat")].jsToNumber(), lng = stops[i][QStringLiteral("lng")].jsToNumber();
    if (std::isfinite(lat) && std::isfinite(lng)) m_mapc->fitPoints({ QVariant(QVariantList{ lat, lng }) }, 10);
}

void PlanEditor::setActiveStop(int i)
{
    if (i == m_active) return;
    m_active = i;
    emit activeStopChanged();
}

void PlanEditor::setPickMode(bool on)
{
    if (on && !m_open) return;
    if (on == m_pick) return;
    m_pick = on;
    emit pickModeChanged();
}

// ---------------- 写进数据（边改边存） ----------------

void PlanEditor::applyNow()
{
    if (!m_open) return;
    if (m_app->readOnly()) { refreshSaveState(); return; }
    if (!pe::validate(m_plan).ok()) { refreshSaveState(); return; }
    // Electron 保存时：名称去掉首尾空白；站点名空 → "未命名地点"（数据里写的就是这几个字，Electron 一样）
    Value out = m_plan;
    {
        json::Object &po = out.mutObject();
        po.set(QStringLiteral("name"), Value(str(out, "name").trimmed()));
        json::Value *sv = po.find(kStops);
        if (sv && sv->isArray())
            for (Value &s : sv->mutArray()) {
                const QString n = str(s, "name").trimmed();
                s.mutObject().set(QStringLiteral("name"), Value(n.isEmpty() ? QStringLiteral("未命名地点") : n));
            }
    }
    const Value *cur = pe::findPlan(m_store->data(), planId());
    if (!cur || !(*cur == out)) {
        m_store->modify([&](Value &d) { pe::putPlan(d, out); });
        m_inStore = true;
        m_app->noteChanged(false);
    }
    refreshSaveState();
}

void PlanEditor::refreshSaveState()
{
    QString st, note;
    if (!m_open) st = QStringLiteral("saved");
    else if (m_app->readOnly()) { st = QStringLiteral("readonly"); note = trd("数据现在只能看、不能改"); }
    else {
        const pe::Problems pr = pe::validate(m_plan);
        if (!pr.ok()) {
            const bool onlyMissing = !pr.dateOrder && pr.badCoords.isEmpty();
            if (m_isNew && !m_inStore && onlyMissing) {
                st = QStringLiteral("waiting");
                note = trd("填好名称、加上至少一个站点后自动保存");
            } else {
                st = QStringLiteral("invalid");
                note = pr.nameEmpty ? trd("名称还没填，先不保存") : pr.dateOrder ? trd("结束日期早于开始日期，先不保存")
                     : pr.noStops ? trd("至少需要一个站点，先不保存") : trd("有站点的坐标无效，先不保存");
            }
        } else if (m_applyTimer.isActive() || m_store->isDirty()) st = QStringLiteral("pending");
        else st = QStringLiteral("saved");
    }
    if (st == m_saveState && note == m_saveNote) return;
    m_saveState = st;
    m_saveNote = note;
    emit saveStateChanged();
}

// ---------------- 给界面的内容 ----------------

void PlanEditor::refreshView()
{
    if (!m_open) { m_view.clear(); emit planChanged(); return; }
    const Value &p = m_plan;
    const bool route = str(p, "type") == QLatin1String("route");
    const pe::Problems pr = pe::validate(p);
    const QSet<QString> bad(pr.badCoords.begin(), pr.badCoords.end());
    const QString pcolor = planColorOf(p);
    const QString pshape = str(p, "shape").isEmpty() ? (route ? QStringLiteral("circle") : QStringLiteral("pin")) : str(p, "shape");
    const QString mode = str(p, "markerMode").isEmpty() ? QStringLiteral("number") : str(p, "markerMode");

    QHash<int, pe::LegRow> legs;
    for (const pe::LegRow &r : pe::legRows(p)) legs.insert(r.from, r);

    QVariantList stops;
    bool anyIcon = !str(p, "icon").isEmpty();
    const json::Array &arr = p[kStops].asArray();
    for (int i = 0; i < int(arr.size()); ++i) {
        const Value &s = arr[i];
        if (!str(s, "icon").isEmpty()) anyIcon = true;
        QVariantMap m;
        const QString sid = str(s, "id");
        m.insert(QStringLiteral("id"), sid);
        m.insert(QStringLiteral("num"), i + 1);
        m.insert(QStringLiteral("name"), str(s, "name"));
        const Value &la = s[QStringLiteral("lat")], &ln = s[QStringLiteral("lng")];
        m.insert(QStringLiteral("lat"), la.isNumber() ? QVariant(la.toNumber()) : QVariant(la.toString()));
        m.insert(QStringLiteral("lng"), ln.isNumber() ? QVariant(ln.toNumber()) : QVariant(ln.toString()));
        m.insert(QStringLiteral("arriveDate"), str(s, "arriveDate"));
        m.insert(QStringLiteral("departDate"), str(s, "departDate"));
        m.insert(QStringLiteral("notes"), str(s, "notes"));
        m.insert(QStringLiteral("color"), str(s, "color"));
        m.insert(QStringLiteral("shape"), str(s, "shape"));
        m.insert(QStringLiteral("icon"), str(s, "icon"));
        m.insert(QStringLiteral("effColor"), str(s, "color").isEmpty() ? pcolor : str(s, "color"));
        m.insert(QStringLiteral("effShape"), !str(s, "shape").isEmpty() ? str(s, "shape") : !str(p, "shape").isEmpty() ? str(p, "shape") : QStringLiteral("circle"));
        m.insert(QStringLiteral("effIcon"), !str(s, "icon").isEmpty() ? str(s, "icon") : str(p, "icon"));
        m.insert(QStringLiteral("own"), !str(s, "color").isEmpty() || !str(s, "shape").isEmpty() || !str(s, "icon").isEmpty());
        m.insert(QStringLiteral("bad"), bad.contains(sid));
        m.insert(QStringLiteral("counts"), pe::stopCounts(m_store->data(), str(p, "id"), s));
        m.insert(QStringLiteral("days"), tp::map::plans::schedDays(s, QString()));
        if (legs.contains(i)) {
            const pe::LegRow &r = legs.value(i);
            QVariantMap l{ { QStringLiteral("mode"), r.mode }, { QStringLiteral("km"), r.straightKm } };
            QString state;
            if (!r.road) state = QStringLiteral("line");
            else if (r.badCoords) state = QStringLiteral("bad");
            else if (r.computed) { state = QStringLiteral("road"); l.insert(QStringLiteral("dist"), r.dist); l.insert(QStringLiteral("time"), r.time); }
            else {
                const LegState ls = m_legState.value(sid);
                const bool remembered = m_roads && m_roads->remembered(r.key);
                if ((ls.key == r.key && ls.status == QLatin1String("fail")) || remembered) {
                    state = QStringLiteral("fail");
                    l.insert(QStringLiteral("msg"), ls.key == r.key && !ls.msg.isEmpty() ? ls.msg : (m_roads ? m_roads->failReason(r.key) : QString()));
                } else if (ls.key == r.key && ls.status == QLatin1String("loading")) state = QStringLiteral("loading");
                else state = QStringLiteral("wait");
            }
            l.insert(QStringLiteral("state"), state);
            m.insert(QStringLiteral("leg"), l);
        }
        stops << m;
    }

    QVariantMap v;
    v.insert(QStringLiteral("id"), str(p, "id"));
    v.insert(QStringLiteral("name"), str(p, "name"));
    v.insert(QStringLiteral("type"), route ? QStringLiteral("route") : QStringLiteral("single"));
    v.insert(QStringLiteral("status"), tp::map::plans::statusOf(p));
    v.insert(QStringLiteral("regions"), tp::map::plans::regionsOf(p));
    v.insert(QStringLiteral("startDate"), str(p, "startDate"));
    v.insert(QStringLiteral("endDate"), str(p, "endDate"));
    v.insert(QStringLiteral("notes"), str(p, "notes"));
    v.insert(QStringLiteral("color"), str(p, "color"));
    v.insert(QStringLiteral("effColor"), pcolor);
    v.insert(QStringLiteral("shape"), str(p, "shape"));
    v.insert(QStringLiteral("effShape"), pshape);
    v.insert(QStringLiteral("icon"), str(p, "icon"));
    v.insert(QStringLiteral("markerMode"), mode);
    // Electron renderPlanStyle 的两句提示
    v.insert(QStringLiteral("styleNote"), route && mode == QLatin1String("icon") && !anyIcon
                                              ? trd("路线标记选了「图标」，但计划和站点都还没设图标，现在会显示成空白标记。")
                                          : route && mode == QLatin1String("number") && anyIcon
                                              ? trd("已设置的图标要把「路线标记」切到「图标」才会在地图上显示。") : QString());
    const QStringList sug = pe::suggestPlanRegions(p);
    const QStringList cur = tp::map::plans::regionsOf(p);
    v.insert(QStringLiteral("regionSuggest"), sug);
    v.insert(QStringLiteral("autoRegion"), m_autoRegion);
    v.insert(QStringLiteral("regionSame"), QSet<QString>(sug.begin(), sug.end()) == QSet<QString>(cur.begin(), cur.end()));
    v.insert(QStringLiteral("stops"), stops);
    v.insert(QStringLiteral("cover"), !str(p, "cover").isEmpty());
    m_view = v;

    // 问题（Electron validateEditor 的句子）。新计划名称没碰过时不标红
    QVariantMap pv;
    if (pr.nameEmpty && (m_nameTouched || !m_isNew)) pv.insert(QStringLiteral("name"), trd("请填写计划名称"));
    if (pr.dateOrder) pv.insert(QStringLiteral("date"), trd("结束日期早于开始日期"));
    if (pr.noStops && !m_isNew) pv.insert(QStringLiteral("stops"), trd("至少需要一个站点"));
    if (!pr.badCoords.isEmpty()) {
        QStringList names;
        for (const Value &s : arr)
            if (bad.contains(str(s, "id"))) names << (str(s, "name").isEmpty() ? trd("未命名地点") : str(s, "name"));
        pv.insert(QStringLiteral("coords"), trd("坐标无效（纬度 -90~90，经度 -180~180）：%1").arg(names.join(trd("、"))));
    }
    m_problemsView = pv;

    const pe::Stats st = pe::stats(p);
    m_statsView = { { QStringLiteral("stops"), st.stops }, { QStringLiteral("km"), st.km }, { QStringLiteral("approx"), st.approx },
                    { QStringLiteral("driveSec"), st.driveSec }, { QStringLiteral("walkSec"), st.walkSec },
                    { QStringLiteral("hasDrive"), st.hasDrive }, { QStringLiteral("hasWalk"), st.hasWalk },
                    { QStringLiteral("driveApprox"), st.driveApprox }, { QStringLiteral("walkApprox"), st.walkApprox },
                    { QStringLiteral("route"), route } };
    emit planChanged();
}

// ---------------- 编辑时自动算单段（M-18） ----------------

int PlanEditor::legsPending() const
{
    if (!m_open) return 0;
    int n = 0;
    for (const pe::LegRow &r : pe::legRows(m_plan)) {
        if (!r.road || r.badCoords || r.computed) continue;
        const LegState ls = m_legState.value(str(m_plan[kStops].asArray()[r.from], "id"));
        if (ls.key == r.key && ls.status == QLatin1String("fail")) continue;
        if (m_roads && m_roads->remembered(r.key)) continue;
        ++n;
    }
    return n;
}

void PlanEditor::scheduleLegs()
{
    if (!m_open || m_legBusy) return;
    if (legsPending() > 0) m_legTimer.start();
}

void PlanEditor::runLegs()
{
    if (!m_open || m_legBusy) return;
    for (const pe::LegRow &r : pe::legRows(m_plan)) {
        if (!r.road || r.badCoords || r.computed) continue;
        const QString sid = str(m_plan[kStops].asArray()[r.from], "id");
        const LegState ls = m_legState.value(sid);
        if (ls.key == r.key && ls.status == QLatin1String("fail")) continue;
        if (m_roads && m_roads->remembered(r.key)) continue;
        const json::Array &stops = m_plan[kStops].asArray();
        const double alat = stops[r.from][QStringLiteral("lat")].jsToNumber(), alng = stops[r.from][QStringLiteral("lng")].jsToNumber();
        const double blat = stops[r.from + 1][QStringLiteral("lat")].jsToNumber(), blng = stops[r.from + 1][QStringLiteral("lng")].jsToNumber();
        m_legState.insert(sid, { QStringLiteral("loading"), r.key, {} });
        m_legBusy = true;
        refreshView();
        const QUrl url = roads::osrmUrl(r.mode, alat, alng, blat, blng);
        const int gen = m_legGen;
        const QString key = r.key, mode = r.mode;
        auto done = [this, gen, sid, key, mode](int status, const QByteArray &body, int netError) {
            if (gen != m_legGen) return;
            onLegResult(sid, key, mode, status, body, netError);
        };
        if (m_fetch) m_fetch(url, done);
        else if (m_net) m_net->get(url, roads::kTimeoutMs, this, [done](const services::NetClient::Reply &rep) { done(rep.status, rep.body, int(rep.error)); });
        else done(0, {}, int(services::NetClient::Error::Network));
        return;
    }
}

void PlanEditor::onLegResult(const QString &stopId, const QString &key, const QString &mode, int status, const QByteArray &body, int netError)
{
    using Err = services::NetClient::Error;
    m_legBusy = false;
    const bool haveBody = !body.isEmpty() && (Err(netError) == Err::None || Err(netError) == Err::Http);
    roads::OsrmResult r;
    QString msg;
    bool ok = false;
    if (haveBody) {
        r = roads::parseOsrm(body, status);
        if (r.kind == roads::OsrmResult::Ok) ok = true;
        else msg = m_roads ? m_roads->errorText(netError, &r, mode) : QString();
    } else if (Err(netError) == Err::Http) {
        r.kind = roads::OsrmResult::BadReply;
        r.detail = QString::number(status);
        msg = m_roads ? m_roads->errorText(netError, &r, mode) : QString();
    } else {
        msg = m_roads ? m_roads->errorText(netError, nullptr, mode) : QString();
    }
    if (ok) {
        m_legState.remove(stopId);
        // 请求期间站点可能又被拖动 / 改了方式：只有还对得上才写入（Electron 一样）
        const json::Array &stops = m_plan[kStops].asArray();
        int idx = -1;
        for (int i = 0; i < int(stops.size()); ++i)
            if (str(stops[i], "id") == stopId) { idx = i; break; }
        bool wrote = false;
        if (idx >= 0) {
            roads::Pending leg;
            leg.planId = planId();
            leg.stopIndex = idx;
            leg.mode = mode;
            leg.key = key;
            json::Object ro;
            ro.set(QStringLiteral("plans"), Value(json::Array{ m_plan }));
            Value root(std::move(ro));
            if (roads::applyLeg(root, leg, r.path, r.dist, r.time)) {
                m_plan = root[QStringLiteral("plans")].asArray().front();
                wrote = true;
            }
        }
        qInfo("编辑器：一段道路路线算好了（%s，%.0f 米%s）", qUtf8Printable(mode), r.dist, wrote ? "" : "；站点已经变了，没写");
        emit legDone(key, true);
        if (wrote) { changed(); return; }
    } else {
        m_legState.insert(stopId, { QStringLiteral("fail"), key, msg });
        if (m_roads) m_roads->noteEditorFail(key, msg, r.permanent());
        qInfo("编辑器：一段道路路线没算出来：%s", qUtf8Printable(msg));
        emit legDone(key, false);
    }
    refreshView();
    scheduleLegs();
}

// ---------------- 地名搜索、点选 ----------------

void PlanEditor::search(const QString &q)
{
    if (!m_open || !m_geo) return;
    const QString t = q.trimmed();
    if (t.isEmpty()) { clearSearch(); return; }
    const int gen = ++m_searchGen;
    m_searchState = QStringLiteral("busy");
    m_searchError.clear();
    m_results.clear();
    m_hoverResult = -1;
    emit searchChanged();
    m_geo->search(t, this, [this, gen](const QVariantList &rows, const QString &err, bool) {
        if (gen != m_searchGen || !m_open) return;
        m_results = rows;
        m_searchError = err;
        m_searchState = !err.isEmpty() ? QStringLiteral("error") : rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("done");
        emit searchChanged();
        showResults();
    });
}

// PlanEditor 方案 B：结果标在地图上。一个都不在眼前时把镜头移过去框住它们（最多缩放 10），否则不动镜头
void PlanEditor::showResults()
{
    if (!m_mapc || m_results.isEmpty()) return;
    QVariantList pts;
    for (const QVariant &v : std::as_const(m_results)) {
        const QVariantMap r = v.toMap();
        pts.push_back(QVariantList{ r.value(QStringLiteral("lat")), r.value(QStringLiteral("lng")) });
    }
    if (!m_mapc->anyVisible(pts)) m_mapc->fitPoints(pts, 10);
}

void PlanEditor::clearSearch()
{
    ++m_searchGen;
    if (m_searchState == QLatin1String("idle") && m_results.isEmpty()) return;
    m_searchState = QStringLiteral("idle");
    m_searchError.clear();
    m_results.clear();
    m_hoverResult = -1;
    emit searchChanged();
    emit hoverResultChanged();
}

int PlanEditor::addResult(int i)
{
    if (i < 0 || i >= m_results.size()) return -1;
    const QVariantMap r = m_results[i].toMap();
    const int idx = addStop(r.value(QStringLiteral("name")).toString(), r.value(QStringLiteral("lat")).toDouble(), r.value(QStringLiteral("lng")).toDouble());
    clearSearch();
    return idx;
}

void PlanEditor::pickAt(double lat, double lng)
{
    if (!m_open) return;
    const int i = addStop(QString(), lat, lng);
    if (i < 0 || !m_geo) return;
    // 试着反查地名；失败就保持"未命名地点"，不影响使用（Electron 一样）
    const QString sid = str(m_plan[kStops].asArray()[i], "id");
    const int gen = m_legGen;
    m_geo->reverse(lat, lng, this, [this, sid, gen](const QString &name) {
        if (!m_open || gen != m_legGen || name.isEmpty()) return;
        const json::Array &stops = m_plan[kStops].asArray();
        for (int k = 0; k < int(stops.size()); ++k)
            if (str(stops[k], "id") == sid && str(stops[k], "name").isEmpty()) { setStopField(k, QStringLiteral("name"), name); return; }
    });
}

// ---------------- 样式选择器的数据 ----------------

QStringList PlanEditor::presetColors() const
{
    // Electron PRESET_COLORS
    return { QStringLiteral("#2a78d6"), QStringLiteral("#eb6834"), QStringLiteral("#1baf7a"), QStringLiteral("#eda100"),
             QStringLiteral("#e87ba4"), QStringLiteral("#008300"), QStringLiteral("#4a3aa7"), QStringLiteral("#e34948") };
}

QString PlanEditor::statusColor(const QString &status) const
{
    return status == QLatin1String("completed") ? QStringLiteral("#2b8a3e") : status == QLatin1String("wish") ? QStringLiteral("#7b61b5") : QStringLiteral("#e8590c");
}

QString PlanEditor::iconChar(const QString &name) const
{
    if (name.isEmpty()) return {};
    const char32_t cp = tp::map::msicons::codepoint(name.toLatin1().constData());
    return cp ? QString::fromUcs4(&cp, 1) : QString();
}

QVariantList PlanEditor::iconGroups() const
{
    // Electron ICON_GROUPS 原样（名字、分组、标签）；码位来自打包的图标字体子集（msicons.h），字体里没有的不列
    struct I { const char *name; QString label; };
    struct G { QString group; std::vector<I> icons; };
    const std::vector<G> groups = {
        { trd("标记"), { { "location_on", trd("位置") }, { "star", trd("星标") }, { "favorite", trd("喜欢") }, { "flag", trd("旗帜") }, { "push_pin", trd("图钉") },
                         { "bookmark", trd("书签") }, { "check_circle", trd("已完成") }, { "help", trd("待定") }, { "priority_high", trd("注意") },
                         { "visibility", trd("观景") }, { "home", trd("家") }, { "work", trd("工作") }, { "school", trd("学校") }, { "info", trd("信息") },
                         { "warning", trd("警告") } } },
        { trd("交通"), { { "flight", trd("飞机") }, { "local_airport", trd("机场") }, { "train", trd("火车") }, { "tram", trd("电车") },
                         { "directions_subway", trd("地铁") }, { "directions_bus", trd("巴士") }, { "directions_car", trd("自驾") }, { "local_taxi", trd("出租车") },
                         { "airport_shuttle", trd("房车 接驳") }, { "directions_boat", trd("轮渡 船") }, { "sailing", trd("帆船") }, { "anchor", trd("港口") },
                         { "directions_bike", trd("自行车") }, { "two_wheeler", trd("摩托") }, { "moped", trd("电驴") }, { "electric_scooter", trd("滑板车") },
                         { "directions_walk", trd("步行") }, { "gondola_lift", trd("缆车") }, { "local_gas_station", trd("加油") }, { "local_parking", trd("停车") },
                         { "luggage", trd("行李") } } },
        { trd("住宿"), { { "hotel", trd("酒店") }, { "bed", trd("床位 青旅") }, { "cottage", trd("民宿") }, { "cabin", trd("小木屋") }, { "villa", trd("别墅") },
                         { "holiday_village", trd("度假村") }, { "houseboat", trd("船屋") }, { "camping", trd("露营 帐篷") }, { "apartment", trd("公寓") },
                         { "house", trd("住宅") } } },
        { trd("餐饮"), { { "restaurant", trd("餐厅") }, { "local_cafe", trd("咖啡") }, { "coffee", trd("咖啡杯") }, { "bakery_dining", trd("面包") },
                         { "ramen_dining", trd("拉面") }, { "rice_bowl", trd("米饭") }, { "lunch_dining", trd("汉堡") }, { "dinner_dining", trd("正餐") },
                         { "set_meal", trd("套餐") }, { "local_pizza", trd("披萨") }, { "icecream", trd("冰淇淋") }, { "local_bar", trd("酒吧") },
                         { "wine_bar", trd("葡萄酒") }, { "liquor", trd("烈酒") }, { "local_drink", trd("饮品") }, { "tapas", trd("小吃") } } },
        { trd("景点文化"), { { "museum", trd("博物馆") }, { "account_balance", trd("古建筑") }, { "temple_buddhist", trd("佛寺") }, { "temple_hindu", trd("印度教寺庙") },
                             { "church", trd("教堂") }, { "mosque", trd("清真寺") }, { "synagogue", trd("犹太会堂") }, { "castle", trd("城堡") },
                             { "fort", trd("要塞 长城") }, { "location_city", trd("城市") }, { "stadium", trd("体育场") }, { "theater_comedy", trd("剧院") },
                             { "attractions", trd("游乐园") }, { "festival", trd("节庆") }, { "music_note", trd("音乐") }, { "nightlife", trd("夜生活") },
                             { "palette", trd("艺术") }, { "local_library", trd("图书馆") }, { "photo_camera", trd("拍照") }, { "camera", trd("相机") },
                             { "travel_explore", trd("探索") } } },
        { trd("自然风光"), { { "landscape", trd("山景") }, { "terrain", trd("地形") }, { "filter_hdr", trd("山峰") }, { "mountain_flag", trd("登顶 雪山") },
                             { "volcano", trd("火山") }, { "landslide", trd("峡谷") }, { "forest", trd("森林") }, { "park", trd("公园") }, { "nature", trd("树") },
                             { "local_florist", trd("花") }, { "eco", trd("生态") }, { "beach_access", trd("海滩") }, { "waves", trd("海浪") },
                             { "water", trd("湖 水域") }, { "water_drop", trd("泉水") }, { "pool", trd("泳池") }, { "hot_tub", trd("温泉") }, { "spa", trd("水疗") } } },
        { trd("户外运动"), { { "hiking", trd("徒步") }, { "nordic_walking", trd("健走") }, { "directions_run", trd("跑步") }, { "pedal_bike", trd("骑行") },
                             { "kayaking", trd("皮划艇") }, { "rowing", trd("划船") }, { "surfing", trd("冲浪") }, { "scuba_diving", trd("潜水") },
                             { "phishing", trd("钓鱼") }, { "downhill_skiing", trd("滑雪") }, { "snowboarding", trd("单板") }, { "sledding", trd("雪橇") },
                             { "ice_skating", trd("滑冰") }, { "snowmobile", trd("雪地摩托") }, { "paragliding", trd("滑翔伞") }, { "skateboarding", trd("滑板") },
                             { "golf_course", trd("高尔夫") }, { "sports_soccer", trd("足球") }, { "sports_basketball", trd("篮球") },
                             { "sports_motorsports", trd("赛车") }, { "fitness_center", trd("健身") }, { "self_improvement", trd("冥想") } } },
        { trd("天气天象"), { { "sunny", trd("晴天 日出") }, { "nights_stay", trd("夜晚 星空 极光") }, { "ac_unit", trd("雪") }, { "sunny_snowing", trd("雨雪") },
                             { "thunderstorm", trd("雷雨") }, { "cloud", trd("云") } } },
        { trd("动物"), { { "pets", trd("动物 宠物") }, { "cruelty_free", trd("兔子") }, { "emoji_nature", trd("昆虫") }, { "flutter_dash", trd("鸟") },
                         { "bug_report", trd("虫子") }, { "egg", trd("蛋") } } },
        { trd("购物生活"), { { "shopping_bag", trd("购物") }, { "storefront", trd("商店") }, { "local_mall", trd("商场") }, { "local_grocery_store", trd("超市") },
                             { "local_pharmacy", trd("药店") }, { "local_hospital", trd("医院") }, { "atm", trd("ATM") }, { "currency_exchange", trd("换汇") },
                             { "wc", trd("厕所") }, { "wifi", trd("WiFi") }, { "schedule", trd("时间") }, { "event", trd("日程") }, { "error", trd("问题") } } },
    };
    QVariantList out;
    for (const G &g : groups) {
        QVariantList icons;
        for (const I &i : g.icons) {
            const char32_t cp = tp::map::msicons::codepoint(i.name);
            if (!cp) continue;
            icons << QVariantMap{ { QStringLiteral("name"), QString::fromLatin1(i.name) }, { QStringLiteral("label"), i.label },
                                  { QStringLiteral("cp"), QString::fromUcs4(&cp, 1) } };
        }
        out << QVariantMap{ { QStringLiteral("group"), g.group }, { QStringLiteral("icons"), icons } };
    }
    return out;
}

} // namespace tp::app
