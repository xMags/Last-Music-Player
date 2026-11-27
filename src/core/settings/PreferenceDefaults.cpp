#include "core/settings/PreferenceDefaults.h"

#include "core/settings/AppearanceSettings.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

namespace lmp::core {

void resetPreferencesToDefaults(SettingsStore& store)
{
    namespace keys = settings::keys;
    store.setBoolean(keys::ShowAlbumArt, true);
    store.setBoolean(keys::WindowsMediaControls, true);
    store.setBoolean(keys::Gapless, true);
    store.setBoolean(keys::Autoplay, true);
    store.setBoolean(keys::DiscordPresence, false);
    store.setInteger(keys::CloseBehavior, 0);
    store.setString(keys::ScanFormats, QStringLiteral(".mp3,.flac"));
    store.setString(keys::Theme, settings::storedThemeChoice(settings::ThemeChoice::System));
    store.setString(keys::AccentColor, settings::storedAccentChoice(0));
    store.setString(keys::WindowBackdrop, settings::storedBackdropChoice(settings::BackdropChoice::Solid));
    store.setString(keys::OutputDeviceId, QString());
    for (int band = 0; band < keys::EqBandCount; ++band) {
        store.setNumber(keys::eqBand(band), 0.0);
    }
    store.setNumber(keys::EqPreamp, 0.0);
}

} // namespace lmp::core
