#include "ui/settings/SourceModeViewModel.h"

#include <QFuture>
#include <QUrl>
#include <QVariantMap>

#include <optional>

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/account/AccountSessionState.h"
#include "core/account/AccountSignOutCoordinator.h"
#include "core/async/Result.h"
#include "core/db/LibraryQueries.h"
#include "core/db/LibraryQueryTypes.h"
#include "core/mode/ModeService.h"
#include "core/mode/SourceMode.h"
#include "core/net/UserMessages.h"
#include "core/provider/ProviderClient.h"
#include "platform/IExternalLinks.h"

namespace lmp::ui {

namespace {

using core::account::AccountSessionState;
using core::mode::ApiKeyConnectOutcome;
using core::mode::ApiKeyDisconnectOutcome;
using core::mode::SourceMode;

// What the form says after a connect, in the current app's words where it
// had them.
QString connectStatusText(ApiKeyConnectOutcome outcome)
{
    switch (outcome) {
    case ApiKeyConnectOutcome::Connected:
        return SourceModeViewModel::tr("Configured");
    case ApiKeyConnectOutcome::MissingAddress:
        return SourceModeViewModel::tr("Missing provider URL");
    case ApiKeyConnectOutcome::InvalidAddress:
        return SourceModeViewModel::tr("Enter an http or https address");
    case ApiKeyConnectOutcome::MissingKey:
        return SourceModeViewModel::tr("Enter a new API key");
    case ApiKeyConnectOutcome::InvalidKey:
        return SourceModeViewModel::tr("This key has characters that cannot be sent");
    case ApiKeyConnectOutcome::Unauthorized:
        return SourceModeViewModel::tr("Unauthorized");
    case ApiKeyConnectOutcome::Unavailable:
        return SourceModeViewModel::tr("Provider unavailable");
    case ApiKeyConnectOutcome::Busy:
        return SourceModeViewModel::tr("Cleanup is in progress");
    case ApiKeyConnectOutcome::StorageFailed:
        return SourceModeViewModel::tr("Could not store API key");
    case ApiKeyConnectOutcome::Superseded:
        break;
    }
    return {};
}

QString disconnectStatusText(ApiKeyDisconnectOutcome outcome)
{
    switch (outcome) {
    case ApiKeyDisconnectOutcome::Disconnected:
        return SourceModeViewModel::tr("Not configured");
    case ApiKeyDisconnectOutcome::AddressCleanupFailed:
        return SourceModeViewModel::tr("API key removed; provider URL cleanup failed");
    case ApiKeyDisconnectOutcome::KeyRemovalFailed:
        return SourceModeViewModel::tr("Could not remove API key");
    case ApiKeyDisconnectOutcome::Busy:
        return SourceModeViewModel::tr("Cleanup is in progress");
    }
    return {};
}

std::optional<SourceMode> modeFromName(const QString& name)
{
    if (name == core::mode::LocalOnlyModeName) {
        return SourceMode::LocalOnly;
    }
    if (name == core::mode::ApiKeyModeName) {
        return SourceMode::ApiKey;
    }
    if (name == core::mode::AccountModeName) {
        return SourceMode::Account;
    }
    return std::nullopt;
}

QVariantMap card(SourceMode mode, const QString& title, const QString& description, const QString& iconName)
{
    return {
        {QStringLiteral("mode"), QString(core::mode::sourceModeName(mode))},
        {QStringLiteral("title"), title},
        {QStringLiteral("description"), description},
        {QStringLiteral("iconName"), iconName},
    };
}

void setState(QVariantMap& card, bool selected, bool ready, bool enabled, const QString& status, bool statusReady)
{
    card.insert(QStringLiteral("selected"), selected);
    card.insert(QStringLiteral("enabled"), enabled);
    card.insert(QStringLiteral("badge"), selected ? SourceModeViewModel::tr("ACTIVE")
                                                  : (ready ? SourceModeViewModel::tr("READY")
                                                           : SourceModeViewModel::tr("SET UP")));
    card.insert(QStringLiteral("status"), status);
    card.insert(QStringLiteral("statusReady"), statusReady);
}

} // namespace

SourceModeViewModel::SourceModeViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_mode(parts.mode)
    , m_session(parts.session)
    , m_accountClient(parts.accountClient)
    , m_signOut(parts.signOut)
    , m_links(parts.links)
    , m_library(parts.library)
{
    connect(&m_mode, &core::mode::ModeService::changed, this, &SourceModeViewModel::changed);
    connect(&m_session, &core::account::AccountSessionService::stateChanged, this, &SourceModeViewModel::changed);
}

QString SourceModeViewModel::mode() const
{
    return QString(core::mode::sourceModeName(m_mode.mode()));
}

QString SourceModeViewModel::localStatus() const
{
    if (m_library == nullptr) {
        return tr("Local library ready");
    }
    const core::db::LibraryStats stats = m_library->libraryStats();
    return tr("%1 tracks · %2 albums indexed").arg(stats.songCount).arg(stats.albumCount);
}

QVariantList SourceModeViewModel::cards() const
{
    const SourceMode current = m_mode.mode();
    const bool accountAvailable = m_session.isAvailable();
    const bool providerReady = m_mode.isAvailable(SourceMode::ApiKey);

    QVariantMap offline = card(SourceMode::LocalOnly, tr("Offline"),
                               tr("Everything plays from your music folder. Nothing leaves this PC."),
                               QStringLiteral("laptop"));
    setState(offline, current == SourceMode::LocalOnly, true, true, localStatus(), true);

    QVariantMap account = card(SourceMode::Account, tr("Account"),
                               tr("Sync your library and playlists across your devices."), QStringLiteral("person"));
    QString accountStatusLine;
    if (signedIn()) {
        const QString username = m_session.profile().username;
        accountStatusLine = username.isEmpty() ? tr("Signed in") : tr("Signed in as @%1").arg(username);
    } else {
        accountStatusLine = accountAvailable ? tr("Not signed in") : tr("Unavailable in this build");
    }
    setState(account, current == SourceMode::Account, signedIn(), accountAvailable, accountStatusLine, signedIn());

    QVariantMap apiKey = card(SourceMode::ApiKey, tr("API key"), tr("Point the app at your own Music API provider."),
                              QStringLiteral("lock"));
    setState(apiKey, current == SourceMode::ApiKey, providerReady, true,
             providerReady ? tr("Provider configured") : tr("No provider configured"), providerReady);

    return {offline, account, apiKey};
}

bool SourceModeViewModel::apiKeyShown() const
{
    return m_mode.apiKeySelected();
}

QString SourceModeViewModel::providerBaseUrl() const
{
    const QString saved = m_mode.providerBaseUrl();
    return saved.isEmpty() ? QString(core::provider::DefaultProviderBaseUrl) : saved;
}

bool SourceModeViewModel::providerConnected() const
{
    return m_mode.isAvailable(SourceMode::ApiKey);
}

bool SourceModeViewModel::accountShown() const
{
    return m_mode.accountSelected();
}

bool SourceModeViewModel::signedIn() const
{
    const AccountSessionState state = m_session.state();
    return state == AccountSessionState::Validated || state == AccountSessionState::Offline;
}

bool SourceModeViewModel::signingIn() const
{
    return m_session.state() == AccountSessionState::SigningIn;
}

bool SourceModeViewModel::accountOffline() const
{
    return m_session.state() == AccountSessionState::Offline;
}

QString SourceModeViewModel::accountName() const
{
    const core::account::AccountProfile& profile = m_session.profile();
    const QString name = profile.displayName.isEmpty() ? profile.username : profile.displayName;
    return name.isEmpty() ? tr("Account") : name;
}

QString SourceModeViewModel::accountUsername() const
{
    const QString username = m_session.profile().username;
    return username.isEmpty() ? QString() : QStringLiteral("@") + username;
}

QString SourceModeViewModel::accountPlan() const
{
    const QString plan = m_session.profile().planLabel;
    return plan.isEmpty() ? tr("Account library") : plan;
}

QString SourceModeViewModel::accountStatus() const
{
    if (!m_session.isAvailable()) {
        return tr("Account integration is unavailable in this build.");
    }
    switch (m_session.state()) {
    case AccountSessionState::SigningIn:
        return tr("Waiting for browser sign-in...");
    case AccountSessionState::Validated:
        return tr("Connected");
    case AccountSessionState::Offline:
        return tr("Offline. Showing the cached account library.");
    case AccountSessionState::SignedOut:
        break;
    }
    return failureText().value_or(tr("Not signed in"));
}

bool SourceModeViewModel::accountStatusError() const
{
    return m_session.isAvailable() && failureText().has_value();
}

std::optional<QString> SourceModeViewModel::failureText() const
{
    if (m_session.state() != AccountSessionState::SignedOut) {
        return std::nullopt;
    }
    const std::optional<core::Failure>& failure = m_session.lastFailure();
    if (!failure) {
        return std::nullopt;
    }
    const std::optional<core::net::UserMessage> message = core::net::userMessageForSignInFailure(*failure);
    if (!message) {
        return std::nullopt;
    }
    return core::net::userMessageText(*message);
}

bool SourceModeViewModel::canManageAccount() const
{
    return signedIn() && m_accountClient.managementUrl().isValid();
}

void SourceModeViewModel::choose(const QString& name)
{
    const std::optional<SourceMode> wanted = modeFromName(name);
    if (!wanted || *wanted == m_mode.mode()) {
        return;
    }
    // Its session comes from a browser round trip, so signing in comes
    // first and switches the mode itself.
    if (*wanted == SourceMode::Account && !m_mode.isAvailable(SourceMode::Account)) {
        if (m_session.isAvailable()) {
            emit signInRequested();
        }
        return;
    }
    if (!m_mode.setMode(*wanted)) {
        // Refused: the cards still show the mode the service kept.
        emit changed();
    }
}

void SourceModeViewModel::connectProvider(const QString& baseUrl, const QString& apiKey)
{
    const quint64 attempt = ++m_connectAttempt;
    m_connecting = true;
    setApiKeyStatus(tr("Connecting..."));
    m_mode.connectApiKey(baseUrl, apiKey).then(this, [this, attempt](ApiKeyConnectOutcome outcome) {
        // A newer connect or a disconnect has its own status.
        if (attempt != m_connectAttempt) {
            return;
        }
        m_connecting = false;
        setApiKeyStatus(connectStatusText(outcome));
        if (outcome == ApiKeyConnectOutcome::Connected) {
            emit connected();
        }
    });
}

void SourceModeViewModel::disconnectProvider()
{
    ++m_connectAttempt;
    m_connecting = false;
    setApiKeyStatus(disconnectStatusText(m_mode.disconnectApiKey()));
    emit changed();
}

void SourceModeViewModel::beginSignIn()
{
    const quint64 attempt = ++m_signInAttempt;
    setSignInError(QString());
    m_session.beginSignIn().then(this, [this, attempt](const core::Result<void>& result) {
        // A click while waiting starts a new sign-in; the one it replaced
        // finishes as cancelled and must not touch the dialog.
        if (attempt != m_signInAttempt) {
            return;
        }
        if (!result) {
            const std::optional<core::net::UserMessage> message =
                core::net::userMessageForSignInFailure(result.failure());
            setSignInError(message ? core::net::userMessageText(*message) : QString());
            return;
        }
        if (!m_mode.setMode(SourceMode::Account)) {
            setSignInError(tr("Could not activate the account library."));
            return;
        }
        emit signInFinished();
    });
}

void SourceModeViewModel::cancelSignIn()
{
    ++m_signInAttempt;
    m_session.cancelSignIn();
    setSignInError(QString());
}

void SourceModeViewModel::clearSignInError()
{
    setSignInError(QString());
}

void SourceModeViewModel::manageAccount()
{
    const QUrl url = m_accountClient.managementUrl();
    if (url.isValid()) {
        [[maybe_unused]] const bool opened = m_links.open(url);
    }
}

void SourceModeViewModel::signOut()
{
    m_signOut.signOut().then(this, [this](const core::account::AccountLogoutResult& result) {
        if (!result.signedOut) {
            // Nothing changed; the block keeps showing the session.
            emit changed();
        }
    });
}

void SourceModeViewModel::setApiKeyStatus(const QString& status)
{
    m_apiKeyStatus = status;
    emit apiKeyStatusChanged();
}

void SourceModeViewModel::setSignInError(const QString& error)
{
    if (m_signInError == error) {
        return;
    }
    m_signInError = error;
    emit signInErrorChanged();
}

} // namespace lmp::ui
