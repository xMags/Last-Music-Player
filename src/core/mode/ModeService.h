#pragma once

#include "core/account/SecretStore.h"
#include "core/async/RequestScope.h"
#include "core/async/Result.h"
#include "core/mode/Capabilities.h"
#include "core/mode/SourceMode.h"
#include "core/provider/ProviderClient.h"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace lmp::core {
class SettingsStore;
class UserDataOperationGate;
}

namespace lmp::core::account {
class AccountSessionService;
}

namespace lmp::core::db {
class AccountRepository;
}

namespace lmp::core::mode {

enum class ApiKeyConnectOutcome {
    Connected,
    MissingAddress,
    // Not an http or https address with a host (see isUsableProviderBaseUrl).
    InvalidAddress,
    MissingKey,
    // The key has control characters and cannot be sent.
    InvalidKey,
    // The provider answered 401.
    Unauthorized,
    // Any other failure of the status call.
    Unavailable,
    // User data is being cleaned up; nothing was stored.
    Busy,
    // The key could not be stored and read back, or the address not saved.
    StorageFailed,
    // A newer connect, a disconnect or a mode change came first.
    Superseded,
};

enum class ApiKeyDisconnectOutcome {
    Disconnected,
    // The key is gone, but the saved address could not be removed.
    AddressCleanupFailed,
    // The key could not be removed; nothing changed.
    KeyRemovalFailed,
    Busy,
};

// What a remote request captured when it started. Its result is dropped once
// the scope is no longer current.
struct RemoteScope {
    SourceMode mode = SourceMode::LocalOnly;
    quint64 generation = 0;
};

// The current music source: the stored mode, which modes can be used, the API
// key connection, the switch back to LocalOnly when a remote source goes away,
// and the database's active library context, which follows every change.
//
// Scope generation: every mode change, API key connect or disconnect, change
// of account owner, and change of session state while in Account mode makes
// every earlier RemoteScope stale. It starts at 1, so a restart begins where
// a fresh service would.
//
// Threading: lives on the GUI thread and is used only there; continuations
// run there with this object as their context. Settings, credential and
// database writes are short synchronous calls on that thread (the database's
// owner thread). Neither the key nor the address is ever logged.
class ModeService final : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        SettingsStore& settings;
        account::SecretStore& secrets;
        account::AccountSessionService& session;
        provider::ProviderClient& provider;
        db::AccountRepository& accounts;
        // Connecting and disconnecting are refused while user data is being
        // cleaned up.
        UserDataOperationGate& operationGate;
    };

    // Every dependency must outlive this object.
    explicit ModeService(const Dependencies& dependencies, QObject* parent = nullptr);

    // Call once before anything else: moves a plaintext key out of the
    // settings file, reads the stored mode (on a first run storing ApiKey
    // when an address and key exist, else LocalOnly) and sets the database
    // context. Account starts with the local context until the session
    // names its owner.
    account::LegacyKeyMigration start();

    [[nodiscard]] SourceMode mode() const { return m_mode; }
    // Which source is chosen, for code that keeps that source's own data
    // (the account library, its sync, its artists). Features ask
    // capabilities() instead; only the mode service and the remote facade
    // compare modes themselves.
    [[nodiscard]] bool accountSelected() const { return m_mode == SourceMode::Account; }
    [[nodiscard]] bool apiKeySelected() const { return m_mode == SourceMode::ApiKey; }
    [[nodiscard]] bool isAvailable(SourceMode mode) const;
    [[nodiscard]] bool isSelectable(SourceMode mode) const;
    // The current mode is remote and available.
    [[nodiscard]] bool hasRemoteAccess() const;
    // False (and nothing changes) when the mode is not selectable. Also false
    // when the database refused the account context; the service is then
    // back in LocalOnly.
    [[nodiscard]] bool setMode(SourceMode mode);

    // The saved base address, as the user typed it; empty when none.
    [[nodiscard]] QString providerBaseUrl() const;
    [[nodiscard]] QStringList providerCapabilities() const { return m_providerCapabilities; }
    // Where provider requests go and the key they carry, read now; nothing
    // outside a usable ApiKey mode or when the key cannot be read. The key
    // is never logged.
    [[nodiscard]] std::optional<provider::ProviderConnection> providerConnection() const;

    // Tests the address and key with the status call, then stores the key,
    // saves the address and enters ApiKey mode.
    [[nodiscard]] QFuture<ApiKeyConnectOutcome> connectApiKey(const QString& baseUrl, const QString& apiKey);
    // Removes the key and the address; ApiKey mode falls back to LocalOnly.
    [[nodiscard]] ApiKeyDisconnectOutcome disconnectApiKey();
    // Asks the configured provider for its capabilities again (after a
    // start in ApiKey mode). NotAvailable outside a usable ApiKey mode,
    // Cancelled when the scope changed while it waited.
    [[nodiscard]] QFuture<Result<void>> refreshProviderStatus();

    void setConnectivity(Connectivity connectivity);
    [[nodiscard]] Capabilities capabilities() const;

    [[nodiscard]] RemoteScope captureScope() const { return {m_mode, m_generation}; }
    [[nodiscard]] bool isCurrent(const RemoteScope& scope) const;

signals:
    // The mode, a mode's availability, the provider or the connectivity
    // changed; capabilities() may differ.
    void changed();

private:
    using Token = RequestScope::Token;

    [[nodiscard]] ModeConditions conditions() const;
    [[nodiscard]] bool readProviderConfigured();
    [[nodiscard]] SourceMode resolveStoredMode();
    [[nodiscard]] ApiKeyConnectOutcome finishConnect(Token token, const QString& baseUrl, const QString& apiKey,
                                                     const Result<provider::ProviderStatus>& status);
    // Stores and publishes the mode and sets the database context; false
    // when the database refused the context.
    bool commitMode(SourceMode mode);
    bool applyDatabaseContext();
    void onSessionStateChanged();
    void onOwnerChanged(const QString& ownerId);

    SettingsStore& m_settings;
    account::SecretStore& m_secrets;
    account::AccountSessionService& m_session;
    provider::ProviderClient& m_provider;
    db::AccountRepository& m_accounts;
    UserDataOperationGate& m_operationGate;

    SourceMode m_mode = SourceMode::LocalOnly;
    bool m_providerConfigured = false;
    QStringList m_providerCapabilities;
    Connectivity m_connectivity;
    quint64 m_generation = 1;
    // Pending connects; a newer connect, a disconnect or a mode change makes
    // the older ones stale.
    RequestScope m_connects;
};

} // namespace lmp::core::mode
