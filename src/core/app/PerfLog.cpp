#include "core/app/PerfLog.h"

namespace lmp::core {

// Warnings and above only by default: debug lines are off until a rule turns
// them on.
Q_LOGGING_CATEGORY(lcPerf, "lmp.perf", QtWarningMsg)

PerfScope::PerfScope(const char* what)
    : m_what(what)
    , m_enabled(lcPerf().isDebugEnabled())
{
    if (m_enabled) {
        m_timer.start();
    }
}

PerfScope::~PerfScope()
{
    if (m_enabled && !m_logged) {
        qCDebug(lcPerf).noquote().nospace() << m_what << ": " << m_timer.elapsed() << " ms";
    }
}

void PerfScope::done(const QString& details)
{
    if (!m_enabled || m_logged) {
        return;
    }
    m_logged = true;
    qCDebug(lcPerf).noquote().nospace() << m_what << ": " << details << ", " << m_timer.elapsed() << " ms";
}

} // namespace lmp::core
