// MapController："已解锁"手动标记（0.8.5）。0.8.7 从 mapcontroller.cpp 拆出来，内容没改。
#include "mapcontroller.h"
#include "mapcontroller_p.h"

#include "datastore.h"
#include "settings.h"
#include "tileserver.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cmath>


namespace tp::map {

using detail::compact;
using detail::kUnlockMaxZoom;
[[maybe_unused]] static const QString &kEmptyFc = detail::emptyFc();

// ---- "已解锁"手动标记（0.8.5，Electron map-app.js v4.0：manualOf / setManual / toggleRegion / renderUnlockList / setUnlockHover） ----

void MapController::setUnlockEdit(bool on)
{
    if (on && (m_base != QLatin1String("unlock") || !m_unlockModel.isLoaded())) on = false;
    if (on == m_unlockEdit) return;
    m_unlockEdit = on;
    if (!on) setUnlockHover(QString());
    emit unlockEditChanged();
}

int MapController::manualIndexOf(int unit) const
{
    // 整块点亮的组（小国家、港澳台）：标了其中任意一块就算整块
    const QVector<UnlockModel::Manual> list = m_trips.manualRegions();
    for (int i = 0; i < list.size(); ++i)
        if (m_unlockModel.sameUnit(unit, m_unlockModel.indexOf(list[i].id))) return i;
    return -1;
}

QString MapController::unitSubText(int i) const
{
    switch (m_unlockModel.unitSub(i)) {
    case UnlockModel::Sub::China: return trd("中国");
    case UnlockModel::Sub::WholeCountry: return trd("整个国家 / 地区");
    case UnlockModel::Sub::Country: break;
    }
    return placeName(m_unlockModel.country(i));
}

bool MapController::editable()
{
    if (!m_store) return false;
    if (m_store->isReadOnly()) { emit toast(trd("现在是只读模式，不能改。"), true); return false; }
    return true;
}

QVariantList MapController::unlockRows(const QString &q) const
{
    QVariantList out;
    if (!m_unlockModel.isLoaded()) return out;
    QVector<int> units;
    if (!q.trimmed().isEmpty()) units = m_unlockModel.search(q, 120);
    else {
        // 没搜索：列出已经手动标记的（整块点亮的组只列一行）
        QSet<int> seen;
        for (const UnlockModel::Manual &m : m_trips.manualRegions()) {
            const int i = m_unlockModel.indexOf(m.id);
            if (i < 0) continue;
            bool dup = false;
            for (int j : std::as_const(seen)) if (m_unlockModel.sameUnit(i, j)) { dup = true; break; }
            if (dup) continue;
            seen.insert(i);
            units << i;
            if (units.size() >= 120) break;
        }
    }
    const QVector<UnlockModel::Manual> manual = m_trips.manualRegions();
    for (int i : units) {
        const QString id = m_unlockModel.unitId(i);
        const QStringList plans = m_unlock.plansById.value(id);
        const int mi = manualIndexOf(i);
        out << QVariantMap{ { "id", id }, { "name", placeName(m_unlockModel.unitName(i)) }, { "sub", unitSubText(i) },
                            { "byPlan", !plans.isEmpty() }, { "plans", plans.join(QStringLiteral("、")) },
                            { "manual", mi >= 0 }, { "year", mi >= 0 && manual[mi].year ? QString::number(manual[mi].year) : QString() } };
    }
    return out;
}

bool MapController::setManual(const QString &id, bool on)
{
    const int unit = m_unlockModel.indexOf(id);
    if (unit < 0 || !editable()) return false;
    if (on && !m_unlock.plansById.value(id).isEmpty()) return false;   // 已经由计划点亮
    const bool had = manualIndexOf(unit) >= 0;
    if (had == on) return false;
    m_store->modify([&](json::Value &d) {
        json::Object &root = d.mutObject();
        json::Array list;
        // 别的省（以及认不出来的 id）原样留着，连同里面的年份和不认识的字段
        for (const json::Value &r : std::as_const(d)[QStringLiteral("visitedRegions")].asArray()) {
            const json::Value *rid = r.asObject().find(QStringLiteral("id"));
            const int g = rid ? m_unlockModel.indexOf(rid->toString()) : -1;
            if (!(g >= 0 && m_unlockModel.sameUnit(unit, g))) list.push_back(r);
        }
        if (on) {
            json::Object o;
            o.set(QStringLiteral("id"), json::Value(id));
            list.push_back(json::Value(std::move(o)));
        }
        // if (list.length) DATA.visitedRegions = list; else delete DATA.visitedRegions;
        if (list.empty()) root.remove(QStringLiteral("visitedRegions"));
        else root.set(QStringLiteral("visitedRegions"), json::Value(std::move(list)));
    });
    qInfo("已解锁：手动%s %s", on ? "标记" : "取消", qUtf8Printable(id));
    if (m_onEdited) m_onEdited(700);   // Electron：700 ms 后 persist(quiet)
    m_rebuildTimer.stop();
    rebuildData();                      // 马上重新上色（不等 60 ms 的防抖），面板列表也马上对
    return true;
}

QString MapController::setManualYear(const QString &id, const QString &year)
{
    const int unit = m_unlockModel.indexOf(id);
    const int mi = unit < 0 ? -1 : manualIndexOf(unit);
    if (mi < 0) return {};
    const QString y = year.trimmed();
    static const QRegularExpression four(QStringLiteral("^\\d{4}$"));
    const bool ok = four.match(y).hasMatch();
    const QVector<UnlockModel::Manual> list = m_trips.manualRegions();
    if ((ok && list[mi].year == y.toInt()) || (!ok && !list[mi].year)) return ok ? y : QString();
    if (!editable()) return list[mi].year ? QString::number(list[mi].year) : QString();
    const QString wantId = list[mi].id;
    m_store->modify([&](json::Value &d) {
        json::Array &arr = d.mutObject().find(QStringLiteral("visitedRegions"))->mutArray();
        for (json::Value &r : arr) {
            json::Object &o = r.mutObject();
            const json::Value *rid = o.find(QStringLiteral("id"));
            if (!rid || rid->toString() != wantId) continue;
            // Electron：if (/^\d{4}$/.test(y)) r.year = y; else delete r.year;（年份存成字符串，和 Electron 一样）
            if (ok) o.set(QStringLiteral("year"), json::Value(y));
            else o.remove(QStringLiteral("year"));
            break;
        }
    });
    if (m_onEdited) m_onEdited(700);
    m_rebuildTimer.stop();
    rebuildData();
    return ok ? y : QString();
}

void MapController::toggleRegion(const QString &id)
{
    const int unit = m_unlockModel.indexOf(id);
    if (unit < 0) return;
    const QStringList plans = m_unlock.plansById.value(id);
    if (!plans.isEmpty()) {
        emit toast(trd("%1 已经由计划「%2」点亮，不用手动标记。").arg(placeName(m_unlockModel.unitName(unit)), plans.first()), false);
        return;
    }
    setManual(id, manualIndexOf(unit) < 0);
}

QString MapController::unlockIdAt(double x, double y) const
{
    if (!m_unlockModel.isLoaded()) return {};
    const geo::LatLng ll = m_cam.coordinateFor(x, y);
    return m_unlockModel.unitId(m_unlockModel.unitAt(geo::normLng(ll.lng), ll.lat));
}

QVariantMap MapController::unlockDemo() const
{
    if (!m_unlockModel.isLoaded()) return {};
    QString id;
    for (const UnlockModel::Manual &m : m_trips.manualRegions())
        if (m_unlockModel.indexOf(m.id) >= 0 && m_unlock.plansById.value(m.id).isEmpty()) { id = m.id; break; }
    if (id.isEmpty() && !m_unlock.propsById.isEmpty()) {
        QStringList ids = m_unlock.propsById.keys();
        std::sort(ids.begin(), ids.end());
        id = ids.first();
    }
    const int i = m_unlockModel.indexOf(id);
    if (i < 0) return {};
    // 外框中心可能不在省里（弯月形的省）：不在就用省里第一个顶点附近
    geo::LatLng c = m_unlockModel.unitCenter(i);
    if (!m_unlockModel.sameUnit(m_unlockModel.unitAt(c.lng, c.lat), i)) {
        const geo::LatLng probe = c;
        bool found = false;
        for (double r = 0.05; r < 3 && !found; r *= 1.6)
            for (int k = 0; k < 16 && !found; ++k) {
                const double a = k * geo::kPi / 8;
                const geo::LatLng q{ probe.lat + r * std::sin(a), probe.lng + r * std::cos(a) };
                if (m_unlockModel.sameUnit(m_unlockModel.unitAt(q.lng, q.lat), i)) { c = q; found = true; }
            }
    }
    return { { "id", id }, { "lat", c.lat }, { "lng", c.lng } };
}

QString MapController::unlockEditHover(double x, double y)
{
    if (!m_unlockEdit) return {};
    const QString id = unlockIdAt(x, y);
    setUnlockHover(id);
    if (id.isEmpty()) return {};
    const int unit = m_unlockModel.indexOf(id);
    // `<b>${unitName}</b> · ${unitSub}<br>${已由计划点亮 | 手动标记 · 点击取消 | 点击标记为去过}`
    const QString st = !m_unlock.plansById.value(id).isEmpty() ? trd("已由计划点亮")
                     : manualIndexOf(unit) >= 0 ? trd("手动标记 · 点击取消") : trd("点击标记为去过");
    return QStringLiteral("<b>%1</b> · %2<br>%3").arg(placeName(m_unlockModel.unitName(unit)).toHtmlEscaped(), unitSubText(unit).toHtmlEscaped(), st.toHtmlEscaped());
}

bool MapController::unlockEditClick(double x, double y)
{
    if (!m_unlockEdit) return false;
    const QString id = unlockIdAt(x, y);
    if (id.isEmpty()) return false;
    toggleRegion(id);
    return true;
}

void MapController::setUnlockHover(const QString &id)
{
    // Electron v5.2.0：单独一个小数据源，只放这一个省（或同一组的几块）；同一个省不重复处理
    if (id == m_unlockHoverId) return;
    m_unlockHoverId = id;
    const int unit = m_unlockModel.indexOf(id);
    m_unlockHoverJson = unit < 0 ? kEmptyFc : compact(m_unlockModel.unitFeatures(unit));
    emit unlockHoverChanged();
}


} // namespace tp::map
