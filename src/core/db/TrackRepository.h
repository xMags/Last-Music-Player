#pragma once

#include "core/async/Result.h"
#include "core/db/Database.h"
#include "core/model/Track.h"

#include <QByteArray>
#include <QHash>
#include <QLatin1StringView>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace lmp::core::db {

// Library songs in the Tracks table: writing scanned and remote songs,
// pruning after a scan, play statistics, the liked flag and the resume
// position, and wiping all user data.
//
// Not thread-safe. It works on the database's connection and belongs to the
// database's owner thread, like the Database it was given; that Database must
// outlive it.
//
// An operation that earlier versions reported as false or 0 returns a
// Failure: FailureKind::NotFound when there was nothing to act on (an empty
// key), FailureKind::Storage when SQLite refused. An operation that earlier
// versions skipped silently on an empty key still succeeds without writing.
class TrackRepository {
public:
    // The Provider of a downloaded song's row: a "local" row the download
    // index owns (syncDownloadTracks), shown only while the music source is
    // this PC (the EffectiveTracks view) and left alone by folder scans.
    static constexpr QLatin1StringView DownloadProvider{"download"};

    // Where a downloaded song's like comes from.
    enum class LikeSource {
        // A song of this PC, or one whose source is not known.
        None,
        // The account's songs (AccountTracks), of `accountId` when known.
        Account,
        // The songs kept in API key mode (remote rows).
        Provider,
    };

    // A finished download, as syncDownloadTracks writes it.
    struct DownloadedTrack {
        Track track;
        LikeSource likeSource = LikeSource::None;
        // Empty when the download does not name its account (any account's
        // song then counts).
        QString accountId;
    };

    explicit TrackRepository(Database& database);

    // Removes every library, collection and account row, then truncates the
    // write-ahead log and compacts the file. All or nothing up to the commit;
    // the checkpoint and VACUUM run after it.
    [[nodiscard]] Result<void> clearAllUserData();

    // Nothing happens when a scan starts: rows go inactive only once the scan
    // completes, so an interrupted scan keeps the old local library visible.
    void beginLocalScan();
    // Marks every local row inactive except those whose source key is in
    // `activeSourceKeys`, in one transaction. Downloaded songs' rows are not
    // the scan's and are left as they are.
    [[nodiscard]] Result<void> completeLocalScan(const QStringList& activeSourceKeys);

    // Makes the downloaded songs' rows these songs (local rows with
    // DownloadProvider, keyed by catalogSourceKey): each is written and
    // active, every other downloaded song's row goes inactive. A row that a
    // folder scan keeps active for the same file stays the scan's. Plays
    // stay on the rows.
    //
    // A song's like follows its source (matched by source address): when
    // the source knows the song and its like differs from the one this
    // last carried (DownloadLikes), the row takes it. A like changed on
    // this PC in between therefore stays until the source's changes.
    //
    // One transaction; a song without a usable key is left out.
    [[nodiscard]] Result<void> syncDownloadTracks(const QList<DownloadedTrack>& songs);
    // Downloaded songs' rows that know their length: source key to seconds.
    [[nodiscard]] QHash<QString, double> downloadTrackDurations();

    // Inserts or updates the row with this source key and returns its id.
    // Catalogue ids already stored survive an update that brings none.
    [[nodiscard]] Result<qint64> upsertLocalTrack(const Track& track, const QString& sourceKey);
    // As upsertLocalTrack, with the track's origin (or "remote") as the
    // stored Provider. Source and artwork addresses that are not safe to keep
    // are stored empty.
    [[nodiscard]] Result<qint64> upsertRemoteTrack(const Track& track, const QString& sourceKey);

    // One more play, now, with `playOrder` as its place in the history.
    [[nodiscard]] Result<void> recordPlayback(const QString& sourceKey, quint64 playOrder);
    // Where playback stopped, for the resume shelf. Zero or less clears it.
    [[nodiscard]] Result<void> recordPlaybackPosition(const QString& sourceKey, double positionSeconds);
    // Raises the stored statistics to at least these values; nothing happens
    // when both are zero.
    [[nodiscard]] Result<void> mergePlaybackStats(const QString& sourceKey, quint32 playCount, quint64 lastPlayedOrder);
    // The highest history position stored, or 0.
    [[nodiscard]] quint64 maxLastPlayedOrder();

    [[nodiscard]] Result<void> setLiked(const QString& sourceKey, bool liked);
    // False for an empty key, an unknown row or a failed read.
    [[nodiscard]] bool isLiked(const QString& sourceKey);

private:
    [[nodiscard]] Result<qint64> upsertTrack(const Track& track,
                                             const QString& sourceKind,
                                             const QString& provider,
                                             const QString& sourceKey,
                                             bool active,
                                             const QByteArray& sql);

    SqliteConnection& m_connection;
};

} // namespace lmp::core::db
