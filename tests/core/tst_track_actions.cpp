#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <memory>

#include "ModeServiceFixture.h"
#include "PlayerFixture.h"
#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "core/mode/SourceMode.h"
#include "core/model/CatalogSourceKey.h"

using lmp::core::TrackActions;
using lmp::core::Track;
using lmp::core::mode::SourceMode;

// What a song's menu does: play, like and playlists, each asking the mode first.
class TrackActionsTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void localSongsPlayRemoteOnesNeedStreaming();
    void playNextAndQueueReachThePlayer();
    void likeFlipsAndPersists();
    void closedGateRefusesChanges();
    void playlistsAddRemoveAndCreate();
    void accountSongsWaitForAccountSync();
    void openingPagesIsReported();

private:
    [[nodiscard]] Track remoteSong() const;

    std::unique_ptr<PlayerFixture> m_player;
    std::unique_ptr<lmp::test::ModeServiceFixture> m_modes;
    std::unique_ptr<lmp::core::PlayerController> m_controller;
    std::unique_ptr<lmp::core::db::CollectionRepository> m_collections;
    std::unique_ptr<lmp::core::db::GroupQueries> m_groups;
    std::unique_ptr<lmp::core::UserDataOperationGate> m_gate;
    std::unique_ptr<TrackActions> m_actions;
};

void TrackActionsTest::init()
{
    m_player = std::make_unique<PlayerFixture>();
    QVERIFY(m_player->ok());
    m_modes = std::make_unique<lmp::test::ModeServiceFixture>();
    (void)m_modes->modes.start();
    m_controller = m_player->makeController();
    m_collections = std::make_unique<lmp::core::db::CollectionRepository>(m_player->library.database());
    m_groups = std::make_unique<lmp::core::db::GroupQueries>(m_player->library.database());
    m_gate = std::make_unique<lmp::core::UserDataOperationGate>();
    m_actions = std::make_unique<TrackActions>(TrackActions::Dependencies{
        *m_controller, m_player->tracks, *m_collections, *m_groups, m_modes->modes, *m_gate});
}

void TrackActionsTest::cleanup()
{
    m_actions.reset();
    m_gate.reset();
    m_groups.reset();
    m_collections.reset();
    m_controller.reset();
    m_modes.reset();
    m_player.reset();
}

Track TrackActionsTest::remoteSong() const
{
    Track track;
    track.title = QStringLiteral("Remote");
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("account");
    track.remoteId = QStringLiteral("r1");
    track.sourceUrl = QStringLiteral("https://example.invalid/r1");
    return track;
}

void TrackActionsTest::localSongsPlayRemoteOnesNeedStreaming()
{
    const Track local = m_player->song(QStringLiteral("One"));
    QVERIFY(m_actions->canPlay(local));
    QVERIFY(!m_actions->canPlay(remoteSong()));
    Track gone = local;
    gone.unavailable = true;
    QVERIFY(!m_actions->canPlay(gone));

    QVERIFY(m_actions->play(local, m_player->songs({QStringLiteral("One"), QStringLiteral("Two")})));
    settle();
    QCOMPARE(lastOpenedTitle(m_player->audio), QStringLiteral("One"));
    QVERIFY(!m_actions->play(remoteSong()));
}

void TrackActionsTest::playNextAndQueueReachThePlayer()
{
    QVERIFY(m_actions->play(m_player->song(QStringLiteral("One"))));
    settle();
    QVERIFY(m_actions->addToQueue(m_player->song(QStringLiteral("Last"))));
    QVERIFY(m_actions->playNext(m_player->song(QStringLiteral("Next"))));
    const QStringList titles = visibleTitles(*m_controller);
    QVERIFY(titles.contains(QStringLiteral("Next")));
    QVERIFY(titles.contains(QStringLiteral("Last")));
    QVERIFY(titles.indexOf(QStringLiteral("Next")) < titles.indexOf(QStringLiteral("Last")));
}

void TrackActionsTest::likeFlipsAndPersists()
{
    const Track song = m_player->song(QStringLiteral("One"));
    const QString key = lmp::core::catalogSourceKey(song);
    QSignalSpy liked(m_actions.get(), &TrackActions::likedChanged);
    QCOMPARE(m_actions->toggleLike(song), std::optional<bool>(true));
    QVERIFY(m_player->tracks.isLiked(key));
    QCOMPARE(liked.count(), 1);
    QCOMPARE(liked.first().at(0).toString(), key);
    QCOMPARE(m_actions->toggleLike(song), std::optional<bool>(false));
    QVERIFY(!m_player->tracks.isLiked(key));
}

void TrackActionsTest::closedGateRefusesChanges()
{
    QVERIFY(m_gate->closeAdmissions());
    QCOMPARE(m_actions->toggleLike(m_player->song(QStringLiteral("One"))), std::nullopt);
    QVERIFY(!m_actions->addToNewPlaylist(m_player->song(QStringLiteral("One")), QStringLiteral("Mix")));
}

void TrackActionsTest::playlistsAddRemoveAndCreate()
{
    QSignalSpy lists(m_actions.get(), &TrackActions::playlistsChanged);
    QSignalSpy changed(m_actions.get(), &TrackActions::playlistChanged);
    const lmp::core::Result<QString> key = m_actions->addToNewPlaylist(m_player->song(QStringLiteral("One")),
                                                                       QStringLiteral("  Road Trip "));
    QVERIFY(key);
    QCOMPARE(lists.count(), 1);
    QCOMPARE(m_actions->playlists().size(), 1);
    QCOMPARE(m_actions->playlists().first().title, QStringLiteral("Road Trip"));
    QCOMPARE(m_actions->playlists().first().trackCount, 1);

    QVERIFY(m_actions->addToPlaylist(m_player->song(QStringLiteral("Two")), key.value()));
    QCOMPARE(m_actions->playlists().first().trackCount, 2);
    QVERIFY(changed.count() >= 2);

    // Removing needs the song's row, as a playlist page has it.
    Track second = m_player->song(QStringLiteral("Two"));
    second.rowId = m_player->library.number("SELECT Id FROM Tracks WHERE Title='Two';");
    QVERIFY(second.rowId > 0);
    QVERIFY(m_actions->removeFromPlaylist(second, key.value()));
    QCOMPARE(m_actions->playlists().first().trackCount, 1);

    QVERIFY(!m_actions->addToNewPlaylist(m_player->song(QStringLiteral("One")), QStringLiteral("   ")));
}

void TrackActionsTest::accountSongsWaitForAccountSync()
{
    QVERIFY(m_modes->validate(QStringLiteral("owner-a")));
    QVERIFY(m_modes->modes.setMode(SourceMode::Account));
    QVERIFY(!m_actions->canKeep(remoteSong()));
    QCOMPARE(m_actions->toggleLike(remoteSong()), std::nullopt);
    // Local songs keep their likes in every mode.
    QVERIFY(m_actions->canKeep(m_player->song(QStringLiteral("One"))));
}

void TrackActionsTest::openingPagesIsReported()
{
    QSignalSpy album(m_actions.get(), &TrackActions::albumRequested);
    QSignalSpy artist(m_actions.get(), &TrackActions::artistRequested);
    m_actions->openAlbum(m_player->song(QStringLiteral("One")));
    m_actions->openArtist(m_player->song(QStringLiteral("One")));
    QCOMPARE(album.count(), 1);
    QCOMPARE(artist.count(), 1);
}

QTEST_MAIN(TrackActionsTest)
#include "tst_track_actions.moc"
