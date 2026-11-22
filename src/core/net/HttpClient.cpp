#include "core/net/HttpClient.h"

#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QThread>
#include <QTimer>
#include <QVariant>

#include <optional>

namespace lmp::core::net {

namespace {

using Promise = QPromise<Result<HttpResponse>>;

void finishPromise(Promise& promise, Result<HttpResponse> result)
{
    // A future the caller cancelled refuses the result; finishing is still
    // required so it reports finished exactly once.
    [[maybe_unused]] const bool stored = promise.addResult(std::move(result));
    promise.finish();
}

[[nodiscard]] std::optional<Failure> validate(const HttpRequest& request)
{
    // Details never include the URL: query strings can carry credentials.
    const QString scheme = request.url.scheme();
    if (!request.url.isValid() || request.url.host().isEmpty()
        || (scheme != QLatin1StringView("http") && scheme != QLatin1StringView("https"))) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("request URL is not an http(s) URL"));
    }
    if (request.timeout.count() <= 0) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("request has no timeout"));
    }
    if (request.maxResponseBytes <= 0) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("request has no response size limit"));
    }
    if ((request.method == HttpMethod::Get || request.method == HttpMethod::Head) && !request.body.isEmpty()) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("GET and HEAD requests carry no body"));
    }
    return std::nullopt;
}

[[nodiscard]] Failure statusFailure(int status)
{
    const QString detail = QStringLiteral("HTTP status %1").arg(status);
    if (status == 401) {
        return Failure::of(FailureKind::Unauthorized, detail, status);
    }
    if (status == 403) {
        return Failure::of(FailureKind::Forbidden, detail, status);
    }
    if (status == 404) {
        return Failure::of(FailureKind::NotFound, detail, status);
    }
    if (status == 429) {
        return Failure::of(FailureKind::RateLimited, detail, status);
    }
    if (status >= 500) {
        return Failure::of(FailureKind::Server, detail, status);
    }
    if (status >= 400) {
        // A request the service did not accept (400, 409, ...): the caller
        // decides what it means, so it gets no more specific kind here.
        return Failure::of(FailureKind::Unknown, detail, status);
    }
    // 1xx as a final status, or a 3xx that was not followed.
    return Failure::of(FailureKind::InvalidResponse, detail, status);
}

[[nodiscard]] Failure networkFailure(QNetworkReply::NetworkError error, int status)
{
    // errorString() is left out on purpose: it can quote the URL.
    const QString detail = QStringLiteral("network error %1").arg(static_cast<int>(error));
    switch (error) {
    case QNetworkReply::OperationCanceledError:
        return Failure::of(FailureKind::Cancelled, detail, status);
    case QNetworkReply::TimeoutError:
        return Failure::of(FailureKind::Timeout, detail, status);
    case QNetworkReply::ProtocolFailure:
    case QNetworkReply::ProtocolUnknownError:
    case QNetworkReply::ProtocolInvalidOperationError:
        return Failure::of(FailureKind::InvalidResponse, detail, status);
    default:
        return Failure::of(FailureKind::Network, detail, status);
    }
}

} // namespace

QByteArray HttpResponse::header(QByteArrayView name) const
{
    for (const auto& [key, value] : headers) {
        if (key.compare(name, Qt::CaseInsensitive) == 0) {
            return value;
        }
    }
    return {};
}

struct HttpClient::Pending {
    Promise promise;
    QByteArray body;
    qint64 maxBytes = 0;
    // Both are children of the reply, so they never outlive it.
    QTimer* deadline = nullptr;
    QFutureWatcher<Result<HttpResponse>>* cancelWatcher = nullptr;
    Ending ending = Ending::None;
};

HttpClient::HttpClient(QObject* parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
}

HttpClient::~HttpClient()
{
    cancelAll();
}

QFuture<Result<HttpResponse>> HttpClient::send(const HttpRequest& request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto pending = std::make_unique<Pending>();
    pending->promise.start();
    QFuture<Result<HttpResponse>> future = pending->promise.future();
    if (std::optional<Failure> invalid = validate(request)) {
        finishPromise(pending->promise, std::move(*invalid));
        return future;
    }

    QNetworkReply* reply = start(request);
    pending->maxBytes = request.maxResponseBytes;
    watch(reply, *pending, request);
    auto* watcher = new QFutureWatcher<Result<HttpResponse>>(reply);
    pending->cancelWatcher = watcher;
    connect(watcher, &QFutureWatcherBase::canceled, this, [this, reply]() {
        abortWith(reply, Ending::Cancelled);
    });
    watcher->setFuture(future);
    m_pending.emplace(reply, std::move(pending));
    return future;
}

QNetworkReply* HttpClient::start(const HttpRequest& request)
{
    QNetworkRequest networkRequest(request.url);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                request.redirects == RedirectPolicy::Follow
                                    ? QNetworkRequest::NoLessSafeRedirectPolicy
                                    : QNetworkRequest::ManualRedirectPolicy);
    for (const auto& [name, value] : request.headers) {
        networkRequest.setRawHeader(name, value);
    }

    switch (request.method) {
    case HttpMethod::Get:
        return m_manager->get(networkRequest);
    case HttpMethod::Head:
        return m_manager->head(networkRequest);
    case HttpMethod::Post:
        return m_manager->post(networkRequest, request.body);
    case HttpMethod::Put:
        return m_manager->put(networkRequest, request.body);
    case HttpMethod::Patch:
        return m_manager->sendCustomRequest(networkRequest, QByteArrayLiteral("PATCH"), request.body);
    case HttpMethod::Delete:
        if (request.body.isEmpty()) {
            return m_manager->deleteResource(networkRequest);
        }
        return m_manager->sendCustomRequest(networkRequest, QByteArrayLiteral("DELETE"), request.body);
    }
    Q_UNREACHABLE_RETURN(nullptr);
}

void HttpClient::watch(QNetworkReply* reply, Pending& pending, const HttpRequest& request)
{
    // The reply pointer captured below stays valid for every connection: each
    // one is cut in onFinished or cancelAll before the reply is deleted.
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    pending.deadline = deadline;
    connect(deadline, &QTimer::timeout, this, [this, reply]() { abortWith(reply, Ending::TimedOut); });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply]() { onMetaDataChanged(reply); });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() { onReadyRead(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onFinished(reply); });
    deadline->start(request.timeout);
}

void HttpClient::cancelAll()
{
    // Taken out first so nothing that runs during abort() can find them.
    std::unordered_map<QNetworkReply*, std::unique_ptr<Pending>> pending = std::exchange(m_pending, {});
    for (auto& [reply, entry] : pending) {
        QObject::disconnect(reply, nullptr, this, nullptr);
        QObject::disconnect(entry->deadline, nullptr, this, nullptr);
        QObject::disconnect(entry->cancelWatcher, nullptr, this, nullptr);
        entry->deadline->stop();
        reply->abort();
        finishPromise(entry->promise, Failure::of(FailureKind::Cancelled, QStringLiteral("request cancelled")));
        reply->deleteLater();
    }
}

void HttpClient::onMetaDataChanged(QNetworkReply* reply)
{
    const auto found = m_pending.find(reply);
    if (found == m_pending.end()) {
        return;
    }
    bool known = false;
    const qint64 announced = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&known);
    if (known && announced > found->second->maxBytes) {
        abortWith(reply, Ending::Oversized);
    }
}

void HttpClient::onReadyRead(QNetworkReply* reply)
{
    const auto found = m_pending.find(reply);
    if (found == m_pending.end()) {
        return;
    }
    Pending& pending = *found->second;
    if (pending.body.size() + reply->bytesAvailable() > pending.maxBytes) {
        abortWith(reply, Ending::Oversized);
        return;
    }
    pending.body += reply->readAll();
}

void HttpClient::onFinished(QNetworkReply* reply)
{
    auto node = m_pending.extract(reply);
    if (node.empty()) {
        return;
    }
    const std::unique_ptr<Pending> pending = std::move(node.mapped());
    QObject::disconnect(reply, nullptr, this, nullptr);
    QObject::disconnect(pending->deadline, nullptr, this, nullptr);
    QObject::disconnect(pending->cancelWatcher, nullptr, this, nullptr);
    pending->deadline->stop();
    finishPromise(pending->promise, outcomeOf(*reply, *pending));
    reply->deleteLater();
}

void HttpClient::abortWith(QNetworkReply* reply, Ending ending)
{
    const auto found = m_pending.find(reply);
    if (found == m_pending.end()) {
        return;
    }
    if (found->second->ending == Ending::None) {
        found->second->ending = ending;
    }
    // Emits finished, which completes the request through onFinished.
    reply->abort();
}

Result<HttpResponse> HttpClient::outcomeOf(QNetworkReply& reply, Pending& pending)
{
    const QVariant statusAttribute = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    const QString oversized = QStringLiteral("response is larger than %1 bytes").arg(pending.maxBytes);

    switch (pending.ending) {
    case Ending::Oversized:
        return Failure::of(FailureKind::InvalidResponse, oversized, status);
    case Ending::TimedOut:
        return Failure::of(FailureKind::Timeout, QStringLiteral("no complete response in time"), status);
    case Ending::Cancelled:
        return Failure::of(FailureKind::Cancelled, QStringLiteral("request cancelled"), status);
    case Ending::None:
        break;
    }

    if (pending.body.size() + reply.bytesAvailable() > pending.maxBytes) {
        return Failure::of(FailureKind::InvalidResponse, oversized, status);
    }
    pending.body += reply.readAll();

    const QNetworkReply::NetworkError error = reply.error();
    if (status == 0) {
        if (error == QNetworkReply::NoError) {
            return Failure::of(FailureKind::InvalidResponse, QStringLiteral("response has no HTTP status"));
        }
        return networkFailure(error, 0);
    }
    if (status < 100 || status > 599) {
        return Failure::of(FailureKind::InvalidResponse, QStringLiteral("HTTP status out of range"), status);
    }
    if (status < 200 || status >= 300) {
        return statusFailure(status);
    }
    if (error != QNetworkReply::NoError) {
        // A success status whose body did not arrive whole.
        return networkFailure(error, status);
    }
    return HttpResponse{status, reply.rawHeaderPairs(), std::move(pending.body)};
}

} // namespace lmp::core::net
