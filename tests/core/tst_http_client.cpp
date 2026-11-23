#include <QByteArray>
#include <QElapsedTimer>
#include <QFuture>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include "core/net/HttpClient.h"

using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::net::HttpClient;
using lmp::core::net::HttpMethod;
using lmp::core::net::HttpRequest;
using lmp::core::net::HttpResponse;
using lmp::core::net::RedirectPolicy;

namespace {

struct ReceivedRequest {
    QByteArray method;
    QByteArray path;
    QHash<QByteArray, QByteArray> headers; // names in lower case
    QByteArray body;
};

// A one-request-per-connection HTTP/1.1 server on 127.0.0.1 with scripted
// answers per path. Unknown paths get a 404.
class TestServer : public QObject {
public:
    using Responder = std::function<void(QTcpSocket*)>;

    TestServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() { acceptConnections(); });
    }

    [[nodiscard]] bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }

    [[nodiscard]] QUrl url(const QByteArray& path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(QString::fromLatin1(path)));
    }

    void route(const QByteArray& path, Responder responder) { m_routes.insert(path, std::move(responder)); }

    static void respond(QTcpSocket* socket, int status, const QByteArray& body, const QByteArray& extraHeaders = {})
    {
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " Status\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "Connection: close\r\n" + extraHeaders + "\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QList<ReceivedRequest> requests;
    int closedConnections = 0;

private:
    void acceptConnections()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onData(socket); });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
                ++closedConnections;
                socket->deleteLater();
            });
        }
    }

    void onData(QTcpSocket* socket)
    {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }
        ReceivedRequest request;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        request.method = requestLine.value(0);
        request.path = requestLine.value(1);
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines[i].indexOf(':');
            if (colon > 0) {
                request.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
            }
        }
        const qsizetype length = request.headers.value("content-length", "0").toLongLong();
        if (buffer.size() < headerEnd + 4 + length) {
            return;
        }
        request.body = buffer.mid(headerEnd + 4, length);
        m_buffers.remove(socket);
        requests.append(request);

        const auto found = m_routes.constFind(request.path);
        if (found == m_routes.constEnd()) {
            respond(socket, 404, "missing");
            return;
        }
        found.value()(socket);
    }

    QTcpServer m_server;
    QHash<QByteArray, Responder> m_routes;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

[[nodiscard]] bool waitFor(const QFuture<Result<HttpResponse>>& future, int milliseconds = 10000)
{
    return QTest::qWaitFor([&future]() { return future.isFinished(); }, milliseconds);
}

} // namespace

class HttpClientTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void successCarriesStatusHeadersAndBody();
    void postBodyReachesTheServer();
    void statusCodesMapToFailureKinds_data();
    void statusCodesMapToFailureKinds();
    void retryAfterIsKeptOnTheFailure();
    void silentServerTimesOut();
    void refusedRedirectFailsAndIsNotFollowed();
    void allowedRedirectIsFollowed();
    void announcedOversizedBodyFails();
    void streamedOversizedBodyFails();
    void bodyAtTheLimitIsAccepted();
    void refusedConnectionIsNetwork();
    void requestWithoutTimeoutOrLimitIsRefused();
    void cancelAllFinishesAsCancelled();
    void destroyingTheClientFinishesAsCancelled();
    void cancellingTheFutureAbortsTheRequest();
    void headersOnlyStopsAtTheHeaders();
    void headersOnlyKeepsFailureStatuses();

private:
    [[nodiscard]] HttpRequest request(const QByteArray& path) const
    {
        HttpRequest result;
        result.url = m_server->url(path);
        result.timeout = std::chrono::seconds(5);
        result.maxResponseBytes = 1000;
        return result;
    }

    std::unique_ptr<TestServer> m_server;
};

void HttpClientTest::init()
{
    m_server = std::make_unique<TestServer>();
    QVERIFY(m_server->listen());
    m_server->route("/ok", [](QTcpSocket* socket) { TestServer::respond(socket, 200, "hello", "X-Test: present\r\n"); });
    m_server->route("/echo", [](QTcpSocket* socket) { TestServer::respond(socket, 201, "echoed"); });
    m_server->route("/hang", [](QTcpSocket*) {});
    m_server->route("/redirect", [](QTcpSocket* socket) {
        TestServer::respond(socket, 302, {}, "Location: /target\r\n");
    });
    m_server->route("/target", [](QTcpSocket* socket) { TestServer::respond(socket, 200, "target"); });
    m_server->route("/big-announced", [](QTcpSocket* socket) {
        // Announces more than the limit, then stalls after a few bytes.
        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 5000\r\n\r\n0123456789");
    });
    m_server->route("/big-streamed", [](QTcpSocket* socket) {
        const QByteArray chunk(600, 'a');
        socket->write("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
        socket->write("258\r\n" + chunk + "\r\n258\r\n" + chunk + "\r\n0\r\n\r\n");
        socket->disconnectFromHost();
    });
    m_server->route("/audio", [](QTcpSocket* socket) {
        // A large body that never finishes: only its headers may be waited for.
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 50000000\r\n\r\n0123456789");
    });
    m_server->route("/limited", [](QTcpSocket* socket) {
        TestServer::respond(socket, 429, "{}", "Retry-After: 70001\r\n");
    });
    m_server->route("/limited-without-delay", [](QTcpSocket* socket) { TestServer::respond(socket, 503, {}); });
    m_server->route("/at-limit", [](QTcpSocket* socket) { TestServer::respond(socket, 200, QByteArray(1000, 'b')); });
    for (int status : {400, 401, 403, 404, 409, 429, 500, 503}) {
        m_server->route("/status/" + QByteArray::number(status),
                        [status](QTcpSocket* socket) { TestServer::respond(socket, status, "error body"); });
    }
}

void HttpClientTest::successCarriesStatusHeadersAndBody()
{
    HttpClient client;
    const QFuture<Result<HttpResponse>> future = client.send(request("/ok"));
    QVERIFY(waitFor(future));
    const Result<HttpResponse> result = future.result();
    QVERIFY(result);
    QCOMPARE(result.value().status, 200);
    QCOMPARE(result.value().body, QByteArray("hello"));
    QCOMPARE(result.value().header("x-test"), QByteArray("present"));
    QCOMPARE(result.value().header("X-Missing"), QByteArray());
    QCOMPARE(client.pendingCount(), std::size_t{0});
}

void HttpClientTest::postBodyReachesTheServer()
{
    HttpClient client;
    HttpRequest post = request("/echo");
    post.method = HttpMethod::Post;
    post.headers = {{"Content-Type", "application/json"}, {"X-Requested-With", "test-client"}};
    post.body = R"({"a":1})";
    const QFuture<Result<HttpResponse>> future = client.send(post);
    QVERIFY(waitFor(future));
    QVERIFY(future.result());
    QCOMPARE(future.result().value().status, 201);

    QCOMPARE(m_server->requests.size(), 1);
    const ReceivedRequest& received = m_server->requests.first();
    QCOMPARE(received.method, QByteArray("POST"));
    QCOMPARE(received.body, QByteArray(R"({"a":1})"));
    QCOMPARE(received.headers.value("content-type"), QByteArray("application/json"));
    QCOMPARE(received.headers.value("x-requested-with"), QByteArray("test-client"));
}

void HttpClientTest::statusCodesMapToFailureKinds_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<FailureKind>("kind");
    QTest::newRow("400") << 400 << FailureKind::Unknown;
    QTest::newRow("401") << 401 << FailureKind::Unauthorized;
    QTest::newRow("403") << 403 << FailureKind::Forbidden;
    QTest::newRow("404") << 404 << FailureKind::NotFound;
    QTest::newRow("409") << 409 << FailureKind::Unknown;
    QTest::newRow("429") << 429 << FailureKind::RateLimited;
    QTest::newRow("500") << 500 << FailureKind::Server;
    QTest::newRow("503") << 503 << FailureKind::Server;
}

void HttpClientTest::statusCodesMapToFailureKinds()
{
    QFETCH(int, status);
    QFETCH(FailureKind, kind);
    HttpClient client;
    const QFuture<Result<HttpResponse>> future = client.send(request("/status/" + QByteArray::number(status)));
    QVERIFY(waitFor(future));
    const Result<HttpResponse> result = future.result();
    QVERIFY(!result);
    QCOMPARE(result.failure().kind, kind);
    QCOMPARE(result.failure().httpStatus, status);
}

void HttpClientTest::retryAfterIsKeptOnTheFailure()
{
    HttpClient client;
    const QFuture<Result<HttpResponse>> limited = client.send(request("/limited"));
    QVERIFY(waitFor(limited));
    QVERIFY(!limited.result());
    QCOMPARE(limited.result().failure().kind, FailureKind::RateLimited);
    QCOMPARE(limited.result().failure().httpStatus, 429);
    QVERIFY(limited.result().failure().retryAfterSeconds == std::optional<int>(70001));

    const QFuture<Result<HttpResponse>> unavailable = client.send(request("/limited-without-delay"));
    QVERIFY(waitFor(unavailable));
    QVERIFY(!unavailable.result());
    QCOMPARE(unavailable.result().failure().kind, FailureKind::Server);
    QVERIFY(!unavailable.result().failure().retryAfterSeconds.has_value());
}

void HttpClientTest::silentServerTimesOut()
{
    HttpClient client;
    HttpRequest hang = request("/hang");
    hang.timeout = std::chrono::milliseconds(300);
    QElapsedTimer elapsed;
    elapsed.start();
    const QFuture<Result<HttpResponse>> future = client.send(hang);
    QVERIFY(waitFor(future));
    QCOMPARE(future.result().failure().kind, FailureKind::Timeout);
    QVERIFY(elapsed.elapsed() >= 250);
    QVERIFY(elapsed.elapsed() < 3000);
}

void HttpClientTest::refusedRedirectFailsAndIsNotFollowed()
{
    HttpClient client;
    const QFuture<Result<HttpResponse>> future = client.send(request("/redirect"));
    QVERIFY(waitFor(future));
    const Result<HttpResponse> result = future.result();
    QVERIFY(!result);
    QCOMPARE(result.failure().kind, FailureKind::InvalidResponse);
    QCOMPARE(result.failure().httpStatus, 302);

    QTest::qWait(100);
    QCOMPARE(m_server->requests.size(), 1);
    QCOMPARE(m_server->requests.first().path, QByteArray("/redirect"));
}

void HttpClientTest::allowedRedirectIsFollowed()
{
    HttpClient client;
    HttpRequest follow = request("/redirect");
    follow.redirects = RedirectPolicy::Follow;
    const QFuture<Result<HttpResponse>> future = client.send(follow);
    QVERIFY(waitFor(future));
    const Result<HttpResponse> result = future.result();
    QVERIFY(result);
    QCOMPARE(result.value().body, QByteArray("target"));
    QCOMPARE(m_server->requests.size(), 2);
    QCOMPARE(m_server->requests.last().path, QByteArray("/target"));
}

void HttpClientTest::announcedOversizedBodyFails()
{
    HttpClient client;
    QElapsedTimer elapsed;
    elapsed.start();
    const QFuture<Result<HttpResponse>> future = client.send(request("/big-announced"));
    QVERIFY(waitFor(future));
    QCOMPARE(future.result().failure().kind, FailureKind::InvalidResponse);
    // Refused on the announcement, not by waiting for the timeout.
    QVERIFY(elapsed.elapsed() < 3000);
}

void HttpClientTest::streamedOversizedBodyFails()
{
    HttpClient client;
    const QFuture<Result<HttpResponse>> future = client.send(request("/big-streamed"));
    QVERIFY(waitFor(future));
    QCOMPARE(future.result().failure().kind, FailureKind::InvalidResponse);
}

void HttpClientTest::bodyAtTheLimitIsAccepted()
{
    HttpClient client;
    const QFuture<Result<HttpResponse>> future = client.send(request("/at-limit"));
    QVERIFY(waitFor(future));
    QVERIFY(future.result());
    QCOMPARE(future.result().value().body.size(), 1000);
}

void HttpClientTest::refusedConnectionIsNetwork()
{
    QTcpServer closed;
    QVERIFY(closed.listen(QHostAddress::LocalHost, 0));
    const quint16 port = closed.serverPort();
    closed.close();

    HttpClient client;
    HttpRequest refused = request("/ok");
    refused.url = QUrl(QStringLiteral("http://127.0.0.1:%1/ok").arg(port));
    refused.timeout = std::chrono::seconds(15);
    const QFuture<Result<HttpResponse>> future = client.send(refused);
    QVERIFY(waitFor(future, 15000));
    QCOMPARE(future.result().failure().kind, FailureKind::Network);
}

void HttpClientTest::requestWithoutTimeoutOrLimitIsRefused()
{
    HttpClient client;
    HttpRequest noTimeout = request("/ok");
    noTimeout.timeout = std::chrono::milliseconds(0);
    HttpRequest noLimit = request("/ok");
    noLimit.maxResponseBytes = 0;
    HttpRequest notHttp = request("/ok");
    notHttp.url = QUrl(QStringLiteral("file:///C:/x"));
    HttpRequest getWithBody = request("/ok");
    getWithBody.body = "x";

    for (const HttpRequest& invalid : {noTimeout, noLimit, notHttp, getWithBody}) {
        const QFuture<Result<HttpResponse>> future = client.send(invalid);
        QVERIFY(future.isFinished());
        QCOMPARE(future.result().failure().kind, FailureKind::Unknown);
    }
    QCOMPARE(client.pendingCount(), std::size_t{0});
    QTest::qWait(50);
    QVERIFY(m_server->requests.isEmpty());
}

void HttpClientTest::cancelAllFinishesAsCancelled()
{
    HttpClient client;
    const QFuture<Result<HttpResponse>> future = client.send(request("/hang"));
    QTRY_COMPARE(m_server->requests.size(), 1);
    client.cancelAll();
    QVERIFY(future.isFinished());
    QCOMPARE(future.result().failure().kind, FailureKind::Cancelled);
    QCOMPARE(client.pendingCount(), std::size_t{0});
    QTRY_COMPARE(m_server->closedConnections, 1);
}

void HttpClientTest::destroyingTheClientFinishesAsCancelled()
{
    auto client = std::make_unique<HttpClient>();
    const QFuture<Result<HttpResponse>> first = client->send(request("/hang"));
    const QFuture<Result<HttpResponse>> second = client->send(request("/hang"));
    QTRY_COMPARE(m_server->requests.size(), 2);
    client.reset();
    for (const QFuture<Result<HttpResponse>>& future : {first, second}) {
        QVERIFY(future.isFinished());
        QCOMPARE(future.resultCount(), 1);
        QCOMPARE(future.result().failure().kind, FailureKind::Cancelled);
    }
    QTRY_COMPARE(m_server->closedConnections, 2);
}

void HttpClientTest::cancellingTheFutureAbortsTheRequest()
{
    HttpClient client;
    QFuture<Result<HttpResponse>> future = client.send(request("/hang"));
    QTRY_COMPARE(m_server->requests.size(), 1);
    future.cancel();
    QTRY_COMPARE(client.pendingCount(), std::size_t{0});
    QVERIFY(future.isCanceled());
    QVERIFY(future.isFinished());
    QTRY_COMPARE(m_server->closedConnections, 1);
}

void HttpClientTest::headersOnlyStopsAtTheHeaders()
{
    HttpClient client;
    HttpRequest probe = request("/audio");
    probe.headersOnly = true;
    QElapsedTimer elapsed;
    elapsed.start();
    const QFuture<Result<HttpResponse>> future = client.send(probe);
    QVERIFY(waitFor(future));
    QVERIFY(future.result());
    QCOMPARE(future.result().value().status, 200);
    QVERIFY(future.result().value().body.isEmpty());
    QCOMPARE(future.result().value().header("Content-Type"), QByteArray("audio/mpeg"));
    QVERIFY(elapsed.elapsed() < 3000);
}

void HttpClientTest::headersOnlyKeepsFailureStatuses()
{
    HttpClient client;
    HttpRequest probe = request("/status/404");
    probe.headersOnly = true;
    const QFuture<Result<HttpResponse>> missing = client.send(probe);
    QVERIFY(waitFor(missing));
    QCOMPARE(missing.result().failure().httpStatus, 404);
    HttpRequest limited = request("/limited");
    limited.headersOnly = true;
    const QFuture<Result<HttpResponse>> refused = client.send(limited);
    QVERIFY(waitFor(refused));
    QVERIFY(refused.result().failure().retryAfterSeconds == std::optional<int>(70001));
}

QTEST_GUILESS_MAIN(HttpClientTest)
#include "tst_http_client.moc"
