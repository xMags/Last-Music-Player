#pragma once

#include <QFuture>
#include <QString>
#include <QTest>

#include <optional>
#include <utility>

#include "AccountSessionFixture.h"
#include "FakeHttpTransport.h"
#include "TempLibrary.h"
#include "core/account/AccountSessionState.h"
#include "core/account/SecretStore.h"
#include "core/db/AccountRepository.h"
#include "core/mode/ModeService.h"
#include "core/provider/ProviderClient.h"
#include "core/settings/SettingKeys.h"

namespace lmp::test {

inline const QString kProviderStatusPath = QStringLiteral("/v1/providers");
inline const QString kProviderBase = QStringLiteral("https://provider.example.test");

// A mode service over the account session fixture (fake network and
// credentials, real settings file), a real library database in a temporary
// folder and a provider client on the same fake network.
class ModeServiceFixture {
public:
    ModeServiceFixture()
        : accounts(library.database())
        , provider(session.transport)
        , modes({session.settings, session.secrets, session.service, provider, accounts, session.gate})
    {
    }

    void storeProvider(const QString& base = kProviderBase, const QString& key = QStringLiteral("key-a"))
    {
        session.settings.setString(core::settings::keys::ProviderBaseUrl, base);
        session.credentials.values.insert(core::account::SecretStore::ProviderApiKeyTarget, key);
    }

    [[nodiscard]] QString storedKey() const
    {
        return session.credentials.values.value(core::account::SecretStore::ProviderApiKeyTarget,
                                                QStringLiteral("<none>"));
    }

    [[nodiscard]] QString storedMode() const
    {
        return session.settings.string(core::settings::keys::RemoteAccessMode, QStringLiteral("<none>"));
    }

    [[nodiscard]] QString contextMode()
    {
        return library.text("SELECT RemoteMode FROM ActiveAccountContext WHERE SingletonId=1;");
    }

    [[nodiscard]] QString contextAccount()
    {
        return library.text("SELECT AccountId FROM ActiveAccountContext WHERE SingletonId=1;");
    }

    // Restores a stored session the service confirms.
    [[nodiscard]] bool validate(const QString& owner)
    {
        session.storeSession(QStringLiteral("bearer-") + owner);
        session.transport.script(kProfilePath, AccountSessionFixture::profileAnswer(owner));
        session.service.restore();
        return QTest::qWaitFor(
            [this, owner]() {
                return session.service.state() == core::account::AccountSessionState::Validated
                    && session.service.profile().id == owner;
            },
            3000);
    }

    // Restores a stored session the service cannot be asked about.
    [[nodiscard]] bool restoreOffline(const QString& owner)
    {
        session.storeSession(QStringLiteral("bearer-") + owner);
        session.setHints(owner);
        session.transport.script(kProfilePath, FakeHttpTransport::failed(core::FailureKind::Network));
        session.service.restore();
        return waitForSession(core::account::AccountSessionState::Offline);
    }

    [[nodiscard]] bool waitForSession(core::account::AccountSessionState state) const
    {
        return QTest::qWaitFor([this, state]() { return session.service.state() == state; }, 3000);
    }

    // The outcome once the future finished, or nothing after 3 s.
    template <typename T>
    [[nodiscard]] static std::optional<T> outcomeOf(QFuture<T> future)
    {
        if (!QTest::qWaitFor([&future]() { return future.isFinished(); }, 3000) || future.resultCount() != 1) {
            return std::nullopt;
        }
        return future.result();
    }

    // Not an outcome the service has: what a connect that never finished reads as.
    static constexpr auto kNeverFinished = static_cast<core::mode::ApiKeyConnectOutcome>(-1);

    [[nodiscard]] static core::mode::ApiKeyConnectOutcome connectOutcome(
        QFuture<core::mode::ApiKeyConnectOutcome> future)
    {
        return outcomeOf(std::move(future)).value_or(kNeverFinished);
    }

    [[nodiscard]] core::mode::ApiKeyConnectOutcome connect(const QString& base, const QString& key)
    {
        return connectOutcome(modes.connectApiKey(base, key));
    }

    AccountSessionFixture session;
    TempLibrary library;
    core::db::AccountRepository accounts;
    core::provider::ProviderClient provider;
    core::mode::ModeService modes;
};

} // namespace lmp::test
