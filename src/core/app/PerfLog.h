#pragma once

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QString>

namespace lmp::core {

// "lmp.perf": what the GUI thread is doing when content arrives or heavy work
// starts, with counts and durations, to line up with Qt's render-loop timing
// (QT_LOGGING_RULES="lmp.perf=true"). Off by default; with it off nothing is
// timed and no text is built. Lines carry kinds, counts and durations only:
// never titles, names, identifiers or addresses.
Q_DECLARE_LOGGING_CATEGORY(lcPerf)

// Times one block of work for lcPerf. With the category off it reads no
// clock and builds no text. It logs "<what>: <ms> ms" when it ends, or the
// details given to done() instead.
class PerfScope {
public:
    // `what` is a fixed text that outlives the scope (a string literal).
    explicit PerfScope(const char* what);
    ~PerfScope();
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

    // Whether the category is on; build details only when it is.
    [[nodiscard]] bool enabled() const { return m_enabled; }
    // Logs "<what>: <details>, <ms> ms" now, instead of at the end.
    void done(const QString& details);

private:
    const char* m_what;
    bool m_enabled;
    bool m_logged = false;
    QElapsedTimer m_timer;
};

} // namespace lmp::core
