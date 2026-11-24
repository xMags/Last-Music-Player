#include "core/app/AppDataReset.h"

#include "core/account/AccountSessionService.h"
#include "core/account/AccountSignOutCoordinator.h"
#include "core/playback/PlayerController.h"
#include "core/playback/StreamCache.h"
#include "core/remote/ArtworkService.h"
#include "core/settings/PreferenceDefaults.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

namespace lmp::core {

AppDataReset::AppDataReset(const Dependencies& dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(dependencies)
{
}

QFuture<bool> AppDataReset::run()
{
    if (m_running) {
        return QtFuture::makeReadyValueFuture(false);
    }
    m_running = true;
    return m_deps.signOut.signOut().then(this, [this](const account::AccountLogoutResult& result) {
        if (!result.signedOut) {
            // The service could not be reached or refused: the session is
            // forgotten here anyway, as a reset has to work offline.
            [[maybe_unused]] const bool removed = m_deps.session.removeLocalSession();
        }
        const bool erased = eraseLocalData();
        m_running = false;
        return erased;
    });
}

bool AppDataReset::eraseLocalData()
{
    m_deps.player.reset();
    resetPreferencesToDefaults(m_deps.settings);
    // The player keeps its own copy of autoplay.
    m_deps.player.setAutoplay(m_deps.settings.boolean(settings::keys::Autoplay, true));
    m_deps.artwork.clear();
    return m_deps.streamCache.clear();
}

} // namespace lmp::core
