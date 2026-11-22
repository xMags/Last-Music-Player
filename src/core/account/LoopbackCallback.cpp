#include "core/account/LoopbackCallback.h"

#include "core/account/LoopbackRequest.h"
#include "core/app/AppIdentity.h"

#include <QAbstractSocket>
#include <QByteArrayView>
#include <QHostAddress>
#include <QTcpSocket>

#include <utility>

namespace lmp::core::account {

namespace {

// After this many refused requests the wait ends, so a local process cannot
// keep the sign-in open by probing it.
constexpr int kMaxRefusedRequests = 8;

// The pages and their headers are fixed: no request data is ever echoed, and
// the browser may not run, embed or remember anything from them.
constexpr QByteArrayView kSuccessPage{
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\n"
    "Referrer-Policy: no-referrer\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "Connection: close\r\n\r\n"
    "<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign-in complete</title></head>"
    "<body><main><h1>Sign-in complete</h1><p>You can return to Last Music Player.</p></main></body></html>"};

constexpr QByteArrayView kFailurePage{
    "HTTP/1.1 400 Bad Request\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\n"
    "Referrer-Policy: no-referrer\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "Connection: close\r\n\r\n"
    "<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign-in stopped</title></head>"
    "<body><main><h1>Sign-in stopped</h1><p>Return to Last Music Player and try again.</p></main></body></html>"};

// Text that must travel as single bytes; empty when a character is outside
// ASCII.
QByteArray asciiBytes(const QString& text)
{
    QByteArray bytes;
    bytes.reserve(text.size());
    for (const QChar character : text) {
        if (character.unicode() > 0x7f) {
            return {};
        }
        bytes.append(static_cast<char>(character.unicode()));
    }
    return bytes;
}

void closeWithoutAnswer(QTcpSocket* socket)
{
    socket->abort();
    socket->deleteLater();
}

} // namespace

LoopbackCallback::LoopbackCallback(QObject* parent)
    : LoopbackCallback(Limits{}, parent)
{
}

LoopbackCallback::LoopbackCallback(Limits limits, QObject* parent)
    : QObject(parent)
    , m_limits(limits)
{
    m_waitTimer.setSingleShot(true);
    m_readTimer.setSingleShot(true);
    // One connection waits in the system backlog at a time, as before.
    m_server.setListenBacklogSize(1);
    connect(&m_server, &QTcpServer::newConnection, this, [this]() { takeNextConnection(); });
    connect(&m_server, &QTcpServer::acceptError, this, [this](QAbstractSocket::SocketError) {
        finish(Failure::of(FailureKind::Network, QStringLiteral("sign-in callback could not accept a connection")));
    });
    connect(&m_waitTimer, &QTimer::timeout, this,
            [this]() { finish(Failure::of(FailureKind::Timeout, QStringLiteral("sign-in callback timed out"))); });
    // A connection that does not deliver its request in time is answered
    // with what it sent so far.
    connect(&m_readTimer, &QTimer::timeout, this, [this]() { answerRequest(); });
}

LoopbackCallback::~LoopbackCallback()
{
    finish(Failure::of(FailureKind::Cancelled, QStringLiteral("sign-in callback was destroyed")));
}

QFuture<Result<QString>> LoopbackCallback::start(const QString& expectedState)
{
    finish(Failure::of(FailureKind::Cancelled, QStringLiteral("a newer sign-in callback started")));

    m_promise.emplace();
    m_promise->start();
    QFuture<Result<QString>> future = m_promise->future();
    m_refusedRequests = 0;
    m_redirectUri.clear();
    m_expectedState = asciiBytes(expectedState);
    m_expectedPath = asciiBytes(AppIdentity::accountCallbackPath());
    if (m_expectedState.isEmpty() || m_expectedPath.isEmpty()) {
        finish(Failure::of(FailureKind::InvalidResponse, QStringLiteral("sign-in callback state is not usable")));
        return future;
    }
    if (!listenOnLoopback()) {
        finish(Failure::of(FailureKind::Network, QStringLiteral("sign-in callback could not listen")));
        return future;
    }
    m_waitTimer.start(m_limits.wholeWait);
    return future;
}

QString LoopbackCallback::redirectUri() const
{
    return m_redirectUri;
}

void LoopbackCallback::cancel()
{
    finish(Failure::of(FailureKind::Cancelled, QStringLiteral("sign-in callback was cancelled")));
}

bool LoopbackCallback::listenOnLoopback()
{
    // Port 0: the operating system picks a free port.
    bool ipv6 = false;
    if (!m_server.listen(QHostAddress(QHostAddress::LocalHost), 0)) {
        if (!m_server.listen(QHostAddress(QHostAddress::LocalHostIPv6), 0)) {
            return false;
        }
        ipv6 = true;
    }
    const QByteArray port = QByteArray::number(m_server.serverPort());
    m_authority = (ipv6 ? QByteArrayLiteral("[::1]:") : QByteArrayLiteral("127.0.0.1:")) + port;
    m_redirectUri =
        QStringLiteral("http://") + QString::fromLatin1(m_authority) + QString::fromLatin1(m_expectedPath);
    return true;
}

void LoopbackCallback::takeNextConnection()
{
    // Requests are answered one at a time, as they arrive.
    if (!m_promise || m_connection || !m_server.hasPendingConnections()) {
        return;
    }
    QTcpSocket* socket = m_server.nextPendingConnection();
    if (socket == nullptr) {
        return;
    }
    // Qt keeps no more unread bytes than one request may have.
    socket->setReadBufferSize(kMaxLoopbackRequestBytes);
    m_connection = socket;
    m_request.clear();
    connect(socket, &QTcpSocket::readyRead, this, [this]() { readRequest(); });
    connect(socket, &QTcpSocket::disconnected, this, [this]() { readRequest(); answerRequest(); });
    connect(socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        readRequest();
        answerRequest();
    });
    m_readTimer.start(m_limits.requestRead);
    readRequest();
}

void LoopbackCallback::readRequest()
{
    if (!m_connection) {
        return;
    }
    const qint64 room = kMaxLoopbackRequestBytes - m_request.size();
    if (room > 0) {
        m_request += m_connection->read(room);
    }
    // The request ends at the blank line after the headers; a body is never
    // expected. A full buffer is answered as it is.
    if (m_request.contains("\r\n\r\n") || m_request.size() >= kMaxLoopbackRequestBytes) {
        answerRequest();
    }
}

void LoopbackCallback::answerRequest()
{
    if (!m_connection || !m_promise) {
        return;
    }
    m_readTimer.stop();
    QTcpSocket* socket = m_connection.data();
    m_connection.clear();
    socket->disconnect(this);

    const LoopbackRequestResult request = parseLoopbackRequest(m_request, m_authority, m_expectedPath, m_expectedState);
    m_request.clear();
    const QByteArrayView page = request.outcome == LoopbackRequestOutcome::Code ? kSuccessPage : kFailurePage;
    if (socket->state() == QAbstractSocket::ConnectedState) {
        // The socket closes itself once the page is written.
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        socket->write(page.data(), page.size());
        socket->disconnectFromHost();
    } else {
        closeWithoutAnswer(socket);
    }

    switch (request.outcome) {
    case LoopbackRequestOutcome::Code:
        finish(request.code);
        return;
    case LoopbackRequestOutcome::Cancelled:
        finish(Failure::of(FailureKind::Cancelled, QStringLiteral("sign-in was stopped in the browser")));
        return;
    case LoopbackRequestOutcome::Invalid:
        break;
    }
    ++m_refusedRequests;
    if (m_refusedRequests >= kMaxRefusedRequests) {
        finish(Failure::of(FailureKind::InvalidResponse, QStringLiteral("sign-in callback refused too many requests")));
        return;
    }
    takeNextConnection();
}

void LoopbackCallback::finish(Result<QString> result)
{
    if (!m_promise) {
        return;
    }
    // Everything is closed before the result is published, because a
    // continuation may start a new wait or destroy this object.
    QPromise<Result<QString>> promise = std::move(*m_promise);
    m_promise.reset();
    m_waitTimer.stop();
    m_readTimer.stop();
    if (m_connection) {
        QTcpSocket* socket = m_connection.data();
        m_connection.clear();
        socket->disconnect(this);
        closeWithoutAnswer(socket);
    }
    while (m_server.hasPendingConnections()) {
        if (QTcpSocket* pending = m_server.nextPendingConnection(); pending != nullptr) {
            closeWithoutAnswer(pending);
        }
    }
    m_server.close();
    m_request.clear();

    promise.addResult(std::move(result));
    promise.finish();
}

} // namespace lmp::core::account
