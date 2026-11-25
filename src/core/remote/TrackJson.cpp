#include "core/remote/TrackJson.h"

#include <limits>
#include "core/net/Json.h"
#include "core/account/AccountMusicContract.h"
#include "core/net/ArtworkUrl.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/text/CaseFold.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>

namespace lmp::core::remote::track_json {

namespace {

// The text at `key`, or `fallback` when it is missing or not text.
QString text(const QJsonObject& object, QLatin1StringView key, const QString& fallback = {})
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

QString formatTime(double seconds)
{
    // Out of range or not a number reads as nothing long.
    const int whole = static_cast<int>(json::wholeNumber(seconds, 0, std::numeric_limits<int>::max()));
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

bool isRemoteKind(const Track& track)
{
    return foldCaseAscii(track.sourceKind) == QLatin1StringView("remote");
}

std::optional<account::PortableAccountTrack> portableTrack(const Track& track)
{
    if (!isCompatibleAccountRemoteTrack(track)) {
        return std::nullopt;
    }
    account::PortableAccountTrack value;
    value.id = track.remoteId;
    value.title = track.title;
    value.artist = track.artist;
    value.album = track.album;
    value.genre = track.genre;
    value.artworkUrl = net::durableAccountArtworkUrl(track.artworkUrl);
    value.sourceKind = track.sourceKind;
    if (!track.sourceTag.isEmpty()) {
        value.provider = track.sourceTag;
    } else if (track.origin != QLatin1StringView("account")) {
        value.provider = track.origin;
    }
    value.sourceUrl = track.sourceUrl;
    value.catalogId = track.catalogId;
    value.albumCatalogId = track.albumCatalogId;
    value.artistCatalogId = track.artistCatalogId;
    value.durationSec = track.durationSeconds;
    if (!account::isValidPortableTrack(value)) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<Track> trackFromProviderJson(const QJsonObject& item)
{
    const QString streamUrl = text(item, QLatin1StringView("streamUrl"));
    const QString sourceUrl = text(item, QLatin1StringView("sourceUrl"));
    if ((!streamUrl.isEmpty() && !net::isSafeRemoteUrl(streamUrl, net::RemoteUrlUse::EphemeralMedia))
        || (!sourceUrl.isEmpty() && !net::isSafeRemoteUrl(sourceUrl, net::RemoteUrlUse::Durable))
        || (streamUrl.isEmpty() && sourceUrl.isEmpty())) {
        return std::nullopt;
    }
    const QString provider = text(item, QLatin1StringView("provider"), QStringLiteral("provider"));
    if (provider == ProviderErrorTag) {
        return std::nullopt;
    }

    Track track;
    track.title = text(item, QLatin1StringView("title"), QStringLiteral("Provider track"));
    track.artist = text(item, QLatin1StringView("artist"), QStringLiteral("Provider"));
    track.album = text(item, QLatin1StringView("album"), provider);
    track.genre = QStringLiteral("Remote");
    track.filePath = streamUrl;
    track.sourceUrl = sourceUrl;
    track.remoteId = text(item, QLatin1StringView("id"));
    track.catalogId = text(item, QLatin1StringView("catalogId"));
    track.albumCatalogId = text(item, QLatin1StringView("albumCatalogId"));
    track.artistCatalogId = text(item, QLatin1StringView("artistCatalogId"));
    track.sourceKind = QStringLiteral("remote");
    // Earlier versions kept the provider name in the row's origin, which the
    // song's SourceKey is built from (catalogSourceKey).
    track.origin = provider;
    // The visible label names no source; routing uses origin and sourceUrl.
    track.sourceLabel = QStringLiteral("Music API");
    const QJsonValue durationMs = item.value(QLatin1StringView("durationMs"));
    const double milliseconds = durationMs.isDouble() ? durationMs.toDouble() : 0.0;
    track.durationSeconds = milliseconds > 0.0 ? milliseconds / 1000.0 : 0.0;
    track.durationText = track.durationSeconds > 0.0 ? formatTime(track.durationSeconds) : QString();
    track.artworkUrl = net::normalizeArtworkUrlForDisplay(text(item, QLatin1StringView("artworkUrl")));
    return track;
}

QList<Track> parseProviderTracks(const QByteArray& payload, qsizetype limit)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    const QJsonValue results = document.object().value(QLatin1StringView("results"));
    if (!results.isArray()) {
        return {};
    }
    QList<Track> tracks;
    for (const QJsonValue& value : results.toArray()) {
        if (tracks.size() >= limit) {
            break;
        }
        if (!value.isObject()) {
            continue;
        }
        if (std::optional<Track> track = trackFromProviderJson(value.toObject())) {
            tracks.append(std::move(*track));
        }
    }
    return tracks;
}

QList<Track> parseProviderTrackArray(const QJsonArray& results)
{
    QList<Track> tracks;
    tracks.reserve(results.size());
    for (const QJsonValue& value : results) {
        if (!value.isObject()) {
            continue;
        }
        if (std::optional<Track> track = trackFromProviderJson(value.toObject())) {
            track->index = static_cast<int>(tracks.size() + 1);
            tracks.append(std::move(*track));
        }
    }
    return tracks;
}

bool isCompatibleAccountRemoteTrack(const Track& track, bool requireRemoteId)
{
    if (!isRemoteKind(track) || (requireRemoteId && track.remoteId.isEmpty())) {
        return false;
    }
    return net::isSafeRemoteUrl(track.sourceUrl, net::RemoteUrlUse::Durable);
}

QByteArray accountTrackJson(const Track& track)
{
    const std::optional<account::PortableAccountTrack> value = portableTrack(track);
    return value ? account::serializePortableTrack(*value) : QByteArray();
}

QByteArray accountPlaylistJson(const QString& name)
{
    const QString title = name.trimmed();
    if (title.isEmpty()) {
        return {};
    }
    QJsonObject object;
    object.insert(QLatin1StringView("name"), title);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray accountPlaylistImportJson(const QString& sourceUrl)
{
    const QString canonical = sourceUrl.trimmed();
    if (!net::isHttpUrl(canonical)) {
        return {};
    }
    QJsonObject object;
    object.insert(QLatin1StringView("sourceUrl"), canonical);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray accountPlaylistUpdateJson(const QString& name, const QList<Track>& tracks)
{
    QList<account::PortableAccountTrack> portableTracks;
    portableTracks.reserve(tracks.size());
    for (const Track& track : tracks) {
        if (std::optional<account::PortableAccountTrack> value = portableTrack(track)) {
            portableTracks.append(std::move(*value));
        }
    }
    return account::serializePlaylistReplacement(name.trimmed(), portableTracks);
}

} // namespace lmp::core::remote::track_json
