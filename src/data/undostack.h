// 撤销 / 重做（功能清单 D-13，0.5.0）。语义照搬 Electron map-app.js（v5.0.0 起）：
//   - 每次保存成功时，把"上一次保存时的样子"压进撤销栈（最多 30 步），清空重做栈；
//     停手 0.8 秒才自动保存，所以连续改的几下合成一步；内容没变的保存不算一步。
//   - 撤销 = 换回上一版（当前这一版压进重做栈），调用方随后立即保存；重做反过来。
//   - 撤销 / 重做之后的那次保存不再记一步（调用方用 Suppress 包住）。
// 快照直接存 json::Value：容器是写时复制的，30 份快照只多占改过的那部分（Electron 存 30 份完整 JSON 字符串）。
// 换数据文件夹、重新读取、恢复历史版本之后调用 reset()（对应 Electron 重新加载页面）。
#pragma once

#include "jsonvalue.h"

#include <QtGlobal>

#include <deque>
#include <optional>

namespace tp::data {

class UndoStack
{
public:
    static constexpr int kMaxSteps = 30;

    // 清空两个栈；saved = 现在磁盘上（刚读进来）的样子
    void reset(const json::Value &saved);
    // 保存成功后调用；now = 保存后的数据。返回 true = 记了一步
    bool noteSaved(const json::Value &now);
    // 撤销：返回要换上的那一版（没有可撤销的返回空）；current = 现在窗口里的数据
    std::optional<json::Value> undo(const json::Value &current);
    std::optional<json::Value> redo(const json::Value &current);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    int undoCount() const { return int(m_undo.size()); }
    int redoCount() const { return int(m_redo.size()); }
    // 0.13.3 每一版数据一个编号（每次记一步、撤销 / 重做换版时跟着走），AI 撤销联动按编号认，不靠步数。
    // where(id)：1 = 这一版就是现在或在撤销栈里（改动还在），-1 = 在重做栈里（被撤销了），0 = 不知道（挤出栈或换了分支）
    quint64 stateId() const { return m_savedId; }
    int where(quint64 id) const
    {
        if (!id) return 0;
        if (id == m_savedId) return 1;
        for (quint64 x : m_undoIds) if (x == id) return 1;
        for (quint64 x : m_redoIds) if (x == id) return -1;
        return 0;
    }
    // 撤销 / 重做记录里的每一版（含"上次保存时的样子"），清理照片时用：撤销回去还要用到的文件不能删（0.7.0）
    template <class F> void forEachSnapshot(F f) const
    {
        for (const json::Value &v : m_undo) f(v);
        for (const json::Value &v : m_redo) f(v);
        f(m_saved);
    }

    // 站点编辑器（0.8.9，PO 选"边改边存"）：打开到关闭之间自动保存多次，在撤销里只算一步。
    // beginGroup 之后第一次记下的那一步照常压栈，以后的保存只更新"上次保存时的样子"、不再压栈；
    // rollbackGroup（"放弃这次的修改"恢复并保存之后调用）把编辑期间压进去的那一步拿掉，撤销栈回到打开编辑器之前
    void beginGroup() { m_group = true; m_groupPushed = false; }
    void endGroup() { m_group = false; m_groupPushed = false; }
    void rollbackGroup(const json::Value &now);
    bool inGroup() const { return m_group; }

    // 撤销 / 重做后的那次保存不记一步（Electron 的 undoing 标志）
    class Suppress
    {
    public:
        explicit Suppress(UndoStack &s) : m_s(s), m_old(s.m_suppressed) { s.m_suppressed = true; }
        ~Suppress() { m_s.m_suppressed = m_old; }
        Suppress(const Suppress &) = delete;
        Suppress &operator=(const Suppress &) = delete;
    private:
        UndoStack &m_s;
        bool m_old;
    };

private:
    std::optional<json::Value> step(std::deque<json::Value> &from, std::deque<json::Value> &to,
                                    std::deque<quint64> &fromIds, std::deque<quint64> &toIds, const json::Value &current);

    std::deque<json::Value> m_undo, m_redo;
    std::deque<quint64> m_undoIds, m_redoIds;   // 和 m_undo / m_redo 一一对应
    json::Value m_saved;
    quint64 m_savedId = 0, m_nextId = 0;
    bool m_suppressed = false;
    bool m_group = false, m_groupPushed = false;
};

} // namespace tp::data
