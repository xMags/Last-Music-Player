#include "core/account/AccountClient.h"

#include "core/app/AppIdentity.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QFuture>

#include <algorithm>
#include <optional>

namespace lmp::core::account {

namespace {

template <typename T>
[[nodiscard]] QFuture<Result<T>> failedFuture(Failure failure)
{
    return QtFuture::makeReadyValueFuture(Result<T>(std::move(failure)));
}

// Header values must not carry line breaks or other control characters,
// which could end the header early.
[[nodiscard]] bool isSafeHeaderText(const QString& value)
{
    return std::none_of(value.cbegin(), value.cend(), [](QChar character) {
        const char16_t unit = character.unicode();
        return unit < 0x20 || unit == 0x7f;
    });
}

[[nodiscard]] std::optional<Failure> validateCall(const AccountCall& call, bool configured)
{
    // Details never quote the path or the session: both can carry secrets.
    if (!configured) {
        return Failure::of(FailureKind::NotAvailable, QStringLiteral("account service is not configured"));
    }
    if (!call.pathAndQuery.startsWith(QLatin1StringView("/v1/"))
        || call.pathAndQuery.startsWith(QLatin1StringView("//"))) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("account path is invalid"));
    }
    if (!isSafeHeaderText(call.bearerSession)) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("account session is not valid header text"));
    }
    return std::nullopt;
}

} // namespace

AccountClient::AccountClient(net::IHttpTransport& transport, const PrivateConfig& config, LoopbackHttp loopback)
    : m_transport(transport)
    , m_baseOrigin(normalizeTrustedAccountOrigin(config.accountApiOrigin, loopback))
    , m_frontendOrigin(normalizeTrustedAccountOrigin(config.accountFrontendOrigin, loopback))
    , m_loopback(loopback)
{
}

QUrl AccountClient::managementUrl() const
{
    if (m_frontendOrigin.isEmpty()) {
        return {};
    }
    return QUrl(m_frontendOrigin + QStringLiteral("/account"), QUrl::StrictMode);
}

QString AccountClient::authorizeUrl(const QString& redirectUri, const PkceTransaction& transaction) const
{
    return buildAccountAuthorizeUrl(m_baseOrigin, redirectUri, transaction);
}

QFuture<Result<net::HttpResponse>> AccountClient::send(const AccountCall& call)
{
    if (std::optional<Failure> invalid = validateCall(call, isConfigured())) {
        return failedFuture<net::HttpResponse>(std::move(*invalid));
    }

    net::HttpRequest request;
    request.method = call.method;
    request.url = QUrl(m_baseOrigin + call.pathAndQuery, QUrl::StrictMode);
    request.timeout = call.timeout;
    request.redirects = net::RedirectPolicy::Refuse;
    request.maxResponseBytes = call.maxResponseBytes;
    request.headers.append({QByteArrayLiteral("X-Requested-With"), AppIdentity::accountRequestedWith().toUtf8()});
    request.headers.append({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
    if (!call.bearerSession.isEmpty()) {
        request.headers.append({QByteArrayLiteral("Authorization"), "Bearer " + call.bearerSession.toUtf8()});
    }
    if (!call.jsonBody.isEmpty()) {
        request.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json; charset=utf-8")});
        request.body = call.jsonBody;
    }
    return m_transport.send(request);
}

QFuture<Result<QString>> AccountClient::exchangeCode(const QString& code, const QString& redirectUri,
                                                     const QString& verifier)
{
    const QJsonObject payload{
        {QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
        {QStringLiteral("client_id"), AppIdentity::accountClientId()},
        {QStringLiteral("redirect_uri"), redirectUri},
        {QStringLiteral("code"), code},
        {QStringLiteral("code_verifier"), verifier},
    };
    AccountCall call;
    call.method = net::HttpMethod::Post;
    call.pathAndQuery = QStringLiteral("/v1/sso/token");
    call.jsonBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    // A plain transform of the answer: it captures nothing that could be
    // gone when it runs, so it needs no context object.
    return send(call).then([](const Result<net::HttpResponse>& response) -> Result<QString> {
        if (!response) {
            return response.failure();
        }
        QString bearer = parseAccountBearerSession(response.value().body);
        if (bearer.isEmpty()) {
            return Failure::of(FailureKind::InvalidResponse, QStringLiteral("token answer holds no valid session"));
        }
        return bearer;
    });
}

QFuture<Result<AccountProfile>> AccountClient::fetchProfile(const QString& bearerSession)
{
    AccountCall call;
    call.method = net::HttpMethod::Get;
    call.pathAndQuery = QStringLiteral("/v1/auth/me");
    call.bearerSession = bearerSession;
    const LoopbackHttp loopback = m_loopback;
    return send(call).then([loopback](const Result<net::HttpResponse>& response) -> Result<AccountProfile> {
        if (!response) {
            return response.failure();
        }
        AccountProfile profile = parseAccountProfile(response.value().body, loopback);
        if (profile.id.isEmpty()) {
            return Failure::of(FailureKind::InvalidResponse, QStringLiteral("profile answer holds no valid owner"));
        }
        return profile;
    });
}

QFuture<Result<void>> AccountClient::logout(const QString& bearerSession)
{
    AccountCall call;
    call.method = net::HttpMethod::Post;
    call.pathAndQuery = QStringLiteral("/v1/auth/logout");
    call.bearerSession = bearerSession;
    call.jsonBody = QByteArrayLiteral("{}");
    return send(call).then([](const Result<net::HttpResponse>& response) -> Result<void> {
        if (!response) {
            return response.failure();
        }
        return Result<void>::success();
    });
}

} // namespace lmp::core::account
