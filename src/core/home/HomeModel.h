#pragma once

#include "core/db/AccountRecords.h"
#include "core/library/HomeMixBuilder.h"
#include "core/model/Track.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

// Home's shelves and mixes for the account library, as the reference client
// derives them from the library answer (history, liked songs and playlists).
// Songs are told apart by their remote id. Pure functions, no I/O.
namespace lmp::core::home {

inline constexpr int OnlineMixLength = 12;
inline constexpr int StarterMixCount = 5;

struct HomeLibrary {
    // Most recent first.
    QList<Track> history;
    QList<Track> liked;
    // Every playlist's songs, playlist after playlist.
    QList<Track> playlistTracks;
};

struct HomeModel {
    // Every song once, merged across history, likes and playlists.
    QList<Track> catalog;
    QList<Track> listenAgain;
    QList<Track> mostPlayed;
    QList<Track> liked;
    QList<Track> recentlyAdded;
    // The nine mixes; `genre` is the caption's genre, empty for "Made for you".
    QList<HomeMix> mixes;
};

// When the mixes are made again: once a day, and when a library with no
// music gets some.
[[nodiscard]] QString homeMixKey(const HomeLibrary& library, const QDate& today);

[[nodiscard]] HomeModel deriveHomeModel(const HomeLibrary& library);

// The account library kept on this PC as the model's input: history (played
// songs, newest play first), liked songs, and each playlist's songs; each
// song carries its plays and play times.
[[nodiscard]] HomeLibrary homeLibraryFromSnapshot(const db::AccountLibrarySnapshot& snapshot);

// A mix's caption: its genre, else "Made for you" ("From artists you follow"
// for a starter mix, whose genre field holds that text).
[[nodiscard]] QString mixCaption(const HomeMix& mix);

struct StarterMixPlan {
    // The mix without songs; they come from searching each artist.
    HomeMix mix;
    QStringList artists;
};

// Daily mixes for a listener with followed artists and no music yet: the
// artists dealt out one per mix in turn, up to three a mix and five mixes.
[[nodiscard]] QList<StarterMixPlan> planStarterMixes(const QStringList& followed);
// One song from each list in turn, without repeats, up to a mix's length.
[[nodiscard]] QList<Track> interleaveMixTracks(const QList<QList<Track>>& groups);
[[nodiscard]] QList<Track> mergeHomeMixTracks(const QList<Track>& local, const QList<Track>& remote,
                                              bool prependRemote = false);
// What to search the provider for to fill a mix; empty for none.
[[nodiscard]] QString providerQueryForMix(const HomeMix& mix, const QList<Track>& catalog);

// Values ranked by plays, likes and position; "unknown artist" and blanks
// left out; display text as first seen.
[[nodiscard]] QStringList rankedValues(const QList<Track>& tracks, bool byGenre);
// Exactly `count` labels when possible: the ranked ones, then fallbacks not
// already there (case-insensitively).
[[nodiscard]] QStringList fillLabels(const QStringList& ranked, const QStringList& fallback, int count);

} // namespace lmp::core::home
