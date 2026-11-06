#include "navhistory.h"

namespace tp::app {

NavHistory::NavHistory(QObject *parent) : QObject(parent)
{
    m_hist.push_back({ QStringLiteral("map"), {} });   // 启动时在地图页（Electron 启动时 go(上次的页面)；原生版总是从地图开始）
    m_idx = 0;
}

void NavHistory::go(const QString &page, const QVariantMap &params, bool replace)
{
    const Entry e{ page, params };
    if (replace && m_idx >= 0) {
        m_hist[m_idx] = e;
    } else if (m_idx < 0 || m_hist[m_idx].page != page || m_hist[m_idx].params != params) {
        m_hist.resize(m_idx + 1);
        m_hist.push_back(e);
        m_idx = int(m_hist.size()) - 1;
        if (m_hist.size() > kMax) { m_hist.removeFirst(); --m_idx; }
    } else {
        return;   // 和当前一样：什么都不变
    }
    emit changed();
}

bool NavHistory::back()
{
    if (m_idx <= 0) return false;
    --m_idx;
    emit changed();
    return true;
}

bool NavHistory::forward()
{
    if (m_idx < 0 || m_idx >= m_hist.size() - 1) return false;
    ++m_idx;
    emit changed();
    return true;
}

} // namespace tp::app
