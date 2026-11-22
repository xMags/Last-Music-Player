#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <limits>

#include "ui/player/PlayerBarViewModel.h"

using lmp::ui::PlayerBarViewModel;

class PlayerBarViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void startsIdleWithDefaultVolume();
    void formatsTimesAsMinutesAndSeconds();
    void repeatCyclesOffAllOne();
    void togglesFlipAndNotify();
    void volumeIsClampedAndRounded();
    void seekBubbleNeedsASongWithALength();
    void seekingFollowsTheRatio();
};

void PlayerBarViewModelTest::startsIdleWithDefaultVolume()
{
    PlayerBarViewModel bar;
    QVERIFY(!bar.hasTrack());
    QCOMPARE(bar.volume(), 62);
    QVERIFY(!bar.muted());
    QCOMPARE(bar.repeatMode(), 0);
    QVERIFY(!bar.displayTitle().isEmpty());
    QCOMPARE(bar.elapsedText(), QStringLiteral("0:00"));
    QCOMPARE(bar.progress(), 0.0);
}

void PlayerBarViewModelTest::formatsTimesAsMinutesAndSeconds()
{
    QCOMPARE(PlayerBarViewModel::formatTime(0), QStringLiteral("0:00"));
    QCOMPARE(PlayerBarViewModel::formatTime(59.9), QStringLiteral("0:59"));
    QCOMPARE(PlayerBarViewModel::formatTime(61), QStringLiteral("1:01"));
    QCOMPARE(PlayerBarViewModel::formatTime(3725), QStringLiteral("62:05"));
    QCOMPARE(PlayerBarViewModel::formatTime(-5), QStringLiteral("0:00"));
    QCOMPARE(PlayerBarViewModel::formatTime(std::numeric_limits<qreal>::quiet_NaN()),
             QStringLiteral("0:00"));
}

void PlayerBarViewModelTest::repeatCyclesOffAllOne()
{
    PlayerBarViewModel bar;
    bar.cycleRepeat();
    QCOMPARE(bar.repeatMode(), 1);
    bar.cycleRepeat();
    QCOMPARE(bar.repeatMode(), 2);
    bar.cycleRepeat();
    QCOMPARE(bar.repeatMode(), 0);
    bar.setRepeatMode(7);
    QCOMPARE(bar.repeatMode(), 2);
}

void PlayerBarViewModelTest::togglesFlipAndNotify()
{
    PlayerBarViewModel bar;
    QSignalSpy state(&bar, &PlayerBarViewModel::stateChanged);
    QSignalSpy volume(&bar, &PlayerBarViewModel::volumeChanged);
    bar.togglePlay();
    bar.toggleShuffle();
    bar.toggleLike();
    bar.toggleMute();
    QVERIFY(bar.playing());
    QVERIFY(bar.shuffle());
    QVERIFY(bar.liked());
    QVERIFY(bar.muted());
    QCOMPARE(state.count(), 3);
    QCOMPARE(volume.count(), 1);
}

void PlayerBarViewModelTest::volumeIsClampedAndRounded()
{
    PlayerBarViewModel bar;
    bar.setVolume(150);
    QCOMPARE(bar.volume(), 100);
    bar.setVolume(-3);
    QCOMPARE(bar.volume(), 0);
    bar.setVolumeRatio(0.426);
    QCOMPARE(bar.volume(), 43);
    QCOMPARE(bar.volumeLabelAt(0.5), QStringLiteral("50%"));
    QCOMPARE(bar.volumeLabelAt(2), QStringLiteral("100%"));
}

void PlayerBarViewModelTest::seekBubbleNeedsASongWithALength()
{
    PlayerBarViewModel bar;
    bar.setTotalSeconds(200);
    QVERIFY(bar.seekLabelAt(0.5).isEmpty());
    bar.setTitle(QStringLiteral("Song"));
    QCOMPARE(bar.seekLabelAt(0.5), QStringLiteral("1:40"));
    bar.setTotalSeconds(0);
    QVERIFY(bar.seekLabelAt(0.5).isEmpty());
}

void PlayerBarViewModelTest::seekingFollowsTheRatio()
{
    PlayerBarViewModel bar;
    bar.setTotalSeconds(240);
    bar.seekToRatio(0.25);
    QCOMPARE(bar.elapsedSeconds(), 60.0);
    QCOMPARE(bar.progress(), 0.25);
    QCOMPARE(bar.elapsedText(), QStringLiteral("1:00"));
    QCOMPARE(bar.totalText(), QStringLiteral("4:00"));
    bar.seekToRatio(3);
    QCOMPARE(bar.elapsedSeconds(), 240.0);
}

QTEST_GUILESS_MAIN(PlayerBarViewModelTest)
#include "tst_player_bar_view_model.moc"
