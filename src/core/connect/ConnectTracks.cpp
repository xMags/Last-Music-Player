#include "core/connect/ConnectTracks.h"

#include <limits>
#include "core/net/Json.h"
#include "core/account/AccountLibrarySync.h"
#include "core/account/AccountMusicContract.h"
#include "core/net/ArtworkUrl.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/playback/QueueTrackIdentity.h"
#include "core/remote/TrackJson.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonValue>

#include <cmath>
#include <optional>

namespace lmp::core::connect {

namespace {

[[nodiscard]] QString text(const QJsonObject& object, QLatin1StringView key, qsizetype limit = 300)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().left(limit) : QString();
}

[[nodiscard]] QString madeUpId(const Track& track)
{
    const QByteArray digest =
        QCryptographicHash::hash(queueTrackIdentity(track).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("lmp-") + QString::fromLatin1(digest.left(32));
}

[[nodiscard]] QString formatLength(double seconds)
{
    if (!(seconds > 0.0) || !std::isfinite(seconds)) {
        return {};
    }
    const qint64 total = json::wholeNumber(std::round(seconds), 0, std::numeric_limits<qint64>::max());
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

QJsonObject connectTrackJson(const Track& track)
{
    const QByteArray portable = remote::track_json::accountTrackJson(track);
    if (!portable.isEmpty()) {
        return QJsonDocument::fromJson(portable).object();
    }
    QJsonObject object;
    object.insert(QLatin1StringView("id"), track.remoteId.isEmpty() ? madeUpId(track) : track.remoteId);
    object.insert(QLatin1StringView("title"), track.title);
    object.insert(QLatin1StringView("artist"), track.artist);
    object.insert(QLatin1StringView("album"), track.album);
    object.insert(QLatin1StringView("durationSec"), std::max(0.0, track.durationSeconds));
    const bool remote = track.isRemote() && net::isSafeRemoteUrl(track.sourceUrl, net::RemoteUrlUse::Durable);
    if (!remote) {
        object.insert(QLatin1StringView("unavailable"), true);
        object.insert(QLatin1StringView("unavailableReason"), QStringLiteral("This song is a file on another device."));
        return object;
    }
    object.insert(QLatin1StringView("sourceKind"), track.sourceKind.isEmpty() ? QStringLiteral("remote") : track.sourceKind);
    const QString provider = !track.sourceTag.isEmpty() ? track.sourceTag : track.origin;
    if (!provider.isEmpty()) {
        object.insert(QLatin1StringView("provider"), provider);
    }
    object.insert(QLatin1StringView("sourceUrl"), track.sourceUrl);
    const QString artwork = net::durableAccountArtworkUrl(track.artworkUrl);
    if (!artwork.isEmpty()) {
        object.insert(QLatin1StringView("artworkUrl"), artwork);
    }
    if (!track.catalogId.isEmpty()) {
        object.insert(QLatin1StringView("catalogId"), track.catalogId);
    }
    if (!track.albumCatalogId.isEmpty()) {
        object.insert(QLatin1StringView("albumCatalogId"), track.albumCatalogId);
    }
    if (!track.artistCatalogId.isEmpty()) {
        object.insert(QLatin1StringView("artistCatalogId"), track.artistCatalogId);
    }
    return object;
}

Track trackFromConnect(const QJsonObject& object)
{
    if (object.value(QLatin1StringView("unavailable")) != QJsonValue(true)) {
        const std::optional<account::PortableAccountTrack> portable =
            account::parsePortableTrack(QJsonDocument(object).toJson(QJsonDocument::Compact));
        if (portable) {
            return account::trackFromPortable(*portable);
        }
    }
    Track track;
    track.remoteId = text(object, QLatin1StringView("id"), 128);
    track.title = text(object, QLatin1StringView("title"));
    if (track.title.isEmpty()) {
        track.title = text(object, QLatin1StringView("name"));
    }
    track.artist = text(object, QLatin1StringView("artist"));
    track.album = text(object, QLatin1StringView("album"));
    const QString artwork = text(object, QLatin1StringView("artworkUrl"), 2048);
    if (net::isSafeRemoteUrl(artwork, net::RemoteUrlUse::Durable)) {
        track.artworkUrl = artwork;
    }
    const double length = object.value(QLatin1StringView("durationSec")).toDouble();
    track.durationSeconds = std::isfinite(length) ? std::max(0.0, length) : 0.0;
    track.durationText = formatLength(track.durationSeconds);
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("account");
    track.unavailable = true;
    return track;
}

QString connectTrackId(const QJsonObject& object)
{
    return text(object, QLatin1StringView("id"), 128);
}

} // namespace lmp::core::connect
