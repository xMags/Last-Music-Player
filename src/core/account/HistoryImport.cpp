#include "core/account/HistoryImport.h"

#include "core/remote/TrackJson.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace lmp::core::account {

namespace {

[[nodiscard]] QString text(const QJsonObject& object, const char* key, const QString& fallback)
{
    const QJsonValue value = object.value(QLatin1StringView(key));
    return value.isString() ? value.toString() : fallback;
}

[[nodiscard]] double number(const QJsonObject& object, const char* key, double fallback)
{
    const QJsonValue value = object.value(QLatin1StringView(key));
    return value.isDouble() ? value.toDouble() : fallback;
}

[[nodiscard]] QString formatTime(double seconds)
{
    const int whole = static_cast<int>(seconds);
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

QString historyImportEventId(const QString& accountId, const QString& sourceKey)
{
    if (accountId.isEmpty() || sourceKey.isEmpty()) {
        return {};
    }
    QByteArray digest =
        QCryptographicHash::hash((accountId + QLatin1Char('\n') + sourceKey).toUtf8(), QCryptographicHash::Sha256);
    digest[6] = static_cast<char>((static_cast<quint8>(digest.at(6)) & 0x0FU) | 0x50U);
    digest[8] = static_cast<char>((static_cast<quint8>(digest.at(8)) & 0x3FU) | 0x80U);
    const QByteArray hex = digest.left(16).toHex();
    return QString::fromLatin1(hex.mid(0, 8) + '-' + hex.mid(8, 4) + '-' + hex.mid(12, 4) + '-' + hex.mid(16, 4) + '-'
                               + hex.mid(20, 12));
}

QString normalizeLegacyPlayedAt(const QString& value)
{
    if (value.size() == 19 && value.at(4) == QLatin1Char('-') && value.at(7) == QLatin1Char('-')
        && value.at(10) == QLatin1Char(' ') && value.at(13) == QLatin1Char(':') && value.at(16) == QLatin1Char(':')) {
        QString converted = value;
        converted[10] = QLatin1Char('T');
        converted.append(QLatin1Char('Z'));
        return converted;
    }
    if (value.size() == 20 && value.at(10) == QLatin1Char('T') && value.endsWith(QLatin1Char('Z'))) {
        return value;
    }
    return {};
}

std::optional<Track> resolvedImportTrack(const QByteArray& payload, const Track& legacy)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonValue resultValue = document.object().value(QStringLiteral("result"));
    if (!resultValue.isObject()) {
        return std::nullopt;
    }
    const QJsonObject result = resultValue.toObject();
    const QJsonValue nestedValue = result.value(QStringLiteral("track"));
    const bool nested = nestedValue.isObject();
    const QJsonObject item = nested ? nestedValue.toObject() : result;

    const QString remoteId = text(item, "id", text(item, "remoteId", text(item, "trackId", {})));
    const QString sourceUrl = text(item, "sourceUrl", text(item, "url", {}));
    if (remoteId.isEmpty() || sourceUrl.isEmpty()) {
        return std::nullopt;
    }
    Track track;
    track.remoteId = remoteId;
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("account");
    track.sourceTag = text(item, "provider", legacy.sourceTag);
    track.sourceLabel = QStringLiteral("Account");
    track.sourceUrl = sourceUrl;
    track.title = text(item, "title", text(item, "name", legacy.title));
    track.artist = text(item, "artist", legacy.artist);
    track.album = text(item, "album", legacy.album);
    track.genre = text(item, "genre", legacy.genre);
    track.catalogId = text(item, "catalogId", legacy.catalogId);
    track.albumCatalogId = text(item, "albumCatalogId", legacy.albumCatalogId);
    track.artistCatalogId = text(item, "artistCatalogId", legacy.artistCatalogId);
    QString artwork = text(item, "artworkUrl", text(item, "imageUrl", {}));
    if (artwork.isEmpty() && nested) {
        artwork = text(result, "artworkUrl", text(result, "imageUrl", legacy.artworkUrl));
    }
    track.artworkUrl = artwork.isEmpty() ? legacy.artworkUrl : artwork;
    const double seconds = number(item, "durationSec", number(item, "durationSeconds", legacy.durationSeconds));
    const double milliseconds = number(item, "durationMs", 0.0);
    track.durationSeconds = milliseconds > 0.0 ? milliseconds / 1000.0 : seconds;
    track.durationText = track.durationSeconds > 0.0 ? formatTime(track.durationSeconds) : legacy.durationText;
    if (!remote::track_json::isCompatibleAccountRemoteTrack(track)) {
        return std::nullopt;
    }
    return track;
}

} // namespace lmp::core::account
