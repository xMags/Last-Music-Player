#include "core/app/StallWatch.h"

#include "core/app/PerfLog.h"

#include <QThread>

#include <chrono>

namespace lmp::core {

namespace {

constexpr qint64 kStallMs = 100;
constexpr int kBeatMs = 16;
constexpr unsigned long kCheckMs = 20;

[[nodiscard]] qint64 steadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

StallRule::StallRule(qint64 thresholdMs)
    : m_thresholdMs(thresholdMs)
{
}

std::optional<qint64> StallRule::check(qint64 lastBeatMs, qint64 nowMs)
{
    if (m_stalled) {
        if (lastBeatMs > m_beatBefore) {
            m_stalled = false;
            return lastBeatMs - m_beatBefore;
        }
        return std::nullopt;
    }
    if (nowMs - lastBeatMs > m_thresholdMs) {
        m_stalled = true;
        m_beatBefore = lastBeatMs;
    }
    return std::nullopt;
}

std::unique_ptr<StallWatcher> StallWatcher::create()
{
    if (!lcPerf().isDebugEnabled()) {
        return nullptr;
    }
    auto watcher = std::make_unique<StallWatcher>([]() { return steadyMs(); });
    watcher->start();
    return watcher;
}

StallWatcher::StallWatcher(Clock clock, QObject* parent)
    : QObject(parent)
    , m_clock(std::move(clock))
{
    m_beat.setInterval(kBeatMs);
    m_beat.setTimerType(Qt::PreciseTimer);
    connect(&m_beat, &QTimer::timeout, this, [this]() { m_lastBeatMs.store(m_clock()); });
}

StallWatcher::~StallWatcher()
{
    stop();
}

void StallWatcher::start()
{
    if (m_thread != nullptr) {
        return;
    }
    m_lastBeatMs.store(m_clock());
    m_beat.start();
    // Owned here and joined in stop(); parented to this object as well, so
    // it can never outlive it.
    m_thread = QThread::create([this]() { watch(); });
    m_thread->setParent(this);
    m_thread->start();
}

void StallWatcher::stop()
{
    m_beat.stop();
    if (m_thread == nullptr) {
        return;
    }
    m_stopping.store(true);
    m_thread->wait();
    m_thread->deleteLater();
    m_thread = nullptr;
}

void StallWatcher::watch()
{
    StallRule rule(kStallMs);
    while (!m_stopping.load()) {
        QThread::msleep(kCheckMs);
        if (const std::optional<qint64> stall = rule.check(m_lastBeatMs.load(), m_clock())) {
            qCDebug(lcPerf).nospace() << "GUI event loop blocked: " << *stall << " ms";
        }
    }
}

} // namespace lmp::core
