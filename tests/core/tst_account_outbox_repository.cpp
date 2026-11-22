#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "TempLibrary.h"
#include "core/db/AccountOutboxRecords.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/model/Track.h"

#include <optional>

using lmp::core::FailureKind;
using lmp::core::Track;
using lmp::core::db::AccountOutboxRepository;
using lmp::core::db::AccountPendingCounts;
using lmp::core::db::AccountRetryQueue;
using lmp::core::db::OutboxFailureMark;
using lmp::core::db::OutboxRows;
using lmp::core::db::PendingLikeRecord;
using lmp::core::db::PlaybackEventRecord;

namespace {

const QString kOwner = QStringLiteral("owner-a");

Track song(const QString& remoteId)
{
    Track track;
    track.remoteId = remoteId;
    track.title = QStringLiteral("Song ") + remoteId;
    track.sourceKind = QStringLiteral("remote");
    track.sourceUrl = QStringLiteral("https://music.example.test/tracks/") + remoteId;
    return track;
}

PlaybackEventRecord playbackEvent(const QString& eventId, const QString& playedAtUtc)
{
    PlaybackEventRecord record;
    record.eventId = eventId;
    record.accountId = kOwner;
    record.remoteTrackId = QStringLiteral("track-") + eventId;
    record.track = song(record.remoteTrackId);
    record.playedAtUtc = playedAtUtc;
    record.positionSeconds = 30;
    return record;
}

PendingLikeRecord pendingLike(const QString& remoteId, bool desiredState, const QString& updatedAtUtc)
{
    PendingLikeRecord record;
    record.accountId = kOwner;
    record.remoteTrackId = remoteId;
    record.track = song(remoteId);
    record.desiredState = desiredState;
    record.updatedAtUtc = updatedAtUtc;
    return record;
}

QStringList eventIds(const QList<PlaybackEventRecord>& events)
{
    QStringList ids;
    for (const PlaybackEventRecord& record : events) {
        ids << record.eventId;
    }
    return ids;
}

OutboxFailureMark retryIn(int seconds)
{
    return OutboxFailureMark{QStringLiteral("network"), seconds, false};
}

OutboxFailureMark blockedMark()
{
    return OutboxFailureMark{QStringLiteral("rejected"), 600, true};
}

} // namespace

class AccountOutboxRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void playbackEventsQueueOncePerId();
    void playbackFailuresScheduleRetries();
    void acknowledgingRemovesOnlyListedEvents();
    void likesKeepOneRequestPerSong();
    void preferencesMergeAndAcknowledgeByRevision();
    void preferenceFailuresTouchPendingKeysOnly();
    void pendingCountsSplitBlockedRows();
    void resettingTheScheduleMakesRowsDue();
};

void AccountOutboxRepositoryTest::playbackEventsQueueOncePerId()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountOutboxRepository outbox(library.database());

    PlaybackEventRecord noSong = playbackEvent(QStringLiteral("e1"), QStringLiteral("2026-08-01T00:00:00Z"));
    noSong.track.reset();
    const auto refused = outbox.enqueuePlaybackEvent(noSong);
    QVERIFY(!refused);
    QCOMPARE(refused.failure().kind, FailureKind::NotFound);
    PlaybackEventRecord noId = playbackEvent(QString(), QStringLiteral("2026-08-01T00:00:00Z"));
    QVERIFY(!outbox.enqueuePlaybackEvent(noId));
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountProfiles;"), 0);

    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e2"), QStringLiteral("2026-08-02T00:00:00Z"))));
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e1"), QStringLiteral("2026-08-01T00:00:00Z"))));
    PlaybackEventRecord repeat = playbackEvent(QStringLiteral("e1"), QStringLiteral("2026-09-09T00:00:00Z"));
    QVERIFY(outbox.enqueuePlaybackEvent(repeat));
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountProfiles WHERE AccountId='owner-a';"), 1);

    const QList<PlaybackEventRecord> loaded = outbox.loadPendingPlaybackEvents(kOwner);
    QCOMPARE(eventIds(loaded), (QStringList{QStringLiteral("e1"), QStringLiteral("e2")}));
    const PlaybackEventRecord& first = loaded.at(0);
    QCOMPARE(first.playedAtUtc, QStringLiteral("2026-08-01T00:00:00Z"));
    QCOMPARE(first.positionSeconds, 30.0);
    QCOMPARE(first.attemptCount, 0);
    QCOMPARE(first.nextAttemptAtUnix, 0);
    QVERIFY(!first.blocked);
    QVERIFY(first.track.has_value());
    QCOMPARE(first.track->remoteId, QStringLiteral("track-e1"));
    QCOMPARE(first.track->origin, QStringLiteral("account"));
    QVERIFY(first.trackJson.contains(QStringLiteral("\"id\":\"track-e1\"")));

    QCOMPARE(outbox.loadPendingPlaybackEvents(kOwner, 1).size(), 1);
    QVERIFY(outbox.loadPendingPlaybackEvents(kOwner, 0).isEmpty());
    QVERIFY(outbox.loadPendingPlaybackEvents(QStringLiteral("owner-b")).isEmpty());

    QVERIFY(outbox.markPlaybackEventsAttempted(kOwner, {QStringLiteral("e1"), QStringLiteral("e1")}));
    QCOMPARE(library.number("SELECT AttemptCount FROM PlaybackEvents WHERE EventId='e1';"), 2);
    QVERIFY(!library.text("SELECT LastAttemptAtUtc FROM PlaybackEvents WHERE EventId='e1';").isEmpty());
    QVERIFY(!outbox.markPlaybackEventsAttempted(QString(), {QStringLiteral("e1")}));
}

void AccountOutboxRepositoryTest::playbackFailuresScheduleRetries()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountOutboxRepository outbox(library.database());
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e1"), QStringLiteral("2026-08-01T00:00:00Z"))));
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e2"), QStringLiteral("2026-08-02T00:00:00Z"))));
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e3"), QStringLiteral("2026-08-03T00:00:00Z"))));
    QVERIFY(!outbox.secondsUntilNextAccountRetry(kOwner, AccountRetryQueue::History).has_value());

    QVERIFY(outbox.markPlaybackEventsFailed(kOwner, {QStringLiteral("e1")}, retryIn(3600)));
    QVERIFY(outbox.markPlaybackEventsFailed(kOwner, {QStringLiteral("e2")}, blockedMark()));
    // A negative delay counts as none, so the row is due at once.
    QVERIFY(outbox.markPlaybackEventsFailed(kOwner, {QStringLiteral("e3")}, retryIn(-50)));

    QCOMPARE(library.number("SELECT NextAttemptAtUnix-unixepoch() BETWEEN 3598 AND 3600 FROM PlaybackEvents WHERE EventId='e1';"), 1);
    QCOMPARE(library.text("SELECT NextAttemptAtUnix || '|' || IsBlocked || '|' || LastFailureCategory || '|' || AttemptCount "
                          "FROM PlaybackEvents WHERE EventId='e2';"),
             QStringLiteral("0|1|rejected|1"));
    QCOMPARE(eventIds(outbox.loadPendingPlaybackEvents(kOwner)), QStringList{QStringLiteral("e3")});
    const QList<PlaybackEventRecord> all = outbox.loadPendingPlaybackEvents(kOwner, 100, OutboxRows::All);
    QCOMPARE(all.size(), 3);
    QVERIFY(all.at(0).nextAttemptAtUnix > 0);
    QCOMPARE(all.at(0).lastFailureCategory, QStringLiteral("network"));
    QVERIFY(all.at(1).blocked);

    const std::optional<int> wait = outbox.secondsUntilNextAccountRetry(kOwner, AccountRetryQueue::History);
    QVERIFY(wait.has_value());
    QVERIFY(*wait >= 3598 && *wait <= 3600);
    QVERIFY(!outbox.secondsUntilNextAccountRetry(kOwner, AccountRetryQueue::Likes).has_value());
    QVERIFY(!outbox.secondsUntilNextAccountRetry(QString(), AccountRetryQueue::History).has_value());
}

void AccountOutboxRepositoryTest::acknowledgingRemovesOnlyListedEvents()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountOutboxRepository outbox(library.database());
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e1"), QStringLiteral("2026-08-01T00:00:00Z"))));
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e2"), QStringLiteral("2026-08-02T00:00:00Z"))));
    PlaybackEventRecord other = playbackEvent(QStringLiteral("e3"), QStringLiteral("2026-08-03T00:00:00Z"));
    other.accountId = QStringLiteral("owner-b");
    QVERIFY(outbox.enqueuePlaybackEvent(other));

    QVERIFY(outbox.acknowledgePlaybackEvents(kOwner, {QStringLiteral("e1"), QStringLiteral("e3"), QStringLiteral("none")}));
    QCOMPARE(library.text("SELECT group_concat(EventId) FROM (SELECT EventId FROM PlaybackEvents ORDER BY EventId);"),
             QStringLiteral("e2,e3"));
    QVERIFY(outbox.acknowledgePlaybackEvents(kOwner, {}));
    QVERIFY(!outbox.acknowledgePlaybackEvents(QString(), {QStringLiteral("e2")}));
}

void AccountOutboxRepositoryTest::likesKeepOneRequestPerSong()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountOutboxRepository outbox(library.database());

    PendingLikeRecord noSong = pendingLike(QStringLiteral("t1"), true, QStringLiteral("2026-08-01T00:00:00Z"));
    noSong.track.reset();
    QVERIFY(!outbox.enqueuePendingLike(noSong));

    QVERIFY(outbox.enqueuePendingLike(pendingLike(QStringLiteral("t1"), true, QStringLiteral("2026-08-01T00:00:00Z"))));
    QVERIFY(outbox.markPendingLikeAttempted(kOwner, QStringLiteral("t1")));
    QVERIFY(outbox.markPendingLikeFailed(kOwner, QStringLiteral("t1"), blockedMark()));
    QCOMPARE(library.text("SELECT AttemptCount || '|' || IsBlocked FROM PendingLikes;"), QStringLiteral("2|1"));
    QVERIFY(outbox.loadPendingLikes(kOwner).isEmpty());

    // A newer request replaces the waiting one and starts over.
    QVERIFY(outbox.enqueuePendingLike(pendingLike(QStringLiteral("t1"), false, QStringLiteral("2026-08-02T00:00:00Z"))));
    QCOMPARE(library.text("SELECT DesiredState || '|' || AttemptCount || '|' || COALESCE(LastAttemptAtUtc,'<null>') || '|' || "
                          "NextAttemptAtUnix || '|' || LastFailureCategory || '|' || IsBlocked FROM PendingLikes;"),
             QStringLiteral("0|0|<null>|0||0"));
    const QList<PendingLikeRecord> due = outbox.loadPendingLikes(kOwner);
    QCOMPARE(due.size(), 1);
    QVERIFY(!due.at(0).desiredState);
    QCOMPARE(due.at(0).updatedAtUtc, QStringLiteral("2026-08-02T00:00:00Z"));
    QCOMPARE(due.at(0).track->title, QStringLiteral("Song t1"));

    QVERIFY(outbox.markPendingLikeFailed(kOwner, QStringLiteral("t1"), retryIn(900)));
    QVERIFY(outbox.loadPendingLikes(kOwner).isEmpty());
    QCOMPARE(outbox.loadPendingLikes(kOwner, 10, OutboxRows::All).size(), 1);
    const std::optional<int> wait = outbox.secondsUntilNextAccountRetry(kOwner, AccountRetryQueue::Likes);
    QVERIFY(wait.has_value() && *wait >= 898 && *wait <= 900);

    // The reply to the older request leaves the newer one queued.
    QVERIFY(outbox.acknowledgePendingLike(kOwner, QStringLiteral("t1"), true));
    QCOMPARE(library.number("SELECT COUNT(*) FROM PendingLikes;"), 1);
    QVERIFY(outbox.acknowledgePendingLike(kOwner, QStringLiteral("t1"), false));
    QCOMPARE(library.number("SELECT COUNT(*) FROM PendingLikes;"), 0);
    QVERIFY(!outbox.acknowledgePendingLike(kOwner, QString(), false));
    QVERIFY(!outbox.markPendingLikeAttempted(QString(), QStringLiteral("t1")));
}

void AccountOutboxRepositoryTest::preferencesMergeAndAcknowledgeByRevision()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountOutboxRepository outbox(library.database());
    const QString theme = QStringLiteral("theme");

    QCOMPARE(outbox.savePendingAccountPreference(kOwner, theme, QStringLiteral("\"dark\"")).valueOr(0), 1);
    QCOMPARE(outbox.savePendingAccountPreference(kOwner, theme, QStringLiteral("\"light\"")).valueOr(0), 2);
    const auto refused = outbox.savePendingAccountPreference(kOwner, theme, QString());
    QVERIFY(!refused);
    QCOMPARE(refused.failure().kind, FailureKind::NotFound);

    // A pending local change wins over the service's value.
    QVERIFY(outbox.mergeRemoteAccountPreference(kOwner, theme, QStringLiteral("\"remote\""), 2));
    QCOMPARE(library.text("SELECT ValueJson FROM AccountPreferences;"), QStringLiteral("\"light\""));

    QVERIFY(outbox.acknowledgeAccountPreference(kOwner, theme, 1));
    QCOMPARE(library.number("SELECT Pending FROM AccountPreferences;"), 1);
    QVERIFY(outbox.acknowledgeAccountPreference(kOwner, theme, 2));
    QCOMPARE(library.number("SELECT Pending FROM AccountPreferences;"), 0);
    QVERIFY(!outbox.acknowledgeAccountPreference(kOwner, theme, 0));

    // Merges only against the revision that was observed.
    QVERIFY(outbox.mergeRemoteAccountPreference(kOwner, theme, QStringLiteral("\"stale\""), 1));
    QCOMPARE(library.text("SELECT ValueJson FROM AccountPreferences;"), QStringLiteral("\"light\""));
    QVERIFY(outbox.mergeRemoteAccountPreference(kOwner, theme, QStringLiteral("\"remote\""), 2));
    QCOMPARE(library.text("SELECT ValueJson || '|' || Revision FROM AccountPreferences;"), QStringLiteral("\"remote\"|2"));

    QVERIFY(outbox.mergeRemoteAccountPreference(kOwner, QStringLiteral("volume"), QStringLiteral("0.5"), 7));
    const auto records = outbox.loadAccountPreferences(kOwner);
    QCOMPARE(records.size(), 2);
    QCOMPARE(records.at(1).key, QStringLiteral("volume"));
    QCOMPARE(records.at(1).revision, 0);
    QVERIFY(!records.at(1).pending);
    QVERIFY(outbox.loadAccountPreferences(kOwner, OutboxRows::DueOnly).isEmpty());
    QVERIFY(!outbox.mergeRemoteAccountPreference(QString(), theme, QStringLiteral("1"), 0));
}

void AccountOutboxRepositoryTest::preferenceFailuresTouchPendingKeysOnly()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountOutboxRepository outbox(library.database());
    QVERIFY(outbox.savePendingAccountPreference(kOwner, QStringLiteral("a"), QStringLiteral("1")));
    QVERIFY(outbox.savePendingAccountPreference(kOwner, QStringLiteral("b"), QStringLiteral("2")));
    QVERIFY(outbox.mergeRemoteAccountPreference(kOwner, QStringLiteral("c"), QStringLiteral("3"), 0));

    QVERIFY(outbox.markAccountPreferencesFailed(kOwner, {QStringLiteral("a"), QStringLiteral("c")}, retryIn(120)));
    QCOMPARE(library.text("SELECT group_concat(Row) FROM (SELECT PreferenceKey || '=' || AttemptCount AS Row "
                          "FROM AccountPreferences ORDER BY PreferenceKey);"),
             QStringLiteral("a=1,b=0,c=0"));
    const auto due = outbox.loadAccountPreferences(kOwner, OutboxRows::DueOnly);
    QCOMPARE(due.size(), 1);
    QCOMPARE(due.at(0).key, QStringLiteral("b"));
    const std::optional<int> wait = outbox.secondsUntilNextAccountRetry(kOwner, AccountRetryQueue::Preferences);
    QVERIFY(wait.has_value() && *wait >= 118 && *wait <= 120);

    // Saving again starts the retries over.
    QCOMPARE(outbox.savePendingAccountPreference(kOwner, QStringLiteral("a"), QStringLiteral("9")).valueOr(0), 2);
    QCOMPARE(outbox.loadAccountPreferences(kOwner, OutboxRows::DueOnly).size(), 2);
}

void AccountOutboxRepositoryTest::pendingCountsSplitBlockedRows()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountOutboxRepository outbox(library.database());
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e1"), QStringLiteral("2026-08-01T00:00:00Z"))));
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e2"), QStringLiteral("2026-08-02T00:00:00Z"))));
    QVERIFY(outbox.markPlaybackEventsFailed(kOwner, {QStringLiteral("e2")}, blockedMark()));
    QVERIFY(outbox.enqueuePendingLike(pendingLike(QStringLiteral("t1"), true, QStringLiteral("2026-08-01T00:00:00Z"))));
    QVERIFY(outbox.enqueuePendingLike(pendingLike(QStringLiteral("t2"), true, QStringLiteral("2026-08-01T00:00:00Z"))));
    QVERIFY(outbox.markPendingLikeFailed(kOwner, QStringLiteral("t2"), blockedMark()));
    QVERIFY(outbox.savePendingAccountPreference(kOwner, QStringLiteral("a"), QStringLiteral("1")));
    QVERIFY(outbox.savePendingAccountPreference(kOwner, QStringLiteral("b"), QStringLiteral("1")));
    QVERIFY(outbox.markAccountPreferencesFailed(kOwner, {QStringLiteral("b")}, blockedMark()));
    QVERIFY(outbox.mergeRemoteAccountPreference(kOwner, QStringLiteral("c"), QStringLiteral("1"), 0));

    const AccountPendingCounts counts = outbox.loadAccountPendingCounts(kOwner);
    QCOMPARE(counts.pendingHistory, 1);
    QCOMPARE(counts.blockedHistory, 1);
    QCOMPARE(counts.pendingLikes, 1);
    QCOMPARE(counts.blockedLikes, 1);
    QCOMPARE(counts.pendingPreferences, 1);
    QCOMPARE(counts.blockedPreferences, 1);
    QCOMPARE(outbox.recoverablePendingChangeCount(kOwner), std::optional<std::size_t>(3));

    const AccountPendingCounts none = outbox.loadAccountPendingCounts(QString());
    QCOMPARE(none.pendingHistory + none.blockedLikes + none.pendingPreferences, 0);
    QCOMPARE(outbox.loadAccountPendingCounts(QStringLiteral("owner-b")).pendingHistory, 0);
}

void AccountOutboxRepositoryTest::resettingTheScheduleMakesRowsDue()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountOutboxRepository outbox(library.database());
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e1"), QStringLiteral("2026-08-01T00:00:00Z"))));
    QVERIFY(outbox.enqueuePlaybackEvent(playbackEvent(QStringLiteral("e2"), QStringLiteral("2026-08-02T00:00:00Z"))));
    QVERIFY(outbox.markPlaybackEventsFailed(kOwner, {QStringLiteral("e1")}, retryIn(3600)));
    QVERIFY(outbox.markPlaybackEventsFailed(kOwner, {QStringLiteral("e2")}, blockedMark()));
    QVERIFY(outbox.enqueuePendingLike(pendingLike(QStringLiteral("t1"), true, QStringLiteral("2026-08-01T00:00:00Z"))));
    QVERIFY(outbox.markPendingLikeFailed(kOwner, QStringLiteral("t1"), retryIn(3600)));
    QVERIFY(outbox.savePendingAccountPreference(kOwner, QStringLiteral("a"), QStringLiteral("1")));
    QVERIFY(outbox.markAccountPreferencesFailed(kOwner, {QStringLiteral("a")}, blockedMark()));

    QVERIFY(outbox.resetRecoverableAccountRetrySchedule(kOwner));
    QCOMPARE(eventIds(outbox.loadPendingPlaybackEvents(kOwner)), QStringList{QStringLiteral("e1")});
    QCOMPARE(outbox.loadPendingLikes(kOwner).size(), 1);
    QVERIFY(outbox.loadAccountPreferences(kOwner, OutboxRows::DueOnly).isEmpty());
    QCOMPARE(library.text("SELECT LastFailureCategory FROM PlaybackEvents WHERE EventId='e1';"), QString());
    QCOMPARE(library.text("SELECT LastFailureCategory FROM PlaybackEvents WHERE EventId='e2';"), QStringLiteral("rejected"));

    QVERIFY(outbox.resetAccountRetrySchedule(kOwner));
    QCOMPARE(outbox.loadPendingPlaybackEvents(kOwner).size(), 2);
    QCOMPARE(outbox.loadAccountPreferences(kOwner, OutboxRows::DueOnly).size(), 1);
    QCOMPARE(outbox.loadAccountPendingCounts(kOwner).blockedHistory, 0);
    // Attempt counts are history, not schedule.
    QCOMPARE(library.number("SELECT AttemptCount FROM PlaybackEvents WHERE EventId='e2';"), 1);

    QVERIFY(!outbox.resetAccountRetrySchedule(QString()));
    QVERIFY(!outbox.resetRecoverableAccountRetrySchedule(QString()));
}

QTEST_GUILESS_MAIN(AccountOutboxRepositoryTest)
#include "tst_account_outbox_repository.moc"
