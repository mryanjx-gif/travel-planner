#include "plancontroller.h"

#include "appcontroller.h"
#include "datastore.h"
#include "mapcontroller.h"
#include "perf.h"
#include "settings.h"
#include "thumbprovider.h"

#include <QDate>
#include <QDateTime>

namespace tp::app {

using json::Value;
namespace plans = map::plans;

namespace {
const QString kFilterKey = QStringLiteral("mapFilter");
const QString kRecentKey = QStringLiteral("recentPlans");   // 0.8.8：最近打开的计划（Ctrl K 的"最近"）
constexpr int kRecentMax = 8;
// 0.8.10 模式
const QString kModeKey = QStringLiteral("mapMode");
const QString kAlsoKey = QStringLiteral("modeAlso");
const QString kPartsKey = QStringLiteral("modeAlsoParts");

QVariant toVariant(const Value &v)
{
    switch (v.type()) {
    case Value::Type::Bool: return v.toBool();
    case Value::Type::Number: return v.toNumber();
    case Value::Type::String: return v.toString();
    case Value::Type::Array: { QVariantList l; for (const Value &x : v.asArray()) l << toVariant(x); return l; }
    case Value::Type::Object: { QVariantMap m; for (const auto &kv : v.asObject().members()) m.insert(kv.first, toVariant(kv.second)); return m; }
    default: return {};
    }
}

Value stringArray(const QStringList &l)
{
    json::Array a;
    for (const QString &s : l) a.push_back(Value(s));
    return Value(std::move(a));
}

QStringList ordered(const QSet<QString> &s, const QStringList &order)
{
    QStringList out;
    for (const QString &k : order)
        if (s.contains(k)) out << k;
    return out;
}
} // namespace

PlanController::PlanController(AppController *app, Settings *settings, data::DataStore *store, map::MapController *mapc, QObject *parent)
    : QObject(parent), m_app(app), m_settings(settings), m_store(store), m_mapc(mapc)
{
    m_mapc->setPlanFilter([this](const Value &p) { return m_solo.isEmpty() ? m_filter.onMap(p) : p[QStringLiteral("id")].toString() == m_solo; });
    m_mapc->setOverlayFilter([this](const Value &p) { return m_solo.isEmpty() && overlayPlan(p); });   // 0.9.7
    connect(m_store, &data::DataStore::contentChanged, this, [this] { refreshNow(); refreshSheet(); refreshList(); });
    // 换数据文件夹 / 重新读取：隐藏列表里已经没有的计划去掉（Electron 刷新页面时 hiddenIds 过滤掉已删除的 id）
    connect(m_app, &AppController::dataChanged, this, [this] { loadFilter(); loadRecent(); refreshNow(); refreshSheet(); });
    connect(m_mapc->markers(), &map::MarkerLayer::selectionChanged, this, [this] {
        refreshSheet();
        // 最近打开：地图上点的、列表里点的、Ctrl K 打开的都算（选中了哪个计划）
        const QString id = m_mapc->markers()->selectedPlan();
        if (!id.isEmpty()) noteRecent(id);
    });
    // 本地日期过了零点："此刻"跟着换（Electron 每次渲染都重新取 todayStr()）
    m_dayTimer.setInterval(60 * 1000);
    connect(&m_dayTimer, &QTimer::timeout, this, [this] {
        const QString t = m_todayOverride.isEmpty() ? QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) : m_todayOverride;
        if (t != m_today) refreshNow();
    });
    m_dayTimer.start();
    loadModes();
    loadFilter();
    loadRecent();
    refreshNow();
}

PlanController::~PlanController()
{
    // 地图比这里活得久（main.cpp 里先建地图）：退出时别让它再调用已经没了的筛选
    m_mapc->setPlanFilter({});
    m_mapc->setOverlayFilter({});
}

QStringList PlanController::filterStatus() const { return ordered(m_filter.status, plans::allStatuses()); }
QStringList PlanController::filterType() const { return ordered(m_filter.type, plans::allTypes()); }
QStringList PlanController::filterRegion() const { return ordered(m_filter.region, plans::allRegions()); }

void PlanController::loadFilter()
{
    QSet<QString> ids;
    for (const Value &p : m_store->data()[QStringLiteral("plans")].asArray()) ids.insert(p[QStringLiteral("id")].toString());
    const QVariantMap saved = toVariant(m_settings->value(kFilterKey)).toMap();
    m_filter = plans::Filter::fromVariant(saved, ids);
    m_filter.status = statusForMode();   // 0.8.10：状态由模式决定，存着的旧状态勾选不再用
    applyFilter(false);
}

void PlanController::applyFilter(bool save)
{
    if (save && m_persist) {
        const QVariantMap v = m_filter.toVariant();
        json::Object o;
        for (const char *k : { "status", "type", "region", "hidden" }) o.set(QString::fromLatin1(k), stringArray(v.value(QString::fromLatin1(k)).toStringList()));
        m_settings->setValue(kFilterKey, Value(std::move(o)));
        m_settings->save();
    }
    m_mapc->refilter();   // 选中的计划被筛掉时地图取消选中（Electron applyFilterChange 关掉详情面板）
    emit filterChanged();
    refreshList();
}

void PlanController::toggleFilter(const QString &group, const QString &key)
{
    if (group == QLatin1String("status")) return;   // 0.8.10 起状态归模式管；0.9.7 起也没有"也显示"了
    QSet<QString> *set = group == QLatin1String("status") ? &m_filter.status : group == QLatin1String("type") ? &m_filter.type
                       : group == QLatin1String("region") ? &m_filter.region : nullptr;
    const QStringList &allowed = group == QLatin1String("status") ? plans::allStatuses() : group == QLatin1String("type") ? plans::allTypes() : plans::allRegions();
    if (!set || !allowed.contains(key)) return;
    if (!set->remove(key)) set->insert(key);
    qInfo("筛选：%s %s → %s", qUtf8Printable(group), qUtf8Printable(key), set->contains(key) ? "显示" : "不显示");
    applyFilter();
}

void PlanController::onlyRegion(const QString &region)
{
    if (!plans::allRegions().contains(region)) return;
    m_filter.region = { region };   // 只看某个区域：区域只留这一项，状态和类型不动
    applyFilter();
}

void PlanController::filterAll()
{
    const QSet<QString> hidden = m_filter.hidden;
    m_filter = plans::Filter::all();
    m_filter.hidden = hidden;   // 全选只管类型、区域（Electron fAll 还管状态；0.8.10 起状态归模式），眼睛隐藏的不动
    m_filter.status = statusForMode();
    applyFilter();
}

void PlanController::filterNone()
{
    m_filter.type.clear();   // 0.8.10：状态归模式，不清
    m_filter.region.clear();
    applyFilter();
}

void PlanController::setHidden(const QString &planId, bool hidden)
{
    if (hidden == m_filter.hidden.contains(planId)) return;
    if (hidden) m_filter.hidden.insert(planId);
    else m_filter.hidden.remove(planId);
    applyFilter();
}

void PlanController::showAllHidden()
{
    if (m_filter.hidden.isEmpty()) return;
    m_filter.hidden.clear();
    applyFilter();
}

QVariantMap PlanController::reveal(const QString &planId)
{
    QVariantMap out{ { QStringLiteral("opened"), QStringList() }, { QStringLiteral("unhid"), false } };
    for (const Value &p : m_store->data()[QStringLiteral("plans")].asArray()) {
        if (p[QStringLiteral("id")].toString() != planId) continue;
        // 0.8.10：状态不在当前模式里时切到它的模式（0.9.7 起没有"也显示"，计划 / 想去模式也是切过去）
        const QString st = plans::statusOf(p);
        QString switched;
        if (!m_filter.status.contains(st)) {
            setMode(modeForStatus(st));
            switched = m_mode;
            m_filter.status = statusForMode();
        }
        bool unhid = false;
        QStringList opened = m_filter.reveal(p, &unhid);   // 状态已经在里面了，这里只会打开类型 / 区域、取消隐藏
        if (opened.isEmpty() && !unhid && switched.isEmpty()) return out;
        qInfo("筛选：为了显示 %s 打开了 %s%s%s", qUtf8Printable(planId), qUtf8Printable(opened.join(u'、')), unhid ? "，取消隐藏" : "",
              switched.isEmpty() ? "" : qPrintable(QStringLiteral("，切到模式 ") + switched));
        applyFilter();
        out.insert(QStringLiteral("opened"), opened);
        out.insert(QStringLiteral("unhid"), unhid);
        if (!switched.isEmpty()) out.insert(QStringLiteral("mode"), switched);
        return out;
    }
    return out;
}

// ---------------- 0.8.10 模式 ----------------

QString PlanController::modePrimary() const
{
    if (m_mode == QLatin1String("wish")) return QStringLiteral("wish");
    if (m_mode == QLatin1String("foot")) return QStringLiteral("completed");
    if (m_mode == QLatin1String("photo")) return {};
    return QStringLiteral("planning");
}

bool PlanController::overlayPlan(const Value &plan) const
{
    if (!overlay()) return false;
    const QString st = plans::statusOf(plan);
    if (m_filter.status.contains(st)) return false;   // 这个模式自己的，正常画
    if (m_overlayOff.value(m_mode).contains(modeForStatus(st))) return false;   // 0.10.5 子选项关了
    plans::Filter f = m_filter;
    f.status = QSet<QString>(plans::allStatuses().begin(), plans::allStatuses().end());
    return f.onMap(plan);   // 类型、区域、眼睛隐藏照样管
}

void PlanController::setOverlay(bool on)
{
    if (!overlayAvailable() || on == m_overlay.value(m_mode)) return;
    m_overlay[m_mode] = on;
    qInfo("叠上其他旅程（%s）：%s", qUtf8Printable(m_mode), on ? "开" : "关");
    saveModes();
    emit overlayChanged();
    applyFilter(false);
}

QVariantMap PlanController::overlayParts() const
{
    QVariantMap m;
    if (!overlayAvailable()) return m;
    for (const QString &k : { QStringLiteral("plan"), QStringLiteral("wish"), QStringLiteral("foot") })
        if (k != m_mode) m.insert(k, !m_overlayOff.value(m_mode).contains(k));
    return m;
}

void PlanController::setOverlayPart(const QString &part, bool on)
{
    if (!overlayParts().contains(part) || on != m_overlayOff.value(m_mode).contains(part)) return;
    if (on) m_overlayOff[m_mode].remove(part);
    else m_overlayOff[m_mode].insert(part);
    qInfo("叠上其他旅程（%s）：%s %s", qUtf8Printable(m_mode), qUtf8Printable(part), on ? "开" : "关");
    saveModes();
    emit overlayChanged();
    applyFilter(false);
}

void PlanController::reopenPartsForTest(const QString &settingsFile)
{
    {
        Settings s;
        s.load(settingsFile);
        s.setValue(kPartsKey, plans::overlayPartsJson(s.value(kPartsKey), m_overlayOff));
        s.save();
    }
    m_overlayOff.clear();
    Settings t;
    t.load(settingsFile);
    m_overlayOff = plans::overlayPartsOff(t.value(kPartsKey));
    emit overlayChanged();
    applyFilter(false);
}

void PlanController::setOverlayForTest(const QString &mode, bool on)
{
    m_overlay[mode] = on;
    emit overlayChanged();
    applyFilter(false);
}

void PlanController::setTestAllStatuses(bool on)
{
    m_testAll = on;
    m_filter.status = statusForMode();
    emit overlayChanged();
    applyFilter(false);
}

QString PlanController::modeForStatus(const QString &status)
{
    if (status == QLatin1String("wish")) return QStringLiteral("wish");
    if (status == QLatin1String("completed")) return QStringLiteral("foot");
    return QStringLiteral("plan");
}

QSet<QString> PlanController::statusForMode() const
{
    if (m_mode == QLatin1String("photo") || m_testAll) return QSet<QString>(plans::allStatuses().begin(), plans::allStatuses().end());
    return { modePrimary() };
}

void PlanController::setMode(const QString &mode)
{
    if (!modes().contains(mode) || mode == m_mode) return;
    qInfo("地图模式：%s → %s", qUtf8Printable(m_mode), qUtf8Printable(mode));
    perf::Scope timing("换模式（C++）", 0);   // 0.9.1
    m_mode = mode;
    m_filter.status = statusForMode();
    saveModes();
    timing.part("写设置");
    emit modeChanged();
    emit overlayChanged();
    timing.part("界面跟着变");
    applyFilter();
    timing.part("筛选 + 地图数据重建 + 列表");
}

void PlanController::setPersist(bool on)
{
    m_persist = on;
    if (on) return;
    m_overlay.clear();
    m_overlayOff.clear();
    if (m_mode != QLatin1String("plan")) { m_mode = QStringLiteral("plan"); emit modeChanged(); }
    emit overlayChanged();
    m_filter.status = statusForMode();
    applyFilter(false);
}

void PlanController::loadModes()
{
    const QString m = m_settings->value(kModeKey).toString();
    if (modes().contains(m)) m_mode = m;
    m_overlay.clear();
    // 0.9.7：modeAlso 每种模式一个开 / 关；0.8.10–0.9.6 存的是状态列表，非空 = 开
    const QVariantMap also = toVariant(m_settings->value(kAlsoKey)).toMap();
    for (auto it = also.constBegin(); it != also.constEnd(); ++it) {
        if (!modes().contains(it.key()) || it.key() == QLatin1String("photo")) continue;
        const QVariant &v = it.value();
        m_overlay[it.key()] = v.typeId() == QMetaType::Bool ? v.toBool() : !v.toList().isEmpty();
    }
    m_overlayOff = plans::overlayPartsOff(m_settings->value(kPartsKey));
    // 0.9.5：settings.json 里旧的 modeBase（每种模式记住的底图）不再读也不再写（0.9.2 起四种模式共用一个底图），原样留着
}

void PlanController::saveModes()
{
    if (!m_persist) return;
    json::Object also;
    for (const QString &m : { QStringLiteral("plan"), QStringLiteral("wish"), QStringLiteral("foot") })
        also.set(m, Value(m_overlay.value(m)));
    m_settings->setValue(kModeKey, Value(m_mode));
    m_settings->setValue(kAlsoKey, Value(std::move(also)));
    m_settings->setValue(kPartsKey, plans::overlayPartsJson(m_settings->value(kPartsKey), m_overlayOff));
    m_settings->save();
}

QVariantMap PlanController::openPlan(const QString &planId, bool fly, int stopIdx)
{
    // Electron selectPlan(id, fly, stopIdx)：先 revealPlan，再选中、聚焦、飞过去
    const QVariantMap r = reveal(planId);
    m_mapc->markers()->selectPlan(planId, stopIdx, stopIdx >= 0);
    if (fly) m_mapc->flyToPlan(planId);
    return r;
}

// ---------------- "此刻"、简易面板 ----------------

QVariantMap PlanController::demo() const
{
    QString best, bestStart, bestEnd;
    int bestN = 0;
    for (const Value &p : m_store->data()[QStringLiteral("plans")].asArray()) {
        if (plans::typeOf(p) != QLatin1String("route") || !plans::dated(p)) continue;
        int n = 0;
        for (const Value &st : p[QStringLiteral("stops")].asArray())
            if (st[QStringLiteral("lat")].isNumber()) ++n;
        // 一个月以内的旅行优先（几年的"留学往返记录"这种不像一趟旅行）
        const int len = plans::days(p[QStringLiteral("startDate")].toString(), p[QStringLiteral("endDate")].toString());
        const int score = n + (len <= 30 ? 1000 : 0);
        if (score > bestN) { bestN = score; best = p[QStringLiteral("id")].toString(); bestStart = p[QStringLiteral("startDate")].toString(); bestEnd = p[QStringLiteral("endDate")].toString(); }
    }
    if (best.isEmpty()) return {};
    QString today = plans::addDays(bestStart, 2);
    if (today > bestEnd) today = bestEnd;
    return { { QStringLiteral("planId"), best }, { QStringLiteral("today"), today }, { QStringLiteral("start"), bestStart }, { QStringLiteral("end"), bestEnd } };
}

void PlanController::setTodayOverride(const QString &day)
{
    m_todayOverride = plans::okDate(day) ? day : QString();
    refreshNow();
}

void PlanController::refreshNow()
{
    m_today = m_todayOverride.isEmpty() ? QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) : m_todayOverride;
    const QVariantMap n = plans::nowInfo(m_store->data(), m_today);
    if (n == m_now) return;
    m_now = n;
    emit nowChanged();
}

void PlanController::refreshSheet()
{
    const QString id = m_mapc->markers()->selectedPlan();
    QVariantMap s = id.isEmpty() ? QVariantMap() : plans::sheetInfo(m_store->data(), id);
    if (!s.isEmpty()) {
        s = withThumbs(s);
        s.insert(QStringLiteral("readOnly"), m_store->isReadOnly());
    }
    if (s == m_sheet) return;
    m_sheet = s;
    emit sheetChanged();
}

QVariantMap PlanController::withThumbs(QVariantMap s)
{
    if (s.isEmpty()) return s;
    // 照片缩略图的地址（缩略图服务按数据文件夹里的相对路径找文件）
    QVariantList stops = s.value(QStringLiteral("stops")).toList();
    for (QVariant &sv : stops) {
        QVariantMap sm = sv.toMap();
        QStringList thumbs;
        for (const QString &f : sm.value(QStringLiteral("photos")).toStringList())
            if (f.startsWith(QLatin1String("photos/"))) thumbs << ThumbProvider::urlFor(f, 320, 0);
        sm.insert(QStringLiteral("thumbs"), thumbs);
        sv = sm;
    }
    s.insert(QStringLiteral("stops"), stops);
    const QString cover = s.value(QStringLiteral("coverFile")).toString();
    s.insert(QStringLiteral("coverThumb"), cover.startsWith(QLatin1String("photos/")) ? ThumbProvider::urlFor(cover, 640, 0) : QString());
    return s;
}

void PlanController::setSolo(const QString &planId)
{
    if (planId == m_solo) return;
    m_solo = planId;
    m_mapc->refilter();
}

// ---------------- 0.8.8：我的旅程、Ctrl K、最近打开 ----------------

void PlanController::refreshList()
{
    const QVariantMap l = plans::listInfo(m_store->data(), m_filter, m_mode == QLatin1String("foot"));
    if (l == m_list) return;
    m_list = l;
    emit listChanged();
}

QVariantList PlanController::commandItems() const { return plans::commandItems(m_store->data()); }

void PlanController::loadRecent()
{
    QSet<QString> ids;
    for (const Value &p : m_store->data()[QStringLiteral("plans")].asArray()) ids.insert(p[QStringLiteral("id")].toString());
    QStringList r;
    for (const QVariant &v : toVariant(m_settings->value(kRecentKey)).toList()) {
        const QString id = v.toString();
        if (ids.contains(id) && !r.contains(id) && r.size() < kRecentMax) r << id;
    }
    if (r == m_recent) return;
    m_recent = r;
    emit recentChanged();
}

void PlanController::noteRecent(const QString &planId)
{
    if (planId.isEmpty() || (!m_recent.isEmpty() && m_recent.first() == planId)) return;
    m_recent.removeAll(planId);
    m_recent.prepend(planId);
    while (m_recent.size() > kRecentMax) m_recent.removeLast();
    if (m_persist) {
        m_settings->setValue(kRecentKey, stringArray(m_recent));
        m_settings->save();
    }
    emit recentChanged();
}

// ---------------- 改数据 ----------------

bool PlanController::edit(const std::function<bool(Value &)> &fn, const char *what)
{
    if (m_store->isReadOnly()) return false;
    bool changed = false;
    // 先在副本上试：没有变化就不碰数据（不标记未保存、不记撤销）
    Value copy = m_store->data();
    if (!fn(copy)) return false;
    m_store->modify([&](Value &d) { changed = fn(d); });
    if (!changed) return false;
    qInfo("计划：%s", what);
    m_app->noteChanged(true);   // Electron 都是马上 persist；撤销是一步
    return true;
}

bool PlanController::setNextTrip(const QString &planId, bool on)
{
    return edit([&](Value &d) { return plans::setNextTrip(d, planId, on); }, on ? "设为下次出发" : "取消下次出发");
}

bool PlanController::setTodoDone(const QString &planId, const QString &todoId, bool done)
{
    return edit([&](Value &d) { return plans::setTodoDone(d, planId, todoId, done); }, done ? "待办做完了" : "待办改回没做");
}

bool PlanController::togglePin(const QString &planId)
{
    const double now = double(QDateTime::currentMSecsSinceEpoch());
    return edit([&](Value &d) { return plans::togglePin(d, planId, now); }, "置顶 / 取消置顶");
}

bool PlanController::markCompleted(const QString &planId)
{
    return edit([&](Value &d) { return plans::markCompleted(d, planId); }, "标记为已完成");
}

bool PlanController::deletePlan(const QString &planId)
{
    const bool selected = m_mapc->markers()->selectedPlan() == planId;
    const bool ok = edit([&](Value &d) { return plans::deletePlan(d, planId); }, "删除计划");
    if (ok && selected) m_mapc->markers()->clearSelection();
    return ok;
}

} // namespace tp::app
