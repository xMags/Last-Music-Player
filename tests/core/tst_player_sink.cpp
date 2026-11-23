#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include "PlayerFixture.h"
#include "core/playback/IPlaybackSink.h"

using lmp::core::PlayerController;
using lmp::core::Track;

namespace {

// An external sink with its own fake player and resolver, refusing the
// songs it is told to.
class FakeSink final : public lmp::core::IPlaybackSink {
public:
    [[nodiscard]] lmp::platform::IAudioPlayer& audio() override { return player; }
    [[nodiscard]] lmp::core::TrackSourceResolver& resolver() override { return sources; }
    [[nodiscard]] bool accepts(const Track& track) const override { return !refused.contains(track.title); }

    FakeAudioPlayer player;
    FakeSourceResolver sources;
    QStringList refused;
};

} // namespace

// Handing playback to an external sink (a cast device) and back.
class PlayerSinkTest : public QObject {
    Q_OBJECT

private slots:
    void handoverReopensTheSongWhereItStood()
    {
        PlayerFixture fixture;
        QVERIFY(fixture.ok());
        auto player = fixture.makeController();
        player->playInContext(fixture.song(QStringLiteral("a")), fixture.songs({"a", "b"}));
        settle();
        fixture.audio.start();
        fixture.audio.position(42.0);

        FakeSink sink;
        player->setSink(&sink, PlayerController::SinkHandover::KeepPlaying);
        settle();
        QVERIFY(player->usesExternalSink());
        QCOMPARE(fixture.audio.stopCalls, 1);
        QCOMPARE(sink.player.opens.size(), 1);
        QCOMPARE(sink.player.opens.last().startAtSeconds, 42.0);
        QVERIFY(sink.player.opens.last().autoplay);
        // The volume follows the sink.
        QVERIFY(!sink.player.volumes.isEmpty());

        // Transport commands go to the sink; the local player hears nothing.
        sink.player.start();
        player->togglePlay();
        QCOMPARE(sink.player.pauseCalls, 1);
        QCOMPARE(fixture.audio.pauseCalls, 0);
        // A late event of the closed local load changes nothing.
        fixture.audio.end();
        settle();
        QCOMPARE(player->currentTrack()->title, QStringLiteral("a"));
        QCOMPARE(sink.player.opens.size(), 1);

        // Back to this PC, keeping it paused.
        sink.player.paused();
        player->setSink(nullptr, PlayerController::SinkHandover::KeepPlaying);
        settle();
        QVERIFY(!player->usesExternalSink());
        QCOMPARE(fixture.audio.opens.size(), 2);
        QVERIFY(!fixture.audio.opens.last().autoplay);
    }

    void aRefusedSongComesBackToThisPc()
    {
        PlayerFixture fixture;
        QVERIFY(fixture.ok());
        auto player = fixture.makeController();
        QSignalSpy refused(player.get(), &PlayerController::sinkRefused);
        player->playInContext(fixture.song(QStringLiteral("a")), fixture.songs({"a", "b"}));
        settle();
        fixture.audio.start();

        FakeSink sink;
        sink.refused = {QStringLiteral("b")};
        player->setSink(&sink, PlayerController::SinkHandover::KeepPlaying);
        settle();
        sink.player.start();
        const qsizetype localOpens = fixture.audio.opens.size();

        player->next();
        settle();
        QCOMPARE(refused.count(), 1);
        QCOMPARE(refused.first().first().value<Track>().title, QStringLiteral("b"));
        QVERIFY(!player->usesExternalSink());
        QCOMPARE(sink.player.opens.size(), 1);
        QCOMPARE(fixture.audio.opens.size(), localOpens + 1);
        QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("b"));
    }

    void stopHandoverLeavesTheSongClosedAndPaused()
    {
        PlayerFixture fixture;
        QVERIFY(fixture.ok());
        auto player = fixture.makeController();
        player->playInContext(fixture.song(QStringLiteral("a")), fixture.songs({"a"}));
        settle();
        FakeSink sink;
        player->setSink(&sink, PlayerController::SinkHandover::KeepPlaying);
        settle();
        sink.player.start();
        sink.player.position(30.0);
        const qsizetype localOpens = fixture.audio.opens.size();

        player->setSink(nullptr, PlayerController::SinkHandover::Stop);
        settle();
        QCOMPARE(player->state(), PlayerController::State::Paused);
        QCOMPARE(fixture.audio.opens.size(), localOpens);
        QCOMPARE(sink.player.stopCalls, 1);
        // The next Play opens it here, where it stood.
        player->togglePlay();
        settle();
        QCOMPARE(fixture.audio.opens.size(), localOpens + 1);
        QCOMPARE(fixture.audio.opens.last().startAtSeconds, 30.0);
    }
};

QTEST_GUILESS_MAIN(PlayerSinkTest)
#include "tst_player_sink.moc"
