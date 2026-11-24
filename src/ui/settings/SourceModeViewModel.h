#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <optional>
#include <QtQml/qqmlregistration.h>

namespace lmp::core::account {
class AccountSessionService;
class AccountSignOutCoordinator;
class AccountClient;
} // namespace lmp::core::account

namespace lmp::core::db {
class LibraryQueries;
}

namespace lmp::core::mode {
class ModeService;
}

namespace lmp::platform {
class IExternalLinks;
}

namespace lmp::ui {

// The music source part of Settings (`App.source`): the three source cards,
// the API key form, the account block and the browser sign-in dialog. The
// rules come from the current app (MainWindow.Settings.cpp 265-505,
// MainWindow.ModeSetup.cpp): Offline and API key switch at once, an API key
// mode without a provider shows its form; Account needs a session, so
// picking it without one opens the sign-in, and a sign-in that succeeds
// switches to Account itself.
//
// GUI thread only.
class SourceModeViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.source")

    // "LocalOnly", "ApiKey" or "Account".
    Q_PROPERTY(QString mode READ mode NOTIFY changed FINAL)
    // One map per card, in the order Offline, Account, API key: mode, title,
    // description, iconName, selected, enabled, badge ("ACTIVE", "READY" or
    // "SET UP"), status and statusReady (the status dot is green).
    Q_PROPERTY(QVariantList cards READ cards NOTIFY changed FINAL)

    // The API key form, shown in API key mode.
    Q_PROPERTY(bool apiKeyShown READ apiKeyShown NOTIFY changed FINAL)
    // The saved address as typed, or the default local provider address.
    Q_PROPERTY(QString providerBaseUrl READ providerBaseUrl NOTIFY changed FINAL)
    Q_PROPERTY(bool providerConnected READ providerConnected NOTIFY changed FINAL)
    Q_PROPERTY(bool connecting READ connecting NOTIFY apiKeyStatusChanged FINAL)
    Q_PROPERTY(QString apiKeyStatus READ apiKeyStatus NOTIFY apiKeyStatusChanged FINAL)

    // The account block, shown in Account mode.
    Q_PROPERTY(bool accountShown READ accountShown NOTIFY changed FINAL)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed FINAL)
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY changed FINAL)
    Q_PROPERTY(bool accountOffline READ accountOffline NOTIFY changed FINAL)
    Q_PROPERTY(QString accountName READ accountName NOTIFY changed FINAL)
    // "@name", or empty.
    Q_PROPERTY(QString accountUsername READ accountUsername NOTIFY changed FINAL)
    Q_PROPERTY(QString accountPlan READ accountPlan NOTIFY changed FINAL)
    Q_PROPERTY(QString accountStatus READ accountStatus NOTIFY changed FINAL)
    // accountStatus is why the last sign-in or restore failed.
    Q_PROPERTY(bool accountStatusError READ accountStatusError NOTIFY changed FINAL)
    // The build names an account page to send the user to.
    Q_PROPERTY(bool canManageAccount READ canManageAccount NOTIFY changed FINAL)

    // The sign-in dialog's error line; empty when there is none.
    Q_PROPERTY(QString signInError READ signInError NOTIFY signInErrorChanged FINAL)
    Q_PROPERTY(bool signingOut READ signingOut NOTIFY changed FINAL)
    Q_PROPERTY(QString signOutError READ signOutError NOTIFY changed FINAL)

public:
    struct Parts {
        core::mode::ModeService& mode;
        core::account::AccountSessionService& session;
        const core::account::AccountClient& accountClient;
        core::account::AccountSignOutCoordinator& signOut;
        platform::IExternalLinks& links;
        // Null when the database did not open.
        core::db::LibraryQueries* library = nullptr;
    };

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit SourceModeViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] QString mode() const;
    [[nodiscard]] QVariantList cards() const;
    [[nodiscard]] bool apiKeyShown() const;
    [[nodiscard]] QString providerBaseUrl() const;
    [[nodiscard]] bool providerConnected() const;
    [[nodiscard]] bool connecting() const { return m_connecting; }
    [[nodiscard]] QString apiKeyStatus() const { return m_apiKeyStatus; }
    [[nodiscard]] bool accountShown() const;
    [[nodiscard]] bool signedIn() const;
    [[nodiscard]] bool signingIn() const;
    [[nodiscard]] bool accountOffline() const;
    [[nodiscard]] QString accountName() const;
    [[nodiscard]] QString accountUsername() const;
    [[nodiscard]] QString accountPlan() const;
    [[nodiscard]] QString accountStatus() const;
    [[nodiscard]] bool accountStatusError() const;
    [[nodiscard]] bool canManageAccount() const;
    [[nodiscard]] QString signInError() const { return m_signInError; }
    [[nodiscard]] bool signingOut() const { return m_signingOut; }
    [[nodiscard]] QString signOutError() const { return m_signOutError; }

    // A card was picked. Account without a session asks for the sign-in
    // dialog (signInRequested) and stays where it was.
    Q_INVOKABLE void choose(const QString& mode);

    // Tests the address and key, then stores them and enters API key mode;
    // the outcome shows in apiKeyStatus. `connected` reports success, so the
    // form can clear the key field.
    Q_INVOKABLE void connectProvider(const QString& baseUrl, const QString& apiKey);
    Q_INVOKABLE void disconnectProvider();

    // Opens the browser sign-in (again, when one is waiting). Success
    // switches to Account mode and reports signInFinished.
    Q_INVOKABLE void beginSignIn();
    // Stops waiting for the browser.
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void clearSignInError();

    Q_INVOKABLE void manageAccount();
    // Signs out; signingOut holds meanwhile. A sign-out that did not happen
    // leaves its message in signOutError, which the next attempt clears.
    Q_INVOKABLE void signOut();

signals:
    void changed();
    void apiKeyStatusChanged();
    void signInErrorChanged();
    void signInRequested();
    // The sign-in finished and Account mode is active.
    void signInFinished();
    void connected();

private:
    void setApiKeyStatus(const QString& status);
    void setSignInError(const QString& error);
    [[nodiscard]] QString localStatus() const;
    // The message for the last failed sign-in or restore, while signed out.
    [[nodiscard]] std::optional<QString> failureText() const;

    core::mode::ModeService& m_mode;
    core::account::AccountSessionService& m_session;
    const core::account::AccountClient& m_accountClient;
    core::account::AccountSignOutCoordinator& m_signOut;
    platform::IExternalLinks& m_links;
    core::db::LibraryQueries* m_library;

    bool m_connecting = false;
    bool m_signingOut = false;
    QString m_signOutError;
    QString m_apiKeyStatus;
    QString m_signInError;
    // Each connect and each sign-in click gets a number; only the newest
    // one reports.
    quint64 m_connectAttempt = 0;
    quint64 m_signInAttempt = 0;
};

} // namespace lmp::ui
