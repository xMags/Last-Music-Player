#pragma once

#include "core/model/Track.h"
#include "core/playback/QueueSnapshot.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace lmp::core {

// One entry of the home history that versions before the library database
// kept in LastMusicState.json. Read only, for the one-time move into the
// database.
struct LegacyHistoryEntry {
    // Built from the entry's track keys as those versions built it: kind
    // and label get their defaults, server ids are not read.
    Track track;
    // The entry's own "key"; empty means "derive it from the track".
    QString key;
    quint32 playCount = 0;
    quint64 lastPlayedOrder = 0;
    // Set by the file reader on its worker: a local song whose file is gone.
    bool localFileMissing = false;

    friend bool operator==(const LegacyHistoryEntry&, const LegacyHistoryEntry&) = default;
};

// The queue saved under "QueueSnapshot", a key this version adds. Versions
// that do not know the key ignore it.
struct SavedQueue {
    // contextQueue, contextIndex, userQueue: QueueEngine::snapshot.
    QueueSnapshot queue;
    // The song that was current; it is not always in the context (a song
    // taken from the user queue leaves it).
    Track currentTrack;
    // True when the context is stored in play order, which this version
    // always writes. False (or missing) means list order: shuffle it afresh.
    bool playOrder = false;

    friend bool operator==(const SavedQueue&, const SavedQueue&) = default;
};

// The contents of LastMusicState.json.
struct PlaybackState {
    static constexpr double DefaultVolume = 0.7;

    // The last song (LastTrackIdentity.h); empty with nothing playing.
    QString lastTrack;
    // Written for earlier versions; the Volume setting is what is used.
    double volume = DefaultVolume;
    // Old keys, read but never written.
    quint64 homePlaySequence = 0;
    QList<LegacyHistoryEntry> homeHistory;
    std::optional<SavedQueue> queue;

    friend bool operator==(const PlaybackState&, const PlaybackState&) = default;
};

// {"LastTrack": "...", "Volume": n} plus "QueueSnapshot" when a queue is
// given, as compact UTF-8 JSON without a byte order mark. The old keys are
// never written.
[[nodiscard]] QByteArray serializePlaybackState(const PlaybackState& state);

// Nothing when the bytes are not well-formed UTF-8, not a JSON object, or a
// key earlier versions read holds a value of another type (they gave up on
// the whole file then). "HomeHistory" that is not an array and entries that
// are not objects are skipped, as before. A "QueueSnapshot" that is not an
// object, or has no usable current song, is ignored on its own.
[[nodiscard]] std::optional<PlaybackState> parsePlaybackState(const QByteArray& bytes);

} // namespace lmp::core
