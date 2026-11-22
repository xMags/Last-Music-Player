#include "core/account/SecretStore.h"

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

namespace lmp::core::account {

namespace keys = settings::keys;

SecretStore::SecretStore(platform::ICredentialStore& credentials, SettingsStore& settings)
    : m_credentials(credentials)
    , m_settings(settings)
{
}

Result<std::optional<QString>> SecretStore::readAccountSession()
{
    return m_credentials.read(AccountSessionTarget);
}

Result<void> SecretStore::writeAccountSession(const QString& session)
{
    return m_credentials.write(AccountSessionTarget, session);
}

Result<void> SecretStore::removeAccountSession()
{
    return m_credentials.remove(AccountSessionTarget);
}

Result<platform::CredentialRemoval> SecretStore::removeAccountSessionIfMatches(const QString& expected)
{
    // An empty expectation never names a real session; checked here as well
    // so no store implementation can get it wrong.
    if (expected.isEmpty()) {
        return platform::CredentialRemoval::Mismatch;
    }
    return m_credentials.removeIfMatches(AccountSessionTarget, expected);
}

Result<std::optional<QString>> SecretStore::readProviderApiKey()
{
    return m_credentials.read(ProviderApiKeyTarget);
}

Result<void> SecretStore::writeProviderApiKey(const QString& apiKey)
{
    return m_credentials.write(ProviderApiKeyTarget, apiKey);
}

Result<void> SecretStore::removeProviderApiKey()
{
    return m_credentials.remove(ProviderApiKeyTarget);
}

LegacyKeyMigration SecretStore::migrateLegacyProviderApiKey()
{
    // Only a text value counts, as before; anything else is left alone.
    const QString legacyKey = m_settings.string(keys::ProviderApiKey);
    if (legacyKey.isEmpty()) {
        return LegacyKeyMigration::NotNeeded;
    }
    if (!writeProviderApiKey(legacyKey)) {
        return LegacyKeyMigration::SecureWriteFailed;
    }
    // The plaintext copy is the only one until the stored one is proven good.
    const Result<std::optional<QString>> stored = readProviderApiKey();
    if (!stored || !stored.value() || *stored.value() != legacyKey) {
        return LegacyKeyMigration::SecureVerificationFailed;
    }
    if (!m_settings.remove(keys::ProviderApiKey)) {
        return LegacyKeyMigration::SettingsCleanupFailed;
    }
    return LegacyKeyMigration::Migrated;
}

} // namespace lmp::core::account
