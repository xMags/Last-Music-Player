#include "app/CompositionRoot.h"

#include <QCoreApplication>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QWindow>

#include "core/app/AppIdentity.h"
#include "platform/windows/WindowsAppIdentity.h"
#include "platform/windows/WindowsSingleInstance.h"
#include "ui/theme/FontLoader.h"
#include "ui/theme/Motion.h"

namespace lmp::app {

namespace {

Q_LOGGING_CATEGORY(lcComposition, "lmp.app.composition")

} // namespace

CompositionRoot::CompositionRoot()
    : m_singleInstance(std::make_unique<platform::windows::WindowsSingleInstance>(
          core::AppIdentity::singleInstanceMutexName(), core::AppIdentity::displayName()))
    , m_windowChromeViewModel(&m_windowChrome)
    , m_sidebar(&m_navigator)
    , m_appRoot({
          .windowChrome = &m_windowChromeViewModel,
          .navigator = &m_navigator,
          .layout = &m_layout,
          .sidebar = &m_sidebar,
          .showGallery = QCoreApplication::arguments().contains(QStringLiteral("--gallery")),
      })
{
    // Before any window exists, so the taskbar and the media session use it.
    // A failure is logged inside and the app runs on.
    [[maybe_unused]] const bool identitySet =
        platform::windows::WindowsAppIdentity::apply(core::AppIdentity::appUserModelId());
    ui::AppRoot::registerInstance(&m_appRoot);
}

CompositionRoot::~CompositionRoot()
{
    ui::AppRoot::registerInstance(nullptr);
}

bool CompositionRoot::claimSingleInstance()
{
    if (m_singleInstance->tryAcquire()) {
        return true;
    }
    m_singleInstance->activateExistingWindow();
    return false;
}

bool CompositionRoot::showMainWindow(QQmlApplicationEngine& engine)
{
    const QList<QObject*> roots = engine.rootObjects();
    QWindow* window = roots.isEmpty() ? nullptr : qobject_cast<QWindow*>(roots.first());
    if (window == nullptr) {
        return false;
    }
    // winId() creates the native window without showing it, so the custom
    // frame is in place before the first frame is drawn.
    const bool custom = m_windowChrome.attach(static_cast<quintptr>(window->winId()));
    if (!custom) {
        qCWarning(lcComposition) << "Keeping the system window frame";
    }
    m_windowChromeViewModel.setAttached(custom);
    if (m_theme) {
        // The frame follows the theme: dark frame and backdrop in dark mode.
        m_windowChromeViewModel.applyTheme(m_theme->dark());
        QObject::connect(m_theme, &ui::ThemeColors::changed, &m_windowChromeViewModel,
                         [this]() {
                             if (m_theme) {
                                 m_windowChromeViewModel.applyTheme(m_theme->dark());
                             }
                         });
    }
    window->show();
    return true;
}

void CompositionRoot::attachEngine(QQmlEngine& engine)
{
    // Before the theme resolves its font families.
    ui::FontLoader::loadBundledFonts();

    m_theme = engine.singletonInstance<ui::Theme*>("LastMusicPlayer", "Theme");

    m_reducedMotion = std::make_unique<platform::windows::WindowsReducedMotion>();
    QPointer<ui::Motion> motion =
        engine.singletonInstance<ui::Motion*>("LastMusicPlayer", "Motion");
    if (motion.isNull()) {
        return;
    }
    motion->setReduced(m_reducedMotion->reduced());
    // The engine owns Motion and is destroyed before this root, so the
    // guarded pointer is checked on every change.
    m_reducedMotion->setChangeHandler([motion](bool reduced) {
        if (motion) {
            motion->setReduced(reduced);
        }
    });
}

} // namespace lmp::app
