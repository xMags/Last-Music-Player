#include <QObject>
#include <QString>
#include <QTest>

#include "core/playback/PlaybackHistoryQualifier.h"

#include <cstdint>

using lmp::core::PlaybackHistoryQualifier;

class PlaybackHistoryQualifierTest : public QObject {
    Q_OBJECT

private slots:
    void playbackHistoryQualification();
    void clearAndEmptyIdentity();
};

void PlaybackHistoryQualifierTest::playbackHistoryQualification()
{
    const QString trackA = QStringLiteral("owner-a|track-a");
    const QString trackB = QStringLiteral("owner-a|track-b");
    PlaybackHistoryQualifier qualifier;
    qualifier.reset(trackA);
    QVERIFY(!qualifier.observe(trackA, true, 1000));
    for (std::uint64_t tick = 2000; tick <= 8000; tick += 1000) {
        QVERIFY(!qualifier.observe(trackA, true, tick));
    }
    QVERIFY(qualifier.observe(trackA, true, 9000));
    qualifier.markCompleted();
    QVERIFY(!qualifier.observe(trackA, true, 10000));

    qualifier.reset(trackB);
    QVERIFY(!qualifier.observe(trackB, true, 1000));
    QVERIFY(!qualifier.observe(trackB, true, 5000));
    QCOMPARE(qualifier.qualifiedSeconds(), 1.0);
    QVERIFY(!qualifier.observe(trackB, false, 6000));
    QVERIFY(!qualifier.observe(trackB, true, 12000));
    QVERIFY(!qualifier.observe(QStringLiteral("owner-b|track-b"), true, 13000));
}

void PlaybackHistoryQualifierTest::clearAndEmptyIdentity()
{
    PlaybackHistoryQualifier qualifier;
    // Nothing was reset: the empty identity never counts.
    QVERIFY(!qualifier.observe(QString(), true, 1000));
    QVERIFY(!qualifier.observe(QString(), true, 2000));
    QCOMPARE(qualifier.qualifiedSeconds(), 0.0);

    const QString track = QStringLiteral("owner-a|track-c");
    qualifier.reset(track);
    QVERIFY(!qualifier.observe(track, true, 100));
    QVERIFY(!qualifier.observe(track, true, 600));
    QCOMPARE(qualifier.qualifiedSeconds(), 0.5);
    // A clock that goes backwards restarts the sample without counting.
    QVERIFY(!qualifier.observe(track, true, 300));
    QCOMPARE(qualifier.qualifiedSeconds(), 0.5);
    QVERIFY(!qualifier.completed());
    qualifier.markCompleted();
    QVERIFY(qualifier.completed());

    qualifier.clear();
    QVERIFY(!qualifier.completed());
    QCOMPARE(qualifier.qualifiedSeconds(), 0.0);
    QVERIFY(!qualifier.observe(track, true, 1000));
    QVERIFY(!qualifier.observe(track, true, 2000));
    QCOMPARE(qualifier.qualifiedSeconds(), 0.0);
}

QTEST_GUILESS_MAIN(PlaybackHistoryQualifierTest)
#include "tst_playback_history_qualifier.moc"
