#include "app/CompositionRoot.h"

#include <QPointer>
#include <QQmlEngine>

#include "core/app/AppIdentity.h"
#include "platform/windows/WindowsAppIdentity.h"
#include "platform/windows/WindowsSingleInstance.h"
#include "ui/theme/FontLoader.h"
#include "ui/theme/Motion.h"

namespace lmp::app {

CompositionRoot::CompositionRoot()
    : m_singleInstance(std::make_unique<platform::windows::WindowsSingleInstance>(
          core::AppIdentity::singleInstanceMutexName(), core::AppIdentity::displayName()))
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

void CompositionRoot::attachEngine(QQmlEngine& engine)
{
    // Before the theme resolves its font families.
    ui::FontLoader::loadBundledFonts();

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
