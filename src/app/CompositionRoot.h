#pragma once

#include <QPointer>

#include <memory>

#include "platform/ISingleInstance.h"
#include "platform/windows/WindowsReducedMotion.h"
#include "platform/windows/WindowsWindowChrome.h"
#include "ui/shell/AppRoot.h"
#include "ui/shell/WindowChromeViewModel.h"
#include "ui/theme/Theme.h"

class QQmlApplicationEngine;
class QQmlEngine;

namespace lmp::app {

// Builds every platform object, service and shell view model once, in
// dependency order, and destroys them in reverse order. Nothing else in the
// application constructs services. Create it after the application object
// and before the QML engine; destroy the engine first.
class CompositionRoot {
public:
    CompositionRoot();
    ~CompositionRoot();

    CompositionRoot(const CompositionRoot&) = delete;
    CompositionRoot& operator=(const CompositionRoot&) = delete;

    // True when this is the only running copy. Otherwise brings the running
    // copy's window to the front and returns false; the caller then exits.
    [[nodiscard]] bool claimSingleInstance();

    // Connects the engine's singletons to the services behind them. Call
    // once, after the engine exists and before QML is loaded.
    void attachEngine(QQmlEngine& engine);

    // Takes over the frame of the main window the engine loaded, then shows
    // it. Returns false when there is no window to show.
    [[nodiscard]] bool showMainWindow(QQmlApplicationEngine& engine);

private:
    std::unique_ptr<platform::ISingleInstance> m_singleInstance;
    std::unique_ptr<platform::windows::WindowsReducedMotion> m_reducedMotion;
    platform::windows::WindowsWindowChrome m_windowChrome;
    ui::WindowChromeViewModel m_windowChromeViewModel;
    ui::AppRoot m_appRoot;
    QPointer<ui::Theme> m_theme; // owned by the QML engine
};

} // namespace lmp::app
