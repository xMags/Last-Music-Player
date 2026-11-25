#include "core/playback/PlaybackHistoryRecorder.h"

#include <QDateTime>

#include "core/db/TrackRepository.h"
#include "core/model/CatalogSourceKey.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QLatin1Char>
#include <QLoggingCategory>

#include <algorithm>

namespace lmp::core {

namespace {

Q_LOGGING_CATEGORY(lcHistory, "lmp.playback.history")

} // namespace

PlaybackHistoryRecorder::PlaybackHistoryRecorder(db::TrackRepository& tracks)
    : m_tracks(tracks)
{
}

void PlaybackHistoryRecorder::begin(const Track& track)
{
    clear();
    const QString sourceKey = catalogSourceKey(track);
    if (sourceKey.isEmpty()) {
        return;
    }
    m_track = track;
    m_sourceKey = sourceKey;
    // A serial per start keeps two plays of the same song apart.
    m_identity = QString::number(++m_playSerial) + QLatin1Char('|') + sourceKey;
    m_qualifier.reset(m_identity);
}

void PlaybackHistoryRecorder::clear()
{
    m_qualifier.clear();
    m_track = Track();
    m_sourceKey.clear();
    m_identity.clear();
}

bool PlaybackHistoryRecorder::observe(bool playing, std::uint64_t nowMilliseconds, double positionSeconds)
{
    if (m_identity.isEmpty() || !m_qualifier.observe(m_identity, playing, nowMilliseconds)) {
        return false;
    }
    m_position = positionSeconds;
    record();
    // Marked even when a write failed, as before: one play, one attempt.
    m_qualifier.markCompleted();
    return true;
}

void PlaybackHistoryRecorder::record()
{
    // In Account mode a play of an account song belongs to the account.
    if (m_accountSink != nullptr && m_accountSink->recordAccountPlay(m_track, m_position)) {
        return;
    }
    const bool remote = m_track.isRemote() || net::isHttpUrl(m_track.filePath);
    if (remote) {
        const Result<qint64> upserted = m_tracks.upsertRemoteTrack(m_track, m_sourceKey);
        if (!upserted) {
            qCWarning(lcHistory) << "Could not store the played song:" << upserted.failure().detail;
        }
    } else if (!m_track.filePath.isEmpty()) {
        const Result<qint64> upserted = m_tracks.upsertLocalTrack(m_track, m_sourceKey);
        if (!upserted) {
            qCWarning(lcHistory) << "Could not store the played song:" << upserted.failure().detail;
        }
    }

    // Other writers (the history migration, sync) can raise the stored
    // order, so the next number is taken from the database each time. It is
    // at least the time in Unix seconds: account songs are ordered by when
    // they were played, in seconds, and History, Home and the mixes sort
    // both kinds together (a counter alone would sort every play of a song
    // on this PC below every account play). Earlier counters stay smaller,
    // which is right: they were played before.
    const auto now = static_cast<quint64>(std::max<qint64>(0, QDateTime::currentSecsSinceEpoch()));
    m_lastPlayOrder = std::max(std::max(m_lastPlayOrder, m_tracks.maxLastPlayedOrder()) + 1, now);
    const Result<void> recorded = m_tracks.recordPlayback(m_sourceKey, m_lastPlayOrder);
    if (!recorded) {
        qCWarning(lcHistory) << "Could not record the play:" << recorded.failure().detail;
    }
}

} // namespace lmp::core
