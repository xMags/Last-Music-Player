#include "app/WindowPlacementKeeper.h"

#include <chrono>

#include "core/app/WindowPlacement.h"
#include "core/settings/SettingsStore.h"
#include "platform/IWindowPlacement.h"

namespace lmp::app {

namespace {

// A move or resize is noted once it has paused this long.
constexpr auto kSettleDelay = std::chrono::milliseconds(400);

} // namespace

WindowPlacementKeeper::WindowPlacementKeeper(platform::IWindowPlacement& placement, core::SettingsStore& settings,
                                             QObject* parent)
    : QObject(parent)
    , m_placement(placement)
    , m_settings(settings)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(kSettleDelay);
    connect(&m_settle, &QTimer::timeout, this, &WindowPlacementKeeper::note);
}

void WindowPlacementKeeper::attach(QWindow& window)
{
    m_window = &window;
    const core::WindowPlacement::Saved saved = core::WindowPlacement::load(m_settings);
    if (saved.size) {
        m_placement.apply(static_cast<quintptr>(window.winId()), *saved.size, saved.position);
    }
    m_maximized = saved.maximized;
    for (auto signal : {&QWindow::xChanged, &QWindow::yChanged, &QWindow::widthChanged, &QWindow::heightChanged}) {
        connect(&window, signal, &m_settle, qOverload<>(&QTimer::start));
    }
    connect(&window, &QWindow::windowStateChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&window, &QWindow::visibleChanged, &m_settle, qOverload<>(&QTimer::start));
}

void WindowPlacementKeeper::note()
{
    // Only a frame the user can see counts; a minimised one has none.
    if (!m_window || !m_window->isVisible()) {
        return;
    }
    const auto handle = static_cast<quintptr>(m_window->winId());
    const std::optional<QRect> frame = m_placement.frame(handle);
    if (!frame) {
        return; // minimised: neither its frame nor its state counts
    }
    // A maximised window keeps the frame it restores to.
    m_maximized = m_placement.maximized(handle);
    if (!m_maximized) {
        m_last = frame;
    }
    save();
}

void WindowPlacementKeeper::save()
{
    if (m_last) {
        core::WindowPlacement::save(m_settings, *m_last);
    }
    core::WindowPlacement::saveMaximized(m_settings, m_maximized);
}

} // namespace lmp::app
