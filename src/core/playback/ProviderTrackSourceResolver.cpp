#include "core/playback/ProviderTrackSourceResolver.h"

#include "core/db/TrackRepository.h"
#include "core/mode/ModeService.h"
#include "core/model/CatalogSourceKey.h"
#include "core/model/TrackKeys.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/playback/PlayerController.h"
#include "core/playback/StreamCache.h"
#include "core/remote/MediaAddresses.h"
#include "core/remote/RemoteMusicService.h"

#include <QFileInfo>
#include <QPointer>

namespace lmp::core {

namespace {

[[nodiscard]] QFuture<Result<QUrl>> ready(Result<QUrl> result)
{
    return QtFuture::makeReadyValueFuture(std::move(result));
}

} // namespace

ProviderTrackSourceResolver::ProviderTrackSourceResolver(Dependencies dependencies)
    : m_deps(dependencies)
{
}

QFuture<Result<QUrl>> ProviderTrackSourceResolver::resolve(const Track& track)
{
    const QString key = track_keys::streamCacheKey(m_deps.remote.scopeKeyParts(), track);
    if (const QString cached = m_deps.cache.readyPath(key); !cached.isEmpty()) {
        return ready(QUrl::fromLocalFile(cached));
    }
    return resolveRemotely(track);
}

QFuture<Result<QUrl>> ProviderTrackSourceResolver::refresh(const Track& track)
{
    return resolveRemotely(track);
}

QFuture<Result<QUrl>> ProviderTrackSourceResolver::resolveRemotely(const Track& track)
{
    if (!net::isHttpUrl(track.sourceUrl)) {
        return ready(Failure::of(FailureKind::NotFound, QStringLiteral("song has no source address")));
    }
    const std::optional<provider::ProviderConnection> connection = m_deps.mode.providerConnection();
    if (!connection) {
        return ready(Failure::of(FailureKind::NotAvailable, QStringLiteral("no provider")));
    }
    const QString key = track_keys::streamCacheKey(m_deps.remote.scopeKeyParts(), track);
    StreamCache* cache = &m_deps.cache;
    db::TrackRepository* tracks = &m_deps.tracks;
    return m_deps.remote.resolve(track.sourceUrl)
        .then(cache, [cache, tracks, track, key, connection = *connection](const Result<QByteArray>& answer) -> Result<QUrl> {
            if (!answer && answer.failure().kind == FailureKind::Cancelled) {
                return answer.failure();
            }
            // A provider that cannot resolve the song still streams it from its link.
            const QString resolved = answer ? remote::parseResolvedStream(answer.value()).streamUrl : QString();
            const QString stream = remote::providerStreamUrl(resolved, track.sourceUrl, track.sourceTag,
                                                             connection.baseUrl, connection.apiKey);
            if (stream.isEmpty()) {
                return Failure::of(FailureKind::NotFound, QStringLiteral("no stream address for this song"));
            }
            const QUrl url(stream, QUrl::StrictMode);
            if (!url.isValid()) {
                return Failure::of(FailureKind::NotFound, QStringLiteral("no stream address for this song"));
            }
            cache->prefetch(key, url);
            if (const QString sourceKey = catalogSourceKey(track); !sourceKey.isEmpty()) {
                [[maybe_unused]] const Result<qint64> kept = tracks->upsertRemoteTrack(track, sourceKey);
            }
            return url;
        });
}

ProviderStreamPrefetcher::ProviderStreamPrefetcher(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(dependencies)
{
    connect(&m_deps.player, &PlayerController::currentTrackChanged, this, &ProviderStreamPrefetcher::prefetchUpcoming);
    connect(&m_deps.player, &PlayerController::queueChanged, this, &ProviderStreamPrefetcher::prefetchUpcoming);
}

void ProviderStreamPrefetcher::prefetchUpcoming()
{
    if (!m_deps.mode.capabilities().prefetchUpcomingSongs) {
        return;
    }
    const std::optional<provider::ProviderConnection> connection = m_deps.mode.providerConnection();
    if (!connection) {
        return;
    }
    const track_keys::RemoteScopeKeyParts scope = m_deps.remote.scopeKeyParts();
    const QList<QueueEntry> upcoming = m_deps.player.visibleQueue();
    for (qsizetype index = 0; index < upcoming.size() && index < Lookahead; ++index) {
        const Track& track = upcoming.at(index).track;
        if (!track.isRemote()) {
            continue;
        }
        const QString key = track_keys::streamCacheKey(scope, track);
        const QString stream = remote::providerStreamUrl(track.filePath, track.sourceUrl, track.sourceTag,
                                                         connection->baseUrl, connection->apiKey);
        if (key.isEmpty() || stream.isEmpty()) {
            continue;
        }
        m_deps.cache.prefetch(key, QUrl(stream, QUrl::StrictMode));
    }
}

} // namespace lmp::core
