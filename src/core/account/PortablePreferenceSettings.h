#pragma once

#include "core/account/AccountMusicContract.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace lmp::core {
class SettingsStore;
}

// Where the account's portable preferences live in Settings.json, and how a
// value moves between the two forms. As earlier versions did it.
namespace lmp::core::account {

// The region used when none was chosen; the service's own default.
inline constexpr QLatin1StringView DefaultCatalogStorefront{"in"};

// The setting key a preference is kept under.
[[nodiscard]] QLatin1StringView settingKeyFor(PortablePreferenceKey key);
// The preference a setting key holds, when it holds one.
[[nodiscard]] std::optional<PortablePreferenceKey> preferenceForSettingKey(QStringView settingKey);

// The local value as preference JSON: the theme lower-cased ("system" when
// unset), the accent as "#RRGGBB" (the stored "#AARRGGBB" without alpha),
// gapless (default on) and data saver (default off) as true or false, the
// region ("in" when unset). Not validated.
[[nodiscard]] QString localPreferenceValue(const SettingsStore& settings, PortablePreferenceKey key);

// Stores a preference value in the settings, when it is valid: the theme
// with a capital first letter, the accent as "#FFRRGGBB".
void applyPortablePreference(SettingsStore& settings, PortablePreferenceKey key, const QString& valueJson);

} // namespace lmp::core::account
