#include "core/playback/ResumePositionTracker.h"

#include "core/db/TrackRepository.h"
#include "core/playback/LastTrackIdentity.h"
#include "core/playback/ResumePositionPolicy.h"

#include <QLoggingCategory>

#include <cmath>

namespace lmp::core {

namespace {

Q_LOGGING_CATEGORY(lcResume, "lmp.playback.resume")

} // namespace

ResumePositionTracker::ResumePositionTracker(db::TrackRepository& tracks)
    : m_tracks(tracks)
{
}

void ResumePositionTracker::observe(const Track& track, double positionSeconds, double durationSeconds)
{
    persist(track, positionSeconds, durationSeconds, false);
}

void ResumePositionTracker::saveNow(const Track& track, double positionSeconds, double durationSeconds)
{
    persist(track, positionSeconds, durationSeconds, true);
}

void ResumePositionTracker::clear(const Track& track)
{
    const QString key = legacyCatalogSourceKey(track);
    if (key.isEmpty()) {
        return;
    }
    write(key, 0.0);
}

void ResumePositionTracker::persist(const Track& track, double positionSeconds, double durationSeconds, bool force)
{
    if (!std::isfinite(positionSeconds) || !std::isfinite(durationSeconds)) {
        return;
    }
    const QString key = legacyCatalogSourceKey(track);
    if (key.isEmpty()) {
        return;
    }
    const double stored = resumePositionToStore(positionSeconds, durationSeconds);
    const bool sameTrack = key == m_persistedKey;
    // Position reports arrive several times a second; a periodic save lands
    // only once the point has moved far enough to be worth keeping.
    if (!force && sameTrack && std::abs(stored - m_persistedSeconds) < PeriodicSaveSeconds) {
        return;
    }
    // Nothing to clear and nothing to store.
    if (stored <= 0.0 && sameTrack && m_persistedSeconds <= 0.0) {
        return;
    }
    write(key, stored);
}

void ResumePositionTracker::write(const QString& sourceKey, double seconds)
{
    m_persistedKey = sourceKey;
    m_persistedSeconds = seconds;
    const Result<void> recorded = m_tracks.recordPlaybackPosition(sourceKey, seconds);
    if (!recorded) {
        qCWarning(lcResume) << "Could not store the resume point:" << recorded.failure().detail;
    }
}

} // namespace lmp::core
