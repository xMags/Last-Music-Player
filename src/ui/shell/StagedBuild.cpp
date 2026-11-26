#include "ui/shell/StagedBuild.h"

namespace lmp::ui {

StagedBuild::StagedBuild(QObject* parent)
    : QObject(parent)
{
    // Zero delay: the next turn of the event loop, after what is already
    // queued there (a frame the last part asked for among it).
    m_step.setSingleShot(true);
    m_step.setInterval(0);
    connect(&m_step, &QTimer::timeout, this, &StagedBuild::serveNext);
}

int StagedBuild::take()
{
    ++m_issued;
    if (!m_step.isActive()) {
        m_step.start();
    }
    return m_issued;
}

void StagedBuild::serveNext()
{
    if (m_served >= m_issued) {
        return;
    }
    ++m_served;
    emit servedChanged();
    if (m_served < m_issued) {
        m_step.start();
    }
}

} // namespace lmp::ui
