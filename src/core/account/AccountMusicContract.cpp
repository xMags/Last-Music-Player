#include "core/account/AccountMusicContract.h"

#include <limits>
#include "core/net/Json.h"
#include "core/net/ArtworkUrl.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/text/CaseFold.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace lmp::core::account {

namespace {

using Keys = std::initializer_list<QLatin1StringView>;

// The readers below treat a value of the wrong type as missing, as earlier
// versions did.
QString stringValue(const QJsonObject& object, QLatin1StringView key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

QString firstString(const QJsonObject& object, Keys keys)
{
    for (const QLatin1StringView key : keys) {
        QString value = stringValue(object, key);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

double numberValue(const QJsonObject& object, QLatin1StringView key, double fallback = 0.0)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toDouble() : fallback;
}

bool booleanValue(const QJsonObject& object, QLatin1StringView key, bool fallback = false)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

std::optional<QJsonObject> objectValue(const QJsonObject& object, QLatin1StringView key)
{
    const QJsonValue value = object.value(key);
    if (!value.isObject()) {
        return std::nullopt;
    }
    return value.toObject();
}

std::optional<QJsonArray> arrayValue(const QJsonObject& object, Keys keys)
{
    for (const QLatin1StringView key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isArray()) {
            return value.toArray();
        }
    }
    return std::nullopt;
}

std::optional<QJsonObject> parseObject(const QByteArray& payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    return document.object();
}

// A JSON text that may be a single value ("true", "\"dark\"").
std::optional<QJsonValue> parseValue(const QString& text)
{
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray("[") + text.toUtf8() + QByteArray("]"), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray() || document.array().size() != 1) {
        return std::nullopt;
    }
    return document.array().first();
}

QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject trackObjectFrom(const QJsonObject& wrapper)
{
    return objectValue(wrapper, QLatin1StringView("track")).value_or(wrapper);
}

QJsonObject portableTrackObject(const PortableAccountTrack& track)
{
    QJsonObject object;
    object.insert(QLatin1StringView("id"), track.id);
    object.insert(QLatin1StringView("title"), track.title);
    object.insert(QLatin1StringView("artist"), track.artist);
    object.insert(QLatin1StringView("album"), track.album);
    object.insert(QLatin1StringView("genre"), track.genre);
    object.insert(QLatin1StringView("artworkUrl"), net::durableAccountArtworkUrl(track.artworkUrl));
    object.insert(QLatin1StringView("sourceKind"), track.sourceKind);
    object.insert(QLatin1StringView("provider"), track.provider);
    object.insert(QLatin1StringView("sourceUrl"), track.sourceUrl);
    object.insert(QLatin1StringView("catalogId"), track.catalogId);
    object.insert(QLatin1StringView("albumCatalogId"), track.albumCatalogId);
    object.insert(QLatin1StringView("artistCatalogId"), track.artistCatalogId);
    object.insert(QLatin1StringView("durationSec"), std::max(0.0, track.durationSec));
    return object;
}

std::optional<PortableAccountTrack> parsePortableTrackObject(const QJsonObject& wrapper)
{
    const QJsonObject object = trackObjectFrom(wrapper);
    const bool nested = wrapper.contains(QLatin1StringView("track")) && wrapper.value(QLatin1StringView("track")).isObject();

    PortableAccountTrack track;
    track.id = firstString(object, {QLatin1StringView("id"), QLatin1StringView("remoteId"), QLatin1StringView("trackId")});
    if (track.id.isEmpty()) {
        track.id = firstString(wrapper, {QLatin1StringView("trackId"), QLatin1StringView("remoteId"), QLatin1StringView("id")});
    }
    track.title = firstString(object, {QLatin1StringView("title"), QLatin1StringView("name")});
    track.artist = firstString(object, {QLatin1StringView("artist"), QLatin1StringView("artistName")});
    track.album = firstString(object, {QLatin1StringView("album"), QLatin1StringView("albumName")});
    track.genre = stringValue(object, QLatin1StringView("genre"));
    track.artworkUrl = firstString(object, {QLatin1StringView("artworkUrl"), QLatin1StringView("imageUrl")});
    if (track.artworkUrl.isEmpty() && nested) {
        track.artworkUrl = firstString(wrapper, {QLatin1StringView("artworkUrl"), QLatin1StringView("imageUrl")});
    }
    track.artworkUrl = net::durableAccountArtworkUrl(track.artworkUrl);
    track.sourceKind = stringValue(object, QLatin1StringView("sourceKind"));
    track.provider = stringValue(object, QLatin1StringView("provider"));
    track.sourceUrl = firstString(object, {QLatin1StringView("sourceUrl"), QLatin1StringView("url")});
    track.catalogId = firstString(object, {QLatin1StringView("catalogId"), QLatin1StringView("sourceId")});
    track.albumCatalogId = firstString(object, {QLatin1StringView("albumCatalogId"), QLatin1StringView("albumId")});
    track.artistCatalogId =
        firstString(object, {QLatin1StringView("artistCatalogId"), QLatin1StringView("artistId")});
    track.durationSec = numberValue(object, QLatin1StringView("durationSec"));
    if (track.durationSec <= 0.0) {
        // These two aliases only read cached data and answers from older
        // desktop builds; requests always use durationSec.
        track.durationSec = numberValue(object, QLatin1StringView("durationSeconds"));
    }
    if (track.durationSec <= 0.0) {
        track.durationSec = numberValue(object, QLatin1StringView("durationMs")) / 1000.0;
    }
    track.durationSec = std::max(0.0, track.durationSec);
    if (track.sourceKind.isEmpty()) {
        track.sourceKind = QStringLiteral("remote");
    }
    if (!isValidPortableTrack(track)) {
        return std::nullopt;
    }
    return track;
}

bool isHexAccent(const QString& value)
{
    return value.size() == 7 && value.front() == QLatin1Char('#')
        && std::all_of(value.begin() + 1, value.end(), [](QChar character) {
               const char16_t c = character.unicode();
               return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
           });
}

bool isStorefront(const QString& value)
{
    return value.size() == 2 && std::all_of(value.begin(), value.end(), [](QChar character) {
               return character.unicode() >= u'a' && character.unicode() <= u'z';
           });
}

std::optional<QString> canonicalStringPreference(PortablePreferenceKey key, const QString& value)
{
    switch (key) {
    case PortablePreferenceKey::Theme: {
        const QString lowered = foldCaseAscii(value);
        if (lowered == QLatin1StringView("system") || lowered == QLatin1StringView("light")
            || lowered == QLatin1StringView("dark")) {
            return lowered;
        }
        break;
    }
    case PortablePreferenceKey::Accent:
        if (isHexAccent(value)) {
            return value.toUpper();
        }
        break;
    case PortablePreferenceKey::Storefront: {
        const QString lowered = foldCaseAscii(value);
        if (isStorefront(lowered)) {
            return lowered;
        }
        break;
    }
    case PortablePreferenceKey::GaplessPlayback:
    case PortablePreferenceKey::DataSaver:
        break;
    }
    return std::nullopt;
}

bool likedFlag(const QJsonObject& wrapper, const QJsonObject& object, bool liked)
{
    return liked || booleanValue(wrapper, QLatin1StringView("liked"))
        || booleanValue(object, QLatin1StringView("liked"), booleanValue(object, QLatin1StringView("isLiked")));
}

void appendLibraryTrack(ParsedAccountLibrary& parsed, QHash<QString, qsizetype>& indexes, const QJsonObject& wrapper,
                        bool liked, const QJsonObject& stats)
{
    std::optional<PortableAccountTrack> track = parsePortableTrackObject(wrapper);
    if (!track) {
        return;
    }

    const QJsonObject object = trackObjectFrom(wrapper);
    QString addedAt = stringValue(wrapper, QLatin1StringView("addedAt"));
    if (addedAt.isEmpty()) {
        addedAt = firstString(object, {QLatin1StringView("addedAt"), QLatin1StringView("dateAdded")});
    }
    const QString firstPlayed =
        firstString(stats, {QLatin1StringView("firstPlayedAt"), QLatin1StringView("firstPlayedAtUtc")});
    const Keys playedKeys{QLatin1StringView("lastPlayedAt"), QLatin1StringView("playedAt"),
                          QLatin1StringView("playedAtUtc")};
    QString lastPlayed = firstString(stats, playedKeys);
    if (lastPlayed.isEmpty()) {
        lastPlayed = firstString(wrapper, playedKeys);
    }
    const qint64 playCount = json::wholeNumber(
        numberValue(stats, QLatin1StringView("playCount"), numberValue(wrapper, QLatin1StringView("playCount"))), 0,
        std::numeric_limits<qint64>::max());
    double lastPosition = numberValue(stats, QLatin1StringView("lastPositionSec"),
                                      numberValue(wrapper, QLatin1StringView("lastPositionSec"), -1.0));
    if (lastPosition < 0.0) {
        lastPosition = numberValue(stats, QLatin1StringView("lastPosition"),
                                   numberValue(wrapper, QLatin1StringView("position")));
    }

    const auto found = indexes.constFind(track->id);
    if (found == indexes.constEnd()) {
        AccountLibraryTrack entry;
        entry.track = std::move(*track);
        entry.liked = likedFlag(wrapper, object, liked);
        entry.playCount = playCount;
        entry.addedAt = addedAt;
        entry.legacyAddedSortKey = numberValue(object, QLatin1StringView("dateAddedSortKey"));
        entry.firstPlayedAt = firstPlayed;
        entry.lastPlayedAt = lastPlayed;
        entry.lastPositionSec = std::max(0.0, lastPosition);
        indexes.insert(entry.track.id, parsed.tracks.size());
        parsed.tracks.append(std::move(entry));
        return;
    }

    AccountLibraryTrack& existing = parsed.tracks[found.value()];
    existing.liked = likedFlag(wrapper, object, existing.liked || liked);
    existing.playCount = std::max(existing.playCount, playCount);
    if (existing.track.artworkUrl.isEmpty() && !track->artworkUrl.isEmpty()) {
        existing.track.artworkUrl = track->artworkUrl;
    }
    if (existing.addedAt.isEmpty() && !addedAt.isEmpty()) {
        existing.addedAt = addedAt;
    }
    // Compared as text, UTF-16 unit by unit, as before.
    if (!lastPlayed.isEmpty() && lastPlayed > existing.lastPlayedAt) {
        existing.lastPlayedAt = lastPlayed;
        existing.lastPositionSec = std::max(0.0, lastPosition);
    }
}

void appendObjects(ParsedAccountLibrary& parsed, QHash<QString, qsizetype>& indexes, const QJsonArray& values,
                   bool liked, bool statsObject)
{
    for (const QJsonValue& value : values) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QJsonObject stats =
            statsObject ? objectValue(object, QLatin1StringView("stats")).value_or(object) : object;
        appendLibraryTrack(parsed, indexes, object, liked, stats);
    }
}

} // namespace

bool isValidPortableTrack(const PortableAccountTrack& track)
{
    if (track.id.isEmpty() || track.title.isEmpty() || track.sourceUrl.isEmpty()) {
        return false;
    }
    // A local row names a path on the machine that scanned it, so it never
    // travels; anything else must be an address that may be kept.
    if (foldCaseAscii(track.sourceKind) == QLatin1StringView("local")) {
        return false;
    }
    return net::isSafeRemoteUrl(track.sourceUrl, net::RemoteUrlUse::Durable);
}

QByteArray serializePortableTrack(const PortableAccountTrack& track)
{
    return isValidPortableTrack(track) ? compact(portableTrackObject(track)) : QByteArray();
}

std::optional<PortableAccountTrack> parsePortableTrack(const QByteArray& payload)
{
    const std::optional<QJsonObject> root = parseObject(payload);
    if (!root) {
        return std::nullopt;
    }
    return parsePortableTrackObject(*root);
}

QByteArray serializeHistoryBatch(const QList<AccountHistoryEvent>& events)
{
    QJsonArray values;
    for (const AccountHistoryEvent& event : events) {
        if (event.eventId.isEmpty() || event.playedAt.isEmpty() || !isValidPortableTrack(event.track)) {
            continue;
        }
        QJsonObject value;
        value.insert(QLatin1StringView("eventId"), event.eventId);
        value.insert(QLatin1StringView("track"), portableTrackObject(event.track));
        value.insert(QLatin1StringView("playedAt"), event.playedAt);
        value.insert(QLatin1StringView("positionSec"), std::floor(std::max(0.0, event.positionSec)));
        values.append(value);
    }
    QJsonObject root;
    root.insert(QLatin1StringView("events"), values);
    return compact(root);
}

std::optional<QByteArray> wrapTrackRequest(const QByteArray& portableTrackJson)
{
    const std::optional<QJsonObject> track = parseObject(portableTrackJson);
    if (!track || !parsePortableTrackObject(*track)) {
        return std::nullopt;
    }
    QJsonObject root;
    root.insert(QLatin1StringView("track"), *track);
    return compact(root);
}

QByteArray serializePlaylistReplacement(const QString& name, const QList<PortableAccountTrack>& tracks)
{
    QJsonObject root;
    if (!name.isEmpty()) {
        root.insert(QLatin1StringView("name"), name);
    }
    QJsonArray values;
    for (const PortableAccountTrack& track : tracks) {
        if (isValidPortableTrack(track)) {
            values.append(portableTrackObject(track));
        }
    }
    root.insert(QLatin1StringView("tracks"), values);
    return compact(root);
}

ParsedAccountLibrary parseAccountLibrary(const QByteArray& payload)
{
    std::optional<QJsonObject> parsedRoot = parseObject(payload);
    if (!parsedRoot) {
        return {};
    }
    QJsonObject root = *parsedRoot;
    if (std::optional<QJsonObject> library = objectValue(root, QLatin1StringView("library"))) {
        root = *library;
    }
    if (std::optional<QJsonObject> data = objectValue(root, QLatin1StringView("data"))) {
        root = *data;
    }
    static const Keys listKeys{QLatin1StringView("tracks"),  QLatin1StringView("songs"),
                               QLatin1StringView("liked"),   QLatin1StringView("likedTracks"),
                               QLatin1StringView("history"), QLatin1StringView("recentlyPlayed"),
                               QLatin1StringView("playlists")};
    if (std::none_of(listKeys.begin(), listKeys.end(), [&root](QLatin1StringView key) { return root.contains(key); })) {
        return {};
    }

    ParsedAccountLibrary parsed;
    parsed.valid = true;
    parsed.homeLayout = root.value(QLatin1StringView("homeLayout"));
    QHash<QString, qsizetype> indexes;

    if (std::optional<QJsonArray> tracks = arrayValue(root, {QLatin1StringView("tracks"), QLatin1StringView("songs")})) {
        appendObjects(parsed, indexes, *tracks, false, false);
    }
    if (std::optional<QJsonArray> liked =
            arrayValue(root, {QLatin1StringView("liked"), QLatin1StringView("likedTracks"),
                              QLatin1StringView("favourites"), QLatin1StringView("favorites")})) {
        appendObjects(parsed, indexes, *liked, true, false);
    }
    if (std::optional<QJsonArray> history = arrayValue(
            root, {QLatin1StringView("history"), QLatin1StringView("recentlyPlayed"), QLatin1StringView("plays")})) {
        appendObjects(parsed, indexes, *history, false, true);
    }

    const std::optional<QJsonArray> playlists = arrayValue(root, {QLatin1StringView("playlists")});
    if (!playlists) {
        return parsed;
    }
    QSet<QString> playlistIds;
    for (const QJsonValue& value : *playlists) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        AccountPlaylist playlist;
        playlist.id = firstString(object, {QLatin1StringView("id"), QLatin1StringView("playlistId")});
        if (playlist.id.isEmpty() || playlistIds.contains(playlist.id)) {
            continue;
        }
        playlistIds.insert(playlist.id);
        playlist.name = firstString(object, {QLatin1StringView("name"), QLatin1StringView("title")});
        playlist.description = stringValue(object, QLatin1StringView("description"));
        playlist.sourceUrl = stringValue(object, QLatin1StringView("sourceUrl"));
        playlist.updatedAt = firstString(object, {QLatin1StringView("updatedAt"), QLatin1StringView("createdAt")});
        QSet<QString> trackIds;
        const std::optional<QJsonArray> items = arrayValue(
            object, {QLatin1StringView("tracks"), QLatin1StringView("items"), QLatin1StringView("songs")});
        for (const QJsonValue& item : items.value_or(QJsonArray())) {
            QString trackId;
            if (item.isString()) {
                trackId = item.toString();
            } else if (item.isObject()) {
                const QJsonObject trackObject = item.toObject();
                if (std::optional<PortableAccountTrack> parsedTrack = parsePortableTrackObject(trackObject)) {
                    trackId = parsedTrack->id;
                    appendLibraryTrack(parsed, indexes, trackObject, false, trackObject);
                }
            }
            if (!trackId.isEmpty() && indexes.contains(trackId) && !trackIds.contains(trackId)) {
                trackIds.insert(trackId);
                playlist.trackIds.append(trackId);
            }
        }
        parsed.playlists.append(std::move(playlist));
    }
    return parsed;
}

std::optional<AccountPlaylist> parsePlaylistAnswer(const QByteArray& payload)
{
    const std::optional<QJsonObject> root = parseObject(payload);
    if (!root) {
        return std::nullopt;
    }
    const QJsonObject playlist = objectValue(*root, QLatin1StringView("playlist")).value_or(*root);
    QJsonObject library;
    library.insert(QLatin1StringView("playlists"), QJsonArray{playlist});
    ParsedAccountLibrary parsed = parseAccountLibrary(compact(library));
    if (parsed.playlists.isEmpty()) {
        return std::nullopt;
    }
    return parsed.playlists.takeFirst();
}

QStringList parseAcknowledgedHistoryEvents(const QByteArray& payload, const QStringList& submittedEventIds)
{
    const std::optional<QJsonObject> root = parseObject(payload);
    if (!root) {
        return {};
    }
    const QSet<QString> submitted(submittedEventIds.begin(), submittedEventIds.end());
    QSet<QString> seen;
    QStringList result;
    for (const QLatin1StringView key : {QLatin1StringView("accepted"), QLatin1StringView("duplicates")}) {
        const std::optional<QJsonArray> values = arrayValue(*root, {key});
        for (const QJsonValue& value : values.value_or(QJsonArray())) {
            if (!value.isString()) {
                continue;
            }
            const QString id = value.toString();
            if (submitted.contains(id) && !seen.contains(id)) {
                seen.insert(id);
                result.append(id);
            }
        }
    }
    return result;
}

QString portablePreferenceName(PortablePreferenceKey key)
{
    switch (key) {
    case PortablePreferenceKey::Theme:
        return QStringLiteral("theme");
    case PortablePreferenceKey::Accent:
        return QStringLiteral("accent");
    case PortablePreferenceKey::GaplessPlayback:
        return QStringLiteral("gaplessPlayback");
    case PortablePreferenceKey::DataSaver:
        return QStringLiteral("dataSaver");
    case PortablePreferenceKey::Storefront:
        return QStringLiteral("storefront");
    }
    return {};
}

std::optional<PortablePreferenceKey> parsePortablePreferenceName(const QString& name)
{
    for (const PortablePreferenceKey key :
         {PortablePreferenceKey::Theme, PortablePreferenceKey::Accent, PortablePreferenceKey::GaplessPlayback,
          PortablePreferenceKey::DataSaver, PortablePreferenceKey::Storefront}) {
        if (name == portablePreferenceName(key)) {
            return key;
        }
    }
    return std::nullopt;
}

PortablePreferences parsePortablePreferences(const QByteArray& payload)
{
    std::optional<QJsonObject> parsedRoot = parseObject(payload);
    if (!parsedRoot) {
        return {};
    }
    QJsonObject root = *parsedRoot;
    for (int depth = 0; depth < 2; ++depth) {
        if (std::optional<QJsonObject> data = objectValue(root, QLatin1StringView("data"))) {
            root = *data;
            continue;
        }
        if (std::optional<QJsonObject> preferences = objectValue(root, QLatin1StringView("preferences"))) {
            root = *preferences;
            continue;
        }
        break;
    }

    const auto readString = [&root](PortablePreferenceKey key) -> std::optional<QString> {
        const QJsonValue value = root.value(portablePreferenceName(key));
        if (!value.isString()) {
            return std::nullopt;
        }
        return canonicalStringPreference(key, value.toString());
    };
    const auto readBoolean = [&root](PortablePreferenceKey key) -> std::optional<bool> {
        const QJsonValue value = root.value(portablePreferenceName(key));
        return value.isBool() ? std::optional<bool>(value.toBool()) : std::nullopt;
    };

    PortablePreferences result;
    result.theme = readString(PortablePreferenceKey::Theme);
    result.accent = readString(PortablePreferenceKey::Accent);
    result.gaplessPlayback = readBoolean(PortablePreferenceKey::GaplessPlayback);
    result.dataSaver = readBoolean(PortablePreferenceKey::DataSaver);
    result.storefront = readString(PortablePreferenceKey::Storefront);
    return result;
}

QByteArray serializePortablePreferences(const PortablePreferences& preferences)
{
    QJsonObject root;
    const auto insertString = [&root](PortablePreferenceKey key, const std::optional<QString>& value) {
        if (value) {
            root.insert(portablePreferenceName(key), *value);
        }
    };
    const auto insertBoolean = [&root](PortablePreferenceKey key, std::optional<bool> value) {
        if (value) {
            root.insert(portablePreferenceName(key), *value);
        }
    };
    insertString(PortablePreferenceKey::Theme, preferences.theme);
    insertString(PortablePreferenceKey::Accent, preferences.accent);
    insertBoolean(PortablePreferenceKey::GaplessPlayback, preferences.gaplessPlayback);
    insertBoolean(PortablePreferenceKey::DataSaver, preferences.dataSaver);
    insertString(PortablePreferenceKey::Storefront, preferences.storefront);
    return compact(root);
}

PortablePreferenceValues portablePreferenceValues(const PortablePreferences& preferences)
{
    const auto quoted = [](const QString& value) { return QLatin1Char('"') + value + QLatin1Char('"'); };
    const auto boolean = [](bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); };
    PortablePreferenceValues values;
    if (preferences.theme) {
        values.insert(PortablePreferenceKey::Theme, quoted(*preferences.theme));
    }
    if (preferences.accent) {
        values.insert(PortablePreferenceKey::Accent, quoted(*preferences.accent));
    }
    if (preferences.gaplessPlayback) {
        values.insert(PortablePreferenceKey::GaplessPlayback, boolean(*preferences.gaplessPlayback));
    }
    if (preferences.dataSaver) {
        values.insert(PortablePreferenceKey::DataSaver, boolean(*preferences.dataSaver));
    }
    if (preferences.storefront) {
        values.insert(PortablePreferenceKey::Storefront, quoted(*preferences.storefront));
    }
    return values;
}

PortablePreferences portablePreferencesFromValues(const PortablePreferenceValues& values)
{
    PortablePreferences preferences;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const std::optional<QString> validated = validatePortablePreferenceValue(it.key(), it.value());
        if (!validated) {
            continue;
        }
        const std::optional<QJsonValue> json = parseValue(*validated);
        if (!json) {
            continue;
        }
        switch (it.key()) {
        case PortablePreferenceKey::Theme:
            preferences.theme = json->toString();
            break;
        case PortablePreferenceKey::Accent:
            preferences.accent = json->toString();
            break;
        case PortablePreferenceKey::GaplessPlayback:
            preferences.gaplessPlayback = json->toBool();
            break;
        case PortablePreferenceKey::DataSaver:
            preferences.dataSaver = json->toBool();
            break;
        case PortablePreferenceKey::Storefront:
            preferences.storefront = json->toString();
            break;
        }
    }
    return preferences;
}

std::optional<QString> validatePortablePreferenceValue(PortablePreferenceKey key, const QString& jsonValue)
{
    const std::optional<QJsonValue> value = parseValue(jsonValue);
    if (!value) {
        return std::nullopt;
    }
    if (key == PortablePreferenceKey::GaplessPlayback || key == PortablePreferenceKey::DataSaver) {
        if (!value->isBool()) {
            return std::nullopt;
        }
        return value->toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (!value->isString()) {
        return std::nullopt;
    }
    const std::optional<QString> canonical = canonicalStringPreference(key, value->toString());
    if (!canonical) {
        return std::nullopt;
    }
    return QLatin1Char('"') + *canonical + QLatin1Char('"');
}

} // namespace lmp::core::account
