#include <QCoreApplication>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPromise>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <algorithm>
#include <functional>
#include <memory>

#include "FakePlaybackClock.h"
#include "ModeServiceFixture.h"
#include "core/account/AccountLibrarySync.h"
#include "core/account/AccountSyncScheduler.h"
#include "core/account/AccountSyncTransport.h"
#include "core/account/MusicSyncService.h"
#include "core/account/PortablePreferenceSettings.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/settings/SettingKeys.h"
#include "platform/INetworkStatus.h"

using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::Track;
using lmp::core::account::AccountSessionState;
using lmp::core::account::MusicSyncService;
using lmp::core::account::ParsedAccountLibrary;
using lmp::core::account::PortablePreferences;
using lmp::core::db::PendingLikeRecord;
using lmp::core::db::PlaybackEventRecord;
using Lane = lmp::core::account::MusicSyncService::Lane;
namespace account = lmp::core::account;
namespace keys = lmp::core::settings::keys;

namespace {

const QString kOwner = QStringLiteral("owner-a");

void settle()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

template <typename T>
QFuture<Result<T>> answer(Result<T> result)
{
    return QtFuture::makeReadyValueFuture(std::move(result));
}

Failure httpFailure(int status)
{
    FailureKind kind = FailureKind::Server;
    if (status == 401) {
        kind = FailureKind::Unauthorized;
    } else if (status == 429) {
        kind = FailureKind::RateLimited;
    } else if (status < 500) {
        kind = FailureKind::InvalidResponse;
    }
    return Failure::of(kind, QStringLiteral("scripted"), status);
}

// Answers each call through a handler the test sets, and records the calls.
class FakeSyncTransport final : public account::AccountSyncTransport {
public:
    struct Call {
        QString kind;
        QString bearer;
        QByteArray body;
        bool liked = false;
        QString remoteId;
    };

    QFuture<Result<ParsedAccountLibrary>> fetchLibrary(const QString& bearer) override
    {
        calls.append({QStringLiteral("library"), bearer, {}, false, {}});
        return library ? library() : answer(Result<ParsedAccountLibrary>(account::parseAccountLibrary("{\"tracks\":[]}")));
    }
    QFuture<Result<void>> setLiked(const QString& bearer, const QByteArray& trackJson, bool liked,
                                   const QString& remoteId) override
    {
        calls.append({QStringLiteral("like"), bearer, trackJson, liked, remoteId});
        return like ? like(remoteId) : answer(Result<void>::success());
    }
    QFuture<Result<QStringList>> postHistoryBatch(const QString& bearer, const QByteArray& batchJson,
                                                  const QStringList& submitted) override
    {
        calls.append({QStringLiteral("history"), bearer, batchJson, false, {}});
        return history ? history(submitted) : answer(Result<QStringList>(submitted));
    }
    QFuture<Result<PortablePreferences>> fetchPreferences(const QString& bearer) override
    {
        calls.append({QStringLiteral("preferences-get"), bearer, {}, false, {}});
        return preferencesGet ? preferencesGet() : answer(Result<PortablePreferences>(PortablePreferences{}));
    }
    QFuture<Result<PortablePreferences>> putPreferences(const QString& bearer, const QByteArray& json) override
    {
        calls.append({QStringLiteral("preferences-put"), bearer, json, false, {}});
        return answer(Result<PortablePreferences>(account::parsePortablePreferences(json)));
    }

    [[nodiscard]] int count(const QString& kind) const
    {
        return static_cast<int>(std::count_if(calls.begin(), calls.end(), [&kind](const Call& call) { return call.kind == kind; }));
    }

    std::function<QFuture<Result<ParsedAccountLibrary>>()> library;
    std::function<QFuture<Result<void>>(const QString&)> like;
    std::function<QFuture<Result<QStringList>>(const QStringList&)> history;
    std::function<QFuture<Result<PortablePreferences>>()> preferencesGet;
    QList<Call> calls;
};

class FakeNetwork final : public lmp::platform::INetworkStatus {
public:
    [[nodiscard]] bool online() const override { return true; }
    [[nodiscard]] bool metered() const override { return false; }
    void setChangeHandler(ChangeHandler changeHandler) override { handler = std::move(changeHandler); }
    ChangeHandler handler;
};

Track accountSong(const QString& id)
{
    Track track;
    track.remoteId = id;
    track.title = QStringLiteral("Song ") + id;
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("account");
    track.sourceUrl = QStringLiteral("https://music.example.test/t/") + id;
    track.durationSeconds = 120;
    return track;
}

PlaybackEventRecord playEvent(const QString& id, const QString& song, const QString& playedAt)
{
    PlaybackEventRecord record;
    record.eventId = id;
    record.accountId = kOwner;
    record.remoteTrackId = song;
    record.track = accountSong(song);
    record.playedAtUtc = playedAt;
    record.positionSeconds = 30;
    return record;
}

PendingLikeRecord likeRecord(const QString& song, bool liked)
{
    PendingLikeRecord record;
    record.accountId = kOwner;
    record.remoteTrackId = song;
    record.track = accountSong(song);
    record.desiredState = liked;
    record.updatedAtUtc = QStringLiteral("2026-08-01T00:00:00Z");
    return record;
}

QByteArray libraryBody()
{
    return "{\"tracks\":["
           "{\"track\":{\"id\":\"s1\",\"title\":\"One\",\"artist\":\"A\",\"sourceKind\":\"remote\","
           "\"sourceUrl\":\"https://music.example.test/t/s1\",\"durationSec\":61},"
           "\"liked\":true,\"addedAt\":\"2026-08-01T00:00:00Z\",\"playCount\":3,"
           "\"lastPlayedAt\":\"2026-08-03T00:00:00Z\"},"
           "{\"id\":\"s2\",\"title\":\"Two\",\"sourceKind\":\"remote\","
           "\"sourceUrl\":\"https://music.example.test/t/s2\"}],"
           "\"playlists\":[{\"id\":\"p1\",\"name\":\"Mix\",\"tracks\":[\"s2\",\"s1\"]}]}";
}

struct SyncFixture {
    SyncFixture()
        : outbox(modes.library.database())
        , sync({.transport = transport,
                .session = modes.session.service,
                .mode = modes.modes,
                .accounts = modes.accounts,
                .outbox = outbox,
                .settings = modes.session.settings,
                .gate = modes.session.gate,
                .clock = clock,
                .nowUtc = [] { return QStringLiteral("2026-08-24T10:00:00Z"); }})
    {
    }

    [[nodiscard]] bool enterAccountMode(const QString& owner = kOwner)
    {
        (void)modes.modes.start();
        const bool ready = modes.validate(owner) && modes.modes.setMode(lmp::core::mode::SourceMode::Account);
        settle();
        return ready;
    }

    [[nodiscard]] std::optional<bool> run(QFuture<bool> future)
    {
        if (!QTest::qWaitFor([&future]() { return future.isFinished(); }, 3000)) {
            return std::nullopt;
        }
        return future.result();
    }

    lmp::test::ModeServiceFixture modes;
    lmp::core::db::AccountOutboxRepository outbox;
    FakeSyncTransport transport;
    FakePlaybackClock clock;
    MusicSyncService sync;
};

} // namespace

// Library sync: every lane, retries, blocking, stale results, the 401 path,
// preferences both ways, and when sync runs on its own.
class MusicSyncServiceTest : public QObject {
    Q_OBJECT

private slots:
    void snapshotLaysUnsentChangesOverTheLibrary();
    void libraryLaneStoresTheServiceLibrary();
    void historyIsSentAndAcknowledged();
    void refusedPlayIsFoundBySplittingTheBatch();
    void passingHistoryFailureRetriesLater();
    void likesAreSentOneByOne();
    void refusedLikeIsBlocked();
    void pulledPreferencesApplyAndLocalChangesWin();
    void localPreferenceChangesAreSent();
    void unauthorizedEndsTheSyncAndSignsOut();
    void staleAnswersAreDropped();
    void staleUnauthorizedKeepsTheNewSession();
    void requestsShareTheRunningSync();
    void needsAValidatedSession();
    void schedulerRunsAtSignInAndOnItsTriggers();
    void offlineSessionIsCheckedAgainUntilItAnswers();
};

void MusicSyncServiceTest::snapshotLaysUnsentChangesOverTheLibrary()
{
    const ParsedAccountLibrary parsed = account::parseAccountLibrary(libraryBody());
    account::AccountLibraryInputs inputs;
    inputs.ownerId = kOwner;
    inputs.profile.displayName = QStringLiteral("Name");
    inputs.nowUtc = QStringLiteral("2026-08-24T10:00:00Z");
    lmp::core::db::AccountLibrarySnapshot existing;
    lmp::core::db::AccountTrackSnapshot cached;
    cached.track = accountSong(QStringLiteral("s2"));
    cached.playCount = 9;
    cached.lastPlayedAtUtc = QStringLiteral("2026-08-05T00:00:00Z");
    cached.lastPositionSeconds = 44;
    existing.tracks.append(cached);
    inputs.existing = existing;
    inputs.pendingLikes = {likeRecord(QStringLiteral("s1"), false), likeRecord(QStringLiteral("new"), true)};
    inputs.pendingEvents = {playEvent(QStringLiteral("e1"), QStringLiteral("s2"), QStringLiteral("2026-08-04T00:00:00Z"))};

    const lmp::core::db::AccountLibrarySnapshot snapshot = account::buildAccountLibrarySnapshot(parsed, inputs);
    QCOMPARE(snapshot.profile.accountId, kOwner);
    QCOMPARE(snapshot.synchronizedAtUtc, QStringLiteral("2026-08-24T10:00:00Z"));
    QCOMPARE(snapshot.tracks.size(), 3);
    const auto& s1 = snapshot.tracks.at(0);
    QCOMPARE(s1.track.title, QStringLiteral("One"));
    QCOMPARE(s1.track.origin, QStringLiteral("account"));
    QCOMPARE(s1.track.durationText, QStringLiteral("1:01"));
    QVERIFY(!s1.track.liked); // the queued unlike wins
    QCOMPARE(s1.playCount, 3);
    QCOMPARE(s1.track.dateAddedSortKey, 0.0);
    const auto& s2 = snapshot.tracks.at(1);
    QCOMPARE(s2.track.dateAddedSortKey, -1.0); // no date added: kept in order
    QCOMPARE(s2.playCount, 9);                 // the cached count was higher
    QCOMPARE(s2.lastPlayedAtUtc, QStringLiteral("2026-08-05T00:00:00Z"));
    QCOMPARE(s2.lastPositionSeconds, 44.0);
    const auto& added = snapshot.tracks.at(2);
    QCOMPARE(added.track.remoteId, QStringLiteral("new"));
    QVERIFY(added.track.liked);
    QCOMPARE(snapshot.playlists.size(), 1);
    QCOMPARE(snapshot.playlists.first().remoteTrackIds, (QStringList{QStringLiteral("s2"), QStringLiteral("s1")}));

    // The stored song travels back to the service unchanged.
    const account::PortableAccountTrack portable = account::portableFromTrack(s1.track);
    QCOMPARE(portable.id, QStringLiteral("s1"));
    QVERIFY(portable.provider.isEmpty());
    QVERIFY(account::isValidPortableTrack(portable));
}

void MusicSyncServiceTest::libraryLaneStoresTheServiceLibrary()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    fixture.transport.library = [] {
        return answer(Result<ParsedAccountLibrary>(account::parseAccountLibrary(libraryBody())));
    };
    QSignalSpy changed(&fixture.sync, &MusicSyncService::libraryChanged);
    QCOMPARE(fixture.run(fixture.sync.sync()), std::optional<bool>(true));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(fixture.sync.libraryRevision(), 1U);
    const std::optional<lmp::core::db::AccountLibrarySnapshot> stored = fixture.modes.accounts.loadAccountLibrary(kOwner);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->tracks.size(), 2);
    QCOMPARE(stored->playlists.size(), 1);
    for (const FakeSyncTransport::Call& call : fixture.transport.calls) {
        QCOMPARE(call.bearer, QStringLiteral("bearer-owner-a"));
    }
    QVERIFY(fixture.sync.lastSafeError().isEmpty());
    QVERIFY(!fixture.sync.isSyncing());
}

void MusicSyncServiceTest::historyIsSentAndAcknowledged()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    for (int index = 1; index <= 3; ++index) {
        QVERIFY(fixture.outbox.enqueuePlaybackEvent(
            playEvent(QStringLiteral("e%1").arg(index), QStringLiteral("s1"), QStringLiteral("2026-08-0%1T00:00:00Z").arg(index))));
    }
    QCOMPARE(fixture.run(fixture.sync.requestSync(Lane::History)), std::optional<bool>(true));
    QCOMPARE(fixture.transport.count(QStringLiteral("history")), 1);
    const QJsonArray events =
        QJsonDocument::fromJson(fixture.transport.calls.last().body).object().value(QLatin1StringView("events")).toArray();
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.first().toObject().value(QLatin1StringView("positionSec")).toDouble(), 30.0);
    QVERIFY(fixture.outbox.loadPendingPlaybackEvents(kOwner, 100, lmp::core::db::OutboxRows::All).isEmpty());
}

void MusicSyncServiceTest::refusedPlayIsFoundBySplittingTheBatch()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    for (int index = 1; index <= 4; ++index) {
        QVERIFY(fixture.outbox.enqueuePlaybackEvent(
            playEvent(QStringLiteral("e%1").arg(index), QStringLiteral("s1"), QStringLiteral("2026-08-0%1T00:00:00Z").arg(index))));
    }
    // The service refuses any batch that holds e3.
    fixture.transport.history = [](const QStringList& submitted) {
        if (submitted.contains(QStringLiteral("e3"))) {
            return answer(Result<QStringList>(httpFailure(400)));
        }
        return answer(Result<QStringList>(submitted));
    };
    QCOMPARE(fixture.run(fixture.sync.requestSync(Lane::History)), std::optional<bool>(false));
    const QList<PlaybackEventRecord> left = fixture.outbox.loadPendingPlaybackEvents(kOwner, 100, lmp::core::db::OutboxRows::All);
    QCOMPARE(left.size(), 1);
    QCOMPARE(left.first().eventId, QStringLiteral("e3"));
    QVERIFY(left.first().blocked);
    QCOMPARE(fixture.outbox.loadAccountPendingCounts(kOwner).blockedHistory, 1);
    QVERIFY(!fixture.sync.lastSafeError().isEmpty());
}

void MusicSyncServiceTest::passingHistoryFailureRetriesLater()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    QVERIFY(fixture.outbox.enqueuePlaybackEvent(playEvent(QStringLiteral("e1"), QStringLiteral("s1"), QStringLiteral("2026-08-01T00:00:00Z"))));
    bool fail = true;
    fixture.transport.history = [&fail](const QStringList& submitted) {
        return fail ? answer(Result<QStringList>(httpFailure(503))) : answer(Result<QStringList>(submitted));
    };
    QCOMPARE(fixture.run(fixture.sync.requestSync(Lane::History)), std::optional<bool>(false));
    QCOMPARE(fixture.transport.count(QStringLiteral("history")), 1);
    const QList<PlaybackEventRecord> waiting = fixture.outbox.loadPendingPlaybackEvents(kOwner, 100, lmp::core::db::OutboxRows::All);
    QCOMPARE(waiting.size(), 1);
    QVERIFY(!waiting.first().blocked);
    QCOMPARE(waiting.first().lastFailureCategory, QStringLiteral("transient"));

    // The lane retries on its own after the first retry delay (5 s); the row
    // becomes due again only when its own time comes, so make it due now.
    fail = false;
    QVERIFY(fixture.outbox.resetRecoverableAccountRetrySchedule(kOwner));
    fixture.clock.advance(5000);
    settle();
    QTRY_COMPARE(fixture.transport.count(QStringLiteral("history")), 2);
    QTRY_VERIFY(fixture.outbox.loadPendingPlaybackEvents(kOwner, 100, lmp::core::db::OutboxRows::All).isEmpty());
}

void MusicSyncServiceTest::likesAreSentOneByOne()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    QVERIFY(fixture.outbox.enqueuePendingLike(likeRecord(QStringLiteral("s1"), true)));
    QVERIFY(fixture.outbox.enqueuePendingLike(likeRecord(QStringLiteral("s2"), false)));
    QCOMPARE(fixture.run(fixture.sync.requestSync(Lane::Likes)), std::optional<bool>(true));
    QCOMPARE(fixture.transport.count(QStringLiteral("like")), 2);
    const FakeSyncTransport::Call& first = fixture.transport.calls.at(fixture.transport.calls.size() - 2);
    QVERIFY(first.liked);
    QCOMPARE(first.remoteId, QStringLiteral("s1"));
    QCOMPARE(QJsonDocument::fromJson(first.body).object().value(QLatin1StringView("id")).toString(), QStringLiteral("s1"));
    QVERIFY(!fixture.transport.calls.last().liked);
    QVERIFY(fixture.outbox.loadPendingLikes(kOwner, 100, lmp::core::db::OutboxRows::All).isEmpty());
}

void MusicSyncServiceTest::refusedLikeIsBlocked()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    QVERIFY(fixture.outbox.enqueuePendingLike(likeRecord(QStringLiteral("s1"), true)));
    QVERIFY(fixture.outbox.enqueuePendingLike(likeRecord(QStringLiteral("s2"), true)));
    fixture.transport.like = [](const QString& id) {
        return id == QStringLiteral("s1") ? answer(Result<void>(httpFailure(422))) : answer(Result<void>::success());
    };
    QCOMPARE(fixture.run(fixture.sync.requestSync(Lane::Likes)), std::optional<bool>(false));
    const QList<PendingLikeRecord> left = fixture.outbox.loadPendingLikes(kOwner, 100, lmp::core::db::OutboxRows::All);
    QCOMPARE(left.size(), 1);
    QVERIFY(left.first().blocked);
    QCOMPARE(left.first().lastFailureCategory, QStringLiteral("rejected"));
    QCOMPARE(fixture.transport.count(QStringLiteral("like")), 2);
}

void MusicSyncServiceTest::pulledPreferencesApplyAndLocalChangesWin()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    lmp::core::SettingsStore& settings = fixture.modes.session.settings;
    PortablePreferences remote;
    remote.theme = QStringLiteral("dark");
    remote.accent = QStringLiteral("#112233");
    remote.dataSaver = true;
    fixture.transport.preferencesGet = [remote] { return answer(Result<PortablePreferences>(remote)); };
    QSignalSpy applied(&fixture.sync, &MusicSyncService::preferencesChanged);
    QCOMPARE(fixture.run(fixture.sync.requestSync(Lane::PreferencePull)), std::optional<bool>(true));
    QCOMPARE(applied.count(), 1);
    QCOMPARE(settings.string(keys::Theme), QStringLiteral("Dark"));
    QCOMPARE(settings.string(keys::AccentColor), QStringLiteral("#FF112233"));
    QVERIFY(settings.boolean(keys::DataSaver, false));
    settle();
    fixture.clock.advance(1000);
    settle();
    // Applying them did not queue them to go back up; the keys the service
    // lacked (gapless, region) were sent once from the local settings.
    QTRY_COMPARE(fixture.transport.count(QStringLiteral("preferences-put")), 1);
    const QJsonObject sent = QJsonDocument::fromJson(fixture.transport.calls.last().body).object();
    QVERIFY(!sent.contains(QLatin1StringView("theme")));
    QCOMPARE(sent.value(QLatin1StringView("gaplessPlayback")).toBool(), true);
    QCOMPARE(sent.value(QLatin1StringView("storefront")).toString(), QStringLiteral("in"));

    // A change made here and not yet sent wins over the service.
    QVERIFY(fixture.sync.savePortablePreference(account::PortablePreferenceKey::Theme, QStringLiteral("\"light\"")));
    QCOMPARE(fixture.run(fixture.sync.requestSync(Lane::PreferencePull)), std::optional<bool>(true));
    QCOMPARE(settings.string(keys::Theme), QStringLiteral("Dark")); // not overwritten by "dark" either way
    QCOMPARE(fixture.outbox.loadAccountPendingCounts(kOwner).pendingPreferences, 1);
}

void MusicSyncServiceTest::localPreferenceChangesAreSent()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    fixture.modes.session.settings.setString(keys::Theme, QStringLiteral("Light"));
    QCOMPARE(fixture.outbox.loadAccountPendingCounts(kOwner).pendingPreferences, 1);
    fixture.clock.advance(1000); // the change waits a second for others
    settle();
    QTRY_COMPARE(fixture.transport.count(QStringLiteral("preferences-put")), 1);
    QCOMPARE(QJsonDocument::fromJson(fixture.transport.calls.last().body).object().value(QLatin1StringView("theme")).toString(),
             QStringLiteral("light"));
    QTRY_COMPARE(fixture.outbox.loadAccountPendingCounts(kOwner).pendingPreferences, 0);
    // Not a portable preference, or not in Account mode: nothing is queued.
    fixture.modes.session.settings.setBoolean(keys::Autoplay, false);
    QCOMPARE(fixture.outbox.loadAccountPendingCounts(kOwner).pendingPreferences, 0);
}

void MusicSyncServiceTest::unauthorizedEndsTheSyncAndSignsOut()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    fixture.transport.library = [] { return answer(Result<ParsedAccountLibrary>(httpFailure(401))); };
    QCOMPARE(fixture.run(fixture.sync.requestSync(Lane::Library)), std::optional<bool>(false));
    QVERIFY(fixture.sync.lastSafeError().contains(QStringLiteral("expired")));
    QTRY_COMPARE(fixture.modes.session.service.state(), AccountSessionState::SignedOut);
}

void MusicSyncServiceTest::staleAnswersAreDropped()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    auto promise = std::make_shared<QPromise<Result<ParsedAccountLibrary>>>();
    fixture.transport.library = [promise] {
        promise->start();
        return promise->future();
    };
    QFuture<bool> running = fixture.sync.requestSync(Lane::Library);
    settle();
    QCOMPARE(fixture.transport.count(QStringLiteral("library")), 1);
    // Away from Account mode and back: the answer belongs to an old scope.
    QVERIFY(fixture.modes.modes.setMode(lmp::core::mode::SourceMode::LocalOnly));
    QVERIFY(fixture.modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    promise->addResult(Result<ParsedAccountLibrary>(account::parseAccountLibrary(libraryBody())));
    promise->finish();
    QCOMPARE(fixture.run(running), std::optional<bool>(false));
    const std::optional<lmp::core::db::AccountLibrarySnapshot> stored = fixture.modes.accounts.loadAccountLibrary(kOwner);
    QVERIFY(!stored || stored->tracks.isEmpty());
}

void MusicSyncServiceTest::staleUnauthorizedKeepsTheNewSession()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    auto promise = std::make_shared<QPromise<Result<ParsedAccountLibrary>>>();
    fixture.transport.library = [promise] {
        promise->start();
        return promise->future();
    };
    QFuture<bool> running = fixture.sync.requestSync(Lane::Library);
    settle();
    // Another account signs in while the request is out.
    QVERIFY(fixture.modes.validate(QStringLiteral("owner-b")));
    promise->addResult(Result<ParsedAccountLibrary>(httpFailure(401)));
    promise->finish();
    QCOMPARE(fixture.run(running), std::optional<bool>(false));
    settle();
    QCOMPARE(fixture.modes.session.service.state(), AccountSessionState::Validated);
    QCOMPARE(fixture.modes.session.service.profile().id, QStringLiteral("owner-b"));
    QCOMPARE(fixture.modes.session.storedSession(), QStringLiteral("bearer-owner-b"));
}

void MusicSyncServiceTest::requestsShareTheRunningSync()
{
    SyncFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    auto promise = std::make_shared<QPromise<Result<ParsedAccountLibrary>>>();
    fixture.transport.library = [promise] {
        promise->start();
        return promise->future();
    };
    QSignalSpy syncing(&fixture.sync, &MusicSyncService::syncingChanged);
    QFuture<bool> first = fixture.sync.requestSync(Lane::Library);
    QFuture<bool> second = fixture.sync.requestSync(Lane::Library);
    settle();
    QVERIFY(fixture.sync.isSyncing());
    QCOMPARE(fixture.transport.count(QStringLiteral("library")), 1);
    fixture.transport.library = [] {
        return answer(Result<ParsedAccountLibrary>(account::parseAccountLibrary(libraryBody())));
    };
    promise->addResult(Result<ParsedAccountLibrary>(account::parseAccountLibrary(libraryBody())));
    promise->finish();
    QCOMPARE(fixture.run(first), std::optional<bool>(true));
    QCOMPARE(fixture.run(second), std::optional<bool>(true));
    // The second request's lane ran once more inside the same sync.
    QCOMPARE(fixture.transport.count(QStringLiteral("library")), 2);
    QCOMPARE(syncing.count(), 2);
}

void MusicSyncServiceTest::needsAValidatedSession()
{
    SyncFixture fixture;
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.restoreOffline(kOwner));
    QVERIFY(fixture.modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    QCOMPARE(fixture.run(fixture.sync.requestSync(Lane::All)), std::optional<bool>(false));
    QVERIFY(fixture.sync.lastSafeError().startsWith(QStringLiteral("Offline")));
    QVERIFY(fixture.transport.calls.isEmpty());
    // An offline session still keeps preference changes for later.
    QVERIFY(fixture.sync.savePortablePreference(account::PortablePreferenceKey::DataSaver, QStringLiteral("true")));
    QVERIFY(!fixture.sync.savePortablePreference(account::PortablePreferenceKey::DataSaver, QStringLiteral("\"x\"")));
}

void MusicSyncServiceTest::schedulerRunsAtSignInAndOnItsTriggers()
{
    SyncFixture fixture;
    FakeNetwork network;
    account::AccountSyncScheduler scheduler({.sync = fixture.sync,
                                             .session = fixture.modes.session.service,
                                             .mode = fixture.modes.modes,
                                             .network = network,
                                             .clock = fixture.clock});
    QVERIFY(fixture.enterAccountMode());
    QTRY_COMPARE(fixture.transport.count(QStringLiteral("library")), 1); // signing in synced
    QTRY_VERIFY(!fixture.sync.isSyncing());

    // Coming back to the window: not within 30 s of the last attempt.
    scheduler.windowActivated();
    settle();
    QCOMPARE(fixture.transport.count(QStringLiteral("library")), 1);
    fixture.clock.advance(30000);
    scheduler.windowActivated();
    QTRY_COMPARE(fixture.transport.count(QStringLiteral("library")), 2);

    // The poll every five minutes.
    fixture.clock.advance(5 * 60 * 1000);
    QTRY_COMPARE(fixture.transport.count(QStringLiteral("library")), 3);

    // The network coming back.
    QVERIFY(network.handler);
    network.handler(true, false);
    QTRY_COMPARE(fixture.transport.count(QStringLiteral("library")), 4);
    network.handler(false, false);
    settle();
    QCOMPARE(fixture.transport.count(QStringLiteral("library")), 4);
}

void MusicSyncServiceTest::offlineSessionIsCheckedAgainUntilItAnswers()
{
    SyncFixture fixture;
    FakeNetwork network;
    account::AccountSyncScheduler scheduler({.sync = fixture.sync,
                                             .session = fixture.modes.session.service,
                                             .mode = fixture.modes.modes,
                                             .network = network,
                                             .clock = fixture.clock});
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.restoreOffline(kOwner));
    QVERIFY(fixture.modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    const auto profileChecks = [&fixture]() {
        return std::count_if(fixture.modes.session.transport.requests.cbegin(),
                             fixture.modes.session.transport.requests.cend(),
                             [](const lmp::core::net::HttpRequest& request) { return request.url.path() == lmp::test::kProfilePath; });
    };
    const auto before = profileChecks();

    // Still unreachable after 15 s: checked once, still offline.
    fixture.modes.session.transport.script(lmp::test::kProfilePath, lmp::test::FakeHttpTransport::failed(lmp::core::FailureKind::Network));
    fixture.clock.advance(14000);
    settle();
    QCOMPARE(profileChecks(), before);
    fixture.clock.advance(1000);
    QTRY_COMPARE(profileChecks(), before + 1);
    QVERIFY(fixture.modes.waitForSession(account::AccountSessionState::Offline));

    // The service answers on the next check: the session is confirmed and
    // the library syncs.
    fixture.modes.session.transport.script(lmp::test::kProfilePath, lmp::test::AccountSessionFixture::profileAnswer(kOwner));
    fixture.clock.advance(15000);
    QVERIFY(fixture.modes.waitForSession(account::AccountSessionState::Validated));
    QTRY_COMPARE(fixture.transport.count(QStringLiteral("library")), 1);

    // Once confirmed it is not checked every 15 s any more.
    const auto confirmed = profileChecks();
    fixture.clock.advance(15000);
    settle();
    QCOMPARE(profileChecks(), confirmed);
}

QTEST_GUILESS_MAIN(MusicSyncServiceTest)
#include "tst_music_sync_service.moc"
