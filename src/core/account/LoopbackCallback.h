#pragma once

#include "core/async/Result.h"

#include <QByteArray>
#include <QFuture>
#include <QObject>
#include <QPointer>
#include <QPromise>
#include <QString>
#include <QTcpServer>
#include <QTimer>

#include <chrono>
#include <optional>

class QTcpSocket;

namespace lmp::core::account {

// The local address the browser returns to after sign-in. Listens on
// 127.0.0.1 (or ::1 when that fails) on a port the operating system picks,
// and answers requests one at a time until one carries the authorization
// code or an error, or until a limit is reached. See parseLoopbackRequest for
// what a valid request is; each request gets a small fixed page (200 for a
// code, 400 otherwise) and its connection is closed.
//
// Outcomes of the future returned by start():
// - the code;
// - Cancelled: the browser reported an error, cancel() was called, start()
//   was called again, or the object was destroyed;
// - Timeout: the whole wait limit passed;
// - InvalidResponse: the expected state is empty or not ASCII, or eight
//   requests were refused;
// - Network: no loopback address could be bound, or accepting failed.
// The listener closes as soon as the future finishes, and the future
// finishes exactly once.
//
// Threading: lives on one thread with an event loop and is used only there;
// sockets are non-blocking and the future finishes on that thread.
class LoopbackCallback final : public QObject {
    Q_OBJECT

public:
    struct Limits {
        // From start() to the end of the wait.
        std::chrono::milliseconds wholeWait{std::chrono::minutes(10)};
        // For one connection to deliver its request.
        std::chrono::milliseconds requestRead{std::chrono::seconds(2)};
    };

    explicit LoopbackCallback(QObject* parent = nullptr);
    LoopbackCallback(Limits limits, QObject* parent);
    ~LoopbackCallback() override;

    LoopbackCallback(const LoopbackCallback&) = delete;
    LoopbackCallback& operator=(const LoopbackCallback&) = delete;

    // Starts listening and waiting for a callback that returns
    // `expectedState`. A wait still running finishes as Cancelled first.
    [[nodiscard]] QFuture<Result<QString>> start(const QString& expectedState);

    // "http://127.0.0.1:<port>/callback" (or "http://[::1]:<port>/...") of
    // the last start(), kept after the wait ends because the token exchange
    // must name it; empty when that start() did not listen.
    [[nodiscard]] QString redirectUri() const;

    // Ends the wait as Cancelled. Does nothing when no wait is running.
    void cancel();

private:
    [[nodiscard]] bool listenOnLoopback();
    void takeNextConnection();
    void readRequest();
    void answerRequest();
    void finish(Result<QString> result);

    Limits m_limits;
    QTcpServer m_server;
    QTimer m_waitTimer;
    QTimer m_readTimer;
    std::optional<QPromise<Result<QString>>> m_promise;
    QPointer<QTcpSocket> m_connection;
    QByteArray m_request;
    QByteArray m_expectedState;
    QByteArray m_expectedPath;
    QByteArray m_authority;
    QString m_redirectUri;
    int m_refusedRequests = 0;
};

} // namespace lmp::core::account
