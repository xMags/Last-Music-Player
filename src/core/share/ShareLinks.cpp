#include "core/share/ShareLinks.h"

#include "core/catalog/CatalogParser.h"
#include "core/remote/TrackJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>

namespace lmp::core::share {

namespace {

[[nodiscard]] std::optional<QJsonObject> trackObject(const Track& track)
{
    const QByteArray json = remote::track_json::accountTrackJson(track);
    if (json.isEmpty()) {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(json);
    return document.isObject() ? std::optional<QJsonObject>(document.object()) : std::nullopt;
}

[[nodiscard]] QString text(const QJsonObject& object, QLatin1StringView name)
{
    const QJsonValue value = object.value(name);
    return value.isString() ? value.toString() : QString();
}

void insertIfSet(QJsonObject& object, const QString& name, const QString& value)
{
    if (!value.trimmed().isEmpty()) {
        object.insert(name, value.trimmed());
    }
}

} // namespace

std::optional<QByteArray> shareRequestBody(const ShareRequest& request)
{
    QJsonObject body;
    switch (request.kind) {
    case ShareKind::Song: {
        const std::optional<QJsonObject> track = trackObject(request.track);
        if (!track) {
            return std::nullopt;
        }
        body.insert(QStringLiteral("kind"), QStringLiteral("song"));
        body.insert(QStringLiteral("track"), *track);
        break;
    }
    case ShareKind::List: {
        QJsonArray tracks;
        for (const Track& track : request.tracks) {
            if (const std::optional<QJsonObject> object = trackObject(track)) {
                tracks.append(*object);
            }
        }
        if (tracks.isEmpty() || request.title.trimmed().isEmpty()) {
            return std::nullopt;
        }
        body.insert(QStringLiteral("kind"), QStringLiteral("list"));
        body.insert(QStringLiteral("title"), request.title.trimmed());
        insertIfSet(body, QStringLiteral("subtitle"), request.subtitle);
        insertIfSet(body, QStringLiteral("artworkUrl"), request.artworkUrl);
        body.insert(QStringLiteral("tracks"), tracks);
        break;
    }
    case ShareKind::Artist: {
        const account::LibraryArtistIdentity& artist = request.artist;
        if (artist.name.trimmed().isEmpty() || artist.provider.trimmed().isEmpty()
            || (artist.catalogId.trimmed().isEmpty() && artist.sourceUrl.trimmed().isEmpty())) {
            return std::nullopt;
        }
        body.insert(QStringLiteral("kind"), QStringLiteral("artist"));
        body.insert(QStringLiteral("name"), artist.name.trimmed());
        body.insert(QStringLiteral("provider"), artist.provider.trimmed());
        insertIfSet(body, QStringLiteral("catalogId"), artist.catalogId);
        insertIfSet(body, QStringLiteral("sourceUrl"), artist.sourceUrl);
        insertIfSet(body, QStringLiteral("artworkUrl"), request.artworkUrl);
        break;
    }
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

std::optional<ShareLink> parseShareLink(const QByteArray& payload)
{
    const QJsonObject body = QJsonDocument::fromJson(payload).object();
    const QString url = text(body, QLatin1StringView("url"));
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme() != QLatin1StringView("https") || parsed.host().isEmpty()) {
        return std::nullopt;
    }
    return ShareLink{.code = text(body, QLatin1StringView("code")),
                     .url = url,
                     .title = text(body, QLatin1StringView("title"))};
}

std::optional<SharedItem> parseSharedItem(const QByteArray& payload, const QString& code)
{
    const QJsonObject body = QJsonDocument::fromJson(payload).object();
    const QString kind = text(body, QLatin1StringView("kind"));
    SharedItem item;
    if (kind == QLatin1StringView("song")) {
        item.kind = ShareKind::Song;
    } else if (kind == QLatin1StringView("list")) {
        item.kind = ShareKind::List;
    } else if (kind == QLatin1StringView("artist")) {
        item.kind = ShareKind::Artist;
    } else {
        return std::nullopt;
    }
    const QString answeredCode = text(body, QLatin1StringView("code"));
    item.code = answeredCode.isEmpty() ? code : answeredCode;
    item.title = text(body, QLatin1StringView("title"));
    item.subtitle = text(body, QLatin1StringView("subtitle"));
    item.artworkUrl = text(body, QLatin1StringView("artworkUrl"));
    const QJsonValue tracks = body.value(QLatin1StringView("tracks"));
    item.tracks = tracks.isArray() ? catalog::parseProviderTracks(tracks)
        : body.value(QLatin1StringView("track")).isObject()
        ? catalog::parseProviderTracks(QJsonArray{body.value(QLatin1StringView("track"))})
        : QList<Track>();
    const QJsonObject artist = body.value(QLatin1StringView("artist")).toObject();
    if (!artist.isEmpty()) {
        item.artist = account::LibraryArtistIdentity{.catalogId = text(artist, QLatin1StringView("catalogId")),
                                                     .sourceUrl = text(artist, QLatin1StringView("sourceUrl")),
                                                     .name = item.title,
                                                     .provider = text(artist, QLatin1StringView("provider"))};
    }
    return item;
}

bool isShareCode(const QString& code)
{
    return code.size() == 10 && std::all_of(code.cbegin(), code.cend(), [](QChar character) {
               return character.unicode() < 128 && character.isLetterOrNumber();
           });
}

std::optional<QString> shareCodeFromLink(const QString& text, const QString& frontendOrigin)
{
    const QUrl link(text.trimmed(), QUrl::StrictMode);
    const QUrl origin(frontendOrigin.trimmed());
    if (!link.isValid() || !origin.isValid() || origin.host().isEmpty() || link.scheme() != origin.scheme()
        || link.host().compare(origin.host(), Qt::CaseInsensitive) != 0 || link.port() != origin.port()) {
        return std::nullopt;
    }
    QString path = link.path();
    if (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    if (!path.startsWith(QLatin1StringView("/s/"))) {
        return std::nullopt;
    }
    const QString code = path.mid(3);
    return isShareCode(code) ? std::optional<QString>(code) : std::nullopt;
}

QString shareText(const ShareRequest& request)
{
    switch (request.kind) {
    case ShareKind::Song:
        return request.track.artist.trimmed().isEmpty()
            ? request.track.title
            : QStringLiteral("%1 by %2").arg(request.track.title, request.track.artist);
    case ShareKind::List:
        return request.subtitle.trimmed().isEmpty() ? request.title
                                                    : request.title + QStringLiteral(" · ") + request.subtitle;
    case ShareKind::Artist:
        return request.artist.name;
    }
    return {};
}

} // namespace lmp::core::share
