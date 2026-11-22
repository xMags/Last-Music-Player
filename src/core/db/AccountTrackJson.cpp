#include "core/db/AccountTrackJson.h"

#include "core/db/AccountRows.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>

namespace lmp::core::db::account_track_json {

namespace {

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

QString serialize(const Track& track)
{
    const QString provider =
        track.sourceTag.isEmpty() ? (track.origin == QLatin1StringView("account") ? QString() : track.origin)
                                  : track.sourceTag;

    QJsonObject object;
    object.insert(QLatin1StringView("remoteId"), track.remoteId);
    object.insert(QLatin1StringView("id"), track.remoteId);
    object.insert(QLatin1StringView("sourceKind"), track.sourceKind);
    object.insert(QLatin1StringView("provider"), provider);
    object.insert(QLatin1StringView("sourceUrl"), account_rows::sanitizeAccountUrl(track.sourceUrl));
    object.insert(QLatin1StringView("title"), track.title);
    object.insert(QLatin1StringView("artist"), track.artist);
    object.insert(QLatin1StringView("album"), track.album);
    object.insert(QLatin1StringView("genre"), track.genre);
    object.insert(QLatin1StringView("artworkUrl"), account_rows::sanitizeAccountArtworkUrl(track.artworkUrl));
    object.insert(QLatin1StringView("catalogId"), track.catalogId);
    object.insert(QLatin1StringView("albumCatalogId"), track.albumCatalogId);
    object.insert(QLatin1StringView("artistCatalogId"), track.artistCatalogId);
    object.insert(QLatin1StringView("dateAdded"), track.dateAdded);
    object.insert(QLatin1StringView("duration"), track.durationText);
    object.insert(QLatin1StringView("durationSeconds"), track.durationSeconds);
    object.insert(QLatin1StringView("durationSec"), track.durationSeconds);
    object.insert(QLatin1StringView("dateAddedSortKey"), track.dateAddedSortKey);
    object.insert(QLatin1StringView("isLiked"), track.liked);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

std::optional<Track> deserialize(const QString& payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = document.object();

    Track track;
    track.remoteId = stringOr(object, QLatin1StringView("id"), stringOr(object, QLatin1StringView("remoteId")));
    track.sourceUrl = stringOr(object, QLatin1StringView("sourceUrl"));
    track.title = stringOr(object, QLatin1StringView("title"));
    track.artist = stringOr(object, QLatin1StringView("artist"));
    track.album = stringOr(object, QLatin1StringView("album"));
    track.genre = stringOr(object, QLatin1StringView("genre"));
    track.artworkUrl = stringOr(object, QLatin1StringView("artworkUrl"));
    track.dateAdded = stringOr(object, QLatin1StringView("dateAdded"));
    track.durationText = stringOr(object, QLatin1StringView("duration"));
    track.durationSeconds = numberOr(object,
                                     QLatin1StringView("durationSec"),
                                     numberOr(object, QLatin1StringView("durationSeconds"), 0.0));
    track.dateAddedSortKey = numberOr(object, QLatin1StringView("dateAddedSortKey"), 0.0);
    track.liked = boolOr(object, QLatin1StringView("isLiked"), false);
    track.sourceKind = stringOr(object, QLatin1StringView("sourceKind"), QStringLiteral("remote"));
    track.origin = QStringLiteral("account");
    track.sourceTag = stringOr(object, QLatin1StringView("provider"));
    track.catalogId = stringOr(object, QLatin1StringView("catalogId"));
    track.albumCatalogId = stringOr(object, QLatin1StringView("albumCatalogId"));
    track.artistCatalogId = stringOr(object, QLatin1StringView("artistCatalogId"));
    track.sourceLabel = QStringLiteral("Account");
    track.filePath.clear();
    return track;
}

} // namespace lmp::core::db::account_track_json
