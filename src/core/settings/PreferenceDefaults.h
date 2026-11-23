#pragma once

namespace lmp::core {

class SettingsStore;

// Settings' "Reset to defaults": writes the default of exactly the keys
// earlier versions reset, in their types: ShowAlbumArt, WindowsMediaControls,
// Gapless and Autoplay on; DiscordPresence off; CloseBehavior 0; ScanFormats
// ".mp3,.flac"; Theme "System"; AccentColor "#FF0097B2"; OutputDeviceId "";
// EqBand0 to EqBand9 and EqPreamp 0. Every other key (volume, shuffle,
// repeat, the music folder, the display name, the music source, the account
// hints, ...) is left as it is.
void resetPreferencesToDefaults(SettingsStore& store);

} // namespace lmp::core
