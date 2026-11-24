#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "core/model/Track.h"

// The Downloads page's collection tabs, built from the songs downloaded on
// this PC: the listener's playlists and recent plays cut down to their
// downloaded songs, and the albums, artists and genres of the downloads
// themselves (the reference's offline-collections.ts). Pure, so it can be
// tested without a page.
namespace lmp::core::offline {

enum class GroupKind { Playlist, Album, Artist, Genre };

struct OfflineGroup {
    GroupKind kind = GroupKind::Playlist;
    // Unique within its tab.
    QString key;
    QString name;
    // The card's second line: "3 of 12 songs", an album's artist, a count.
    QString subtitle;
    // Only the downloaded songs, in the collection's own order.
    QList<Track> tracks;
};

// "1 song", "12 songs".
[[nodiscard]] QString songCountText(int count);

// The downloaded songs of a collection, in its order, each once.
// `collectionKeys` are the collection's songs' download keys;
// `downloadedKeys[i]` is the key of `downloaded[i]`. Empty keys never match.
[[nodiscard]] QList<Track> downloadedPart(const QStringList& collectionKeys, const QList<Track>& downloaded,
                                          const QStringList& downloadedKeys);

// A playlist card's line: "12 songs" when all of it is here, else
// "3 of 12 songs" (`total` counts each song once).
[[nodiscard]] QString playlistSubtitle(int downloadedCount, int total);

// A song's main artist, as Library lists artists: its primary artist when it
// names one (kept whole, so a duo stays one artist), else the first name of
// its credit ("Arijit Singh & Asees Kaur" is Arijit Singh). Empty for none.
[[nodiscard]] QString mainArtist(const Track& track);

// Albums with two or more downloaded songs, most songs first, then by name.
// "Film (Original Motion Picture Soundtrack)" and "Film" are one album,
// shown under the shorter name; playlist names are not albums.
[[nodiscard]] QList<OfflineGroup> albumGroups(const QList<Track>& downloaded, const QStringList& playlistNames);

// The downloads by main artist, most songs first, then by name.
[[nodiscard]] QList<OfflineGroup> artistGroups(const QList<Track>& downloaded);

// The downloads by genre, most songs first, then by name. A genre with a
// single song is left out while another has more.
[[nodiscard]] QList<OfflineGroup> genreGroups(const QList<Track>& downloaded);

} // namespace lmp::core::offline
