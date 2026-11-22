#pragma once

#include "core/model/Track.h"

#include <QList>
#include <QString>
#include <QtGlobal>

namespace lmp::core::db {

// What a page of the songs list asks for. The defaults are those of earlier
// versions.
struct TrackQuery {
    // "All", "History", "Most", "Fav" or "Liked" (compared without regard to case).
    QString filter = QStringLiteral("All");
    // "DateAdded", "Title", "Artist", "Duration" or "MostPlayed" (compared
    // without regard to case). For a playlist or album collection an
    // unrecognised sort keeps the stored order.
    QString sort = QStringLiteral("DateAdded");
    // "All", "Account" or "OnThisPc" (compared exactly).
    QString scope = QStringLiteral("All");
    // Empty, "album", "artist", "genre", "album-collection" or "playlist".
    QString groupKind;
    QString groupKey;
    // Optional case-insensitive substring match across title, artist and
    // album. LIKE wildcards are escaped here, so callers pass the listener's
    // text unchanged rather than building SQL patterns.
    QString searchText;
    // The signed-in account, needed for an account playlist.
    QString accountOwnerId;
    bool includeRemote = true;
    bool activeOnly = true;
    int offset = 0;
    // 0 means every row.
    int limit = 0;
};

struct TrackPage {
    QList<Track> tracks;
    // Every match, not only this page.
    int totalCount = 0;
    double totalSeconds = 0.0;
};

struct LibraryStats {
    int songCount = 0;
    int albumCount = 0;
    int artistCount = 0;
    int genreCount = 0;
    double totalSeconds = 0.0;
};

// A song's play statistics, for exporting and merging them.
struct PlaybackStat {
    Track track;
    QString sourceKey;
    quint32 playCount = 0;
    quint64 lastPlayedOrder = 0;
};

// A remote song played in an older build that the account history can import.
struct HistoryImportCandidate {
    Track track;
    QString sourceKey;
    QString lastPlayedUtc;
};

} // namespace lmp::core::db
