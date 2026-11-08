#include "undostack.h"

namespace tp::data {

void UndoStack::reset(const json::Value &saved)
{
    m_undo.clear();
    m_redo.clear();
    m_undoIds.clear();
    m_redoIds.clear();
    m_saved = saved;
    m_savedId = ++m_nextId;
}

bool UndoStack::noteSaved(const json::Value &now)
{
    if (m_suppressed) return false;
    if (now == m_saved) return false;
    if (m_group && m_groupPushed) {   // 同一次编辑里的后续保存：不再压栈
        m_redo.clear();
        m_redoIds.clear();
        m_saved = now;
        m_savedId = ++m_nextId;
        return false;
    }
    if (m_group) m_groupPushed = true;
    m_undo.push_back(m_saved);
    m_undoIds.push_back(m_savedId);
    while (int(m_undo.size()) > kMaxSteps) { m_undo.pop_front(); m_undoIds.pop_front(); }
    m_redo.clear();
    m_redoIds.clear();
    m_saved = now;
    m_savedId = ++m_nextId;
    return true;
}

std::optional<json::Value> UndoStack::step(std::deque<json::Value> &from, std::deque<json::Value> &to,
                                           std::deque<quint64> &fromIds, std::deque<quint64> &toIds, const json::Value &current)
{
    if (from.empty()) return std::nullopt;
    json::Value snap = from.back();
    from.pop_back();
    to.push_back(current);
    toIds.push_back(m_savedId);
    m_savedId = fromIds.back();
    fromIds.pop_back();
    while (int(to.size()) > kMaxSteps) { to.pop_front(); toIds.pop_front(); }
    m_saved = snap;
    return snap;
}

void UndoStack::rollbackGroup(const json::Value &now)
{
    if (m_group && m_groupPushed && !m_undo.empty()) {
        m_undo.pop_back();
        m_savedId = m_undoIds.back();   // 放弃后回到打开编辑器之前那一版
        m_undoIds.pop_back();
    }
    m_saved = now;
    m_group = m_groupPushed = false;
}

std::optional<json::Value> UndoStack::undo(const json::Value &current) { return step(m_undo, m_redo, m_undoIds, m_redoIds, current); }
std::optional<json::Value> UndoStack::redo(const json::Value &current) { return step(m_redo, m_undo, m_redoIds, m_undoIds, current); }

} // namespace tp::data
