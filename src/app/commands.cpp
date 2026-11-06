#include "commands.h"

#include <QHash>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

namespace tp::app {
namespace commands {

namespace {
constexpr int kMax = 40;          // palette.js：.slice(0, 40)
constexpr int kRecent = 8;        // 最近打开最多几个（原生版）
constexpr int kEmptyPlans = 6;    // palette.js：没输入时列前 6 个计划

// JS 的 String.prototype.trim()：去掉空白和行结束符（QString::trimmed 认的空白基本一样）
QString jsTrimLower(const QString &s) { return s.trimmed().toLower(); }
} // namespace

int kindOrder(const QString &kind)
{
    if (kind == QLatin1String("plan")) return 0;
    if (kind == QLatin1String("stop")) return 1;
    if (kind == QLatin1String("page")) return 2;
    return 3;
}

int score(const Item &it, const QString &q)
{
    const QString l = it.label.toLower();
    if (l.startsWith(q)) return 0;
    if (l.contains(q)) return 1;
    if (!it.alias.isEmpty() && it.alias.toLower().contains(q)) return 1;
    if (it.sub.toLower().contains(q)) return 2;
    return 9;
}

QVector<int> electronOrder(const QVector<Item> &items, const QString &query)
{
    const QString q = jsTrimLower(query);
    struct S { int s, k, i; };
    QVector<S> v;
    for (int i = 0; i < items.size(); ++i) {
        const int s = score(items[i], q);
        if (s < 9) v.push_back({ s, kindOrder(items[i].kind), i });
    }
    std::stable_sort(v.begin(), v.end(), [](const S &a, const S &b) { return a.s != b.s ? a.s < b.s : a.k < b.k; });
    QVector<int> out;
    for (const S &x : v) {
        if (out.size() >= kMax) break;
        out << x.i;
    }
    return out;
}

QVector<Hit> grouped(const QVector<Item> &items, const QString &query, const QStringList &recentIds)
{
    QVector<Hit> out;
    const QString q = jsTrimLower(query);
    if (q.isEmpty()) {
        // 最近：按最近打开的先后，找对应的计划条目
        QHash<QString, int> planAt;
        for (int i = 0; i < items.size(); ++i)
            if (items[i].kind == QLatin1String("plan") && !planAt.contains(items[i].id)) planAt.insert(items[i].id, i);
        QSet<QString> seen;
        for (const QString &id : recentIds) {
            if (out.size() >= kRecent) break;
            const auto it = planAt.constFind(id);
            if (it == planAt.cend() || seen.contains(id)) continue;
            seen.insert(id);
            out.push_back({ it.value(), QStringLiteral("recent"), -1 });
        }
        if (out.isEmpty()) {
            for (int i = 0, n = 0; i < items.size() && n < kEmptyPlans; ++i)
                if (items[i].kind == QLatin1String("plan")) { out.push_back({ i, QStringLiteral("plan"), -1 }); ++n; }
        }
        for (const char *k : { "page", "act" })
            for (int i = 0; i < items.size(); ++i)
                if (items[i].kind == QLatin1String(k)) out.push_back({ i, QString::fromLatin1(k), -1 });
        return out;
    }
    const QVector<int> order = electronOrder(items, query);
    // 每个类别的最好分数
    QHash<QString, int> best;
    for (int i : order) {
        const int s = score(items[i], q);
        const QString k = items[i].kind;
        if (!best.contains(k) || s < best.value(k)) best.insert(k, s);
    }
    QStringList kinds = best.keys();
    std::sort(kinds.begin(), kinds.end(), [&](const QString &a, const QString &b) {
        return best.value(a) != best.value(b) ? best.value(a) < best.value(b) : kindOrder(a) < kindOrder(b);
    });
    for (const QString &k : kinds)
        for (int i : order)
            if (items[i].kind == k) out.push_back({ i, k, score(items[i], q) });
    return out;
}

} // namespace commands

QVariantList CommandRanker::rank(const QVariantList &items, const QString &query, const QStringList &recentIds) const
{
    QVector<commands::Item> v;
    v.reserve(items.size());
    for (const QVariant &x : items) {
        const QVariantMap m = x.toMap();
        v.push_back({ m.value(QStringLiteral("kind")).toString(), m.value(QStringLiteral("label")).toString(),
                      m.value(QStringLiteral("sub")).toString(), m.value(QStringLiteral("id")).toString(),
                      m.value(QStringLiteral("alias")).toString() });
    }
    QVariantList out;
    for (const commands::Hit &h : commands::grouped(v, query, recentIds)) {
        QVariantMap m = items.at(h.index).toMap();
        m.insert(QStringLiteral("group"), h.group);
        m.insert(QStringLiteral("score"), h.score);
        m.insert(QStringLiteral("index"), h.index);
        out << m;
    }
    return out;
}

} // namespace tp::app
