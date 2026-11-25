#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <limits>
#include <memory>

#include <QJsonArray>
#include <QJsonObject>

#include "../core/FakeConnectSocket.h"
#include "../core/ModeServiceFixture.h"
#include "../core/PlayerFixture.h"
#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "core/model/CatalogSourceKey.h"
#include "core/connect/ConnectSync.h"
#include "core/connect/ConnectTracks.h"
#include "core/playback/QueueEngine.h"
#include "ui/player/PlayerBarViewModel.h"

using lmp::core::PlayerController;
using lmp::core::Track;
using lmp::ui::PlayerBarViewModel;

// The bar against a real player controller with a fake audio player.
class PlayerBarViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startsIdleWithTheSavedVolume();
    void formatsTimesAsMinutesAndSeconds();
    void showsTheSongThatPlays();
    void followsTheStateAndPosition();
    void commandsGoToTheController();
    void repeatCyclesOffAllOne();
    void volumeMapsToTheControllersRange();
    void muteShowsAnEmptyVolumeSlider();
    void seekingFollowsTheRatio();
    void seekBubbleNeedsASongWithALength();
    void aNewSongAnimatesButTheLaunchRestoreDoesNot();
    void aLocalSongWithoutArtworkShowsItsEmbeddedCover();
    void anotherDevicesSongIsShownAndControlled();
    void theHeartReadsAndWritesTheLibrary();

private:
    void playSongs(const QStringList& names);

    std::unique_ptr<PlayerFixture> m_fixture;
    std::unique_ptr<PlayerController> m_player;
    std::unique_ptr<PlayerBarViewModel> m_bar;
};

void PlayerBarViewModelTest::init()
{
    m_fixture = std::make_unique<PlayerFixture>();
    QVERIFY(m_fixture->ok());
    m_player = m_fixture->makeController();
    m_bar = std::make_unique<PlayerBarViewModel>(*m_player);
}

void PlayerBarViewModelTest::cleanup()
{
    m_bar.reset();
    m_player.reset();
    m_fixture.reset();
}

void PlayerBarViewModelTest::playSongs(const QStringList& names)
{
    const QList<Track> songs = m_fixture->songs(names);
    m_player->playInContext(songs.first(), songs);
    settle();
    m_fixture->audio.start(200.0);
}

void PlayerBarViewModelTest::startsIdleWithTheSavedVolume()
{
    QVERIFY(!m_bar->hasTrack());
    // Nothing stored yet: the controller's launch level, 0.7.
    QCOMPARE(m_bar->volume(), 70);
    QVERIFY(!m_bar->muted());
    QVERIFY(!m_bar->playing());
    QCOMPARE(m_bar->repeatMode(), 0);
    QVERIFY(!m_bar->displayTitle().isEmpty());
    QVERIFY(m_bar->artworkUrl().isEmpty());
    QCOMPARE(m_bar->elapsedText(), QStringLiteral("0:00"));
    QCOMPARE(m_bar->progress(), 0.0);
}

void PlayerBarViewModelTest::formatsTimesAsMinutesAndSeconds()
{
    QCOMPARE(PlayerBarViewModel::formatTime(0), QStringLiteral("0:00"));
    QCOMPARE(PlayerBarViewModel::formatTime(59.9), QStringLiteral("0:59"));
    QCOMPARE(PlayerBarViewModel::formatTime(61), QStringLiteral("1:01"));
    QCOMPARE(PlayerBarViewModel::formatTime(3725), QStringLiteral("62:05"));
    QCOMPARE(PlayerBarViewModel::formatTime(-5), QStringLiteral("0:00"));
    QCOMPARE(PlayerBarViewModel::formatTime(std::numeric_limits<qreal>::quiet_NaN()), QStringLiteral("0:00"));
}

void PlayerBarViewModelTest::showsTheSongThatPlays()
{
    QSignalSpy track(m_bar.get(), &PlayerBarViewModel::trackChanged);
    playSongs({QStringLiteral("One"), QStringLiteral("Two")});
    QVERIFY(m_bar->hasTrack());
    QCOMPARE(m_bar->title(), QStringLiteral("One"));
    QCOMPARE(m_bar->displayTitle(), QStringLiteral("One"));
    QCOMPARE(m_bar->artist(), QStringLiteral("Artist"));
    QCOMPARE(m_bar->artworkLabel(), QStringLiteral("O"));
    QVERIFY(!m_bar->artworkId().isEmpty());
    QVERIFY(track.count() >= 1);
}

void PlayerBarViewModelTest::followsTheStateAndPosition()
{
    playSongs({QStringLiteral("One")});
    QVERIFY(m_bar->playing());
    QSignalSpy position(m_bar.get(), &PlayerBarViewModel::positionChanged);
    m_fixture->audio.position(50.0);
    QCOMPARE(position.count(), 1);
    QCOMPARE(m_bar->elapsedSeconds(), 50.0);
    QCOMPARE(m_bar->totalSeconds(), 200.0);
    QCOMPARE(m_bar->progress(), 0.25);
    QCOMPARE(m_bar->elapsedText(), QStringLiteral("0:50"));
    QCOMPARE(m_bar->totalText(), QStringLiteral("3:20"));

    QSignalSpy state(m_bar.get(), &PlayerBarViewModel::stateChanged);
    m_fixture->audio.paused();
    QVERIFY(!m_bar->playing());
    QVERIFY(state.count() >= 1);
}

void PlayerBarViewModelTest::commandsGoToTheController()
{
    playSongs({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    m_bar->togglePlay();
    QCOMPARE(m_fixture->audio.pauseCalls, 1);
    m_bar->next();
    settle();
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("Two"));
    QCOMPARE(lastOpenedTitle(m_fixture->audio), QStringLiteral("Two"));
    m_fixture->audio.start(200.0);
    m_bar->previous(); // at the start: back to the song before
    settle();
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("One"));

    QSignalSpy state(m_bar.get(), &PlayerBarViewModel::stateChanged);
    m_bar->toggleShuffle();
    QVERIFY(m_bar->shuffle());
    QCOMPARE(m_player->shuffle(), lmp::core::Shuffle::On);
    m_bar->toggleShuffle();
    QVERIFY(!m_bar->shuffle());
    QCOMPARE(state.count(), 2);

    // Liking is only shown for now.
    m_bar->toggleLike();
    QVERIFY(m_bar->liked());
}

void PlayerBarViewModelTest::repeatCyclesOffAllOne()
{
    m_bar->cycleRepeat();
    QCOMPARE(m_bar->repeatMode(), 1);
    QCOMPARE(m_player->repeatMode(), lmp::core::RepeatMode::All);
    m_bar->cycleRepeat();
    QCOMPARE(m_bar->repeatMode(), 2);
    QCOMPARE(m_player->repeatMode(), lmp::core::RepeatMode::One);
    m_bar->cycleRepeat();
    QCOMPARE(m_bar->repeatMode(), 0);
}

void PlayerBarViewModelTest::volumeMapsToTheControllersRange()
{
    QSignalSpy volume(m_bar.get(), &PlayerBarViewModel::volumeChanged);
    m_bar->setVolumeRatio(0.426);
    QCOMPARE(m_player->volume(), 0.43);
    QCOMPARE(m_bar->volume(), 43);
    QCOMPARE(m_bar->volumeRatio(), 0.43);
    QCOMPARE(volume.count(), 1);
    m_bar->setVolumeRatio(2);
    QCOMPARE(m_bar->volume(), 100);
    m_bar->setVolumeRatio(-1);
    QCOMPARE(m_bar->volume(), 0);
    QCOMPARE(m_bar->volumeLabelAt(0.5), QStringLiteral("50%"));
    QCOMPARE(m_bar->volumeLabelAt(2), QStringLiteral("100%"));
}

void PlayerBarViewModelTest::muteShowsAnEmptyVolumeSlider()
{
    m_bar->setVolumeRatio(0.6);
    m_bar->toggleMute();
    QVERIFY(m_bar->muted());
    QVERIFY(m_player->muted());
    QCOMPARE(m_bar->volume(), 60); // the level is kept
    QCOMPARE(m_bar->volumeRatio(), 0.0);
    QCOMPARE(m_fixture->audio.volumes.last(), 0.0);
    // Moving the slider ends mute.
    m_bar->setVolumeRatio(0.3);
    QVERIFY(!m_bar->muted());
    QCOMPARE(m_bar->volumeRatio(), 0.3);
}

void PlayerBarViewModelTest::seekingFollowsTheRatio()
{
    playSongs({QStringLiteral("One")});
    m_bar->seekToRatio(0.25);
    QCOMPARE(m_fixture->audio.seeks.last(), 50.0);
    QCOMPARE(m_bar->elapsedSeconds(), 50.0);
    QCOMPARE(m_bar->progress(), 0.25);
    m_bar->seekToRatio(3);
    QCOMPARE(m_fixture->audio.seeks.last(), 200.0);
    m_bar->seekToRatio(std::numeric_limits<qreal>::quiet_NaN());
    QCOMPARE(m_fixture->audio.seeks.size(), 2);
}

void PlayerBarViewModelTest::seekBubbleNeedsASongWithALength()
{
    QVERIFY(m_bar->seekLabelAt(0.5).isEmpty());
    playSongs({QStringLiteral("One")});
    QCOMPARE(m_bar->seekLabelAt(0.5), QStringLiteral("1:40"));
}

void PlayerBarViewModelTest::aNewSongAnimatesButTheLaunchRestoreDoesNot()
{
    QSignalSpy changed(m_bar.get(), &PlayerBarViewModel::songChanged);
    const Track restored = m_fixture->song(QStringLiteral("Restored"));
    m_player->restore(lmp::core::QueueEngine(), restored, 12.0);
    QCOMPARE(m_bar->title(), QStringLiteral("Restored"));
    QCOMPARE(changed.count(), 0);

    // Playing the restored song is not a song change either.
    m_bar->togglePlay();
    settle();
    QCOMPARE(changed.count(), 0);

    playSongs({QStringLiteral("Next")});
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_bar->previousTitle(), QStringLiteral("Restored"));
    QCOMPARE(m_bar->previousArtist(), QStringLiteral("Artist"));
    QCOMPARE(m_bar->title(), QStringLiteral("Next"));
}

void PlayerBarViewModelTest::aLocalSongWithoutArtworkShowsItsEmbeddedCover()
{
    playSongs({QStringLiteral("One")});
    const QUrl url = m_bar->artworkUrl();
    QCOMPARE(url.scheme(), QStringLiteral("image"));
    QCOMPARE(url.host(), QStringLiteral("embedded-cover"));
}

void PlayerBarViewModelTest::anotherDevicesSongIsShownAndControlled()
{
    ConnectHarness harness(m_fixture->clock);
    lmp::core::connect::ConnectSync sync(
        {.client = *harness.client, .player = *m_player, .clock = m_fixture->clock, .castDeviceName = {}});
    PlayerBarViewModel bar(*m_player, &sync);
    playSongs({QStringLiteral("Local")});
    QCOMPARE(bar.title(), QStringLiteral("Local"));
    QVERIFY(!bar.remote());

    harness.client->start();
    settle();
    harness.socket().serverOpen();
    Track song;
    song.title = QStringLiteral("On The Phone");
    song.artist = QStringLiteral("Someone");
    song.sourceKind = QStringLiteral("remote");
    song.remoteId = QStringLiteral("p1");
    song.sourceUrl = QStringLiteral("https://music.example.test/t/p1");
    const QJsonObject state{{"track", lmp::core::connect::connectTrackJson(song)},
                            {"playing", true},
                            {"positionSec", 30},
                            {"durationSec", 120},
                            {"volume", 40},
                            {"repeatMode", 1}};
    QSignalSpy tracks(&bar, &PlayerBarViewModel::trackChanged);
    harness.socket().serverSend(
        {{"type", "welcome"},
         {"epoch", 2},
         {"devices", QJsonArray{QJsonObject{{"deviceId", "phone-0001"}, {"name", "Phone"}, {"kind", "android"}}}},
         {"session", QJsonObject{{"activeDeviceId", "phone-0001"}, {"state", state}}}});
    QVERIFY(bar.remote());
    QVERIFY(tracks.count() >= 1);
    QCOMPARE(bar.title(), QStringLiteral("On The Phone"));
    QCOMPARE(bar.playingOn(), QStringLiteral("Phone"));
    QVERIFY(bar.playing());
    QCOMPARE(bar.volume(), 40);
    QCOMPARE(bar.repeatMode(), 1);
    QCOMPARE(bar.elapsedSeconds(), 30.0);
    QCOMPARE(bar.totalText(), QStringLiteral("2:00"));

    // The controls go to the phone; nothing changes here.
    const int pauses = m_fixture->audio.pauseCalls;
    const double localVolume = m_player->volume();
    bar.togglePlay();
    QCOMPARE(harness.lastSent().value("command").toObject().value("type").toString(), QStringLiteral("pause"));
    QVERIFY(!bar.playing());
    QCOMPARE(m_fixture->audio.pauseCalls, pauses);
    bar.setVolumeRatio(0.25);
    m_fixture->clock.advance(200);
    QCOMPARE(harness.lastSent().value("command").toObject().value("value").toInt(), 25);
    QCOMPARE(bar.volume(), 25);
    QCOMPARE(m_player->volume(), localVolume);
}

void PlayerBarViewModelTest::theHeartReadsAndWritesTheLibrary()
{
    lmp::test::ModeServiceFixture modes;
    (void)modes.modes.start();
    lmp::core::db::Database& database = m_fixture->library.database();
    lmp::core::db::CollectionRepository collections(database);
    lmp::core::db::GroupQueries groups(database);
    lmp::core::UserDataOperationGate gate;
    lmp::core::TrackActions actions(lmp::core::TrackActions::Dependencies{
        *m_player, m_fixture->tracks, collections, groups, modes.modes, gate});
    PlayerBarViewModel bar(*m_player, nullptr, &actions);

    // Liked in the library, though the song that plays does not say so (a
    // song sent from another device, a downloaded copy).
    const Track song = m_fixture->song(QStringLiteral("One"));
    QCOMPARE(actions.toggleLike(song), std::optional<bool>(true));
    playSongs({QStringLiteral("One"), QStringLiteral("Two")});
    QVERIFY(!m_player->currentTrack()->liked);
    QVERIFY(bar.liked());

    // The heart writes the like, not only its own look.
    bar.toggleLike();
    QVERIFY(!bar.liked());
    QVERIFY(!m_fixture->tracks.isLiked(lmp::core::catalogSourceKey(song)));

    // A like made elsewhere moves it too.
    QCOMPARE(actions.toggleLike(song), std::optional<bool>(true));
    QVERIFY(bar.liked());

    // Likes that arrive without a like being made here (an account sync, the
    // session checked after launch) show once the heart is read again.
    QVERIFY(m_fixture->tracks.setLiked(lmp::core::catalogSourceKey(song), false));
    QVERIFY(bar.liked());
    bar.refreshLiked();
    QVERIFY(!bar.liked());
}

QTEST_GUILESS_MAIN(PlayerBarViewModelTest)
#include "tst_player_bar_view_model.moc"
