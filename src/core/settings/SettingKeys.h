#pragma once

#include <QLatin1StringView>
#include <QString>

// Every key of Settings.json. Installed versions read and write the same file,
// so the names and the JSON type of each value must never change. No other
// file spells a key as a literal; dynamic keys have a function here.
namespace lmp::core::settings::keys {

// number, 1. Inserted on load when missing.
inline constexpr QLatin1StringView SchemaVersion{"SchemaVersion"};

// number 0 to 1, default 0.7. A stored 0 is reset to 0.7 at launch.
inline constexpr QLatin1StringView Volume{"Volume"};
// string "System", "Light" or "Dark", default "System".
inline constexpr QLatin1StringView Theme{"Theme"};
// string "#AARRGGBB", default "#FF0097B2".
inline constexpr QLatin1StringView AccentColor{"AccentColor"};
// bool, default true. Gapless preference.
inline constexpr QLatin1StringView Gapless{"Gapless"};
// bool, default false. Ask for lower quality streams on metered links.
inline constexpr QLatin1StringView DataSaver{"DataSaver"};
// bool, default true. Keep playing related songs when the queue ends.
inline constexpr QLatin1StringView Autoplay{"Autoplay"};
// bool, default false.
inline constexpr QLatin1StringView ShuffleEnabled{"ShuffleEnabled"};
// number 0 (off), 1 (all) or 2 (one), default 0.
inline constexpr QLatin1StringView RepeatMode{"RepeatMode"};
// bool, default true. Cover art on the player bar.
inline constexpr QLatin1StringView ShowAlbumArt{"ShowAlbumArt"};
// bool, default true. The system media controls.
inline constexpr QLatin1StringView WindowsMediaControls{"WindowsMediaControls"};
// bool, default false. Rich presence in the chat app.
inline constexpr QLatin1StringView DiscordPresence{"DiscordPresence"};
// number, default 0: 0 minimises to the tray, 1 quits.
inline constexpr QLatin1StringView CloseBehavior{"CloseBehavior"};
// string, default "" (the system default output).
inline constexpr QLatin1StringView OutputDeviceId{"OutputDeviceId"};
// number. Mapped but never written in practice.
inline constexpr QLatin1StringView OutputDeviceIndex{"OutputDeviceIndex"};
// number in dB, default 0, clamped to -12 to 0.
inline constexpr QLatin1StringView EqPreamp{"EqPreamp"};
// The number of equaliser bands; see eqBand().
inline constexpr int EqBandCount = 10;
// string of comma-separated extensions, default ".mp3,.flac".
inline constexpr QLatin1StringView ScanFormats{"ScanFormats"};
// string, default "". The music folder.
inline constexpr QLatin1StringView MusicLibraryPath{"MusicLibraryPath"};
// string, default "". The name typed in Settings.
inline constexpr QLatin1StringView UserDisplayName{"UserDisplayName"};
// numbers in physical pixels; defaults 0, 0, INT_MIN, INT_MIN. Saved only for
// a window of at least 800 x 600 that is not minimised.
inline constexpr QLatin1StringView WindowWidth{"WindowWidth"};
inline constexpr QLatin1StringView WindowHeight{"WindowHeight"};
inline constexpr QLatin1StringView WindowX{"WindowX"};
inline constexpr QLatin1StringView WindowY{"WindowY"};
// string "LocalOnly", "ApiKey" or "Account". On first run: "ApiKey" when a
// server address and key exist, otherwise "LocalOnly".
inline constexpr QLatin1StringView RemoteAccessMode{"RemoteAccessMode"};
// string, default "" (normalised to http://127.0.0.1:4527).
inline constexpr QLatin1StringView ProviderBaseUrl{"ProviderBaseUrl"};
// string. Old plaintext key: moved to the credential store, then removed.
inline constexpr QLatin1StringView ProviderApiKey{"ProviderApiKey"};
// string of two lowercase letters, default "in". The catalogue region.
inline constexpr QLatin1StringView CatalogStorefront{"CatalogStorefront"};
// string, newline-separated, with backslash and newline escaped. Default "".
inline constexpr QLatin1StringView RecentSearches{"RecentSearches"};
// string tag, default "grid".
inline constexpr QLatin1StringView BrowseResultView{"BrowseResultView"};
// string "Comfortable" or "Compact", default "Comfortable".
inline constexpr QLatin1StringView LibraryDetailDensity{"LibraryDetailDensity"};
// bool, default false. The one-time move of the old home history into the database.
inline constexpr QLatin1StringView LegacyHomeHistoryMigrated{"LegacyHomeHistoryMigrated"};
// strings, default "". The cached account profile, for signing back in offline.
inline constexpr QLatin1StringView AccountOwnerHint{"AccountOwnerHint"};
inline constexpr QLatin1StringView AccountDisplayNameHint{"AccountDisplayNameHint"};
inline constexpr QLatin1StringView AccountUsernameHint{"AccountUsernameHint"};
inline constexpr QLatin1StringView AccountPlanHint{"AccountPlanHint"};
// string, default "". Marks a sign-out clean-up that did not finish.
inline constexpr QLatin1StringView PendingAccountCleanupOwnerId{"PendingAccountCleanupOwnerId"};
// string, default "" (the Offline folder).
inline constexpr QLatin1StringView DownloadFolder{"DownloadFolder"};
// bool, default true. Avoid metered networks.
inline constexpr QLatin1StringView DownloadOnlyOnWifi{"DownloadOnlyOnWifi"};
// bool, default true.
inline constexpr QLatin1StringView AutoDownloadLiked{"AutoDownloadLiked"};
// bool, default false.
inline constexpr QLatin1StringView DownloadOnBattery{"DownloadOnBattery"};
// bool, default true. Download recently played songs.
inline constexpr QLatin1StringView KeepRecentOffline{"KeepRecentOffline"};

// number in dB, default 0. "EqBand0" to "EqBand9".
[[nodiscard]] inline QString eqBand(int band)
{
    return QStringLiteral("EqBand%1").arg(band);
}

} // namespace lmp::core::settings::keys
