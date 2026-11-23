#include "ui/motion/PageCascade.h"

namespace lmp::ui {

namespace {

[[nodiscard]] qint64 steadyNow()
{
    static QElapsedTimer clock = []() {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return clock.elapsed();
}

} // namespace

PageCascade::PageCascade(std::function<qint64()> now, QObject* parent)
    : QObject(parent)
    , m_now(now ? std::move(now) : std::function<qint64()>(&steadyNow))
{
}

void PageCascade::restart()
{
    m_startedAt = m_now();
    m_next = 0;
}

int PageCascade::nextIndex()
{
    if (m_startedAt < 0 || m_next >= Limit || m_now() - m_startedAt > WindowMs) {
        return -1;
    }
    return m_next++;
}

} // namespace lmp::ui
