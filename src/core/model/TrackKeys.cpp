#include "core/model/TrackKeys.h"

#include "core/playback/LastTrackIdentity.h"
#include "core/text/CaseFold.h"

#include <QLatin1Char>
#include <QLatin1StringView>

namespace lmp::core::track_keys {

QString remoteScopeCacheKey(const RemoteScopeKeyParts& scope)
{
    QString key = mode::sourceModeName(scope.mode);
    if (scope.mode == mode::SourceMode::ApiKey) {
        key += QLatin1Char(':') + scope.cachePartition;
    }
    key += QLatin1Char(':') + QString::number(scope.generation);
    if (scope.mode == mode::SourceMode::Account) {
        key += QLatin1Char(':') + QString::number(scope.accountGeneration);
    }
    return key;
}

QString streamCacheKey(const RemoteScopeKeyParts& scope, const Track& track)
{
    if (scope.mode != mode::SourceMode::ApiKey || track.sourceUrl.isEmpty()) {
        return {};
    }
    return remoteScopeCacheKey(scope) + QLatin1Char('\n') + track.sourceUrl;
}

QString downloadStableKey(const RemoteScopeKeyParts& scope, const Track& track)
{
    const QString trackKey = legacyCatalogSourceKey(track);
    if (trackKey.isEmpty()) {
        return {};
    }
    // Only the stored kind counts here, as before: a song with an http(s)
    // path but kind "local" keeps its plain key.
    if (foldCaseAscii(track.sourceKind) != QLatin1StringView("remote")) {
        return trackKey;
    }
    if (scope.mode == mode::SourceMode::LocalOnly) {
        return {};
    }
    return QLatin1StringView("download|") + remoteScopeCacheKey(scope) + QLatin1Char('\n') + trackKey;
}

} // namespace lmp::core::track_keys
