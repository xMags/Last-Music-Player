#pragma once

#include "core/account/AccountSessionProtocol.h"
#include "core/account/AccountUrlPolicy.h"
#include "core/account/Pkce.h"
#include "core/async/Result.h"
#include "core/config/PrivateConfig.h"
#include "core/net/HttpMessage.h"
#include "core/net/IHttpTransport.h"

#include <QByteArray>
#include <QFuture>
#include <QString>
#include <QUrl>
#include <QtGlobal>

#include <chrono>

namespace lmp::core::account {

// One request to the account service.
struct AccountCall {
    net::HttpMethod method = net::HttpMethod::Get;
    // Must start with "/v1/"; it is appended to the configured origin as is,
    // so the caller percent-encodes every value it puts in the query.
    QString pathAndQuery;
    // Sent as "Authorization: Bearer <session>" when not empty.
    QString bearerSession;
    // Sent as UTF-8 JSON when not empty.
    QByteArray jsonBody;
    std::chrono::milliseconds timeout{std::chrono::seconds(20)};
    qint64 maxResponseBytes = 32 * 1024 * 1024;
};

// The account service's HTTP API.
//
// Every request goes to the configured API origin only (never another host),
// carries "X-Requested-With" and "Accept: application/json", and is never
// redirected: a 3xx answer is a failure, so a bearer session cannot follow a
// redirect to another host. Failures are the transport's (net::HttpClient):
// the HTTP status and any Retry-After delay are on the Failure. A client
// without a trusted origin refuses every call with NotAvailable, and a path
// outside "/v1/" with Unknown, without sending anything.
//
// Holds no state between calls. The transport must outlive the client;
// futures finish on the transport's thread.
class AccountClient {
public:
    AccountClient(net::IHttpTransport& transport, const PrivateConfig& config,
                  LoopbackHttp loopback = loopbackHttpForThisBuild());

    // True when the API origin is a trusted origin.
    [[nodiscard]] bool isConfigured() const { return !m_baseOrigin.isEmpty(); }
    // The API origin without a trailing slash, or empty.
    [[nodiscard]] QString baseOrigin() const { return m_baseOrigin; }

    // "<frontend origin>/account", the page where the user manages the
    // account; empty when the build has no trusted frontend origin.
    [[nodiscard]] QUrl managementUrl() const;

    // The browser address that starts a sign-in (buildAccountAuthorizeUrl),
    // or empty when the client is not configured.
    [[nodiscard]] QString authorizeUrl(const QString& redirectUri, const PkceTransaction& transaction) const;

    // The request core every call goes through.
    [[nodiscard]] QFuture<Result<net::HttpResponse>> send(const AccountCall& call);

    // POST /v1/sso/token: trades the code from the browser for a bearer
    // session. InvalidResponse when the answer holds no valid session.
    [[nodiscard]] QFuture<Result<QString>> exchangeCode(const QString& code, const QString& redirectUri,
                                                        const QString& verifier);

    // GET /v1/auth/me. InvalidResponse when the answer has no valid owner id.
    [[nodiscard]] QFuture<Result<AccountProfile>> fetchProfile(const QString& bearerSession);

    // POST /v1/auth/logout: ends the session on the service.
    [[nodiscard]] QFuture<Result<void>> logout(const QString& bearerSession);

private:
    net::IHttpTransport& m_transport;
    QString m_baseOrigin;
    QString m_frontendOrigin;
    LoopbackHttp m_loopback;
};

} // namespace lmp::core::account
