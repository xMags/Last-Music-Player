#include "ui/shell/ShellLayoutViewModel.h"

#include <algorithm>

namespace lmp::ui {

ShellLayoutViewModel::ShellLayoutViewModel(QObject* parent)
    : QObject(parent)
{
}

void ShellLayoutViewModel::setWindowWidth(qreal width)
{
    if (m_widthKnown && width == m_width) {
        return;
    }
    const bool wasDocked = railDocked();
    const bool first = !m_widthKnown;
    m_width = width;
    m_widthKnown = true;
    if (first) {
        // A window that opens narrow starts with the rail closed.
        if (!railDocked()) {
            m_railOpen = false;
        }
    } else if (railDocked() != wasDocked) {
        // Crossing the docking width opens the rail when widening and
        // closes it when narrowing.
        m_railOpen = railDocked();
    }
    emit changed();
}

int ShellLayoutViewModel::sidebarWidth() const
{
    if (sidebarCollapsed()) {
        return 64;
    }
    return m_width >= kWideMinWidth ? 300 : 240;
}

qreal ShellLayoutViewModel::railOverlayWidth() const
{
    return std::min<qreal>(380, m_width * 0.88);
}

void ShellLayoutViewModel::setRailOpen(bool open)
{
    if (open == m_railOpen) {
        return;
    }
    m_railOpen = open;
    emit changed();
}

void ShellLayoutViewModel::toggleRail()
{
    setRailOpen(!m_railOpen);
}

} // namespace lmp::ui
