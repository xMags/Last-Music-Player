#include "core/model/Track.h"

#include "core/net/ArtworkUrl.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QJsonValue>
#include <QLatin1StringView>

#include <array>

namespace lmp::core {

namespace {

// The snapshot keys, unchanged from earlier versions.
constexpr QLatin1StringView kTitle{"title"};
constexpr QLatin1StringView kArtist{"artist"};
constexpr QLatin1StringView kAlbum{"album"};
constexpr QLatin1StringView kGenre{"genre"};
constexpr QLatin1StringView kFilePath{"filePath"};
constexpr QLatin1StringView kArtworkUrl{"artworkUrl"};
constexpr QLatin1StringView kDateAdded{"dateAdded"};
constexpr QLatin1StringView kDuration{"duration"};
constexpr QLatin1StringView kSourceKind{"sourceKind"};
constexpr QLatin1StringView kProvider{"provider"};
constexpr QLatin1StringView kRemoteProvider{"remoteProvider"};
constexpr QLatin1StringView kSourceUrl{"sourceUrl"};
constexpr QLatin1StringView kSourceLabel{"sourceLabel"};
constexpr QLatin1StringView kRemoteId{"remoteId"};
constexpr QLatin1StringView kRemoteCatalogId{"remoteCatalogId"};
constexpr QLatin1StringView kAlbumCatalogId{"albumCatalogId"};
constexpr QLatin1StringView kArtistCatalogId{"artistCatalogId"};
constexpr QLatin1StringView kDurationSeconds{"durationSeconds"};
constexpr QLatin1StringView kDateAddedSortKey{"dateAddedSortKey"};
constexpr QLatin1StringView kIsLiked{"isLiked"};

constexpr QLatin1StringView kRemote{"remote"};

// A value of the wrong type reads as missing.
QString stringOr(const QJsonObject& object, QLatin1StringView key, const QString& fallback = {})
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

double numberOr(const QJsonObject& object, QLatin1StringView key, double fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toDouble() : fallback;
}

bool boolOr(const QJsonObject& object, QLatin1StringView key, bool fallback)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

} // namespace

bool Track::isRemote() const
{
    return sourceKind.toLower() == kRemote;
}

bool Track::isDownloadable() const
{
    // Exactly "remote" with any case for the first letter only, as before.
    const bool remote = sourceKind.size() == 6
        && (sourceKind.front() == QLatin1Char('r') || sourceKind.front() == QLatin1Char('R'))
        && QStringView(sourceKind).sliced(1) == QLatin1StringView("emote");
    return remote && net::isHttpUrl(sourceUrl);
}

QString Track::sourceBadgeText() const
{
    // Labels that mean "this PC", including generated playlists built from
    // local songs. Anything else names a remote origin, which is what the
    // badge is for, so a purely local library shows no badges.
    static constexpr std::array<QLatin1StringView, 7> localLabels{
        QLatin1StringView("Local"),    QLatin1StringView("On this PC"), QLatin1StringView("Manual"),
        QLatin1StringView("Album"),    QLatin1StringView("Playlist"),   QLatin1StringView("System"),
        QLatin1StringView("Auto mix"),
    };
    if (sourceLabel.isEmpty()) {
        return {};
    }
    for (const QLatin1StringView local : localLabels) {
        if (sourceLabel == local) {
            return {};
        }
    }
    return sourceLabel.toUpper();
}

QJsonObject Track::toSnapshotJson() const
{
    const bool remote = isRemote();
    QString safeArtworkUrl = artworkUrl;
    QString safeSourceUrl = sourceUrl;
    if (remote) {
        // An account cover link ("/v1/music/art/<id>") is a relative path, not
        // a web address, but it stays valid and is what account songs carry.
        const bool coverLink =
            !safeArtworkUrl.isEmpty() && net::accountCoverLinkPath(safeArtworkUrl) == safeArtworkUrl;
        if (!coverLink && !net::isSafeRemoteUrl(safeArtworkUrl, net::RemoteUrlUse::Durable)) {
            safeArtworkUrl.clear();
        }
        if (!net::isSafeRemoteUrl(safeSourceUrl, net::RemoteUrlUse::Durable)) {
            safeSourceUrl.clear();
        }
    }

    QJsonObject object;
    object.insert(kTitle, title);
    object.insert(kArtist, artist);
    object.insert(kAlbum, album);
    object.insert(kGenre, genre);
    object.insert(kFilePath, remote ? QString() : filePath);
    object.insert(kArtworkUrl, safeArtworkUrl);
    object.insert(kDateAdded, dateAdded);
    object.insert(kDuration, durationText);
    object.insert(kSourceKind, sourceKind);
    object.insert(kProvider, origin);
    object.insert(kRemoteProvider, sourceTag);
    object.insert(kSourceUrl, safeSourceUrl);
    object.insert(kSourceLabel, sourceLabel);
    object.insert(kRemoteId, remoteId);
    object.insert(kRemoteCatalogId, catalogId);
    object.insert(kAlbumCatalogId, albumCatalogId);
    object.insert(kArtistCatalogId, artistCatalogId);
    object.insert(kDurationSeconds, durationSeconds);
    object.insert(kDateAddedSortKey, dateAddedSortKey);
    object.insert(kIsLiked, liked);
    return object;
}

Track Track::fromSnapshotJson(const QJsonObject& object)
{
    Track track;
    track.title = stringOr(object, kTitle);
    track.artist = stringOr(object, kArtist);
    track.album = stringOr(object, kAlbum);
    track.genre = stringOr(object, kGenre);
    track.filePath = stringOr(object, kFilePath);
    track.artworkUrl = stringOr(object, kArtworkUrl);
    track.dateAdded = stringOr(object, kDateAdded);
    track.durationText = stringOr(object, kDuration);
    // Old snapshots without a kind are remote when the "path" is a web address.
    track.sourceKind = stringOr(object, kSourceKind,
                                net::isHttpUrl(track.filePath) ? QString(kRemote) : QStringLiteral("local"));
    track.origin = stringOr(object, kProvider);
    track.sourceTag = stringOr(object, kRemoteProvider);
    track.sourceUrl = stringOr(object, kSourceUrl);
    track.remoteId = stringOr(object, kRemoteId);
    track.catalogId = stringOr(object, kRemoteCatalogId);
    track.albumCatalogId = stringOr(object, kAlbumCatalogId);
    track.artistCatalogId = stringOr(object, kArtistCatalogId);
    track.sourceLabel = stringOr(object, kSourceLabel,
                                 track.sourceKind == kRemote ? QStringLiteral("Remote") : QStringLiteral("Local"));
    track.liked = boolOr(object, kIsLiked, false);
    track.durationSeconds = numberOr(object, kDurationSeconds, 0.0);
    track.dateAddedSortKey = numberOr(object, kDateAddedSortKey, 0.0);
    return track;
}

} // namespace lmp::core
