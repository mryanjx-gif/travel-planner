// Ctrl K 命令面板的排序（0.8.8，功能清单 S-12）。只依赖 Qt Core，单元测试 tst_app 共用。
// 打分照搬 Electron v6.3.0 palette.js filter()：
//   - 输入去掉首尾空白、转小写；标题以它开头 0 分，包含 1 分，副标题包含 2 分，都不包含不列；
//   - 同分按 计划 → 站点 → 页面 → 操作（ord），再同就按原来的先后（Array.prototype.sort 是稳定的）；最多 40 条。
// 原生版（PO 0.8.8 选的方案 B）在这之上分组显示：
//   - 有输入：上面 40 条按类别分组，组的先后 = 组里最好的分数，同分再按 计划 → 站点 → 页面 → 操作；组内保持原来的先后；
//   - 没输入：最近（最近打开过的计划，最多 8 个；一个都没有时换成 Electron 的"前 6 个计划"）→ 页面 → 操作。
// 条目的文字（标题、副标题）由界面先翻译好再交给这里，所以英文界面按英文打分。
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace tp::app {

namespace commands {

struct Item
{
    QString kind;    // plan / stop / page / act
    QString label;
    QString sub;
    QString id;      // 计划 id（plan、stop），最近打开用
    QString alias;   // 0.8.10：别名（页面的旧名字：计划库、愿望清单、相册），包含就算 1 分，和名字包含一样
};

// 一项的分数（Electron score）：0 开头、1 包含（或别名包含）、2 副标题包含、9 不匹配。q 要已经是小写、去掉首尾空白
int score(const Item &it, const QString &q);
int kindOrder(const QString &kind);   // plan 0, stop 1, page 2, act 3（其他算 3）

struct Hit
{
    int index;       // 在输入里的下标
    QString group;   // recent / plan / stop / page / act
    int score;       // 没输入时为 -1
};

// Electron filter() 的结果顺序（有输入时）：返回下标，最多 40 个
QVector<int> electronOrder(const QVector<Item> &items, const QString &query);
// 原生版的分组结果（见文件头）
QVector<Hit> grouped(const QVector<Item> &items, const QString &query, const QStringList &recentIds);

} // namespace commands

// 给 QML：commands.rank(条目, 输入, 最近打开的计划) → 按显示顺序的条目（原样带回，多 group、score、index 三个键）
class CommandRanker : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QVariantList rank(const QVariantList &items, const QString &query, const QStringList &recentIds) const;
};

} // namespace tp::app
