// The browser sign-in half of AccountSessionService.

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "platform/IExternalLinks.h"

#include <QUrl>
#include <QFuture>

#include <utility>

namespace lmp::core::account {

namespace {

[[nodiscard]] QFuture<Result<void>> refusedSignIn(Failure failure)
{
    return QtFuture::makeReadyValueFuture(Result<void>(std::move(failure)));
}

} // namespace

QFuture<Result<void>> AccountSessionService::beginSignIn()
{
    std::optional<UserDataOperationGate::Lease> lease = m_operationGate.tryEnter();
    if (!lease) {
        return refusedSignIn(Failure::of(FailureKind::NotAvailable, QStringLiteral("user data is being cleaned up")));
    }
    const Token token = beginIntent();
    if (!m_client.isConfigured()) {
        // Nothing starts, so the state stays; only the reason is reported.
        m_lastFailure = Failure::of(FailureKind::NotAvailable, QStringLiteral("account service is not configured"));
        emit stateChanged();
        return refusedSignIn(*m_lastFailure);
    }

    m_signIn.emplace();
    m_signIn->token = token;
    m_signIn->lease = std::move(lease);
    m_signIn->promise.start();
    const QFuture<Result<void>> outcome = m_signIn->promise.future();

    // The profile and session of a previous sign-in stay until this one ends.
    ++m_generation;
    m_state = AccountSessionState::SigningIn;
    m_lastFailure.reset();
    emit stateChanged();
    if (!isSignInCurrent(token)) {
        return outcome;
    }

    const PkceTransaction transaction = createPkceTransaction();
    const QFuture<Result<QString>> code = m_callback.start(transaction.state);
    openBrowser(token, transaction, code);
    return outcome;
}

void AccountSessionService::openBrowser(Token token, const PkceTransaction& transaction,
                                        QFuture<Result<QString>> code)
{
    const QString redirectUri = m_callback.redirectUri();
    if (redirectUri.isEmpty()) {
        failSignIn(token, Failure::of(FailureKind::Unknown, QStringLiteral("sign-in listener could not start")));
        return;
    }
    const QString authorize = m_client.authorizeUrl(redirectUri, transaction);
    const QUrl address = QUrl::fromEncoded(authorize.toLatin1(), QUrl::StrictMode);
    if (authorize.isEmpty() || !address.isValid() || !m_links.open(address)) {
        // No continuation is attached yet, so ending the wait finishes
        // nothing but the listener.
        m_callback.cancel();
        failSignIn(token, Failure::of(FailureKind::Unknown, QStringLiteral("browser could not be opened")));
        return;
    }
    if (!isSignInCurrent(token)) {
        return;
    }
    m_signIn->verifier = transaction.verifier;
    m_signIn->redirectUri = redirectUri;
    code.then(this, [this, token](const Result<QString>& result) { onCallbackFinished(token, result); });
}

void AccountSessionService::cancelSignIn()
{
    // Only the wait for the browser can be stopped; an exchange already sent
    // runs to its end, as before.
    m_callback.cancel();
}

bool AccountSessionService::isSignInCurrent(Token token) const
{
    return m_intents.isCurrent(token) && m_signIn && m_signIn->token == token;
}

void AccountSessionService::onCallbackFinished(Token token, const Result<QString>& code)
{
    if (!isSignInCurrent(token)) {
        return;
    }
    if (!code) {
        Failure failure = code.failure();
        // The listener reports a local socket problem as Network; it is not
        // a problem reaching the service, so it must not read like one.
        if (failure.kind == FailureKind::Network) {
            failure.kind = FailureKind::Unknown;
        }
        failSignIn(token, failure);
        return;
    }
    const QString verifier = std::exchange(m_signIn->verifier, QString());
    m_client.exchangeCode(code.value(), m_signIn->redirectUri, verifier)
        .then(this, [this, token](const Result<QString>& bearer) { onCodeExchanged(token, bearer); });
}

void AccountSessionService::onCodeExchanged(Token token, const Result<QString>& bearer)
{
    if (!isSignInCurrent(token)) {
        return;
    }
    if (!bearer) {
        failSignIn(token, bearer.failure());
        return;
    }
    const QString session = bearer.value();
    m_client.fetchProfile(session).then(this, [this, token, session](const Result<AccountProfile>& profile) {
        onSignInProfile(token, session, profile);
    });
}

void AccountSessionService::onSignInProfile(Token token, const QString& bearer, const Result<AccountProfile>& profile)
{
    if (!isSignInCurrent(token)) {
        return;
    }
    if (!profile) {
        failSignIn(token, profile.failure());
        return;
    }
    if (!commitValidated(token, profile.value(), bearer, true)) {
        failSignIn(token, Failure::of(FailureKind::Storage, QStringLiteral("session could not be stored and read back")));
        return;
    }
    finishSignIn(token, Result<void>::success());
}

void AccountSessionService::failSignIn(Token token, const Failure& failure)
{
    // The stored credential is not touched here: before the commit nothing
    // was written, and a commit that wrote a session it could not read back
    // has already taken it out again.
    commitSignedOut(token, CredentialChange::Keep, {}, failure);
    finishSignIn(token, failure);
}

void AccountSessionService::finishSignIn(Token token, Result<void> result)
{
    if (!m_signIn || m_signIn->token != token) {
        return;
    }
    SignInAttempt attempt = std::move(*m_signIn);
    m_signIn.reset();
    [[maybe_unused]] const bool stored = attempt.promise.addResult(std::move(result));
    attempt.promise.finish();
}

} // namespace lmp::core::account
