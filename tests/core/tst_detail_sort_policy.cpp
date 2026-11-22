#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/library/DetailSortPolicy.h"

using lmp::core::defaultDetailSort;
using lmp::core::DetailSort;
using lmp::core::detailSortLabel;
using lmp::core::detailSortQueryValue;
using lmp::core::detailTrackOrder;
using lmp::core::DetailTrackSortKey;
using lmp::core::kindSupportsCuratedOrder;
using lmp::core::parseDetailSort;

class DetailSortPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void detailSortPolicy();
    void everyTagValueAndLabel();
    void findFoldsAsciiLettersOnly();
};

void DetailSortPolicyTest::detailSortPolicy()
{
    QVERIFY(kindSupportsCuratedOrder(QStringLiteral("PLAYLIST")));
    QVERIFY(kindSupportsCuratedOrder(QStringLiteral("album-collection")));
    QVERIFY(!kindSupportsCuratedOrder(QStringLiteral("artist")));
    QVERIFY(defaultDetailSort(true) == DetailSort::Curated);
    QVERIFY(defaultDetailSort(false) == DetailSort::DateAdded);
    QVERIFY(parseDetailSort(QStringLiteral("custom"), false) == DetailSort::DateAdded);
    QVERIFY(parseDetailSort(QStringLiteral("DURATION"), true) == DetailSort::Duration);
    QVERIFY(detailSortQueryValue(DetailSort::Curated).isEmpty());
    QCOMPARE(detailSortLabel(DetailSort::DateAdded), QStringLiteral("Recently added"));

    const QList<DetailTrackSortKey> tracks{
        {QStringLiteral("Zulu"), QStringLiteral("Beta"), QStringLiteral("One"), 120.0},
        {QStringLiteral("alpha"), QStringLiteral("Gamma"), QStringLiteral("Two"), 300.0},
        {QStringLiteral("Bravo"), QStringLiteral("beta"), QStringLiteral("Three"), 180.0},
    };
    QCOMPARE(detailTrackOrder(tracks, QString(), DetailSort::Curated), (QList<qsizetype>{0, 1, 2}));
    QCOMPARE(detailTrackOrder(tracks, QStringLiteral("two"), DetailSort::Curated), (QList<qsizetype>{1}));
    QCOMPARE(detailTrackOrder(tracks, QStringLiteral("BETA"), DetailSort::Title), (QList<qsizetype>{2, 0}));
    QCOMPARE(detailTrackOrder(tracks, QString(), DetailSort::Artist), (QList<qsizetype>{2, 0, 1}));
    QCOMPARE(detailTrackOrder(tracks, QString(), DetailSort::Duration), (QList<qsizetype>{1, 2, 0}));
}

void DetailSortPolicyTest::everyTagValueAndLabel()
{
    QVERIFY(kindSupportsCuratedOrder(QStringLiteral("Auto-Playlist")));
    QVERIFY(!kindSupportsCuratedOrder(QStringLiteral("genre")));
    QVERIFY(parseDetailSort(QStringLiteral("Title"), false) == DetailSort::Title);
    QVERIFY(parseDetailSort(QStringLiteral("artist"), false) == DetailSort::Artist);
    QVERIFY(parseDetailSort(QStringLiteral("DateAdded"), true) == DetailSort::DateAdded);
    QVERIFY(parseDetailSort(QStringLiteral("CUSTOM"), true) == DetailSort::Curated);
    QVERIFY(parseDetailSort(QStringLiteral("shuffle"), true) == DetailSort::Curated);
    QVERIFY(parseDetailSort(QString(), false) == DetailSort::DateAdded);

    QCOMPARE(detailSortQueryValue(DetailSort::Title), QStringLiteral("Title"));
    QCOMPARE(detailSortQueryValue(DetailSort::Artist), QStringLiteral("Artist"));
    QCOMPARE(detailSortQueryValue(DetailSort::Duration), QStringLiteral("Duration"));
    QCOMPARE(detailSortQueryValue(DetailSort::DateAdded), QStringLiteral("DateAdded"));
    QCOMPARE(detailSortLabel(DetailSort::Title), QStringLiteral("Title"));
    QCOMPARE(detailSortLabel(DetailSort::Artist), QStringLiteral("Artist"));
    QCOMPARE(detailSortLabel(DetailSort::Duration), QStringLiteral("Duration"));
    QCOMPARE(detailSortLabel(DetailSort::Curated), QStringLiteral("Custom order"));

    // DateAdded keeps the source order, and equal durations fall back to title.
    const QList<DetailTrackSortKey> tracks{
        {QStringLiteral("b"), QStringLiteral("x"), QString(), 60.0},
        {QStringLiteral("A"), QStringLiteral("y"), QString(), 60.0},
    };
    QCOMPARE(detailTrackOrder(tracks, QString(), DetailSort::DateAdded), (QList<qsizetype>{0, 1}));
    QCOMPARE(detailTrackOrder(tracks, QString(), DetailSort::Duration), (QList<qsizetype>{1, 0}));
    QCOMPARE(detailTrackOrder(tracks, QStringLiteral("none"), DetailSort::Title), QList<qsizetype>());
}

void DetailSortPolicyTest::findFoldsAsciiLettersOnly()
{
    // Folding changes A to Z only, as before: ASCII capitals match, an
    // accented capital matches only itself.
    const QList<DetailTrackSortKey> tracks{
        {QStringLiteral("Café Blue"), QString(), QString(), 0.0},
        {QStringLiteral("Other"), QString(), QString(), 0.0},
    };
    QCOMPARE(detailTrackOrder(tracks, QStringLiteral("CAFé"), DetailSort::Curated), (QList<qsizetype>{0}));
    QCOMPARE(detailTrackOrder(tracks, QStringLiteral("CAFÉ"), DetailSort::Curated), (QList<qsizetype>{}));
}

QTEST_GUILESS_MAIN(DetailSortPolicyTest)
#include "tst_detail_sort_policy.moc"
