#pragma once

#include "core/model/Track.h"
#include "core/playback/PlaybackHistoryQualifier.h"

#include <QString>
#include <QtGlobal>

#include <cstdint>

namespace lmp::core {

namespace db {
class TrackRepository;
}

// Writes a play to the history once the song has played long enough
// (PlaybackHistoryQualifier: 8 seconds of time actually playing). Each start
// of a song is its own play, so a replayed song counts again; one play is
// written at most once.
//
// Writing a play, as earlier versions did: the song's row is inserted or
// updated (local or remote, by its kind) and then gets one more play with
// the next number in the play order, one above the highest stored. Plays in
// the account library are written by the account work, not here.
//
// Not thread-safe; owned by the player controller and used on the
// repository's thread.
class PlaybackHistoryRecorder final {
public:
    explicit PlaybackHistoryRecorder(db::TrackRepository& tracks);

    // A new play of `track` starts; anything counted before is dropped.
    void begin(const Track& track);
    // Stops counting until the next begin().
    void clear();
    // One sample: `playing` is whether the song is playing at `nowMilliseconds`
    // (a monotonic clock). True when this sample wrote the play.
    [[nodiscard]] bool observe(bool playing, std::uint64_t nowMilliseconds);

private:
    void record();

    db::TrackRepository& m_tracks;
    PlaybackHistoryQualifier m_qualifier;
    Track m_track;
    QString m_sourceKey;
    QString m_identity;
    quint64 m_playSerial = 0;
    quint64 m_lastPlayOrder = 0;
};

} // namespace lmp::core
