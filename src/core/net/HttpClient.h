#pragma once

#include "core/async/Result.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QUrl>

#include <chrono>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>

class QNetworkAccessManager;
class QNetworkReply;

namespace lmp::core::net {

enum class HttpMethod { Get, Head, Post, Put, Patch, Delete };

enum class RedirectPolicy {
    // A 3xx response is a failure and its Location is never requested.
    // Account requests use this.
    Refuse,
    // Follows redirects that do not downgrade from https to http.
    Follow,
};

using HttpHeaders = QList<std::pair<QByteArray, QByteArray>>;

// One request. The timeout and the size limit have no defaults on purpose:
// every caller states them, and send() fails a request that leaves either
// unset.
struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    QUrl url;
    HttpHeaders headers;
    QByteArray body;
    // The whole exchange, from sending to the last byte of the body.
    std::chrono::milliseconds timeout{0};
    RedirectPolicy redirects = RedirectPolicy::Refuse;
    // The largest body accepted; anything larger is abandoned as soon as it
    // is announced or received.
    qint64 maxResponseBytes = 0;
};

// A response with a 2xx status. Every other status is a Failure.
struct HttpResponse {
    int status = 0;
    HttpHeaders headers;
    QByteArray body;

    // The first header with this name, compared without case; empty when
    // there is none.
    [[nodiscard]] QByteArray header(QByteArrayView name) const;
};

// Sends HTTP requests through one QNetworkAccessManager.
//
// Failures: no connection, DNS or TLS errors are Network; the request's own
// timeout is Timeout; cancelAll(), destruction or an abort from outside is
// Cancelled; 401 Unauthorized, 403 Forbidden, 404 NotFound, 429 RateLimited,
// 5xx Server, any other 4xx Unknown; a 3xx that was not followed, a body over
// the limit, a malformed HTTP response or a status outside 100 to 599 is
// InvalidResponse. Failures carry the HTTP status when there was one.
//
// Threading: lives on one thread and must only be used there; results arrive
// on that thread. Each returned future finishes exactly once. Calling
// cancel() on a future aborts its request. Pending futures finish as
// Cancelled when the client is destroyed.
class HttpClient : public QObject {
    Q_OBJECT

public:
    explicit HttpClient(QObject* parent = nullptr);
    ~HttpClient() override;

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    [[nodiscard]] QFuture<Result<HttpResponse>> send(const HttpRequest& request);

    // Aborts every pending request; their futures finish as Cancelled.
    void cancelAll();

    [[nodiscard]] std::size_t pendingCount() const { return m_pending.size(); }

private:
    // Why the client stopped a request before it finished on its own.
    enum class Ending { None, TimedOut, Oversized, Cancelled };
    struct Pending;

    [[nodiscard]] QNetworkReply* start(const HttpRequest& request);
    void watch(QNetworkReply* reply, Pending& pending, const HttpRequest& request);
    void onMetaDataChanged(QNetworkReply* reply);
    void onReadyRead(QNetworkReply* reply);
    void onFinished(QNetworkReply* reply);
    void abortWith(QNetworkReply* reply, Ending ending);
    [[nodiscard]] static Result<HttpResponse> outcomeOf(QNetworkReply& reply, Pending& pending);

    QNetworkAccessManager* m_manager = nullptr;
    std::unordered_map<QNetworkReply*, std::unique_ptr<Pending>> m_pending;
};

} // namespace lmp::core::net
