#pragma once

#include <QObject>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QThread;

namespace lmp::core {

// The timing rule of the GUI stall watcher, on its own for tests: the GUI
// thread beats now and then; a check that finds no beat for longer than
// `thresholdMs` marks a stall, and the first check after the GUI beats again
// gives the stall's length (from the last beat before it to the first after).
// Each stall is reported once.
class StallRule {
public:
    explicit StallRule(qint64 thresholdMs);

    // The watcher's check at `nowMs`, with the GUI's last beat at `lastBeatMs`.
    [[nodiscard]] std::optional<qint64> check(qint64 lastBeatMs, qint64 nowMs);

private:
    qint64 m_thresholdMs;
    bool m_stalled = false;
    qint64 m_beatBefore = 0;
};

// Logs to lmp.perf every time the GUI event loop does not turn for over
// 100 ms, with how long it was, once the loop turns again. Made only while
// lmp.perf is on (create() returns nothing otherwise, so no thread exists).
//
// Owned by the composition root; lives on the GUI thread. A timer on the GUI
// thread beats every 16 ms; a watcher thread it owns checks every 20 ms. The
// thread is stopped and joined by stop() and by the destructor; it is never
// detached.
class StallWatcher : public QObject {
    Q_OBJECT

public:
    using Clock = std::function<qint64()>;

    // Nothing while lmp.perf is off.
    [[nodiscard]] static std::unique_ptr<StallWatcher> create();

    explicit StallWatcher(Clock clock, QObject* parent = nullptr);
    ~StallWatcher() override;

    void start();
    // Stops the beat and joins the watcher thread. Safe to call twice.
    void stop();

private:
    void watch();

    Clock m_clock;
    QTimer m_beat;
    std::atomic<qint64> m_lastBeatMs = 0;
    std::atomic<bool> m_stopping = false;
    QThread* m_thread = nullptr;
};

} // namespace lmp::core
