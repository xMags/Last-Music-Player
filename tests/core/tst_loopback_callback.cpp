#include <QAbstractSocket>
#include <QByteArray>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFuture>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#include <chrono>
#include <memory>

#include "core/account/LoopbackCallback.h"
#include "core/async/Result.h"

using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::account::LoopbackCallback;

namespace {

using CodeFuture = QFuture<Result<QString>>;

QHostAddress hostOf(const QString& redirectUri)
{
    return QHostAddress(QUrl(redirectUri).host());
}

quint16 portOf(const QString& redirectUri)
{
    return static_cast<quint16>(QUrl(redirectUri).port());
}

QByteArray authorityOf(const QString& redirectUri)
{
    const QUrl url(redirectUri);
    const QByteArray host = url.host() == QStringLiteral("::1") ? QByteArrayLiteral("[::1]") : url.host().toLatin1();
    return host + ':' + QByteArray::number(url.port());
}

QByteArray requestFor(const QString& redirectUri, const QByteArray& target)
{
    return "GET " + target + " HTTP/1.1\r\nHost: " + authorityOf(redirectUri) + "\r\nConnection: close\r\n\r\n";
}

// A client in the test's own thread: the event loop keeps running while it
// waits, so the listener can answer.
class Client {
public:
    explicit Client(const QString& redirectUri)
    {
        m_socket.connectToHost(hostOf(redirectUri), portOf(redirectUri));
    }

    [[nodiscard]] bool waitConnected()
    {
        QDeadlineTimer deadline(std::chrono::seconds(3));
        while (m_socket.state() != QAbstractSocket::ConnectedState && !deadline.hasExpired()
               && m_socket.state() != QAbstractSocket::UnconnectedState) {
            QTest::qWait(5);
        }
        return m_socket.state() == QAbstractSocket::ConnectedState;
    }

    void send(const QByteArray& bytes) { m_socket.write(bytes); }

    // Everything received until the listener closes the connection.
    [[nodiscard]] QByteArray response()
    {
        QDeadlineTimer deadline(std::chrono::seconds(3));
        while (m_socket.state() != QAbstractSocket::UnconnectedState && !deadline.hasExpired()) {
            m_received += m_socket.readAll();
            QTest::qWait(5);
        }
        m_received += m_socket.readAll();
        return m_received;
    }

    [[nodiscard]] bool closedByListener() const { return m_socket.state() == QAbstractSocket::UnconnectedState; }

private:
    QTcpSocket m_socket;
    QByteArray m_received;
};

QByteArray exchange(const QString& redirectUri, const QByteArray& bytes)
{
    Client client(redirectUri);
    if (!client.waitConnected()) {
        return {};
    }
    client.send(bytes);
    return client.response();
}

FailureKind failureOf(const CodeFuture& future)
{
    return future.result().isOk() ? FailureKind::Unknown : future.result().failure().kind;
}

LoopbackCallback::Limits limits(std::chrono::milliseconds wholeWait, std::chrono::milliseconds requestRead)
{
    return LoopbackCallback::Limits{.wholeWait = wholeWait, .requestRead = requestRead};
}

// A short read limit, so a slow request is answered quickly.
const LoopbackCallback::Limits kShortRead = limits(std::chrono::seconds(10), std::chrono::milliseconds(100));

} // namespace

class LoopbackCallbackTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsCodeAfterRefusedRequest();
    void answersWithFixedPages();
    void browserErrorCancels();
    void cancelEndsStalledConnection();
    void stalledConnectionReachesWholeLimit();
    void timesOutAndStartsAgain();
    void slowRequestIsAnsweredAfterReadLimit();
    void oversizedRequestIsRefused();
    void endsAfterEightRefusedRequests();
    void closesListenerAfterCode();
    void finishesExactlyOnce();
    void startReplacesRunningWait();
    void destroyingFinishesAsCancelled();
    void refusesUnusableState();
};

void LoopbackCallbackTest::acceptsCodeAfterRefusedRequest()
{
    LoopbackCallback callback;
    const CodeFuture future = callback.start(QStringLiteral("expected-state"));
    const QString redirectUri = callback.redirectUri();
    QVERIFY(redirectUri.startsWith(QStringLiteral("http://127.0.0.1:")));
    QVERIFY(redirectUri.endsWith(QStringLiteral("/callback")));
    QVERIFY(portOf(redirectUri) > 0);

    const QByteArray refused = exchange(redirectUri, requestFor(redirectUri, "/wrong?code=ignored&state=expected-state"));
    QVERIFY(refused.startsWith("HTTP/1.1 400 Bad Request\r\n"));
    QVERIFY(!future.isFinished());

    const QByteArray accepted =
        exchange(redirectUri, requestFor(redirectUri, "/callback?code=accepted-code&state=expected-state"));
    QVERIFY(accepted.startsWith("HTTP/1.1 200 OK\r\n"));
    QTRY_VERIFY(future.isFinished());
    QVERIFY(future.result().isOk());
    QCOMPARE(future.result().value(), QStringLiteral("accepted-code"));
    QCOMPARE(callback.redirectUri(), redirectUri);
}

void LoopbackCallbackTest::answersWithFixedPages()
{
    const QByteArray headers = "Content-Type: text/html; charset=utf-8\r\n"
                               "Cache-Control: no-store\r\n"
                               "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\n"
                               "Referrer-Policy: no-referrer\r\n"
                               "X-Content-Type-Options: nosniff\r\n"
                               "Connection: close\r\n\r\n";
    LoopbackCallback callback;
    const CodeFuture future = callback.start(QStringLiteral("s"));
    const QString redirectUri = callback.redirectUri();
    QCOMPARE(exchange(redirectUri, requestFor(redirectUri, "/callback?state=s")),
             "HTTP/1.1 400 Bad Request\r\n" + headers
                 + "<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign-in stopped</title></head>"
                   "<body><main><h1>Sign-in stopped</h1><p>Return to Last Music Player and try again.</p></main>"
                   "</body></html>");
    QCOMPARE(exchange(redirectUri, requestFor(redirectUri, "/callback?code=c&state=s")),
             "HTTP/1.1 200 OK\r\n" + headers
                 + "<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign-in complete</title></head>"
                   "<body><main><h1>Sign-in complete</h1><p>You can return to Last Music Player.</p></main>"
                   "</body></html>");
    QTRY_VERIFY(future.isFinished());
}

void LoopbackCallbackTest::browserErrorCancels()
{
    LoopbackCallback callback;
    const CodeFuture future = callback.start(QStringLiteral("s"));
    const QString redirectUri = callback.redirectUri();
    const QByteArray page = exchange(redirectUri, requestFor(redirectUri, "/callback?error=access_denied&state=s"));
    QVERIFY(page.startsWith("HTTP/1.1 400 Bad Request\r\n"));
    QTRY_VERIFY(future.isFinished());
    QCOMPARE(failureOf(future), FailureKind::Cancelled);
}

void LoopbackCallbackTest::cancelEndsStalledConnection()
{
    LoopbackCallback callback;
    const CodeFuture future = callback.start(QStringLiteral("cancel-state"));
    Client stalled(callback.redirectUri());
    QVERIFY(stalled.waitConnected());
    stalled.send("G");
    QTest::qWait(50);
    QVERIFY(!future.isFinished());
    callback.cancel();
    QVERIFY(future.isFinished());
    QCOMPARE(failureOf(future), FailureKind::Cancelled);
    // The connection is closed without a page.
    QVERIFY(stalled.response().isEmpty());
    QVERIFY(stalled.closedByListener());
}

void LoopbackCallbackTest::stalledConnectionReachesWholeLimit()
{
    LoopbackCallback callback(limits(std::chrono::milliseconds(200), std::chrono::seconds(2)), nullptr);
    QElapsedTimer elapsed;
    elapsed.start();
    const CodeFuture future = callback.start(QStringLiteral("timeout-state"));
    Client stalled(callback.redirectUri());
    QVERIFY(stalled.waitConnected());
    stalled.send("G");
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 2000);
    QVERIFY(elapsed.elapsed() < 1000);
    QCOMPARE(failureOf(future), FailureKind::Timeout);
    QVERIFY(stalled.response().isEmpty());
}

void LoopbackCallbackTest::timesOutAndStartsAgain()
{
    LoopbackCallback callback(limits(std::chrono::milliseconds(150), std::chrono::seconds(2)), nullptr);
    const CodeFuture first = callback.start(QStringLiteral("timeout-state"));
    QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), 2000);
    QCOMPARE(failureOf(first), FailureKind::Timeout);

    const CodeFuture second = callback.start(QStringLiteral("timeout-state"));
    QVERIFY(!second.isFinished());
    QVERIFY(!callback.redirectUri().isEmpty());
    callback.cancel();
    QCOMPARE(failureOf(second), FailureKind::Cancelled);
}

void LoopbackCallbackTest::slowRequestIsAnsweredAfterReadLimit()
{
    LoopbackCallback callback(kShortRead, nullptr);
    const CodeFuture future = callback.start(QStringLiteral("s"));
    const QString redirectUri = callback.redirectUri();
    Client slow(redirectUri);
    QVERIFY(slow.waitConnected());
    slow.send("GET /callback?code=c&state=s HTTP/1.1\r\n");
    QVERIFY(slow.response().startsWith("HTTP/1.1 400 Bad Request\r\n"));
    QVERIFY(!future.isFinished());

    QVERIFY(exchange(redirectUri, requestFor(redirectUri, "/callback?code=c&state=s")).startsWith("HTTP/1.1 200 OK"));
    QTRY_VERIFY(future.isFinished());
    QCOMPARE(future.result().value(), QStringLiteral("c"));
}

void LoopbackCallbackTest::oversizedRequestIsRefused()
{
    LoopbackCallback callback(kShortRead, nullptr);
    const CodeFuture future = callback.start(QStringLiteral("s"));
    const QString redirectUri = callback.redirectUri();
    const QByteArray oversized = "GET /callback?code=c&state=s HTTP/1.1\r\nX-Padding: " + QByteArray(9000, 'p');
    // Only the first 8192 bytes are read. The rest is never read, so the
    // system may reset the connection before the refusal page arrives.
    const QByteArray answer = exchange(redirectUri, oversized);
    QVERIFY(answer.isEmpty() || answer.startsWith("HTTP/1.1 400 Bad Request\r\n"));
    QVERIFY(!future.isFinished());
    QVERIFY(exchange(redirectUri, requestFor(redirectUri, "/callback?code=c&state=s")).startsWith("HTTP/1.1 200 OK"));
    QTRY_VERIFY(future.isFinished());
    QCOMPARE(future.result().value(), QStringLiteral("c"));
}

void LoopbackCallbackTest::endsAfterEightRefusedRequests()
{
    LoopbackCallback callback;
    const CodeFuture future = callback.start(QStringLiteral("s"));
    const QString redirectUri = callback.redirectUri();
    for (int attempt = 1; attempt <= 7; ++attempt) {
        QVERIFY(exchange(redirectUri, requestFor(redirectUri, "/callback?code=c&state=wrong")).startsWith("HTTP/1.1 400"));
        QVERIFY2(!future.isFinished(), qPrintable(QString::number(attempt)));
    }
    QVERIFY(exchange(redirectUri, requestFor(redirectUri, "/callback?code=c&state=wrong")).startsWith("HTTP/1.1 400"));
    QTRY_VERIFY(future.isFinished());
    QCOMPARE(failureOf(future), FailureKind::InvalidResponse);
}

void LoopbackCallbackTest::closesListenerAfterCode()
{
    LoopbackCallback callback;
    const CodeFuture future = callback.start(QStringLiteral("s"));
    const QString redirectUri = callback.redirectUri();
    QVERIFY(exchange(redirectUri, requestFor(redirectUri, "/callback?code=c&state=s")).startsWith("HTTP/1.1 200 OK"));
    QTRY_VERIFY(future.isFinished());
    Client late(redirectUri);
    QVERIFY(!late.waitConnected());
}

void LoopbackCallbackTest::finishesExactlyOnce()
{
    LoopbackCallback callback;
    const CodeFuture future = callback.start(QStringLiteral("s"));
    const QString redirectUri = callback.redirectUri();
    QVERIFY(exchange(redirectUri, requestFor(redirectUri, "/callback?code=c&state=s")).startsWith("HTTP/1.1 200 OK"));
    QTRY_VERIFY(future.isFinished());
    callback.cancel();
    QCOMPARE(future.resultCount(), 1);
    QVERIFY(future.result().isOk());
    QVERIFY(!future.isCanceled());
}

void LoopbackCallbackTest::startReplacesRunningWait()
{
    LoopbackCallback callback;
    const CodeFuture first = callback.start(QStringLiteral("first"));
    const CodeFuture second = callback.start(QStringLiteral("second"));
    QVERIFY(first.isFinished());
    QCOMPARE(failureOf(first), FailureKind::Cancelled);
    QVERIFY(!second.isFinished());
    const QString redirectUri = callback.redirectUri();
    QVERIFY(exchange(redirectUri, requestFor(redirectUri, "/callback?code=c&state=second")).startsWith("HTTP/1.1 200"));
    QTRY_VERIFY(second.isFinished());
    QCOMPARE(second.result().value(), QStringLiteral("c"));
}

void LoopbackCallbackTest::destroyingFinishesAsCancelled()
{
    CodeFuture future;
    {
        auto callback = std::make_unique<LoopbackCallback>();
        future = callback->start(QStringLiteral("s"));
        QVERIFY(!future.isFinished());
    }
    QVERIFY(future.isFinished());
    QCOMPARE(failureOf(future), FailureKind::Cancelled);
}

void LoopbackCallbackTest::refusesUnusableState()
{
    LoopbackCallback callback;
    const CodeFuture empty = callback.start(QString());
    QVERIFY(empty.isFinished());
    QCOMPARE(failureOf(empty), FailureKind::InvalidResponse);
    QVERIFY(callback.redirectUri().isEmpty());
    const CodeFuture nonAscii = callback.start(QStringLiteral("st") + QChar(0x00e9));
    QVERIFY(nonAscii.isFinished());
    QCOMPARE(failureOf(nonAscii), FailureKind::InvalidResponse);
}

QTEST_GUILESS_MAIN(LoopbackCallbackTest)
#include "tst_loopback_callback.moc"
