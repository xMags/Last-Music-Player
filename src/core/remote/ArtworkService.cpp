#include "core/remote/ArtworkService.h"
#include "core/app/PerfLog.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/net/ArtworkUrl.h"
#include "core/net/HttpMessage.h"
#include "core/net/IHttpTransport.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/playback/PlaybackClock.h"
#include "core/remote/ArtworkDiskCache.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <optional>
#include <utility>

namespace lmp::core::remote {

namespace {

constexpr std::chrono::seconds kPictureTimeout{20};

template <typename T>
[[nodiscard]] QFuture<Result<T>> ready(Result<T> result)
{
    return QtFuture::makeReadyValueFuture(std::move(result));
}

// A failure another run could get past: the service was busy or the
// network wobbled, as opposed to a picture that is not there.
// The wait the server asked for (Retry-After), or 0.
[[nodiscard]] std::chrono::milliseconds serverWait(const Failure& failure)
{
    return std::chrono::seconds(std::max(0, failure.retryAfterSeconds.value_or(0)));
}

[[nodiscard]] bool isTransient(const Failure& failure)
{
    switch (failure.kind) {
    case FailureKind::Timeout:
    case FailureKind::Network:
    case FailureKind::RateLimited:
    case FailureKind::Server:
        return true;
    default:
        break;
    }
    return failure.httpStatus == 408 || failure.httpStatus == 429 || failure.httpStatus >= 500;
}

// The picture a resolve answer names: the song's own artwork, else the
// result's ("artworkUrl", then "imageUrl").
[[nodiscard]] QString resolvedArtworkUrl(const QByteArray& body)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    const QJsonValue result = document.object().value(QLatin1StringView("result"));
    if (!result.isObject()) {
        return {};
    }
    const auto picture = [](const QJsonObject& object) {
        const QJsonValue artwork = object.value(QLatin1StringView("artworkUrl"));
        if (artwork.isString() && !artwork.toString().isEmpty()) {
            return artwork.toString();
        }
        const QJsonValue image = object.value(QLatin1StringView("imageUrl"));
        return image.isString() ? image.toString() : QString();
    };
    const QJsonValue track = result.toObject().value(QLatin1StringView("track"));
    if (track.isObject()) {
        QString nested = picture(track.toObject());
        if (!nested.isEmpty()) {
            return nested;
        }
    }
    return picture(result.toObject());
}

// The form a picture address is keyed and fetched by: a cover link's path,
// or the address asked for at display size.
[[nodiscard]] QString pictureAddress(const QString& artworkUrl)
{
    const QString coverLink = net::accountCoverLinkPath(artworkUrl);
    return coverLink.isEmpty() ? net::normalizeArtworkUrlForDisplay(artworkUrl) : coverLink;
}

} // namespace

ArtworkService::ArtworkService(const Dependencies& dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(dependencies)
    , m_timer(dependencies.clock.createTimer([this]() { runDueSteps(); }))
{
    // Pictures and requests belong to the account (or the mode) they were
    // fetched under.
    connect(&m_deps.session, &account::AccountSessionService::ownerChanged, this, &ArtworkService::clear);
    connect(&m_deps.mode, &mode::ModeService::changed, this, [this]() {
        if (!m_requests.isEmpty() || !m_cache.isEmpty()) {
            const mode::SourceMode now = m_deps.mode.mode();
            if (now != m_lastMode) {
                clear();
            }
        }
        m_lastMode = m_deps.mode.mode();
    });
    m_lastMode = m_deps.mode.mode();
    connect(&m_deps.session, &account::AccountSessionService::restoreFinished, this,
            &ArtworkService::resumeAwaitingSession);
}

ArtworkService::~ArtworkService()
{
    m_timer->stop();
    for (AwaitingSession& waiting : m_awaitingSession) {
        std::vector<Promise> waiters{std::move(waiting.promise)};
        settle(waiters, Failure::of(FailureKind::Cancelled, QStringLiteral("artwork service closed")));
    }
    for (auto it = m_requests.begin(); it != m_requests.end(); ++it) {
        settle(it->waiters, Failure::of(FailureKind::Cancelled, QStringLiteral("artwork service closed")));
    }
}

QString ArtworkService::keyFor(const QString& artworkUrl, const QString& sourceUrl)
{
    // Every size of the same picture shares one key and one fetch.
    return artworkUrl.isEmpty() ? QStringLiteral("source|") + sourceUrl : QStringLiteral("artwork|") + artworkUrl;
}

bool ArtworkService::isCached(const QString& artworkUrl, const QString& sourceUrl) const
{
    return m_cache.contains(keyFor(pictureAddress(artworkUrl), sourceUrl));
}

QString ArtworkService::requestKey(const QString& artworkUrl, const QString& sourceUrl) const
{
    const bool accountMode = m_deps.mode.mode() == mode::SourceMode::Account;
    const QString coverLink = net::accountCoverLinkPath(artworkUrl);
    const QString normalized = pictureAddress(artworkUrl);
    const bool hasArtwork = coverLink.isEmpty() ? !normalized.isEmpty() && net::isHttpUrl(normalized) : accountMode;
    const bool canResolve = normalized.isEmpty() && net::isHttpUrl(sourceUrl) && accountMode;
    if (!hasArtwork && !canResolve) {
        return {};
    }
    return keyFor(hasArtwork ? normalized : QString(), hasArtwork ? QString() : sourceUrl);
}

QString ArtworkService::diskScope() const
{
    if (m_deps.disk == nullptr) {
        return {};
    }
    if (m_deps.mode.mode() != mode::SourceMode::Account) {
        return QStringLiteral("open");
    }
    // An account's pictures go through its session: they are its own.
    const QString owner = m_deps.session.profile().id;
    return owner.isEmpty() ? QString() : QStringLiteral("account|") + owner;
}

QString ArtworkService::accountKeptScope() const
{
    if (m_deps.disk == nullptr || m_deps.mode.mode() == mode::SourceMode::Account) {
        return {};
    }
    const QString owner = m_deps.session.profile().id;
    return owner.isEmpty() ? QString() : QStringLiteral("account|") + owner;
}

QString ArtworkService::diskKey(const QString& artworkUrl, const QString& sourceUrl) const
{
    return ArtworkDiskCache::fileKey(diskScope(), requestKey(artworkUrl, sourceUrl));
}

QFuture<bool> ArtworkService::keep(const QString& artworkUrl, const QString& sourceUrl)
{
    const QString fileKey = diskKey(artworkUrl, sourceUrl);
    if (fileKey.isEmpty()) {
        return QtFuture::makeReadyValueFuture(false);
    }
    ArtworkDiskCache* disk = m_deps.disk;
    auto promise = std::make_shared<QPromise<bool>>();
    promise->start();
    QFuture<bool> future = promise->future();
    disk->keepIfCached(fileKey).then(this, [this, disk, fileKey, artworkUrl, sourceUrl, promise](bool kept) {
        if (kept) {
            promise->addResult(true);
            promise->finish();
            return;
        }
        fetch(artworkUrl, sourceUrl, Priority::Backfill)
            .then(this, [this, disk, fileKey, artworkUrl, sourceUrl, promise](const Result<QByteArray>& bytes) {
                // Only for the account it was asked for.
                const bool ok = bytes && !bytes.value().isEmpty() && diskKey(artworkUrl, sourceUrl) == fileKey;
                if (ok) {
                    disk->keep(fileKey, bytes.value());
                }
                promise->addResult(ok);
                promise->finish();
            });
    });
    return future;
}

void ArtworkService::release(const QString& artworkUrl, const QString& sourceUrl)
{
    if (const QString fileKey = diskKey(artworkUrl, sourceUrl); !fileKey.isEmpty()) {
        m_deps.disk->release(fileKey);
    }
}

void ArtworkService::keepOnly(const QList<QString>& artworkUrls)
{
    const QString scope = diskScope();
    if (scope.isEmpty()) {
        return;
    }
    QSet<QString> keys;
    for (const QString& url : artworkUrls) {
        if (const QString fileKey = diskKey(url); !fileKey.isEmpty()) {
            keys.insert(fileKey);
        }
    }
    m_deps.disk->releaseAllExcept(scope, keys);
}

void ArtworkService::probeDisk(const QString& key, quint64 id, const QString& diskKey)
{
    m_deps.disk->read(diskKey).then(this, [this, key, id](const std::optional<QByteArray>& bytes) {
        onProbe(key, id, bytes);
    });
}

void ArtworkService::onProbe(const QString& key, quint64 id, const std::optional<QByteArray>& bytes)
{
    const auto found = m_requests.find(key);
    if (found == m_requests.end() || found->id != id || !found->probing) {
        return; // dropped meanwhile (an account change)
    }
    if (bytes && !bytes->isEmpty()) {
        Request request = m_requests.take(key);
        qCDebug(lcPerf) << "cover read from disk:" << bytes->size() << "bytes";
        store(key, *bytes);
        settle(request.waiters, *bytes);
        return;
    }
    if (!found->moreDiskKeys.isEmpty()) {
        probeDisk(key, id, found->moreDiskKeys.takeFirst());
        return;
    }
    if (found->diskOnly) {
        Request request = m_requests.take(key);
        settle(request.waiters, Failure::of(FailureKind::NotFound, QStringLiteral("picture is not on this PC")));
        return;
    }
    // Not on disk: to the network, at the priority it holds now.
    found->probing = false;
    (found->priority == Priority::Visible ? m_visibleQueue : m_backfillQueue).push_back(key);
    startRequests();
}

QFuture<Result<QByteArray>> ArtworkService::fetch(const QString& artworkUrl, const QString& sourceUrl,
                                                 Priority priority)
{
    const bool accountMode = m_deps.mode.mode() == mode::SourceMode::Account;
    const QString coverLink = net::accountCoverLinkPath(artworkUrl);
    const QString normalized = pictureAddress(artworkUrl);
    // The covers the signed-in account kept for its downloads stay readable
    // in the other modes: they are files on this PC.
    const QString accountKept = accountKeptScope();
    // A cover link loads from the account service only; elsewhere it can
    // only be one of those kept covers.
    const bool diskOnly = !coverLink.isEmpty() && !accountMode && !accountKept.isEmpty();
    const bool hasArtwork =
        coverLink.isEmpty() ? !normalized.isEmpty() && net::isHttpUrl(normalized) : accountMode || diskOnly;
    const bool canResolve = normalized.isEmpty() && net::isHttpUrl(sourceUrl) && accountMode;
    if (!hasArtwork && !canResolve) {
        return ready(Result<QByteArray>(Failure::of(FailureKind::NotFound, QStringLiteral("no picture address"))));
    }

    const QString key = keyFor(hasArtwork ? normalized : QString(), hasArtwork ? QString() : sourceUrl);
    if (std::optional<QByteArray> bytes = cached(key)) {
        return ready(Result<QByteArray>(*std::move(bytes)));
    }

    auto promise = std::make_shared<QPromise<Result<QByteArray>>>();
    promise->start();
    QFuture<Result<QByteArray>> future = promise->future();

    // Every Account mode fetch needs the session; while the stored one is
    // still being checked it would fail for good, so it waits instead.
    if (accountMode && m_deps.session.isRestoring() && !m_deps.session.captureOperation()) {
        m_awaitingSession.push_back(AwaitingSession{
            .artworkUrl = artworkUrl, .sourceUrl = sourceUrl, .priority = priority, .promise = std::move(promise)});
        return future;
    }

    if (auto pending = m_requests.find(key); pending != m_requests.end()) {
        pending->waiters.push_back(std::move(promise));
        if (priority == Priority::Visible) {
            promote(hasArtwork ? normalized : QString(), hasArtwork ? QString() : sourceUrl);
        }
        return future;
    }

    Request request;
    request.id = ++m_nextId;
    request.artworkUrl = hasArtwork ? normalized : QString();
    request.sourceUrl = hasArtwork ? QString() : sourceUrl;
    request.priority = priority;
    request.waiters.push_back(std::move(promise));
    // The disk first: a picture shown before needs no network.
    request.diskOnly = diskOnly;
    if (!diskOnly) {
        request.diskKey = ArtworkDiskCache::fileKey(diskScope(), key);
    }
    if (const QString kept = ArtworkDiskCache::fileKey(accountKept, key); !kept.isEmpty()) {
        request.moreDiskKeys.append(kept);
    }
    QString firstProbe = request.diskKey;
    if (firstProbe.isEmpty() && !request.moreDiskKeys.isEmpty()) {
        firstProbe = request.moreDiskKeys.takeFirst();
    }
    if (!firstProbe.isEmpty()) {
        request.probing = true;
        const quint64 id = request.id;
        m_requests.insert(key, std::move(request));
        probeDisk(key, id, firstProbe);
        return future;
    }
    m_requests.insert(key, std::move(request));
    (priority == Priority::Visible ? m_visibleQueue : m_backfillQueue).push_back(key);
    startRequests();
    return future;
}

void ArtworkService::promote(const QString& artworkUrl, const QString& sourceUrl)
{
    const QString key = keyFor(artworkUrl.isEmpty() ? QString() : pictureAddress(artworkUrl),
                               artworkUrl.isEmpty() ? sourceUrl : QString());
    const auto pending = m_requests.find(key);
    if (pending == m_requests.end() || pending->priority == Priority::Visible) {
        return;
    }
    pending->priority = Priority::Visible;
    // A request waiting out a back-off keeps waiting: promoting it must not
    // let a scrolling user hammer a relay that is refusing.
    if (pending->deferred) {
        return;
    }
    const auto queued = std::find(m_backfillQueue.begin(), m_backfillQueue.end(), key);
    if (queued == m_backfillQueue.end()) {
        return; // already running, at the slot it took
    }
    m_backfillQueue.erase(queued);
    m_visibleQueue.push_back(key);
    startRequests();
}

void ArtworkService::resumeAwaitingSession()
{
    std::vector<AwaitingSession> waiting = std::exchange(m_awaitingSession, {});
    for (AwaitingSession& entry : waiting) {
        Promise promise = std::move(entry.promise);
        fetch(entry.artworkUrl, entry.sourceUrl, entry.priority)
            .then(this, [promise](const Result<QByteArray>& result) {
                std::vector<Promise> waiters{promise};
                settle(waiters, result);
            });
    }
}

void ArtworkService::clear()
{
    std::vector<Promise> waiters;
    for (auto it = m_requests.begin(); it != m_requests.end(); ++it) {
        for (Promise& promise : it->waiters) {
            waiters.push_back(std::move(promise));
        }
    }
    m_requests.clear();
    m_cache.clear();
    m_cacheUse.clear();
    m_cacheBytes = 0;
    m_visibleQueue.clear();
    m_backfillQueue.clear();
    m_due.erase(std::remove_if(m_due.begin(), m_due.end(),
                               [](const DueStep& step) { return step.kind == DueStep::Kind::Requeue; }),
                m_due.end());
    // Running fetches are not stopped, only orphaned: each still ends and
    // gives back its slot when it finds its request gone.
    settle(waiters, Failure::of(FailureKind::Cancelled, QStringLiteral("account or mode changed")));
    armTimer();
    emit scopeChanged();
}

void ArtworkService::startRequests()
{
    while (m_active < MaxActive) {
        QString key;
        Priority slot = Priority::Visible;
        if (!m_visibleQueue.empty()) {
            key = std::move(m_visibleQueue.front());
            m_visibleQueue.pop_front();
        } else if (m_activeBackfill < MaxActiveBackfill && !m_backfillQueue.empty()) {
            key = std::move(m_backfillQueue.front());
            m_backfillQueue.pop_front();
            slot = Priority::Backfill;
        } else {
            break;
        }
        if (!m_requests.contains(key)) {
            continue;
        }
        ++m_active;
        if (slot == Priority::Backfill) {
            ++m_activeBackfill;
        }
        startRun(key, slot);
    }
}

void ArtworkService::startRun(const QString& key, Priority slot)
{
    Request& request = m_requests[key];
    ++request.runs;
    const quint64 id = request.id;
    const mode::RemoteScope scope = m_deps.mode.captureScope();
    artworkFor(request.artworkUrl, request.sourceUrl)
        .then(this, [this, key, id, slot, scope](const Result<QString>& url) {
            const auto found = m_requests.constFind(key);
            if (found == m_requests.constEnd() || found->id != id) {
                complete(slot);
                return;
            }
            if (!url || url.value().isEmpty()) {
                // No picture to fetch: nothing another run would change.
                finishFailure(key, slot, scope,
                              url ? Failure::of(FailureKind::NotFound, QStringLiteral("song has no picture"))
                                  : url.failure(),
                              false);
                return;
            }
            attempt(key, id, slot, 0, url.value(), scope);
        });
}

QFuture<Result<QString>> ArtworkService::artworkFor(const QString& artworkUrl, const QString& sourceUrl)
{
    if (!artworkUrl.isEmpty()) {
        return ready(Result<QString>(artworkUrl));
    }
    const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
    if (!context) {
        return ready(Result<QString>(Failure::of(FailureKind::NotAvailable, QStringLiteral("no account session"))));
    }
    return m_deps.client.resolve(context->bearerSession, sourceUrl)
        .then([](const Result<QByteArray>& answer) -> Result<QString> {
            if (!answer) {
                return answer.failure();
            }
            return net::normalizeArtworkUrlForDisplay(resolvedArtworkUrl(answer.value()));
        });
}

void ArtworkService::attempt(const QString& key, quint64 id, Priority slot, int attemptIndex, const QString& url,
                             const mode::RemoteScope& scope)
{
    download(url).then(this, [this, key, id, slot, attemptIndex, url, scope](const Result<QByteArray>& bytes) {
        onAttempt(key, id, slot, attemptIndex, url, scope, bytes);
    });
}

void ArtworkService::onAttempt(const QString& key, quint64 id, Priority slot, int attemptIndex, const QString& url,
                               const mode::RemoteScope& scope, const Result<QByteArray>& bytes)
{
    const auto found = m_requests.find(key);
    if (found == m_requests.end() || found->id != id) {
        complete(slot);
        return;
    }
    const bool current = m_deps.mode.isCurrent(scope);
    if (bytes && !bytes.value().isEmpty()) {
        if (!current) {
            // The bytes belong to an account that is no longer shown.
            Request request = m_requests.take(key);
            settle(request.waiters, Failure::of(FailureKind::Cancelled, QStringLiteral("account changed")));
            complete(slot);
            return;
        }
        Request request = m_requests.take(key);
        qCDebug(lcPerf) << "cover fetched:" << bytes.value().size() << "bytes";
        store(key, bytes.value());
        if (!request.diskKey.isEmpty() && m_deps.disk != nullptr) {
            m_deps.disk->store(request.diskKey, bytes.value());
        }
        settle(request.waiters, bytes.value());
        complete(slot);
        return;
    }
    const Failure failure = bytes ? Failure::of(FailureKind::InvalidResponse, QStringLiteral("empty picture"))
                                  : bytes.failure();
    const bool transient = !bytes && isTransient(failure) && serverWait(failure) <= MaxServerWait;
    // A quick retry keeps its slot; a server that asks for a longer wait
    // than that gets the slot back and the request is requeued instead.
    const bool retryInSlot = attemptIndex < static_cast<int>(RetryDelays.size())
                             && serverWait(failure) <= RetryDelays[static_cast<std::size_t>(attemptIndex)];
    if (transient && current && retryInSlot) {
        schedule({.kind = DueStep::Kind::NextAttempt,
                  .key = key,
                  .id = id,
                  .at = m_deps.clock.nowMilliseconds()
                      + static_cast<std::uint64_t>(RetryDelays[static_cast<std::size_t>(attemptIndex)].count()),
                  .attempt = attemptIndex + 1,
                  .slot = slot,
                  .url = url,
                  .scope = scope});
        return; // the slot stays taken until the run ends
    }
    finishFailure(key, slot, scope, failure, transient);
}

void ArtworkService::finishFailure(const QString& key, Priority slot, const mode::RemoteScope& scope,
                                   const Failure& failure, bool transient)
{
    const auto found = m_requests.find(key);
    if (found != m_requests.end()) {
        if (transient && found->runs < MaxRuns && m_deps.mode.isCurrent(scope)) {
            const std::size_t index = static_cast<std::size_t>(std::max(0, found->runs - 1));
            const std::chrono::milliseconds delay =
                std::max(RunBackoff[std::min(index, RunBackoff.size() - 1)], serverWait(failure));
            found->deferred = true;
            schedule({.kind = DueStep::Kind::Requeue,
                      .key = key,
                      .id = found->id,
                      .at = m_deps.clock.nowMilliseconds() + static_cast<std::uint64_t>(delay.count()),
                      .attempt = 0,
                      .slot = slot,
                      .url = {},
                      .scope = scope});
        } else {
            Request request = m_requests.take(key);
            settle(request.waiters, failure);
        }
    }
    complete(slot);
}

void ArtworkService::complete(Priority slot)
{
    m_active = std::max(0, m_active - 1);
    // Given back to the slot it started on, not the priority it may have
    // been promoted to since.
    if (slot == Priority::Backfill) {
        m_activeBackfill = std::max(0, m_activeBackfill - 1);
    }
    startRequests();
}

void ArtworkService::schedule(DueStep step)
{
    m_due.push_back(std::move(step));
    armTimer();
}

void ArtworkService::armTimer()
{
    if (m_due.empty()) {
        m_timer->stop();
        return;
    }
    const auto earliest = std::min_element(m_due.begin(), m_due.end(),
                                           [](const DueStep& a, const DueStep& b) { return a.at < b.at; });
    const std::uint64_t now = m_deps.clock.nowMilliseconds();
    const std::uint64_t wait = earliest->at > now ? earliest->at - now : 0;
    m_timer->start(std::chrono::milliseconds(static_cast<qint64>(wait)));
}

void ArtworkService::runDueSteps()
{
    const std::uint64_t now = m_deps.clock.nowMilliseconds();
    const auto due = std::stable_partition(m_due.begin(), m_due.end(), [now](const DueStep& step) { return step.at > now; });
    std::vector<DueStep> dueNow(std::make_move_iterator(due), std::make_move_iterator(m_due.end()));
    m_due.erase(due, m_due.end());

    bool requeued = false;
    for (DueStep& step : dueNow) {
        const auto found = m_requests.find(step.key);
        if (step.kind == DueStep::Kind::NextAttempt) {
            if (found == m_requests.end() || found->id != step.id) {
                complete(step.slot); // its request was dropped meanwhile
            } else {
                attempt(step.key, step.id, step.slot, step.attempt, step.url, step.scope);
            }
            continue;
        }
        if (found == m_requests.end() || found->id != step.id) {
            continue;
        }
        found->deferred = false;
        // Back in at the priority it holds now: a picture scrolled into view
        // while it waited returns as visible work.
        (found->priority == Priority::Visible ? m_visibleQueue : m_backfillQueue).push_back(step.key);
        requeued = true;
    }
    armTimer();
    if (requeued) {
        startRequests();
    }
}

QFuture<Result<QByteArray>> ArtworkService::download(const QString& url)
{
    if (m_deps.mode.mode() == mode::SourceMode::Account) {
        const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
        if (!context) {
            return ready(Result<QByteArray>(Failure::of(FailureKind::NotAvailable, QStringLiteral("no account session"))));
        }
        account::AccountSessionService* session = &m_deps.session;
        const QString coverLink = net::accountCoverLinkPath(url);
        QFuture<Result<QByteArray>> request = coverLink.isEmpty()
            ? m_deps.client.artwork(context->bearerSession, url)
            : m_deps.client.coverArt(context->bearerSession, coverLink);
        return request
            .then(this, [session, context = *context](const Result<QByteArray>& bytes) {
                if (!bytes && bytes.failure().kind == FailureKind::Unauthorized) {
                    session->handleUnauthorized(context);
                }
                return bytes;
            });
    }
    const QUrl address(url, QUrl::StrictMode);
    if (!address.isValid() || !net::isHttpUrl(url)) {
        return ready(Result<QByteArray>(Failure::of(FailureKind::NotFound, QStringLiteral("picture address is invalid"))));
    }
    net::HttpRequest request;
    request.method = net::HttpMethod::Get;
    request.url = address;
    request.timeout = kPictureTimeout;
    request.redirects = net::RedirectPolicy::Follow;
    request.maxResponseBytes = MaxPictureBytes;
    return m_deps.http.send(request).then([](const Result<net::HttpResponse>& response) -> Result<QByteArray> {
        if (!response) {
            return response.failure();
        }
        return response.value().body;
    });
}

std::optional<QByteArray> ArtworkService::cached(const QString& key)
{
    const auto found = m_cache.find(key);
    if (found == m_cache.end()) {
        return std::nullopt;
    }
    m_cacheUse.splice(m_cacheUse.begin(), m_cacheUse, found->use);
    return found->bytes;
}

void ArtworkService::store(const QString& key, const QByteArray& bytes)
{
    if (const auto existing = m_cache.find(key); existing != m_cache.end()) {
        m_cacheBytes -= static_cast<std::size_t>(existing->bytes.size());
        m_cacheUse.erase(existing->use);
        m_cache.erase(existing);
    }
    const auto size = static_cast<std::size_t>(bytes.size());
    // A picture larger than the whole budget is only on disk.
    if (size > MaxCachedBytes) {
        return;
    }
    // The least recently used pictures make room.
    while (m_cacheBytes + size > MaxCachedBytes && !m_cacheUse.empty()) {
        const auto oldest = m_cache.find(m_cacheUse.back());
        m_cacheBytes -= static_cast<std::size_t>(oldest->bytes.size());
        m_cache.erase(oldest);
        m_cacheUse.pop_back();
    }
    // Read in growing steps, a picture's buffer can be nearly twice its
    // size; the kept copy holds the picture alone.
    QByteArray kept = bytes;
    kept.squeeze();
    m_cacheUse.push_front(key);
    m_cache.insert(key, CachedPicture{.bytes = std::move(kept), .use = m_cacheUse.begin()});
    m_cacheBytes += size;
}

void ArtworkService::settle(std::vector<Promise>& waiters, const Result<QByteArray>& result)
{
    for (const Promise& promise : waiters) {
        promise->addResult(result);
        promise->finish();
    }
    waiters.clear();
}

} // namespace lmp::core::remote
