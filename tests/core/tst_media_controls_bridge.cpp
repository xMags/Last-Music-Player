#include <QList>
#include <QObject>
#include <QTest>

#include <memory>
#include <utility>

#include "PlayerFixture.h"
#include "core/playback/MediaControlsBridge.h"
#include "core/settings/SettingKeys.h"
#include "platform/IMediaControls.h"

using lmp::core::MediaControlsBridge;
using lmp::core::PlayerController;
using lmp::core::Track;
using lmp::platform::IMediaControls;
using lmp::platform::MediaControlsMetadata;
using Command = lmp::platform::IMediaControls::MediaCommand;
using Status = lmp::platform::IMediaControls::PlaybackStatus;
namespace keys = lmp::core::settings::keys;

namespace {

// Records what the bridge sets; send() plays the part of a media key.
class FakeMediaControls final : public IMediaControls {
public:
    struct Timeline {
        double position = 0.0;
        double duration = 0.0;
    };

    void setEnabled(bool enabled) override { enabledCalls.append(enabled); }
    void setMetadata(const MediaControlsMetadata& metadata) override { metadataCalls.append(metadata); }
    void setPlaybackStatus(PlaybackStatus status) override { statusCalls.append(status); }
    void setTimeline(double positionSeconds, double durationSeconds) override
    {
        timelineCalls.append(Timeline{positionSeconds, durationSeconds});
    }

    void send(MediaCommand command) { emit commandReceived(command); }

    QList<bool> enabledCalls;
    QList<MediaControlsMetadata> metadataCalls;
    QList<PlaybackStatus> statusCalls;
    QList<Timeline> timelineCalls;
};

// A controller, the fake controls and the bridge between them.
struct BridgeFixture {
    BridgeFixture()
        : player(fixture.makeController())
    {
    }

    void makeBridge()
    {
        bridge = std::make_unique<MediaControlsBridge>(MediaControlsBridge::Services{
            .player = *player, .controls = controls, .settings = fixture.settings, .clock = fixture.clock});
    }

    // Starts `title` from the context of a, b, c and lets it reach Playing.
    void startPlaying(const QString& title)
    {
        player->playInContext(fixture.song(title), fixture.songs({"a", "b", "c"}));
        settle();
        fixture.audio.start();
    }

    PlayerFixture fixture;
    std::unique_ptr<PlayerController> player;
    FakeMediaControls controls;
    std::unique_ptr<MediaControlsBridge> bridge;
};

} // namespace

class MediaControlsBridgeTest : public QObject {
    Q_OBJECT

private slots:
    void startsEnabledAndEmptyWithNothingPlaying();
    void followsTheSettingAtStartAndWhenItChanges();
    void showsTheCurrentSongWithItsCover();
    void mapsThePlayerStateToTheSystemStatus();
    void sendsProgressAtMostEveryFiveSeconds();
    void aStateChangeSendsProgressAtOnce();
    void toggleAndSkipCommandsReachThePlayer();
    void playAndPauseActOnlyWhenTheyChangeSomething();
    void artworkFileFollowsTheCoverRules();
};

void MediaControlsBridgeTest::startsEnabledAndEmptyWithNothingPlaying()
{
    BridgeFixture f;
    QVERIFY(f.fixture.ok());
    f.makeBridge();
    QCOMPARE(f.controls.enabledCalls, QList<bool>({true}));
    QCOMPARE(f.controls.metadataCalls.size(), 1);
    QVERIFY(f.controls.metadataCalls.last().title.isEmpty());
    QCOMPARE(f.controls.statusCalls, QList<Status>({Status::Closed}));
    QCOMPARE(f.controls.timelineCalls.size(), 1);
    QCOMPARE(f.controls.timelineCalls.last().duration, 0.0);
}

void MediaControlsBridgeTest::followsTheSettingAtStartAndWhenItChanges()
{
    BridgeFixture f;
    QVERIFY(f.fixture.ok());
    f.fixture.settings.setBoolean(keys::WindowsMediaControls, false);
    f.makeBridge();
    QCOMPARE(f.controls.enabledCalls, QList<bool>({false}));

    f.fixture.settings.setBoolean(keys::WindowsMediaControls, true);
    QCOMPARE(f.controls.enabledCalls, QList<bool>({false, true}));
    f.fixture.settings.setBoolean(keys::WindowsMediaControls, false);
    QCOMPARE(f.controls.enabledCalls, QList<bool>({false, true, false}));
    // Other settings leave it alone.
    f.fixture.settings.setBoolean(keys::ShuffleEnabled, true);
    QCOMPARE(f.controls.enabledCalls.size(), 3);
}

void MediaControlsBridgeTest::showsTheCurrentSongWithItsCover()
{
    BridgeFixture f;
    QVERIFY(f.fixture.ok());
    f.makeBridge();
    Track song = f.fixture.song(QStringLiteral("b"));
    song.album = QStringLiteral("Album");
    f.player->playInContext(song, {song});
    settle();

    const MediaControlsMetadata& shown = f.controls.metadataCalls.last();
    QCOMPARE(shown.title, QStringLiteral("b"));
    QCOMPARE(shown.artist, QStringLiteral("Artist"));
    QCOMPARE(shown.album, QStringLiteral("Album"));
    QCOMPARE(shown.artworkFilePath, song.filePath);
}

void MediaControlsBridgeTest::mapsThePlayerStateToTheSystemStatus()
{
    BridgeFixture f;
    QVERIFY(f.fixture.ok());
    f.makeBridge();
    f.startPlaying(QStringLiteral("a"));
    QCOMPARE(f.controls.statusCalls.last(), Status::Playing);
    QVERIFY(f.controls.statusCalls.contains(Status::Changing));
    f.player->togglePlay();
    f.fixture.audio.paused();
    QCOMPARE(f.controls.statusCalls.last(), Status::Paused);

    using State = PlayerController::State;
    QCOMPARE(MediaControlsBridge::statusFor(State::Playing, false), Status::Closed);
    QCOMPARE(MediaControlsBridge::statusFor(State::Idle, true), Status::Paused);
    QCOMPARE(MediaControlsBridge::statusFor(State::Loading, true), Status::Changing);
    QCOMPARE(MediaControlsBridge::statusFor(State::Error, true), Status::Stopped);
}

void MediaControlsBridgeTest::sendsProgressAtMostEveryFiveSeconds()
{
    BridgeFixture f;
    QVERIFY(f.fixture.ok());
    f.makeBridge();
    f.startPlaying(QStringLiteral("a"));
    const qsizetype afterStart = f.controls.timelineCalls.size();

    f.fixture.clock.advance(1000);
    f.fixture.audio.position(1.0);
    f.fixture.clock.advance(2000);
    f.fixture.audio.position(3.0);
    // A seek is not forced either.
    f.player->seek(120.0);
    f.fixture.audio.position(120.0);
    QCOMPARE(f.controls.timelineCalls.size(), afterStart);

    f.fixture.clock.advance(2000); // five seconds after the forced report
    f.fixture.audio.position(122.0);
    QCOMPARE(f.controls.timelineCalls.size(), afterStart + 1);
    QCOMPARE(f.controls.timelineCalls.last().position, 122.0);
    QCOMPARE(f.controls.timelineCalls.last().duration, 200.0);
}

void MediaControlsBridgeTest::aStateChangeSendsProgressAtOnce()
{
    BridgeFixture f;
    QVERIFY(f.fixture.ok());
    f.makeBridge();
    f.startPlaying(QStringLiteral("a"));
    f.fixture.clock.advance(1000);
    f.fixture.audio.position(1.0);
    const qsizetype before = f.controls.timelineCalls.size();

    f.player->togglePlay();
    f.fixture.audio.paused();
    QCOMPARE(f.controls.timelineCalls.size(), before + 1);
    QCOMPARE(f.controls.timelineCalls.last().position, 1.0);
}

void MediaControlsBridgeTest::toggleAndSkipCommandsReachThePlayer()
{
    BridgeFixture f;
    QVERIFY(f.fixture.ok());
    f.makeBridge();
    f.startPlaying(QStringLiteral("a"));

    f.controls.send(Command::Toggle);
    QCOMPARE(f.fixture.audio.pauseCalls, 1);
    f.fixture.audio.paused();
    f.controls.send(Command::Toggle);
    QCOMPARE(f.fixture.audio.playCalls, 1);
    f.fixture.audio.playing();

    f.controls.send(Command::Next);
    settle();
    QCOMPARE(f.player->currentTrack()->title, QStringLiteral("b"));
    f.fixture.audio.start();
    f.controls.send(Command::Previous); // within 3 s: back to the song before
    settle();
    QCOMPARE(f.player->currentTrack()->title, QStringLiteral("a"));
}

void MediaControlsBridgeTest::playAndPauseActOnlyWhenTheyChangeSomething()
{
    BridgeFixture f;
    QVERIFY(f.fixture.ok());
    f.makeBridge();
    f.startPlaying(QStringLiteral("a"));

    f.controls.send(Command::Play);
    QCOMPARE(f.fixture.audio.pauseCalls, 0);
    QCOMPARE(f.fixture.audio.playCalls, 0);
    f.controls.send(Command::Pause);
    QCOMPARE(f.fixture.audio.pauseCalls, 1);
    f.fixture.audio.paused();

    f.controls.send(Command::Pause);
    f.controls.send(Command::Stop);
    QCOMPARE(f.fixture.audio.pauseCalls, 1);
    f.controls.send(Command::Play);
    QCOMPARE(f.fixture.audio.playCalls, 1);
    f.fixture.audio.playing();
    f.controls.send(Command::Stop);
    QCOMPARE(f.fixture.audio.pauseCalls, 2);
}

void MediaControlsBridgeTest::artworkFileFollowsTheCoverRules()
{
    Track local;
    local.sourceKind = QStringLiteral("local");
    local.filePath = QStringLiteral("C:/Music/song.mp3");
    QCOMPARE(MediaControlsBridge::artworkFileFor(local), QStringLiteral("C:\\Music\\song.mp3"));

    Track withImage = local;
    withImage.artworkUrl = QStringLiteral("file:///C:/Covers/front.png");
    QCOMPARE(MediaControlsBridge::artworkFileFor(withImage), QStringLiteral("C:\\Covers\\front.png"));

    Track withRemoteCover = local;
    withRemoteCover.artworkUrl = QStringLiteral("https://example.invalid/cover.jpg");
    QVERIFY(MediaControlsBridge::artworkFileFor(withRemoteCover).isEmpty());

    Track relative;
    relative.sourceKind = QStringLiteral("local");
    relative.filePath = QStringLiteral("song.mp3");
    QVERIFY(MediaControlsBridge::artworkFileFor(relative).isEmpty());
}

QTEST_GUILESS_MAIN(MediaControlsBridgeTest)

#include "tst_media_controls_bridge.moc"
