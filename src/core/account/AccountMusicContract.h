#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <optional>

// The account service's song, library, history and preference payloads, as
// earlier versions read and wrote them. Reading is lenient where they were
// (alias keys, wrong types read as missing); writing always uses the current
// keys. Pure functions, no I/O.
namespace lmp::core::account {

// A song as the account service stores it.
struct PortableAccountTrack {
    QString id;
    QString title;
    QString artist;
    QString album;
    QString genre;
    QString artworkUrl;
    QString sourceKind;
    // The source tag, sent as "provider".
    QString provider;
    QString sourceUrl;
    QString catalogId;
    QString albumCatalogId;
    QString artistCatalogId;
    double durationSec = 0;
};

struct AccountHistoryEvent {
    QString eventId;
    PortableAccountTrack track;
    // ISO 8601 text.
    QString playedAt;
    double positionSec = 0;
};

struct AccountLibraryTrack {
    PortableAccountTrack track;
    bool liked = false;
    qint64 playCount = 0;
    QString addedAt;
    double legacyAddedSortKey = 0;
    QString firstPlayedAt;
    QString lastPlayedAt;
    double lastPositionSec = 0;
};

struct AccountPlaylist {
    QString id;
    QString name;
    QString description;
    QString sourceUrl;
    QString updatedAt;
    // Songs of the parsed library, in playlist order, each once.
    QStringList trackIds;
};

struct ParsedAccountLibrary {
    // The payload was a library (even an empty one).
    bool valid = false;
    QList<AccountLibraryTrack> tracks;
    QList<AccountPlaylist> playlists;
    // Home's layout as the service sent it ("homeLayout"), unread; Undefined
    // when absent. core::home::listedHomeLayout reads it.
    QJsonValue homeLayout = QJsonValue(QJsonValue::Undefined);
};

enum class PortablePreferenceKey {
    Theme,
    Accent,
    GaplessPlayback,
    DataSaver,
    // The catalogue region.
    Storefront,
};

// Preferences that travel with the account. Values are canonical: theme
// "system", "light" or "dark"; accent "#RRGGBB" in upper case; region two
// lower-case letters.
struct PortablePreferences {
    std::optional<QString> theme;
    std::optional<QString> accent;
    std::optional<bool> gaplessPlayback;
    std::optional<bool> dataSaver;
    std::optional<QString> storefront;

    friend bool operator==(const PortablePreferences&, const PortablePreferences&) = default;
};

// Preference values as JSON text: "\"dark\"", "true".
using PortablePreferenceValues = QHash<PortablePreferenceKey, QString>;

// An id, a title and a source address, which is a remote one safe to keep
// (core::net::RemoteUrlUse::Durable; kind "local" never is).
[[nodiscard]] bool isValidPortableTrack(const PortableAccountTrack& track);

// The track object (keys id, title, artist, album, genre, artworkUrl,
// sourceKind, provider, sourceUrl, catalogId, albumCatalogId,
// artistCatalogId, durationSec; the artwork address made durable, the
// length never below 0), or empty when the song is not valid.
[[nodiscard]] QByteArray serializePortableTrack(const PortableAccountTrack& track);

// A track object, or one wrapped as {"track": {...}}. Ids fall back to
// "remoteId" and "trackId", the title to "name", the artist and album to
// "artistName" and "albumName", the artwork to "imageUrl", the source address
// to "url", catalogue ids to "sourceId", "albumId" and "artistId"; the
// length to "durationSeconds" then "durationMs". Kind defaults to "remote".
// Nothing when the text is not an object or the song is not valid.
[[nodiscard]] std::optional<PortableAccountTrack> parsePortableTrack(const QByteArray& payload);

// {"events": [...]} with each event that has an id, a time and a valid song;
// positions are whole seconds, never below 0.
[[nodiscard]] QByteArray serializeHistoryBatch(const QList<AccountHistoryEvent>& events);

// {"track": <object>} for a like or unlike call, or nothing when the payload
// is not a valid track object.
[[nodiscard]] std::optional<QByteArray> wrapTrackRequest(const QByteArray& portableTrackJson);

// {"name": ..., "tracks": [...]} with the valid songs; "name" is left out
// when empty.
[[nodiscard]] QByteArray serializePlaylistReplacement(const QString& name, const QList<PortableAccountTrack>& tracks);

// The library answer, also under "library" and then "data". Songs come from
// "tracks" (or "songs"), then the liked list ("liked", "likedTracks",
// "favourites", "favorites"), then history ("history", "recentlyPlayed",
// "plays", with play statistics under "stats" when present), then songs
// inside playlists. A song seen twice is merged: liked if any copy is, the
// higher play count, the first artwork and date added, and the later play
// time with its position. Not valid when none of those lists is present.
[[nodiscard]] ParsedAccountLibrary parseAccountLibrary(const QByteArray& payload);

// The playlist in a playlist call's answer ({"playlist": {...}}, or the
// playlist object itself), read as a library playlist is. Nothing when it
// has no id.
[[nodiscard]] std::optional<AccountPlaylist> parsePlaylistAnswer(const QByteArray& payload);

// The ids from "accepted" then "duplicates" that were submitted, each once.
[[nodiscard]] QStringList parseAcknowledgedHistoryEvents(const QByteArray& payload,
                                                         const QStringList& submittedEventIds);

// "theme", "accent", "gaplessPlayback", "dataSaver", "storefront".
[[nodiscard]] QString portablePreferenceName(PortablePreferenceKey key);
[[nodiscard]] std::optional<PortablePreferenceKey> parsePortablePreferenceName(const QString& name);

// The preferences object, also under "data" and "preferences" (two levels at
// most). Values that are invalid or of the wrong type are left out.
[[nodiscard]] PortablePreferences parsePortablePreferences(const QByteArray& payload);
[[nodiscard]] QByteArray serializePortablePreferences(const PortablePreferences& preferences);

[[nodiscard]] PortablePreferenceValues portablePreferenceValues(const PortablePreferences& preferences);
// Invalid values are skipped.
[[nodiscard]] PortablePreferences portablePreferencesFromValues(const PortablePreferenceValues& values);

// The canonical JSON text of a value for `key`, or nothing when it is not
// valid for it.
[[nodiscard]] std::optional<QString> validatePortablePreferenceValue(PortablePreferenceKey key,
                                                                     const QString& jsonValue);

} // namespace lmp::core::account
