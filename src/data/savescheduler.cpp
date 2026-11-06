#include "savescheduler.h"

namespace tp::data {

SaveScheduler::SaveScheduler(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SaveScheduler::saveRequested);
}

void SaveScheduler::changed(Mode mode, int delayMs)
{
    if (mode == Mode::Now) {
        m_timer.stop();
        emit saveRequested();
        return;
    }
    m_timer.start(delayMs > 0 ? delayMs : kDefaultDelayMs);   // clearTimeout + setTimeout
}

bool SaveScheduler::flush()
{
    if (!m_timer.isActive()) return false;
    m_timer.stop();
    emit saveRequested();
    return true;
}

void SaveScheduler::cancel() { m_timer.stop(); }

} // namespace tp::data
