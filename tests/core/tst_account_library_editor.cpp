#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include "FakePlaybackClock.h"
#include "ModeServiceFixture.h"
#include "core/account/AccountLibraryEditor.h"
#include "core/account/MusicSyncService.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/db/LibraryQueries.h"

using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::Track;
using lmp::core::account::AccountLibraryEditor;
using lmp::core::account::MusicSyncService;
using lmp::test::FakeHttpTransport;

namespace {

const QString kOwner = QStringLiteral("owner-a");

void settle()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

Track accountSong(const QString& id, const QString& title = QStringLiteral("Song"))
{
    Track track;
    track.remoteId = id;
    track.title = title;
    track.artist = QStringLiteral("Mira Sol");
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("account");
    track.sourceUrl = QStringLiteral("https://music.example.test/t/") + id;
    track.durationSeconds = 100;
    return track;
}

template <typename T>
[[nodiscard]] std::optional<Result<T>> outcome(QFuture<Result<T>> future)
{
    if (!QTest::qWaitFor([&future]() { return future.isFinished(); }, 3000)) {
        return std::nullopt;
    }
    return future.result();
}

struct EditorFixture {
    EditorFixture()
        : outbox(modes.library.database())
        , sync({.transport = modes.session.client,
                .session = modes.session.service,
                .mode = modes.modes,
                .accounts = modes.accounts,
                .outbox = outbox,
                .settings = modes.session.settings,
                .gate = modes.session.gate,
                .clock = clock,
                .nowUtc = [] { return QStringLiteral("2026-08-24T10:00:00Z"); }})
        , editor({.client = modes.session.client,
                  .session = modes.session.service,
                  .mode = modes.modes,
                  .accounts = modes.accounts,
                  .outbox = outbox,
                  .sync = sync,
                  .gate = modes.session.gate,
                  .nowUtc = [] { return QStringLiteral("2026-08-24T10:00:00Z"); },
                  .newEventId = [this] { return QStringLiteral("{EVENT-%1}").arg(++events); }})
    {
    }

    [[nodiscard]] bool enterAccountMode()
    {
        (void)modes.modes.start();
        const bool ready = modes.validate(kOwner) && modes.modes.setMode(lmp::core::mode::SourceMode::Account);
        settle();
        // An empty library answers every sync the edits start.
        modes.session.transport.script(QStringLiteral("/v1/music/library"), FakeHttpTransport::ok("{\"tracks\":[]}"));
        return ready;
    }

    [[nodiscard]] FakeHttpTransport& http() { return modes.session.transport; }

    // The last request sent to `path`.
    [[nodiscard]] lmp::core::net::HttpRequest lastTo(const QString& path) const
    {
        for (auto it = modes.session.transport.requests.crbegin(); it != modes.session.transport.requests.crend(); ++it) {
            if (it->url.path() == path) {
                return *it;
            }
        }
        return {};
    }

    lmp::test::ModeServiceFixture modes;
    lmp::core::db::AccountOutboxRepository outbox;
    FakePlaybackClock clock;
    MusicSyncService sync;
    int events = 0;
    AccountLibraryEditor editor;
};

} // namespace

// Account-mode edits: likes and plays through the outbox, playlist edits on
// the service, and account playlists in the library queries.
class AccountLibraryEditorTest : public QObject {
    Q_OBJECT

private slots:
    void onlyAccountSongsInAccountMode();
    void likeIsStoredQueuedAndSent();
    void unlikeTakesEveryVersionOfTheSong();
    void likeIsRefusedWhileDataIsCleared();
    void playIsQueuedForTheHistoryLane();
    void playlistIsCreatedOnTheService();
    void playlistEditsUseTheirCalls();
    void refusedEditIsReported();
    void accountPlaylistPagesReadTheActiveAccount();
};

void AccountLibraryEditorTest::onlyAccountSongsInAccountMode()
{
    EditorFixture fixture;
    (void)fixture.modes.modes.start();
    QVERIFY(!fixture.editor.isActive());
    QVERIFY(!fixture.editor.handles(accountSong(QStringLiteral("s1"))));
    QVERIFY(fixture.enterAccountMode());
    QVERIFY(fixture.editor.isActive());
    QVERIFY(fixture.editor.handles(accountSong(QStringLiteral("s1"))));
    Track local;
    local.filePath = QStringLiteral("C:/Music/a.mp3");
    local.sourceKind = QStringLiteral("local");
    QVERIFY(!fixture.editor.handles(local));
    Track noId = accountSong(QString());
    QVERIFY(!fixture.editor.handles(noId));
    QVERIFY(AccountLibraryEditor::isAccountPlaylistKey(QStringLiteral("account-playlist|p1")));
    QVERIFY(!AccountLibraryEditor::isAccountPlaylistKey(QStringLiteral("account-playlist|")));
    QVERIFY(!AccountLibraryEditor::isAccountPlaylistKey(QStringLiteral("playlist|1")));
}

void AccountLibraryEditorTest::likeIsStoredQueuedAndSent()
{
    EditorFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    const Track song = accountSong(QStringLiteral("s1"));
    QVERIFY(!fixture.editor.isLiked(song));
    QCOMPARE(fixture.editor.setLiked(song, true), std::optional<bool>(true));
    QVERIFY(fixture.editor.isLiked(song));
    QCOMPARE(fixture.outbox.loadAccountPendingCounts(kOwner).pendingLikes, 1);

    fixture.http().script(QStringLiteral("/v1/music/liked"), FakeHttpTransport::ok("{}"));
    fixture.clock.advance(MusicSyncService::MutationDelay.count());
    QTRY_COMPARE(fixture.http().requestCount(QStringLiteral("/v1/music/liked")), 1);
    QTRY_COMPARE(fixture.outbox.loadAccountPendingCounts(kOwner).pendingLikes, 0);
    const QJsonObject body = QJsonDocument::fromJson(fixture.http().requests.last().body).object();
    QCOMPARE(body.value(QLatin1StringView("track")).toObject().value(QLatin1StringView("id")).toString(),
             QStringLiteral("s1"));
}

void AccountLibraryEditorTest::unlikeTakesEveryVersionOfTheSong()
{
    EditorFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    const Track original = accountSong(QStringLiteral("s1"), QStringLiteral("Blue Sky"));
    const Track live = accountSong(QStringLiteral("s2"), QStringLiteral("Blue Sky")); // the same song from another source
    const Track other = accountSong(QStringLiteral("s3"), QStringLiteral("Green Grass"));
    for (const Track& song : {original, live, other}) {
        QCOMPARE(fixture.editor.setLiked(song, true), std::optional<bool>(true));
    }
    QCOMPARE(fixture.editor.setLiked(original, false), std::optional<bool>(false));
    QVERIFY(!fixture.editor.isLiked(original));
    QVERIFY(!fixture.editor.isLiked(live));  // another version of the same song
    QVERIFY(fixture.editor.isLiked(other));
    const QList<lmp::core::db::PendingLikeRecord> queued =
        fixture.outbox.loadPendingLikes(kOwner, 100, lmp::core::db::OutboxRows::All);
    int unlikes = 0;
    for (const lmp::core::db::PendingLikeRecord& like : queued) {
        unlikes += like.desiredState ? 0 : 1;
    }
    QCOMPARE(unlikes, 2);
}

void AccountLibraryEditorTest::likeIsRefusedWhileDataIsCleared()
{
    EditorFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    QVERIFY(fixture.modes.session.gate.closeAdmissions());
    QVERIFY(!fixture.editor.setLiked(accountSong(QStringLiteral("s1")), true).has_value());
    fixture.modes.session.gate.reopen();
    QCOMPARE(fixture.outbox.loadAccountPendingCounts(kOwner).pendingLikes, 0);
}

void AccountLibraryEditorTest::playIsQueuedForTheHistoryLane()
{
    EditorFixture fixture;
    (void)fixture.modes.modes.start();
    // Outside Account mode the play is not the account's.
    QVERIFY(!fixture.editor.recordAccountPlay(accountSong(QStringLiteral("s1")), 12));
    QVERIFY(fixture.enterAccountMode());
    QVERIFY(fixture.editor.recordAccountPlay(accountSong(QStringLiteral("s1")), 42.5));
    const QList<lmp::core::db::PlaybackEventRecord> events =
        fixture.outbox.loadPendingPlaybackEvents(kOwner, 100, lmp::core::db::OutboxRows::All);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().eventId, QStringLiteral("{EVENT-1}"));
    QCOMPARE(events.first().playedAtUtc, QStringLiteral("2026-08-24T10:00:00Z"));
    QCOMPARE(events.first().positionSeconds, 42.5);
    QCOMPARE(fixture.modes.library.number("SELECT PlayCount FROM AccountTracks WHERE RemoteId='s1';"), 1);

    fixture.http().script(QStringLiteral("/v1/music/history/batch"),
                          FakeHttpTransport::ok("{\"accepted\":[\"{EVENT-1}\"]}"));
    fixture.clock.advance(MusicSyncService::MutationDelay.count());
    QTRY_COMPARE(fixture.http().requestCount(QStringLiteral("/v1/music/history/batch")), 1);
    QTRY_VERIFY(fixture.outbox.loadPendingPlaybackEvents(kOwner, 100, lmp::core::db::OutboxRows::All).isEmpty());
}

void AccountLibraryEditorTest::playlistIsCreatedOnTheService()
{
    EditorFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    fixture.http().script(QStringLiteral("/v1/music/playlists"),
                          FakeHttpTransport::ok("{\"playlist\":{\"id\":\"p9\",\"name\":\"Mix\"}}", 201));
    QSignalSpy changed(&fixture.editor, &AccountLibraryEditor::playlistsChanged);
    const std::optional<Result<QString>> created =
        outcome(fixture.editor.createPlaylist(QStringLiteral(" Mix "), {accountSong(QStringLiteral("s1"))}));
    QVERIFY(created && created->isOk());
    QCOMPARE(created->value(), QStringLiteral("account-playlist|p9"));
    const QJsonObject body = QJsonDocument::fromJson(fixture.lastTo(QStringLiteral("/v1/music/playlists")).body).object();
    QCOMPARE(body.value(QLatin1StringView("name")).toString(), QStringLiteral("Mix"));
    QCOMPARE(body.value(QLatin1StringView("tracks")).toArray().size(), 1);
    // The library follows.
    QTRY_COMPARE(changed.count(), 1);
    QVERIFY(fixture.http().requestCount(QStringLiteral("/v1/music/library")) >= 1);
}

void AccountLibraryEditorTest::playlistEditsUseTheirCalls()
{
    EditorFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    const QString key = QStringLiteral("account-playlist|p 1");
    FakeHttpTransport& http = fixture.http();
    http.script(QStringLiteral("/v1/music/playlists/p 1"), FakeHttpTransport::ok("{}"));
    QVERIFY(outcome(fixture.editor.renamePlaylist(key, QStringLiteral("New")))->isOk());
    QCOMPARE(fixture.lastTo(QStringLiteral("/v1/music/playlists/p 1")).method, lmp::core::net::HttpMethod::Patch);

    http.script(QStringLiteral("/v1/music/playlists/p 1/tracks"), FakeHttpTransport::ok("{}"));
    QVERIFY(outcome(fixture.editor.addToPlaylist(key, accountSong(QStringLiteral("s1"))))->isOk());

    http.script(QStringLiteral("/v1/music/playlists/p 1/tracks/s1"), FakeHttpTransport::ok("{}"));
    http.script(QStringLiteral("/v1/music/playlists/p 1/tracks/s2"), FakeHttpTransport::ok("{}"));
    QVERIFY(outcome(fixture.editor.removeFromPlaylist(key, {accountSong(QStringLiteral("s1")), accountSong(QStringLiteral("s2"))}))
                ->isOk());
    QCOMPARE(http.requestCount(QStringLiteral("/v1/music/playlists/p 1/tracks/s1")), 1);
    QCOMPARE(http.requestCount(QStringLiteral("/v1/music/playlists/p 1/tracks/s2")), 1);

    http.script(QStringLiteral("/v1/music/playlists/p 1"), FakeHttpTransport::ok("{}"));
    QVERIFY(outcome(fixture.editor.replacePlaylist(key, {accountSong(QStringLiteral("s2")), accountSong(QStringLiteral("s1"))}))
                ->isOk());
    const QJsonObject order = QJsonDocument::fromJson(fixture.lastTo(QStringLiteral("/v1/music/playlists/p 1")).body).object();
    QCOMPARE(order.value(QLatin1StringView("tracks")).toArray().first().toObject().value(QLatin1StringView("id")).toString(),
             QStringLiteral("s2"));

    http.script(QStringLiteral("/v1/music/playlists/p 1"), FakeHttpTransport::ok("{}"));
    QVERIFY(outcome(fixture.editor.deletePlaylist(key))->isOk());
    QCOMPARE(fixture.lastTo(QStringLiteral("/v1/music/playlists/p 1")).method, lmp::core::net::HttpMethod::Delete);

    // A key that is not an account playlist sends nothing.
    settle();
    const qsizetype before = http.requests.size();
    QCOMPARE(outcome(fixture.editor.deletePlaylist(QStringLiteral("playlist|4")))->failure().kind,
             FailureKind::NotAvailable);
    QCOMPARE(http.requests.size(), before);
}

void AccountLibraryEditorTest::refusedEditIsReported()
{
    EditorFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    QSignalSpy failed(&fixture.editor, &AccountLibraryEditor::failed);
    fixture.http().script(QStringLiteral("/v1/music/playlists/p1"), FakeHttpTransport::failed(FailureKind::NotFound, 404));
    QCOMPARE(outcome(fixture.editor.renamePlaylist(QStringLiteral("account-playlist|p1"), QStringLiteral("X")))
                 ->failure()
                 .kind,
             FailureKind::NotFound);
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.first().first().toString().isEmpty());
}

void AccountLibraryEditorTest::accountPlaylistPagesReadTheActiveAccount()
{
    EditorFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    lmp::core::db::AccountLibrarySnapshot snapshot;
    snapshot.profile.accountId = kOwner;
    for (const QString& id : {QStringLiteral("s1"), QStringLiteral("s2")}) {
        lmp::core::db::AccountTrackSnapshot entry;
        entry.track = accountSong(id, QStringLiteral("Song ") + id);
        snapshot.tracks.append(entry);
    }
    lmp::core::db::AccountPlaylistSnapshot playlist;
    playlist.playlistId = QStringLiteral("p1");
    playlist.name = QStringLiteral("Mix");
    playlist.remoteTrackIds = {QStringLiteral("s2"), QStringLiteral("s1")};
    snapshot.playlists.append(playlist);
    QVERIFY(fixture.modes.accounts.replaceAccountLibrary(kOwner, snapshot));

    lmp::core::db::LibraryQueries queries(fixture.modes.library.database(), &fixture.modes.accounts);
    lmp::core::db::TrackQuery query;
    query.groupKind = QStringLiteral("playlist");
    query.groupKey = QStringLiteral("account-playlist|p1");
    query.sort = QStringLiteral("Custom"); // the playlist's own order
    const lmp::core::db::TrackPage page = queries.loadTrackPage(query);
    QCOMPARE(page.tracks.size(), 2);
    QCOMPARE(page.tracks.first().remoteId, QStringLiteral("s2"));
}

QTEST_GUILESS_MAIN(AccountLibraryEditorTest)
#include "tst_account_library_editor.moc"
