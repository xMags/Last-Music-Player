#include <QByteArray>
#include <QFuture>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include <chrono>

#include "FakeHttpTransport.h"
#include "core/account/AccountClient.h"
#include "core/account/AccountSessionProtocol.h"
#include "core/account/AccountUrlPolicy.h"
#include "core/app/AppIdentity.h"
#include "core/config/PrivateConfig.h"
#include "core/net/HttpClient.h"

using lmp::core::AppIdentity;
using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::PrivateConfig;
using lmp::core::Result;
using lmp::core::account::AccountCall;
using lmp::core::account::AccountClient;
using lmp::core::account::AccountProfile;
using lmp::core::account::LoopbackHttp;
using lmp::core::net::HttpMethod;
using lmp::core::net::HttpRequest;
using lmp::core::net::HttpResponse;
using lmp::core::net::RedirectPolicy;
using lmp::test::FakeHttpTransport;

namespace {

PrivateConfig exampleConfig()
{
    PrivateConfig config;
    config.accountApiOrigin = QStringLiteral("https://api.account.example.test/");
    config.accountFrontendOrigin = QStringLiteral("https://account.example.test");
    return config.withDefaults();
}

template <typename T>
[[nodiscard]] Result<T> resultOf(const QFuture<Result<T>>& future)
{
    if (!QTest::qWaitFor([&future]() { return future.isFinished(); }, 5000)) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("future did not finish"));
    }
    return future.result();
}

[[nodiscard]] QJsonObject bodyOf(const HttpRequest& request)
{
    return QJsonDocument::fromJson(request.body).object();
}

// Answers every request on its connection with one fixed response and
// remembers the raw request text.
class OneAnswerServer : public QObject {
public:
    explicit OneAnswerServer(QByteArray response)
        : m_response(std::move(response))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() { accept(); });
    }

    [[nodiscard]] bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    [[nodiscard]] QString origin() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    QByteArray received;
    int connections = 0;

private:
    void accept()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            ++connections;
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                received += socket->readAll();
                if (received.contains("\r\n\r\n")) {
                    socket->write(m_response);
                    socket->disconnectFromHost();
                }
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    QTcpServer m_server;
    QByteArray m_response;
};

} // namespace

class AccountClientTest : public QObject {
    Q_OBJECT

private slots:
    void requestCoreSendsTheFixedHeaders();
    void jsonBodyIsSentAsUtf8Json();
    void pathMustStayUnderVersionOne_data();
    void pathMustStayUnderVersionOne();
    void unconfiguredClientSendsNothing_data();
    void unconfiguredClientSendsNothing();
    void bearerWithALineBreakIsRefused();
    void failuresKeepTheirStatusAndRetryAfter();
    void exchangeSendsTheCodeGrant();
    void exchangeRejectsAnInvalidSession();
    void profileIsRequestedWithTheSession();
    void profileWithoutAnOwnerIsRejected();
    void logoutPostsAnEmptyObject();
    void authorizeUrlFollowsTheProtocol();
    void bearerSessionParsing();
    void profileParsing();
    void managementUrlUsesTheFrontendOrigin();
    void configuredBuildMatchesThePrivateConfig();
    void redirectIsNotFollowed();
};

void AccountClientTest::requestCoreSendsTheFixedHeaders()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    QVERIFY(client.isConfigured());
    QCOMPARE(client.baseOrigin(), QStringLiteral("https://api.account.example.test"));

    transport.script(QStringLiteral("/v1/auth/me"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(client.send(AccountCall{HttpMethod::Get, QStringLiteral("/v1/auth/me"),
                                             QStringLiteral("session-a"), {}})).isOk());
    QCOMPARE(transport.requests.size(), 1);
    const HttpRequest& request = transport.requests.front();
    QCOMPARE(request.method, HttpMethod::Get);
    QCOMPARE(request.url, QUrl(QStringLiteral("https://api.account.example.test/v1/auth/me")));
    QCOMPARE(request.header("X-Requested-With"), AppIdentity::accountRequestedWith().toUtf8());
    QCOMPARE(request.header("Accept"), QByteArray("application/json"));
    QCOMPARE(request.header("Authorization"), QByteArray("Bearer session-a"));
    QVERIFY(request.header("Content-Type").isEmpty());
    QVERIFY(request.body.isEmpty());
    QCOMPARE(request.redirects, RedirectPolicy::Refuse);
    QCOMPARE(request.timeout, std::chrono::milliseconds(std::chrono::seconds(20)));
    QVERIFY(request.maxResponseBytes > 0);
}

void AccountClientTest::jsonBodyIsSentAsUtf8Json()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    transport.script(QStringLiteral("/v1/thing"), FakeHttpTransport::ok("{}"));
    AccountCall call{HttpMethod::Post, QStringLiteral("/v1/thing"), {}, QByteArrayLiteral("{\"a\":1}")};
    call.timeout = std::chrono::seconds(5);
    QVERIFY(resultOf(client.send(call)).isOk());
    const HttpRequest& request = transport.requests.front();
    QCOMPARE(request.header("Content-Type"), QByteArray("application/json; charset=utf-8"));
    QCOMPARE(request.body, QByteArray("{\"a\":1}"));
    QVERIFY(request.header("Authorization").isEmpty());
    QCOMPARE(request.timeout, std::chrono::milliseconds(5000));
}

void AccountClientTest::pathMustStayUnderVersionOne_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("accepted");
    QTest::newRow("api path") << QStringLiteral("/v1/auth/me") << true;
    QTest::newRow("other version") << QStringLiteral("/v2/auth/me") << false;
    QTest::newRow("no leading slash") << QStringLiteral("v1/auth/me") << false;
    QTest::newRow("network path") << QStringLiteral("//v1/auth/me") << false;
    QTest::newRow("bare version") << QStringLiteral("/v1") << false;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("absolute") << QStringLiteral("https://other.example.test/v1/") << false;
}

void AccountClientTest::pathMustStayUnderVersionOne()
{
    QFETCH(QString, path);
    QFETCH(bool, accepted);
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    transport.script(path, FakeHttpTransport::ok("{}"));
    const Result<HttpResponse> result = resultOf(client.send(AccountCall{HttpMethod::Get, path, {}, {}}));
    QCOMPARE(result.isOk(), accepted);
    QCOMPARE(transport.requests.size(), accepted ? 1 : 0);
    if (!accepted) {
        QCOMPARE(result.failure().kind, FailureKind::Unknown);
    }
}

void AccountClientTest::unconfiguredClientSendsNothing_data()
{
    QTest::addColumn<QString>("apiOrigin");
    QTest::newRow("empty") << QString();
    QTest::newRow("public http") << QStringLiteral("http://api.account.example.test");
    QTest::newRow("with a path") << QStringLiteral("https://api.account.example.test/base");
}

void AccountClientTest::unconfiguredClientSendsNothing()
{
    QFETCH(QString, apiOrigin);
    FakeHttpTransport transport;
    PrivateConfig config;
    config.accountApiOrigin = apiOrigin;
    AccountClient client(transport, config.withDefaults(), LoopbackHttp::Refused);
    QVERIFY(!client.isConfigured());
    QVERIFY(client.baseOrigin().isEmpty());
    QVERIFY(client.managementUrl().isEmpty());
    QVERIFY(client.authorizeUrl(QStringLiteral("http://127.0.0.1:1/callback"),
                                {QStringLiteral("v"), QStringLiteral("c"), QStringLiteral("s")})
                .isEmpty());

    const Result<AccountProfile> profile = resultOf(client.fetchProfile(QStringLiteral("session-a")));
    QVERIFY(!profile);
    QCOMPARE(profile.failure().kind, FailureKind::NotAvailable);
    QVERIFY(!resultOf(client.exchangeCode(QStringLiteral("c"), QStringLiteral("r"), QStringLiteral("v"))));
    QVERIFY(!resultOf(client.logout(QStringLiteral("session-a"))));
    QCOMPARE(transport.requests.size(), 0);
}

void AccountClientTest::bearerWithALineBreakIsRefused()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    const Result<AccountProfile> profile = resultOf(client.fetchProfile(QStringLiteral("a\r\nX-Injected: 1")));
    QVERIFY(!profile);
    QCOMPARE(transport.requests.size(), 0);
}

void AccountClientTest::failuresKeepTheirStatusAndRetryAfter()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    Failure limited = Failure::of(FailureKind::RateLimited, QStringLiteral("HTTP status 429"), 429);
    limited.retryAfterSeconds = 70001;
    transport.script(QStringLiteral("/v1/auth/me"), limited);
    transport.script(QStringLiteral("/v1/sso/token"), FakeHttpTransport::failed(FailureKind::Unknown, 400));
    transport.script(QStringLiteral("/v1/auth/logout"), FakeHttpTransport::failed(FailureKind::Unauthorized, 401));

    const Result<AccountProfile> profile = resultOf(client.fetchProfile(QStringLiteral("session-a")));
    QVERIFY(!profile);
    QCOMPARE(profile.failure().kind, FailureKind::RateLimited);
    QCOMPARE(profile.failure().httpStatus, 429);
    QVERIFY(profile.failure().retryAfterSeconds == std::optional<int>(70001));

    const Result<QString> exchanged = resultOf(client.exchangeCode(QStringLiteral("c"), QStringLiteral("r"),
                                                                   QStringLiteral("v")));
    QVERIFY(!exchanged);
    QCOMPARE(exchanged.failure().httpStatus, 400);

    const Result<void> loggedOut = resultOf(client.logout(QStringLiteral("session-a")));
    QVERIFY(!loggedOut);
    QCOMPARE(loggedOut.failure().kind, FailureKind::Unauthorized);
}

void AccountClientTest::exchangeSendsTheCodeGrant()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    transport.script(QStringLiteral("/v1/sso/token"),
                     FakeHttpTransport::ok("{\"access_token\":\"opaque-session\",\"token_type\":\"Bearer\"}"));
    const Result<QString> bearer = resultOf(client.exchangeCode(
        QStringLiteral("code-a"), QStringLiteral("http://127.0.0.1:49152/callback"), QStringLiteral("verifier-a")));
    QVERIFY(bearer);
    QCOMPARE(bearer.value(), QStringLiteral("opaque-session"));

    const HttpRequest& request = transport.requests.front();
    QCOMPARE(request.method, HttpMethod::Post);
    QCOMPARE(request.url.path(), QStringLiteral("/v1/sso/token"));
    QVERIFY(request.header("Authorization").isEmpty());
    const QJsonObject body = bodyOf(request);
    QCOMPARE(body.size(), 5);
    QCOMPARE(body.value(QStringLiteral("grant_type")).toString(), QStringLiteral("authorization_code"));
    QCOMPARE(body.value(QStringLiteral("client_id")).toString(), AppIdentity::accountClientId());
    QCOMPARE(body.value(QStringLiteral("redirect_uri")).toString(), QStringLiteral("http://127.0.0.1:49152/callback"));
    QCOMPARE(body.value(QStringLiteral("code")).toString(), QStringLiteral("code-a"));
    QCOMPARE(body.value(QStringLiteral("code_verifier")).toString(), QStringLiteral("verifier-a"));
}

void AccountClientTest::exchangeRejectsAnInvalidSession()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    transport.script(QStringLiteral("/v1/sso/token"), FakeHttpTransport::ok("{\"access_token\":\"two words\"}"));
    const Result<QString> bearer = resultOf(client.exchangeCode(QStringLiteral("c"), QStringLiteral("r"),
                                                                QStringLiteral("v")));
    QVERIFY(!bearer);
    QCOMPARE(bearer.failure().kind, FailureKind::InvalidResponse);
}

void AccountClientTest::profileIsRequestedWithTheSession()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    transport.script(QStringLiteral("/v1/auth/me"),
                     FakeHttpTransport::ok("{\"user\":{\"id\":\"owner-1\",\"displayName\":\"Example User\","
                                           "\"username\":\"example\",\"plan\":\"Test\"}}"));
    const Result<AccountProfile> profile = resultOf(client.fetchProfile(QStringLiteral("session-a")));
    QVERIFY(profile);
    QCOMPARE(profile.value().id, QStringLiteral("owner-1"));
    QCOMPARE(profile.value().displayName, QStringLiteral("Example User"));
    QCOMPARE(profile.value().username, QStringLiteral("example"));
    QCOMPARE(profile.value().planLabel, QStringLiteral("Test"));
    const HttpRequest& request = transport.requests.front();
    QCOMPARE(request.method, HttpMethod::Get);
    QCOMPARE(request.url.path(), QStringLiteral("/v1/auth/me"));
    QCOMPARE(request.header("Authorization"), QByteArray("Bearer session-a"));
}

void AccountClientTest::profileWithoutAnOwnerIsRejected()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    transport.script(QStringLiteral("/v1/auth/me"), FakeHttpTransport::ok("{\"displayName\":\"No Id\"}"));
    const Result<AccountProfile> profile = resultOf(client.fetchProfile(QStringLiteral("session-a")));
    QVERIFY(!profile);
    QCOMPARE(profile.failure().kind, FailureKind::InvalidResponse);
}

void AccountClientTest::logoutPostsAnEmptyObject()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    transport.script(QStringLiteral("/v1/auth/logout"), FakeHttpTransport::ok({}, 204));
    QVERIFY(resultOf(client.logout(QStringLiteral("session-a"))).isOk());
    const HttpRequest& request = transport.requests.front();
    QCOMPARE(request.method, HttpMethod::Post);
    QCOMPARE(request.url.path(), QStringLiteral("/v1/auth/logout"));
    QCOMPARE(request.body, QByteArray("{}"));
    QCOMPARE(request.header("Authorization"), QByteArray("Bearer session-a"));
}

void AccountClientTest::authorizeUrlFollowsTheProtocol()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    const QString url = client.authorizeUrl(
        QStringLiteral("http://127.0.0.1:49152/callback"),
        {QStringLiteral("verifier-must-not-appear"), QStringLiteral("challenge value"), QStringLiteral("state value")});
    QVERIFY(url.startsWith(QStringLiteral("https://api.account.example.test/v1/sso/authorize?")));
    QVERIFY(url.contains(QStringLiteral("client_id=") + AppIdentity::accountClientId()));
    QVERIFY(url.contains(QStringLiteral("redirect_uri=http%3A%2F%2F127.0.0.1%3A49152%2Fcallback")));
    QVERIFY(url.contains(QStringLiteral("state=state%20value")));
    QVERIFY(url.contains(QStringLiteral("code_challenge=challenge%20value")));
    QVERIFY(url.contains(QStringLiteral("code_challenge_method=S256")));
    QVERIFY(!url.contains(QStringLiteral("verifier-must-not-appear")));

    QVERIFY(client.authorizeUrl({}, {QStringLiteral("v"), QStringLiteral("c"), QStringLiteral("s")}).isEmpty());
    QVERIFY(client.authorizeUrl(QStringLiteral("r"), {QStringLiteral("v"), QStringLiteral("c"), {}}).isEmpty());
    QVERIFY(client.authorizeUrl(QStringLiteral("r"), {QStringLiteral("v"), {}, QStringLiteral("s")}).isEmpty());
}

void AccountClientTest::bearerSessionParsing()
{
    using lmp::core::account::parseAccountBearerSession;
    QCOMPARE(parseAccountBearerSession("{\"access_token\":\"opaque-session\"}"), QStringLiteral("opaque-session"));
    QCOMPARE(parseAccountBearerSession("{\"access_token\":\"opaque-session==\",\"token_type\":\"bearer\"}"),
             QStringLiteral("opaque-session=="));
    QVERIFY(parseAccountBearerSession("{\"access_token\":\"opaque-session\",\"token_type\":\"DPoP\"}").isEmpty());
    QVERIFY(parseAccountBearerSession("{\"access_token\":\"opaque-session\",\"token_type\":7}").isEmpty());
    QVERIFY(parseAccountBearerSession("{\"access_token\":\"abc,def\"}").isEmpty());
    QVERIFY(parseAccountBearerSession("{\"access_token\":\"abc=def\"}").isEmpty());
    QVERIFY(parseAccountBearerSession("{\"access_token\":7}").isEmpty());
    QVERIFY(parseAccountBearerSession("{\"access_token\":\"line\\nbreak\"}").isEmpty());
    QVERIFY(parseAccountBearerSession("{\"access_token\":\"two words\"}").isEmpty());
    QVERIFY(parseAccountBearerSession("{\"access_token\":\"===\"}").isEmpty());
    QVERIFY(parseAccountBearerSession("not json").isEmpty());
    QVERIFY(parseAccountBearerSession("[]").isEmpty());
    const QByteArray longest(16 * 1024, 'a');
    QCOMPARE(parseAccountBearerSession("{\"access_token\":\"" + longest + "\"}").size(), 16 * 1024);
    QVERIFY(parseAccountBearerSession("{\"access_token\":\"" + longest + "a\"}").isEmpty());
}

void AccountClientTest::profileParsing()
{
    using lmp::core::account::parseAccountProfile;
    const AccountProfile direct = parseAccountProfile(
        "{\"id\":\"owner-1\",\"displayName\":\"Example User\",\"username\":\"example\",\"plan\":\"Test\","
        "\"avatarUrl\":\"https://cdn.example.test/avatar.png?size=128\"}");
    QCOMPARE(direct.id, QStringLiteral("owner-1"));
    QCOMPARE(direct.displayName, QStringLiteral("Example User"));
    QCOMPARE(direct.avatarUrl, QStringLiteral("https://cdn.example.test/avatar.png?size=128"));

    const AccountProfile nested = parseAccountProfile(
        "{\"user\":{\"id\":\"owner-2\",\"fullName\":\"Fallback Name\",\"username\":\"nested\"}}");
    QCOMPARE(nested.id, QStringLiteral("owner-2"));
    QCOMPARE(nested.displayName, QStringLiteral("Fallback Name"));

    QVERIFY(parseAccountProfile("{\"id\":\"owner with spaces\"}").id.isEmpty());
    QVERIFY(parseAccountProfile("{\"id\":\"owner\\n2\"}").id.isEmpty());
    QVERIFY(parseAccountProfile("{\"user\":\"owner-3\",\"id\":\"owner-3\"}").id.isEmpty());
    QVERIFY(parseAccountProfile("{\"id\":\"" + QByteArray(257, 'o') + "\"}").id.isEmpty());

    const AccountProfile bounded = parseAccountProfile(
        "{\"id\":\"owner-3\",\"displayName\":\"" + QByteArray(257, 'n') + "\",\"username\":\"valid\"}");
    QCOMPARE(bounded.id, QStringLiteral("owner-3"));
    QVERIFY(bounded.displayName.isEmpty());
    QCOMPARE(bounded.username, QStringLiteral("valid"));

    QVERIFY(parseAccountProfile("{\"id\":\"owner-4\",\"avatarUrl\":\"file:///C:/private.png\"}").avatarUrl.isEmpty());
    QVERIFY(parseAccountProfile(
                "{\"id\":\"owner-4\",\"avatarUrl\":\"https://cdn.example.test/avatar.png?access_token=secret\"}")
                .avatarUrl.isEmpty());
    QVERIFY(parseAccountProfile("{\"id\":\"owner-4\",\"avatarUrl\":\"https://cdn.example.test/avatar.png#fragment\"}")
                .avatarUrl.isEmpty());
    QCOMPARE(parseAccountProfile("{\"id\":\"owner-5\",\"avatarUrl\":\"data:image/png;base64,iVBORw==\"}").avatarUrl,
             QStringLiteral("data:image/png;base64,iVBORw=="));
}

void AccountClientTest::managementUrlUsesTheFrontendOrigin()
{
    FakeHttpTransport transport;
    AccountClient client(transport, exampleConfig());
    QCOMPARE(client.managementUrl(), QUrl(QStringLiteral("https://account.example.test/account")));

    PrivateConfig apiOnly;
    apiOnly.accountApiOrigin = QStringLiteral("https://api.account.example.test");
    AccountClient withoutFrontend(transport, apiOnly.withDefaults());
    QVERIFY(withoutFrontend.isConfigured());
    QVERIFY(withoutFrontend.managementUrl().isEmpty());
}

void AccountClientTest::configuredBuildMatchesThePrivateConfig()
{
    // Values are compared with QVERIFY only, so a failure never prints them.
    using lmp::core::account::isTrustedAccountOrigin;
    using lmp::core::account::normalizeTrustedAccountOrigin;
    const PrivateConfig config = PrivateConfig::fromBuild();
    FakeHttpTransport transport;
    const AccountClient client(transport, config);
    QVERIFY(client.isConfigured() == config.hasAccountService());
    if (!client.isConfigured()) {
        QVERIFY(config.accountFrontendOrigin.isEmpty());
        QVERIFY(config.accountMediaOrigin.isEmpty());
        QVERIFY(client.managementUrl().isEmpty());
        return;
    }
    QVERIFY(isTrustedAccountOrigin(config.accountApiOrigin));
    QVERIFY(client.baseOrigin() == normalizeTrustedAccountOrigin(config.accountApiOrigin));
    QVERIFY(!config.accountMediaOrigin.isEmpty());
    QVERIFY(isTrustedAccountOrigin(config.accountMediaOrigin));
    if (config.accountFrontendOrigin.isEmpty()) {
        QVERIFY(client.managementUrl().isEmpty());
        return;
    }
    QVERIFY(client.managementUrl()
            == QUrl(normalizeTrustedAccountOrigin(config.accountFrontendOrigin) + QStringLiteral("/account")));
    QVERIFY(client.managementUrl().scheme() == QStringLiteral("https"));
}

void AccountClientTest::redirectIsNotFollowed()
{
    OneAnswerServer target("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n"
                           "Connection: close\r\n\r\n{}");
    QVERIFY(target.listen());
    OneAnswerServer redirector("HTTP/1.1 302 Found\r\nLocation: " + target.origin().toLatin1()
                               + "/capture\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    QVERIFY(redirector.listen());

    lmp::core::net::HttpClient http;
    PrivateConfig config;
    config.accountApiOrigin = redirector.origin();
    AccountClient client(http, config.withDefaults(), LoopbackHttp::Allowed);
    QVERIFY(client.isConfigured());

    const Result<AccountProfile> profile = resultOf(client.fetchProfile(QStringLiteral("redirect-secret")));
    QVERIFY(!profile);
    QCOMPARE(profile.failure().httpStatus, 302);
    QVERIFY(redirector.received.toLower().contains("authorization: bearer redirect-secret"));
    QTest::qWait(100);
    QCOMPARE(target.connections, 0);
}

QTEST_GUILESS_MAIN(AccountClientTest)
#include "tst_account_client.moc"
