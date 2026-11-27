#include "ui/shell/WindowChromeViewModel.h"

namespace lmp::ui {

WindowChromeViewModel::WindowChromeViewModel(platform::IWindowChrome* chrome, QObject* parent)
    : QObject(parent)
    , m_chrome(chrome)
{
    if (m_chrome != nullptr) {
        // The chrome calls back on this (the GUI) thread; this object
        // removes the handler before it is destroyed.
        m_chrome->setStateHandler([this]() { emit changed(); });
    }
}

WindowChromeViewModel::~WindowChromeViewModel()
{
    if (m_chrome != nullptr) {
        m_chrome->setStateHandler({});
    }
}

void WindowChromeViewModel::setAttached(bool attached)
{
    if (attached == m_attached) {
        return;
    }
    m_attached = attached && m_chrome != nullptr;
    emit changed();
}

bool WindowChromeViewModel::maximized() const
{
    return m_attached && m_chrome->maximized();
}

bool WindowChromeViewModel::active() const
{
    return !m_attached || m_chrome->active();
}

bool WindowChromeViewModel::backdropActive() const
{
    return m_attached && m_chrome->backdropActive();
}

bool WindowChromeViewModel::maximizeHovered() const
{
    return m_attached && m_chrome->maximizeHovered();
}

bool WindowChromeViewModel::maximizePressed() const
{
    return m_attached && m_chrome->maximizePressed();
}

bool WindowChromeViewModel::moving() const
{
    return m_attached && m_chrome->moving();
}

void WindowChromeViewModel::applyTheme(bool dark)
{
    if (!m_attached) {
        return;
    }
    m_chrome->setDarkFrame(dark);
}

void WindowChromeViewModel::setBackdropSupported(bool supported)
{
    m_backdropSupported = supported;
}

void WindowChromeViewModel::setBackdrop(platform::WindowBackdrop backdrop)
{
    if (!m_attached) {
        return;
    }
    const bool wanted = backdrop != platform::WindowBackdrop::None;
    const bool needsRestart = wanted && !m_backdropSupported;
    if (needsRestart != m_backdropNeedsRestart) {
        m_backdropNeedsRestart = needsRestart;
        emit changed();
    }
    // On an opaque surface the see-through frame would show as plain white.
    // The chrome reports any change through the state handler.
    m_chrome->setBackdrop(needsRestart ? platform::WindowBackdrop::None : backdrop);
}

// The layout is reported while QML loads, before the window is attached, so
// it is passed on whenever there is a chrome to keep it.
void WindowChromeViewModel::setDragStripHeight(qreal height)
{
    if (m_chrome != nullptr) {
        m_chrome->setDragStripHeight(height);
    }
}

void WindowChromeViewModel::setCaptionButtons(const QRectF& minimize, const QRectF& maximize,
                                              const QRectF& close)
{
    if (m_chrome != nullptr) {
        m_chrome->setCaptionButtons(minimize, maximize, close);
    }
}

void WindowChromeViewModel::minimize()
{
    if (m_attached) {
        m_chrome->minimize();
    }
}

void WindowChromeViewModel::toggleMaximize()
{
    if (m_attached) {
        m_chrome->toggleMaximize();
    }
}

void WindowChromeViewModel::close()
{
    if (m_attached) {
        m_chrome->close();
    }
}

} // namespace lmp::ui
