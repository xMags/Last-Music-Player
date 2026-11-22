#pragma once

#include "core/async/Result.h"
#include "platform/ICredentialStore.h"

#include <QLatin1StringView>
#include <QString>

#include <optional>

namespace lmp::core {
class SettingsStore;
}

namespace lmp::core::account {

// How moving the provider API key out of Settings.json ended.
enum class LegacyKeyMigration {
    // The settings file holds no key (or holds it as something other than
    // text), so nothing was touched.
    NotNeeded,
    // The key is in the credential store and gone from the settings file.
    Migrated,
    // The credential could not be written; the setting is kept.
    SecureWriteFailed,
    // The credential read back differently; the setting is kept.
    SecureVerificationFailed,
    // The key is secured, but the settings file still holds it.
    SettingsCleanupFailed,
};

// The application's two secrets, kept in the system credential store: the
// account session (the bearer token of the signed-in account) and the API key
// of the user's provider. The target names are shared with installed earlier
// versions and must never change.
//
// Thread-safe as far as its dependencies are: both are.
class SecretStore {
public:
    static constexpr QLatin1StringView AccountSessionTarget{"LastMusicPlayer/AccountSession/v1"};
    static constexpr QLatin1StringView ProviderApiKeyTarget{"LastMusicPlayer/ProviderApiKey/v1"};

    // Both references must outlive this object.
    SecretStore(platform::ICredentialStore& credentials, SettingsStore& settings);

    [[nodiscard]] Result<std::optional<QString>> readAccountSession();
    [[nodiscard]] Result<void> writeAccountSession(const QString& session);
    [[nodiscard]] Result<void> removeAccountSession();
    // Signing out removes the session only when it is still the one being
    // signed out, so a newer sign-in that replaced it in the meantime stays.
    [[nodiscard]] Result<platform::CredentialRemoval> removeAccountSessionIfMatches(const QString& expected);

    [[nodiscard]] Result<std::optional<QString>> readProviderApiKey();
    [[nodiscard]] Result<void> writeProviderApiKey(const QString& apiKey);
    [[nodiscard]] Result<void> removeProviderApiKey();

    // Moves a plaintext API key that earlier versions kept in Settings.json
    // into the credential store, then removes it from the file. Safe to call
    // at every start: once moved there is nothing left to do. The key in the
    // file replaces a stored credential, and the setting is only removed
    // after the credential has been read back unchanged.
    [[nodiscard]] LegacyKeyMigration migrateLegacyProviderApiKey();

private:
    platform::ICredentialStore& m_credentials;
    SettingsStore& m_settings;
};

} // namespace lmp::core::account
