#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace lmp::core {

// Sort orders offered by a collection page (playlist, album, artist, genre).
enum class DetailSort {
    // The collection's own arrangement. Only playlists and album collections
    // have one: they store an explicit position per member.
    Curated,
    DateAdded,
    Title,
    Artist,
    Duration,
};

// Whether a page kind has a stored position to sort by. Artist and genre
// pages are filtered views of the whole library, so they offer no Curated.
[[nodiscard]] bool kindSupportsCuratedOrder(QStringView detailKind);

[[nodiscard]] DetailSort defaultDetailSort(bool supportsCuratedOrder);

// Maps a sort menu tag to a sort. Unknown text, and "custom" for a kind with
// no curated order, both fall back to the kind's default, so a stale tag
// never produces an empty ordering.
[[nodiscard]] DetailSort parseDetailSort(QStringView tag, bool supportsCuratedOrder);

// The value for the track query's sort field. Curated maps to an empty
// string, which the curated collection queries read as "stored position".
[[nodiscard]] QString detailSortQueryValue(DetailSort sort);

[[nodiscard]] QString detailSortLabel(DetailSort sort);

// The fields needed to filter and sort a collection page held in memory.
struct DetailTrackSortKey {
    QString title;
    QString artist;
    QString album;
    double durationSeconds = 0.0;
};

// Indices into `keys` in display order. The query matches title, artist or
// album case-insensitively. Curated and DateAdded keep the source order;
// equal sort keys fall back to the title, then to the source order.
[[nodiscard]] QList<qsizetype> detailTrackOrder(const QList<DetailTrackSortKey>& keys, QStringView query,
                                                DetailSort sort);

} // namespace lmp::core
