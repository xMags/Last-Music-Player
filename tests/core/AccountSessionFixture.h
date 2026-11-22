#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QDeadlineTimer>
#include <QHostAddress>
#include <QList>
#include <QString>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include <chrono>

#include "FakeCredentialStore.h"
#include "FakeHttpTransport.h"
#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/account/SecretStore.h"
#include "core/config/PrivateConfig.h"
#include "core/library/UserDataOperationGate.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/IExternalLinks.h"

namespace lmp::test {

// Records what the service asks the system to open, and opens nothing.
class FakeExternalLinks final : public platform::IExternalLinks {
public:
    bool open(const QUrl& url) override
    {
        opened.append(url);
        return accept;
    }

    QList<QUrl> opened;
    bool accept = true;
};

inline const QString kTokenPath = QStringLiteral("/v1/sso/token");
inline const QString kProfilePath = QStringLiteral("/v1/auth/me");

// A session service with fake network, credentials and browser, and a real
// settings file in a temporary folder.
class AccountSessionFixture {
public:
    explicit AccountSessionFixture(const QString& apiOrigin = QStringLiteral("https://api.account.example.test"))
        : settings(folder.filePath(QStringLiteral("Settings.json")))
        , secrets(credentials, settings)
        , client(transport, configWith(apiOrigin))
        , service({client, secrets, settings, links, gate})
    {
    }

    [[nodiscard]] static core::PrivateConfig configWith(const QString& apiOrigin)
    {
        core::PrivateConfig config;
        config.accountApiOrigin = apiOrigin;
        return config.withDefaults();
    }

    [[nodiscard]] static FakeHttpTransport::Outcome tokenAnswer(const QString& bearer)
    {
        return FakeHttpTransport::ok("{\"access_token\":\"" + bearer.toUtf8() + "\",\"token_type\":\"Bearer\"}");
    }

    [[nodiscard]] static FakeHttpTransport::Outcome profileAnswer(const QString& owner)
    {
        return FakeHttpTransport::ok("{\"user\":{\"id\":\"" + owner.toUtf8()
                                     + "\",\"displayName\":\"Name " + owner.toUtf8()
                                     + "\",\"username\":\"user\",\"plan\":\"Test\"}}");
    }

    // The stored session, "<none>" when there is none.
    [[nodiscard]] QString storedSession() const
    {
        return credentials.values.value(core::account::SecretStore::AccountSessionTarget, QStringLiteral("<none>"));
    }

    void storeSession(const QString& bearer)
    {
        credentials.values.insert(core::account::SecretStore::AccountSessionTarget, bearer);
    }

    void setHints(const QString& owner)
    {
        namespace keys = core::settings::keys;
        settings.setString(keys::AccountOwnerHint, owner);
        settings.setString(keys::AccountDisplayNameHint, QStringLiteral("Hinted ") + owner);
        settings.setString(keys::AccountUsernameHint, QStringLiteral("hinted"));
        settings.setString(keys::AccountPlanHint, QStringLiteral("Hinted plan"));
    }

    // A value from the last address the service opened in the browser.
    [[nodiscard]] QString authorizeValue(const QString& name) const
    {
        if (links.opened.isEmpty()) {
            return {};
        }
        return QUrlQuery(links.opened.last()).queryItemValue(name, QUrl::FullyDecoded);
    }

    // Plays the browser returning to the loopback address with `code` and
    // the state of the last opened sign-in. True when the listener answered.
    [[nodiscard]] bool returnFromBrowser(const QByteArray& code) const
    {
        const QUrl redirect(authorizeValue(QStringLiteral("redirect_uri")));
        const QByteArray state = QUrl::toPercentEncoding(authorizeValue(QStringLiteral("state")));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress(redirect.host()), static_cast<quint16>(redirect.port()));
        QDeadlineTimer deadline(std::chrono::seconds(3));
        while (socket.state() != QAbstractSocket::ConnectedState && socket.state() != QAbstractSocket::UnconnectedState
               && !deadline.hasExpired()) {
            QTest::qWait(5);
        }
        if (socket.state() != QAbstractSocket::ConnectedState) {
            return false;
        }
        const QByteArray authority = redirect.host().toLatin1() + ':' + QByteArray::number(redirect.port());
        socket.write("GET " + redirect.path().toLatin1() + "?code=" + code + "&state=" + state
                     + " HTTP/1.1\r\nHost: " + authority + "\r\nConnection: close\r\n\r\n");
        QByteArray received;
        while (socket.state() != QAbstractSocket::UnconnectedState && !deadline.hasExpired()) {
            received += socket.readAll();
            QTest::qWait(5);
        }
        received += socket.readAll();
        return received.startsWith("HTTP/1.1 200");
    }

    QTemporaryDir folder;
    FakeHttpTransport transport;
    FakeCredentialStore credentials;
    core::SettingsStore settings;
    core::account::SecretStore secrets;
    core::account::AccountClient client;
    FakeExternalLinks links;
    core::UserDataOperationGate gate;
    core::account::AccountSessionService service;
};

} // namespace lmp::test
