#pragma once

#include <QLatin1StringView>
#include <QString>

namespace lmp::core::mode {

// Where remote music comes from. Stored in Settings.json under the key
// RemoteAccessMode with exactly the names below; installed versions read the
// same value, so the names must never change.
enum class SourceMode {
    // Only files on this PC.
    LocalOnly,
    // The user's own provider, reached with a base address and an API key.
    ApiKey,
    // The signed-in account service.
    Account,
};

inline constexpr QLatin1StringView LocalOnlyModeName{"LocalOnly"};
inline constexpr QLatin1StringView ApiKeyModeName{"ApiKey"};
inline constexpr QLatin1StringView AccountModeName{"Account"};

// The stored name of a mode.
[[nodiscard]] constexpr QLatin1StringView sourceModeName(SourceMode mode) noexcept
{
    switch (mode) {
    case SourceMode::ApiKey:
        return ApiKeyModeName;
    case SourceMode::Account:
        return AccountModeName;
    case SourceMode::LocalOnly:
        break;
    }
    return LocalOnlyModeName;
}

// Reads a stored, non-empty value the way installed versions do: exactly
// "Account" and exactly "ApiKey" (case and spaces count) name those modes;
// every other text, "LocalOnly" included, means LocalOnly. An empty or
// missing value is not read here: it is a first run (ModeService).
[[nodiscard]] inline SourceMode sourceModeFromStored(const QString& stored)
{
    if (stored == AccountModeName) {
        return SourceMode::Account;
    }
    if (stored == ApiKeyModeName) {
        return SourceMode::ApiKey;
    }
    return SourceMode::LocalOnly;
}

} // namespace lmp::core::mode
