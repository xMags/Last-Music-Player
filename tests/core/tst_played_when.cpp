#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/home/PlayedWhen.h"

using lmp::core::home::relativePlayedText;

class PlayedWhenTest : public QObject {
    Q_OBJECT

private slots:
    void readsLikeTheReference();
};

void PlayedWhenTest::readsLikeTheReference()
{
    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-11-25T12:00:00.000Z"), Qt::ISODateWithMs);
    const auto at = [&now](qint64 secondsAgo) { return now.addSecs(-secondsAgo).toString(Qt::ISODateWithMs); };
    QCOMPARE(relativePlayedText(QString(), now), QString());
    QCOMPARE(relativePlayedText(QStringLiteral("not a time"), now), QStringLiteral("Recently played"));
    QCOMPARE(relativePlayedText(at(30), now), QStringLiteral("Just now"));
    QCOMPARE(relativePlayedText(at(-3600), now), QStringLiteral("Just now"));
    QCOMPARE(relativePlayedText(at(-2 * 86400), now), QStringLiteral("Recently played"));
    QCOMPARE(relativePlayedText(at(12 * 60), now), QStringLiteral("12 min ago"));
    QCOMPARE(relativePlayedText(at(3 * 3600 + 59), now), QStringLiteral("3 hr ago"));
    QCOMPARE(relativePlayedText(at(30 * 3600), now), QStringLiteral("Yesterday"));
    QCOMPARE(relativePlayedText(at(4 * 86400), now), QStringLiteral("4 days ago"));
    QCOMPARE(relativePlayedText(QStringLiteral("2026-11-25T11:00:00Z"), now), QStringLiteral("1 hr ago"));
}

QTEST_GUILESS_MAIN(PlayedWhenTest)
#include "tst_played_when.moc"
