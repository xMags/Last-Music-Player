#include "core/playback/AutoplayService.h"

#include "core/playback/QueueTrackIdentity.h"
#include "core/playback/QueueTrackLists.h"

namespace lmp::core {

AutoplayService::AutoplayService(AutoplayQueue& queue, AutoplaySource source, QObject* parent)
    : QObject(parent)
    , m_queue(queue)
    , m_source(std::move(source))
{
}

bool AutoplayService::owns(std::uint64_t epoch) const
{
    return epoch == m_epoch && m_queue.autoplay() && m_queue.repeatOff();
}

void AutoplayService::invalidate()
{
    ++m_epoch;
    m_resumeWhenReady = false;
    if (m_inFlight) {
        m_inFlight.reset();
        emit loadingChanged();
    }
}

void AutoplayService::contextReplaced()
{
    invalidate();
    m_seen.clear();
}

QList<Track> AutoplayService::fresh(const QList<Track>& candidates, const Track& seed) const
{
    // Rebuilt after every wait, so a song queued meanwhile is not added again.
    QSet<QString> existing = m_seen;
    existing.insert(queueTrackIdentity(seed));
    for (const Track& track : m_queue.queuedTracks()) {
        existing.insert(queueTrackIdentity(track));
    }
    if (m_source.history) {
        for (const Track& track : m_source.history()) {
            existing.insert(queueTrackIdentity(track));
        }
    }
    QList<Track> result;
    for (const Track& track : uniqueQueueTracks(candidates)) {
        if (result.size() == MaximumTracks) {
            break;
        }
        const bool playable = !track.unavailable
            && (!track.catalogId.isEmpty() || !track.sourceUrl.isEmpty() || !track.streamUrl.isEmpty());
        const QString identity = queueTrackIdentity(track);
        if (playable && !identity.isEmpty() && !existing.contains(identity)) {
            result.append(track);
        }
    }
    return result;
}

void AutoplayService::gather(const Track& seed, std::function<bool()> stillWanted,
                             std::function<void(QList<Track>)> done)
{
    const auto bySearch = [this, seed, stillWanted, done]() {
        const QString query = !seed.artist.trimmed().isEmpty() ? seed.artist.trimmed() : seed.title.trimmed();
        if (!m_source.search || query.isEmpty()) {
            done({});
            return;
        }
        m_source.search(query).then(this, [this, seed, stillWanted, done](const Result<QList<Track>>& found) {
            if (!stillWanted()) {
                return;
            }
            done(found ? fresh(found.value(), seed) : QList<Track>());
        });
    };
    if (seed.sourceUrl.isEmpty() || !m_source.related) {
        bySearch();
        return;
    }
    m_source.related(seed).then(this, [this, seed, stillWanted, done, bySearch](const Result<QList<Track>>& related) {
        if (!stillWanted()) {
            return;
        }
        // An older service has no related songs; search instead.
        const QList<Track> found = related ? fresh(related.value(), seed) : QList<Track>();
        if (!found.isEmpty()) {
            done(found);
            return;
        }
        bySearch();
    });
}

void AutoplayService::extensionWanted(bool queueEnded)
{
    const std::optional<Track> seed = m_queue.currentTrack();
    if (!seed || !m_queue.autoplay() || !m_queue.repeatOff()) {
        return;
    }
    if (m_inFlight && *m_inFlight == m_epoch) {
        m_resumeWhenReady = m_resumeWhenReady || queueEnded;
        return;
    }
    if (m_queue.upcomingCount() > TopUpThreshold) {
        return;
    }
    m_resumeWhenReady = m_resumeWhenReady || queueEnded;
    const std::uint64_t epoch = m_epoch;
    m_inFlight = epoch;
    emit loadingChanged();
    gather(
        *seed, [this, epoch]() { return owns(epoch); },
        [this, epoch](const QList<Track>& tracks) {
            for (const Track& track : tracks) {
                m_seen.insert(queueTrackIdentity(track));
            }
            const bool resume = m_resumeWhenReady && !tracks.isEmpty();
            if (resume) {
                m_resumeWhenReady = false;
            }
            finish(epoch);
            if (!tracks.isEmpty()) {
                m_queue.appendAutoplayTracks(tracks, resume);
            }
        });
}

void AutoplayService::finish(std::uint64_t epoch)
{
    if (m_inFlight && *m_inFlight == epoch) {
        m_inFlight.reset();
        emit loadingChanged();
    }
}

void AutoplayService::startMix(const Track& track)
{
    if (track.unavailable) {
        return;
    }
    // Playing it replaces the queue (contextReplaced); with autoplay on,
    // the top-up that follows fills the queue.
    m_queue.playAlone(track);
    if (m_queue.autoplay() && m_queue.repeatOff()) {
        return;
    }
    const std::uint64_t epoch = m_epoch;
    const QString identity = queueTrackIdentity(track);
    const auto stillThisMix = [this, epoch, identity]() {
        const std::optional<Track> current = m_queue.currentTrack();
        return epoch == m_epoch && current && queueTrackIdentity(*current) == identity;
    };
    gather(track, stillThisMix, [this](const QList<Track>& tracks) {
        if (tracks.isEmpty()) {
            emit mixFoundNothing();
            return;
        }
        for (const Track& added : tracks) {
            m_seen.insert(queueTrackIdentity(added));
        }
        m_queue.appendAutoplayTracks(tracks, false);
    });
}

} // namespace lmp::core
