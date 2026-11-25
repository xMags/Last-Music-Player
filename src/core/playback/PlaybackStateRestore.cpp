#include "core/playback/PlaybackStateRestore.h"

#include "core/db/TrackRepository.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/playback/LastTrackIdentity.h"
#include "core/playback/QueueTrackIdentity.h"
#include "core/playback/ResumePositionPolicy.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <QFileInfo>
#include <algorithm>
#include <QHash>
#include <QLoggingCategory>
#include <QSet>
#include <QString>

#include <utility>

namespace lmp::core {

namespace {

Q_LOGGING_CATEGORY(lcRestore, "lmp.playback.restore")

void warnIfFailed(const Result<void>& result)
{
    if (!result) {
        qCWarning(lcRestore) << "Could not move an old history entry:" << result.failure().detail;
    }
}

void moveEntryToDatabase(const LegacyHistoryEntry& entry, db::TrackRepository& tracks)
{
    const Track& track = entry.track;
    const QString catalogKey = legacyCatalogSourceKey(track);
    if (catalogKey.isEmpty()) {
        return;
    }
    const bool remote = track.isRemote() || net::isHttpUrl(track.filePath);
    if (remote) {
        const Result<qint64> upserted = tracks.upsertRemoteTrack(track, catalogKey);
        if (!upserted) {
            qCWarning(lcRestore) << "Could not store an old history song:" << upserted.failure().detail;
        }
    } else if (!track.filePath.isEmpty()) {
        const Result<qint64> upserted = tracks.upsertLocalTrack(track, catalogKey);
        if (!upserted) {
            qCWarning(lcRestore) << "Could not store an old history song:" << upserted.failure().detail;
        }
    }
    warnIfFailed(tracks.mergePlaybackStats(catalogKey, entry.playCount, entry.lastPlayedOrder));
}

} // namespace

QList<Track> migrateLegacyHomeHistory(const PlaybackState& state, QList<Track> history, SettingsStore& settings,
                                      db::TrackRepository& tracks)
{
    if (settings.boolean(settings::keys::LegacyHomeHistoryMigrated, false)) {
        return history;
    }

    QSet<QString> seenKeys;
    for (const Track& existing : std::as_const(history)) {
        const QString key = homeHistoryKey(existing);
        if (!key.isEmpty()) {
            seenKeys.insert(key);
        }
    }

    for (const LegacyHistoryEntry& entry : state.homeHistory) {
        const QString key = entry.key.isEmpty() ? homeHistoryKey(entry.track) : entry.key;
        if (key.isEmpty() || seenKeys.contains(key) || entry.localFileMissing) {
            continue;
        }
        history.append(entry.track);
        seenKeys.insert(key);
        moveEntryToDatabase(entry, tracks);
    }

    settings.setBoolean(settings::keys::LegacyHomeHistoryMigrated, true);
    return history;
}

namespace {

[[nodiscard]] bool isDownloadedCopy(const Track& track)
{
    return !track.isRemote() && net::isHttpUrl(track.sourceUrl) && !track.filePath.isEmpty();
}

} // namespace

bool holdsDownloadedCopies(const PlaybackState& state, const Track& current)
{
    if (isDownloadedCopy(current)) {
        return true;
    }
    if (!state.queue) {
        return false;
    }
    const QueueSnapshot& queue = state.queue->queue;
    return isDownloadedCopy(state.queue->currentTrack)
        || std::any_of(queue.contextQueue.cbegin(), queue.contextQueue.cend(), isDownloadedCopy)
        || std::any_of(queue.userQueue.cbegin(), queue.userQueue.cend(), isDownloadedCopy);
}

void restoreAccountSongs(PlaybackState& state, Track& current, const QList<Track>& library)
{
    QHash<QString, Track> bySource;
    for (const Track& song : library) {
        if (song.isRemote() && song.origin == QLatin1StringView("account") && !song.sourceUrl.isEmpty()) {
            bySource.insert(song.sourceUrl, song);
        }
    }
    const auto map = [&bySource](Track& track) {
        if (!isDownloadedCopy(track)) {
            return;
        }
        const auto found = bySource.constFind(track.sourceUrl);
        if (found == bySource.constEnd()) {
            return;
        }
        const QString file = track.filePath;
        const double resume = track.resumePositionSeconds;
        track = found.value();
        track.filePath = file;
        if (resume > 0.0) {
            track.resumePositionSeconds = resume;
        }
    };
    map(current);
    if (!state.queue) {
        return;
    }
    map(state.queue->currentTrack);
    for (Track& track : state.queue->queue.contextQueue) {
        map(track);
    }
    for (Track& track : state.queue->queue.userQueue) {
        map(track);
    }
}

std::optional<Track> findLastTrack(const PlaybackState& state, const QList<Track>& history)
{
    if (state.lastTrack.isEmpty()) {
        return std::nullopt;
    }
    for (const Track& candidate : history) {
        if (matchesLastTrackValue(candidate, state.lastTrack)) {
            return candidate;
        }
    }
    if (state.queue && lastTrackValue(state.queue->currentTrack) == state.lastTrack) {
        return state.queue->currentTrack;
    }
    return std::nullopt;
}

bool hasPlayableSource(const Track& track)
{
    return !track.filePath.isEmpty() || (track.isRemote() && !track.sourceUrl.isEmpty());
}

bool isLocalFileMissing(const Track& track)
{
    if (track.isRemote() || track.filePath.isEmpty() || net::isHttpUrl(track.filePath)) {
        return false;
    }
    return !QFileInfo::exists(track.filePath);
}

double resumeStartSeconds(const Track& track)
{
    return canResumeFrom(track.resumePositionSeconds, track.durationSeconds) ? track.resumePositionSeconds : 0.0;
}

QueueEngine restoredQueue(const PlaybackState& state, const Track& current, Shuffle shuffle, RepeatMode repeatMode,
                          const QueueRandom& random)
{
    const QString identity = queueTrackIdentity(current);
    const bool savedQueueFits =
        state.queue && !identity.isEmpty() && queueTrackIdentity(state.queue->currentTrack) == identity;
    if (!savedQueueFits) {
        return QueueEngine::create({current}, 0, {}, shuffle, repeatMode, random);
    }
    const QueueSnapshot& saved = state.queue->queue;
    if (state.queue->playOrder) {
        return QueueEngine::restored(saved, shuffle, repeatMode);
    }
    return QueueEngine::create(saved.contextQueue, saved.contextIndex, saved.userQueue, shuffle, repeatMode, random);
}

} // namespace lmp::core
