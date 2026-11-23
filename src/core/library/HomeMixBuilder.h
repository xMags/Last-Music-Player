#pragma once

#include <QDate>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/model/Track.h"

namespace lmp::core {

// One generated mix: its id ("daily1" to "daily5", "repeat", "discover",
// "timecapsule", "fresh"), the reference's title, badge and colours, the
// description (its leading artists, or the theme line when it has none) and
// the genre that drives it, if any.
struct HomeMix {
    QString id;
    QString title;
    QString badge;
    // ARGB (0xAARRGGBB); the UI turns them into colours.
    quint32 colorStart = 0;
    quint32 colorEnd = 0;
    QString description;
    QString genre;
    QList<Track> tracks;
};

// What the mixes are made from, as the current app gathered it for Home
// (MainWindow.Home.cpp HydrateHomeAsync).
struct HomeMixInputs {
    // The newest songs first (the library page sorted by date added, 240 rows).
    QList<Track> catalog;
    // The play history, most recent first.
    QList<Track> recentHistory;
    // Plays and last play order by the old home history key (homeHistoryKey).
    QHash<QString, quint32> playCounts;
    QHash<QString, quint64> lastPlayedOrder;
    // The genres most listened to, best first, and each one's songs.
    QStringList rankedGenres;
    QHash<QString, QList<Track>> genrePools;
};

// The current app's generated mixes (BuildHomeMixes and
// UpdateMadeForCardLabels), with the reference's names and colours
// (home-model.ts MIX_META):
// - Daily Mix 1 to 5 each take one of the top genres not yet used; without
//   one, a slot falls back to recent history and the top artist (1), the
//   second artist and every third catalogue song (2), the third artist then
//   at most two songs per artist (3), the fourth or fifth artist (4, 5).
//   Every slot then tops itself up from the catalogue, starting at its own
//   fifth of it so fallbacks do not converge.
// - On Repeat: most played, then most recently played.
// - Discover Weekly: songs outside the top genres first, least played, newest.
// - Time Capsule: the oldest additions (dated before undated).
// - Fresh Finds: the newest additions.
// Twelve songs each, without repeats (by homeHistoryKey), playable songs
// only. The result depends on the inputs alone.
class HomeMixBuilder {
public:
    static constexpr int MixLength = 12;
    static constexpr int DailyMixCount = 5;
    // Inputs the current app read for the mixes.
    static constexpr int CatalogSeedSize = 240;
    static constexpr int GenreCount = 6;

    [[nodiscard]] static QList<HomeMix> build(const HomeMixInputs& inputs);

    // Artists by plays (each song counts 1 plus its plays), then by name;
    // "Unknown Artist" and blank names left out. Display names as first seen.
    [[nodiscard]] static QStringList rankedArtists(const QList<Track>& tracks,
                                                   const QHash<QString, quint32>& playCounts);

    // The two leading artists, joined, with " & more" when there are others.
    [[nodiscard]] static QString artistLine(const QList<Track>& tracks, const QHash<QString, quint32>& playCounts);

    // When Home's mixes are made again (home-model.ts homeMixKey): once a
    // day, and when a library with no music gets some. Plays and likes during
    // the day leave them alone.
    [[nodiscard]] static QString refreshKey(const QDate& today, bool hasMusic);

    // The mix with this id's title, badge, colours and theme line, without songs.
    [[nodiscard]] static HomeMix blank(const QString& id);
    [[nodiscard]] static QStringList ids();
};

} // namespace lmp::core
