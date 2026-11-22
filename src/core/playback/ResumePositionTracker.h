#pragma once

#include "core/model/Track.h"

#include <QString>

namespace lmp::core {

namespace db {
class TrackRepository;
}

// Keeps a song's resume point in the database (Tracks.LastPositionSeconds,
// keyed by legacyCatalogSourceKey), by the rules of ResumePositionPolicy and
// the save rules earlier versions used:
// - only a point at least 30 s in with at least 30 s left is stored; any
//   other point stores 0, which clears it;
// - while the song plays, a new point is written only once it differs by
//   10 s or more from the last one written for that song;
// - a forced save writes the exact point at once;
// - nothing is written when 0 would replace a 0 written before;
// - a song played to its end has its point cleared.
//
// Not thread-safe; used on the repository's thread.
class ResumePositionTracker final {
public:
    // Saves are at least this far apart while a song plays.
    static constexpr double PeriodicSaveSeconds = 10.0;

    explicit ResumePositionTracker(db::TrackRepository& tracks);

    // A position report while the song plays.
    void observe(const Track& track, double positionSeconds, double durationSeconds);
    // A pause, a change of song or shutdown: the exact point goes in now.
    void saveNow(const Track& track, double positionSeconds, double durationSeconds);
    // The song played to its end.
    void clear(const Track& track);

private:
    void persist(const Track& track, double positionSeconds, double durationSeconds, bool force);
    void write(const QString& sourceKey, double seconds);

    db::TrackRepository& m_tracks;
    // The song and point written last.
    QString m_persistedKey;
    double m_persistedSeconds = 0.0;
};

} // namespace lmp::core
