#include <QFile>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>

#include "core/account/SecretStore.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/ICredentialStore.h"

using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::SettingsStore;
using lmp::core::account::LegacyKeyMigration;
using lmp::core::account::SecretStore;
using lmp::platform::CredentialRemoval;
using lmp::platform::ICredentialStore;
namespace keys = lmp::core::settings::keys;

namespace {

// Keeps credentials in memory and can be told to fail, following the
// contract of ICredentialStore.
class FakeCredentialStore final : public ICredentialStore {
public:
    Result<std::optional<QString>> read(const QString& target) override
    {
        if (corruptNextRead) {
            corruptNextRead = false;
            return std::optional<QString>(QStringLiteral("wrong"));
        }
        const auto found = values.constFind(target);
        if (found == values.constEnd()) {
            return std::optional<QString>{};
        }
        return std::optional<QString>(found.value());
    }

    Result<void> write(const QString& target, const QString& secret) override
    {
        lastWriteTarget = target;
        ++writeCount;
        if (failWrite || secret.isEmpty()) {
            return Failure::of(FailureKind::Storage, QStringLiteral("write refused"));
        }
        values.insert(target, secret);
        return {};
    }

    Result<void> remove(const QString& target) override
    {
        lastRemoveTarget = target;
        if (failRemove) {
            return Failure::of(FailureKind::Storage, QStringLiteral("remove refused"));
        }
        values.remove(target);
        return {};
    }

    Result<CredentialRemoval> removeIfMatches(const QString& target, const QString& expected) override
    {
        ++conditionalRemoveCount;
        const auto found = values.constFind(target);
        if (found == values.constEnd()) {
            return CredentialRemoval::NotFound;
        }
        if (expected.isEmpty() || found.value() != expected) {
            return CredentialRemoval::Mismatch;
        }
        const Result<void> removed = remove(target);
        if (!removed) {
            return removed.failure();
        }
        return CredentialRemoval::Removed;
    }

    QHash<QString, QString> values;
    QString lastWriteTarget;
    QString lastRemoveTarget;
    int writeCount = 0;
    int conditionalRemoveCount = 0;
    bool failWrite = false;
    bool failRemove = false;
    bool corruptNextRead = false;
};

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// The stored text, or a marker for "no credential" and for a failed read.
QString valueOf(const Result<std::optional<QString>>& result)
{
    if (!result) {
        return QStringLiteral("<failed>");
    }
    return result.value().value_or(QStringLiteral("<none>"));
}

} // namespace

class SecretStoreTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void targetNamesAreTheInstalledOnes();
    void readWriteRemoveBothSecrets();
    void conditionalRemovalKeepsAReplacedSession();
    void emptyExpectedSessionNeverMatches();
    void migrationMovesTheKeyAndRemovesTheSetting();
    void migrationReplacesAnExistingCredential();
    void failedSecureWriteKeepsTheSetting();
    void failedVerificationKeepsTheSetting();
    void failedSettingsCleanupIsReported();
    void migrationIsNotNeededWithoutATextKey();

private:
    [[nodiscard]] QString settingsPath() const { return m_dir->filePath(QStringLiteral("Settings.json")); }

    std::unique_ptr<QTemporaryDir> m_dir;
};

void SecretStoreTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void SecretStoreTest::targetNamesAreTheInstalledOnes()
{
    QCOMPARE(QString(SecretStore::AccountSessionTarget), QStringLiteral("LastMusicPlayer/AccountSession/v1"));
    QCOMPARE(QString(SecretStore::ProviderApiKeyTarget), QStringLiteral("LastMusicPlayer/ProviderApiKey/v1"));
}

void SecretStoreTest::readWriteRemoveBothSecrets()
{
    FakeCredentialStore credentials;
    SettingsStore settings(settingsPath());
    SecretStore secrets(credentials, settings);

    QVERIFY(secrets.writeProviderApiKey(QStringLiteral("provider-secret")));
    QCOMPARE(valueOf(secrets.readProviderApiKey()), QStringLiteral("provider-secret"));
    QCOMPARE(credentials.lastWriteTarget, QString(SecretStore::ProviderApiKeyTarget));

    QVERIFY(secrets.writeAccountSession(QStringLiteral("account-session")));
    QCOMPARE(valueOf(secrets.readAccountSession()), QStringLiteral("account-session"));
    QCOMPARE(credentials.lastWriteTarget, QString(SecretStore::AccountSessionTarget));
    // The two secrets never share an entry.
    QCOMPARE(valueOf(secrets.readProviderApiKey()), QStringLiteral("provider-secret"));

    QVERIFY(secrets.removeProviderApiKey());
    QCOMPARE(credentials.lastRemoveTarget, QString(SecretStore::ProviderApiKeyTarget));
    QCOMPARE(valueOf(secrets.readProviderApiKey()), QStringLiteral("<none>"));

    QVERIFY(secrets.removeAccountSession());
    QCOMPARE(credentials.lastRemoveTarget, QString(SecretStore::AccountSessionTarget));
    QCOMPARE(valueOf(secrets.readAccountSession()), QStringLiteral("<none>"));
    // Removing what is not there succeeds.
    QVERIFY(secrets.removeAccountSession());
}

void SecretStoreTest::conditionalRemovalKeepsAReplacedSession()
{
    FakeCredentialStore credentials;
    SettingsStore settings(settingsPath());
    SecretStore secrets(credentials, settings);
    QVERIFY(secrets.writeAccountSession(QStringLiteral("account-session")));

    const Result<CredentialRemoval> different =
        secrets.removeAccountSessionIfMatches(QStringLiteral("different-session"));
    QVERIFY(different);
    QCOMPARE(different.value(), CredentialRemoval::Mismatch);
    QCOMPARE(valueOf(secrets.readAccountSession()), QStringLiteral("account-session"));

    const Result<CredentialRemoval> matching = secrets.removeAccountSessionIfMatches(QStringLiteral("account-session"));
    QVERIFY(matching);
    QCOMPARE(matching.value(), CredentialRemoval::Removed);
    QCOMPARE(credentials.lastRemoveTarget, QString(SecretStore::AccountSessionTarget));

    const Result<CredentialRemoval> again = secrets.removeAccountSessionIfMatches(QStringLiteral("account-session"));
    QVERIFY(again);
    QCOMPARE(again.value(), CredentialRemoval::NotFound);

    QVERIFY(secrets.writeAccountSession(QStringLiteral("delete-failure")));
    credentials.failRemove = true;
    const Result<CredentialRemoval> failed = secrets.removeAccountSessionIfMatches(QStringLiteral("delete-failure"));
    QVERIFY(!failed);
    QCOMPARE(failed.failure().kind, FailureKind::Storage);
    QCOMPARE(valueOf(secrets.readAccountSession()), QStringLiteral("delete-failure"));
    QVERIFY(!secrets.removeAccountSession());
    QCOMPARE(valueOf(secrets.readAccountSession()), QStringLiteral("delete-failure"));
}

void SecretStoreTest::emptyExpectedSessionNeverMatches()
{
    FakeCredentialStore credentials;
    SettingsStore settings(settingsPath());
    SecretStore secrets(credentials, settings);
    QVERIFY(secrets.writeAccountSession(QStringLiteral("account-session")));

    const Result<CredentialRemoval> result = secrets.removeAccountSessionIfMatches(QString());
    QVERIFY(result);
    QCOMPARE(result.value(), CredentialRemoval::Mismatch);
    QCOMPARE(credentials.conditionalRemoveCount, 0);
    QCOMPARE(valueOf(secrets.readAccountSession()), QStringLiteral("account-session"));
}

void SecretStoreTest::migrationMovesTheKeyAndRemovesTheSetting()
{
    FakeCredentialStore credentials;
    SettingsStore settings(settingsPath());
    settings.setString(keys::ProviderApiKey, QStringLiteral("legacy-plaintext-secret"));
    settings.setString(keys::ProviderBaseUrl, QStringLiteral("https://provider.example"));
    SecretStore secrets(credentials, settings);

    QCOMPARE(secrets.migrateLegacyProviderApiKey(), LegacyKeyMigration::Migrated);
    QCOMPARE(valueOf(secrets.readProviderApiKey()), QStringLiteral("legacy-plaintext-secret"));
    QCOMPARE(credentials.lastWriteTarget, QString(SecretStore::ProviderApiKeyTarget));
    QVERIFY(!settings.contains(keys::ProviderApiKey));

    const QByteArray saved = readFile(settingsPath());
    QVERIFY(!saved.contains("legacy-plaintext-secret"));
    QVERIFY(!saved.contains("ProviderApiKey"));
    QVERIFY(saved.contains("provider.example"));

    // A second start finds nothing left to move.
    QCOMPARE(secrets.migrateLegacyProviderApiKey(), LegacyKeyMigration::NotNeeded);
    QCOMPARE(credentials.writeCount, 1);
}

void SecretStoreTest::migrationReplacesAnExistingCredential()
{
    FakeCredentialStore credentials;
    credentials.values.insert(QString(SecretStore::ProviderApiKeyTarget), QStringLiteral("stored-secret"));
    SettingsStore settings(settingsPath());
    settings.setString(keys::ProviderApiKey, QStringLiteral("settings-secret"));
    SecretStore secrets(credentials, settings);

    QCOMPARE(secrets.migrateLegacyProviderApiKey(), LegacyKeyMigration::Migrated);
    QCOMPARE(valueOf(secrets.readProviderApiKey()), QStringLiteral("settings-secret"));
    QVERIFY(!settings.contains(keys::ProviderApiKey));
}

void SecretStoreTest::failedSecureWriteKeepsTheSetting()
{
    FakeCredentialStore credentials;
    credentials.failWrite = true;
    SettingsStore settings(settingsPath());
    settings.setString(keys::ProviderApiKey, QStringLiteral("must-remain-until-secured"));
    SecretStore secrets(credentials, settings);

    QCOMPARE(secrets.migrateLegacyProviderApiKey(), LegacyKeyMigration::SecureWriteFailed);
    QCOMPARE(settings.string(keys::ProviderApiKey), QStringLiteral("must-remain-until-secured"));
    QVERIFY(readFile(settingsPath()).contains("must-remain-until-secured"));
}

void SecretStoreTest::failedVerificationKeepsTheSetting()
{
    FakeCredentialStore credentials;
    credentials.corruptNextRead = true;
    SettingsStore settings(settingsPath());
    settings.setString(keys::ProviderApiKey, QStringLiteral("verification-source"));
    SecretStore secrets(credentials, settings);

    QCOMPARE(secrets.migrateLegacyProviderApiKey(), LegacyKeyMigration::SecureVerificationFailed);
    QCOMPARE(settings.string(keys::ProviderApiKey), QStringLiteral("verification-source"));
    QVERIFY(readFile(settingsPath()).contains("verification-source"));
}

void SecretStoreTest::failedSettingsCleanupIsReported()
{
    FakeCredentialStore credentials;
    SettingsStore settings(settingsPath());
    settings.setString(keys::ProviderApiKey, QStringLiteral("cleanup-source"));
    // A read-only settings file cannot be saved again.
    const QFileDevice::Permissions writable = QFile::permissions(settingsPath());
    QVERIFY(QFile::setPermissions(settingsPath(), QFileDevice::ReadOwner | QFileDevice::ReadUser));
    SecretStore secrets(credentials, settings);

    const LegacyKeyMigration status = secrets.migrateLegacyProviderApiKey();
    QVERIFY(QFile::setPermissions(settingsPath(), writable));
    QCOMPARE(status, LegacyKeyMigration::SettingsCleanupFailed);
    QCOMPARE(valueOf(secrets.readProviderApiKey()), QStringLiteral("cleanup-source"));
    QCOMPARE(settings.string(keys::ProviderApiKey), QStringLiteral("cleanup-source"));
    QVERIFY(readFile(settingsPath()).contains("cleanup-source"));
}

void SecretStoreTest::migrationIsNotNeededWithoutATextKey()
{
    FakeCredentialStore credentials;
    SettingsStore settings(settingsPath());
    SecretStore secrets(credentials, settings);
    QCOMPARE(secrets.migrateLegacyProviderApiKey(), LegacyKeyMigration::NotNeeded);

    settings.setString(keys::ProviderApiKey, QString());
    QCOMPARE(secrets.migrateLegacyProviderApiKey(), LegacyKeyMigration::NotNeeded);
    QVERIFY(settings.contains(keys::ProviderApiKey));

    // A value of another JSON type is not a key and stays where it is.
    settings.setInteger(keys::ProviderApiKey, 42);
    QCOMPARE(secrets.migrateLegacyProviderApiKey(), LegacyKeyMigration::NotNeeded);
    QCOMPARE(settings.integer(keys::ProviderApiKey), 42);
    QCOMPARE(credentials.writeCount, 0);
}

QTEST_GUILESS_MAIN(SecretStoreTest)
#include "tst_secret_store.moc"
