#pragma once

#include "core/async/Result.h"
#include "core/net/HttpMessage.h"
#include "core/net/IHttpTransport.h"

#include <QFuture>
#include <QObject>

#include <cstddef>
#include <memory>
#include <unordered_map>

class QNetworkAccessManager;
class QNetworkReply;

namespace lmp::core::net {

// Sends HTTP requests through one QNetworkAccessManager.
//
// Failures: no connection, DNS or TLS errors are Network; the request's own
// timeout is Timeout; cancelAll(), destruction or an abort from outside is
// Cancelled; 401 Unauthorized, 403 Forbidden, 404 NotFound, 429 RateLimited,
// 5xx Server, any other 4xx Unknown; a 3xx that was not followed, a body over
// the limit, a malformed HTTP response or a status outside 100 to 599 is
// InvalidResponse. Failures carry the HTTP status when there was one, and a
// failed status carries the delay of its Retry-After header when that header
// is valid (parseRetryAfterSeconds).
//
// Threading: lives on one thread and must only be used there; results arrive
// on that thread. Each returned future finishes exactly once. Calling
// cancel() on a future aborts its request. Pending futures finish as
// Cancelled when the client is destroyed.
class HttpClient : public QObject, public IHttpTransport {
    Q_OBJECT

public:
    explicit HttpClient(QObject* parent = nullptr);
    ~HttpClient() override;

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    [[nodiscard]] QFuture<Result<HttpResponse>> send(const HttpRequest& request) override;

    // Aborts every pending request; their futures finish as Cancelled.
    void cancelAll();

    [[nodiscard]] std::size_t pendingCount() const { return m_pending.size(); }

private:
    // Why the client stopped a request before it finished on its own.
    enum class Ending { None, TimedOut, Oversized, Cancelled, HeadersArrived };
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
