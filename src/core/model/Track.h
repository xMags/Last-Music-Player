#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace lmp::core {

// One song, as every mode and every part of the app sees it. A plain value:
// it holds data only, nothing about how it is drawn. The fields cover the
// earlier track object and the reference client's track, so one type serves
// local files, the account library and the API key server.
struct Track {
    Q_GADGET
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString artist MEMBER artist)
    Q_PROPERTY(QString album MEMBER album)
    Q_PROPERTY(QString genre MEMBER genre)
    Q_PROPERTY(int year MEMBER year)
    Q_PROPERTY(double durationSeconds MEMBER durationSeconds)
    Q_PROPERTY(QString durationText MEMBER durationText)
    Q_PROPERTY(QString filePath MEMBER filePath)
    Q_PROPERTY(QString artworkUrl MEMBER artworkUrl)
    Q_PROPERTY(QString sourceKind MEMBER sourceKind)
    Q_PROPERTY(QString origin MEMBER origin)
    Q_PROPERTY(QString sourceTag MEMBER sourceTag)
    Q_PROPERTY(QString sourceUrl MEMBER sourceUrl)
    Q_PROPERTY(QString sourceReference MEMBER sourceReference)
    Q_PROPERTY(QString streamUrl MEMBER streamUrl)
    Q_PROPERTY(QString sourceLabel MEMBER sourceLabel)
    Q_PROPERTY(qint64 rowId MEMBER rowId)
    Q_PROPERTY(QString remoteId MEMBER remoteId)
    Q_PROPERTY(QString catalogId MEMBER catalogId)
    Q_PROPERTY(QString albumCatalogId MEMBER albumCatalogId)
    Q_PROPERTY(QString artistCatalogId MEMBER artistCatalogId)
    Q_PROPERTY(bool liked MEMBER liked)
    Q_PROPERTY(bool offline MEMBER offline)
    Q_PROPERTY(bool inLibrary MEMBER inLibrary)
    Q_PROPERTY(bool unavailable MEMBER unavailable)

public:
    QString title;
    QString artist;
    QString album;
    QString genre;
    // 0 when unknown.
    int year = 0;
    double durationSeconds = 0;
    // The length as shown, "3:41". Kept because rows and snapshots store it.
    QString durationText;

    // A path on this machine for a local file; empty for anything remote.
    QString filePath;
    QString artworkUrl;

    // "local" or "remote" (stored lowercase; compared without regard to case).
    QString sourceKind;
    // Where the row came from on this machine (the earlier "Provider" field;
    // snapshot key "provider"). Local and account filtering use it.
    QString origin;
    // The source tag the server gives a song: an opaque name compared only
    // against configured values (the earlier "RemoteProvider"; snapshot key
    // "remoteProvider").
    QString sourceTag;
    QString sourceUrl;
    // The server's opaque reference for sourceUrl; it always travels with the
    // sourceUrl it was issued for.
    QString sourceReference;
    // A playable address that may expire; never saved.
    QString streamUrl;
    // Where the song came from, in words ("Local", "On this PC", an account name).
    QString sourceLabel;
    // The quality the server delivers, when it says.
    QString quality;

    // The local database row. Not a catalogue id: account rows store a negated row id.
    qint64 rowId = 0;
    // The server's id for the song (the reference client's track id).
    QString remoteId;
    QString catalogId;
    QString albumCatalogId;
    QString artistCatalogId;
    // The primary artist as the library knows it, when the server says.
    QString primaryArtistName;
    QString primaryArtistCatalogId;

    // When it was added, as shown, and as a number to sort by.
    QString dateAdded;
    double dateAddedSortKey = 0;
    // Timestamps from the account library, ISO 8601 text as the server sends them.
    QString likedAt;
    QString addedAt;
    QString firstPlayedAt;
    QString lastPlayedAt;
    int playCount = 0;
    // Where playback last stopped; 0 means play from the start.
    double resumePositionSeconds = 0;

    bool liked = false;
    // The audio is on this machine (a local file, or a finished download).
    bool offline = false;
    // A search result that is already in the library.
    bool inLibrary = false;
    // For artist rows: the user follows this artist.
    bool followed = false;
    bool unavailable = false;
    QString unavailableReason;
    // Position in the list it belongs to.
    int index = 0;
    // For rows that stand for a group (an album or an artist): how many songs.
    int trackCount = 0;

    [[nodiscard]] bool isRemote() const;
    // A download is possible: a remote song streamed over http(s).
    [[nodiscard]] bool isDownloadable() const;
    // The upper-case source label for a badge, or empty for anything from this PC.
    [[nodiscard]] QString sourceBadgeText() const;

    // The saved form used in LastMusicState.json and its old home history.
    // Remote songs drop their file path, and addresses that are not safe to
    // keep are cleared.
    [[nodiscard]] QJsonObject toSnapshotJson() const;
    [[nodiscard]] static Track fromSnapshotJson(const QJsonObject& object);

    friend bool operator==(const Track&, const Track&) = default;
};

} // namespace lmp::core

Q_DECLARE_METATYPE(lmp::core::Track)
