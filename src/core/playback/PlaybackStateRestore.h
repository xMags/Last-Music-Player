#pragma once

#include "core/model/Track.h"
#include "core/playback/PlaybackStateSnapshot.h"
#include "core/playback/QueueEngine.h"
#include "core/playback/QueueShuffle.h"
#include "core/playback/QueueTypes.h"

#include <QList>

#include <optional>

namespace lmp::core {

class SettingsStore;
namespace db {
class TrackRepository;
}

// The launch steps that turn a read LastMusicState.json into database rows
// and a song to show, as earlier versions took them. Used on the
// repository's thread, except isLocalFileMissing, which touches the disk.

// Moves the old home history into the database once, guarded by the
// LegacyHomeHistoryMigrated setting. `history` is the play history as the
// database has it, newest first; the result is that list with every moved
// entry appended. An entry is skipped when its key (its own "key", else
// homeHistoryKey) is empty or already seen, or its local file is gone. A
// moved entry's row is inserted or updated (remote or local by its kind;
// a local one only with a path) and its play count and order are merged,
// under its legacyCatalogSourceKey. The setting is set afterwards.
[[nodiscard]] QList<Track> migrateLegacyHomeHistory(const PlaybackState& state, QList<Track> history,
                                                    SettingsStore& settings, db::TrackRepository& tracks);

// The first song in `history` the saved "LastTrack" names. When none does,
// the saved queue's current song if "LastTrack" names it (a song never
// played long enough to reach the history).
[[nodiscard]] std::optional<Track> findLastTrack(const PlaybackState& state, const QList<Track>& history);

// Whether the saved song or queue holds a downloaded copy of a song as a
// file (earlier versions queued them so from the Offline page).
[[nodiscard]] bool holdsDownloadedCopies(const PlaybackState& state, const Track& current);
// Gives back each such copy as the account song it is (the library's song
// with origin "account" and the same source address), keeping its file so it
// still plays from disk; other songs stay as they are. `library` is the
// library's songs.
void restoreAccountSongs(PlaybackState& state, Track& current, const QList<Track>& library);

// A restored song needs something to play from: a path, or a remote source
// address.
[[nodiscard]] bool hasPlayableSource(const Track& track);

// A local song whose file is gone. Remote songs and songs without a path
// (or with a web address as their path) are never missing. Touches the disk.
[[nodiscard]] bool isLocalFileMissing(const Track& track);

// The point to start `track` from: its stored resume point when
// canResumeFrom allows it, otherwise 0.
[[nodiscard]] double resumeStartSeconds(const Track& track);

// The queue to show with `current`: the saved queue when its current song is
// `current`, otherwise `current` alone. A queue saved in play order keeps
// it; one in list order is shuffled afresh when shuffle is on.
[[nodiscard]] QueueEngine restoredQueue(const PlaybackState& state, const Track& current, Shuffle shuffle,
                                        RepeatMode repeatMode, const QueueRandom& random);

} // namespace lmp::core
