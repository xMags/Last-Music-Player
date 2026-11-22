#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/library/TrackSearchPolicy.h"

using lmp::core::BrowseLandingCategory;
using lmp::core::BrowseLandingCategoryLimit;
using lmp::core::browseLandingCategories;
using lmp::core::MinimumSearchQueryLength;

class TrackSearchPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void landingRejectsNonGenresAndNormalizesNames();
    void landingPreservesRealRankingThenFillsInCuratedOrder();
    void landingMarksActualCuratedNameAsReal();
    void landingUsesTwelveCuratedGenresForAnEmptyLibrary();
    void landingCapsRealGenresAtTwelve();
    void minimumQueryLength();
    void landingSkipsUncategorizedAndFoldsAsciiOnly();
};

void TrackSearchPolicyTest::landingRejectsNonGenresAndNormalizesNames()
{
    const QList<BrowseLandingCategory> categories = browseLandingCategories(
        {QStringLiteral(" "), QStringLiteral("song"), QStringLiteral("MUSIC"), QStringLiteral(" audio "),
         QStringLiteral("Track"), QStringLiteral("People & Blogs"), QStringLiteral("Unknown Genre"),
         QStringLiteral("  Jazz\t"), QStringLiteral("jAzZ"), QStringLiteral("Rock")});

    QCOMPARE(categories.size(), BrowseLandingCategoryLimit);
    QVERIFY(categories[0].name == QStringLiteral("Jazz") && !categories[0].isCurated);
    QVERIFY(categories[1].name == QStringLiteral("Rock") && !categories[1].isCurated);
}

void TrackSearchPolicyTest::landingPreservesRealRankingThenFillsInCuratedOrder()
{
    const QList<BrowseLandingCategory> categories =
        browseLandingCategories({QStringLiteral("Rock"), QStringLiteral("Jazz")});
    QCOMPARE(categories.size(), BrowseLandingCategoryLimit);
    QVERIFY(categories[0].name == QStringLiteral("Rock") && categories[1].name == QStringLiteral("Jazz"));
    QVERIFY(!categories[0].isCurated && !categories[1].isCurated);
    QVERIFY(categories[2].name == QStringLiteral("Indie") && categories[2].isCurated);
    QVERIFY(categories[3].name == QStringLiteral("Classical") && categories[4].name == QStringLiteral("Electronic"));
}

void TrackSearchPolicyTest::landingMarksActualCuratedNameAsReal()
{
    const QList<BrowseLandingCategory> categories = browseLandingCategories({QStringLiteral("Indie")});
    QVERIFY(categories[0].name == QStringLiteral("Indie") && !categories[0].isCurated);
    QVERIFY(categories[1].name == QStringLiteral("Classical") && categories[1].isCurated);
}

void TrackSearchPolicyTest::landingUsesTwelveCuratedGenresForAnEmptyLibrary()
{
    const QList<BrowseLandingCategory> categories = browseLandingCategories({});
    const QList<QString> expected{
        QStringLiteral("Indie"), QStringLiteral("Classical"), QStringLiteral("Electronic"), QStringLiteral("Ghazal"),
        QStringLiteral("Jazz"),  QStringLiteral("Lo-fi"),     QStringLiteral("Pop"),        QStringLiteral("Hip-Hop"),
        QStringLiteral("Rock"),  QStringLiteral("Chill"),     QStringLiteral("Bollywood"),  QStringLiteral("Dance"),
    };
    QCOMPARE(categories.size(), expected.size());
    for (qsizetype index = 0; index < expected.size(); ++index) {
        QVERIFY(categories[index].name == expected[index] && categories[index].isCurated);
    }
}

void TrackSearchPolicyTest::landingCapsRealGenresAtTwelve()
{
    QList<QString> genres;
    for (int index = 0; index < 20; ++index) {
        genres.append(QStringLiteral("Genre") + QString::number(index));
    }
    const QList<BrowseLandingCategory> categories = browseLandingCategories(genres);
    QCOMPARE(categories.size(), BrowseLandingCategoryLimit);
    QVERIFY(categories.last().name == QStringLiteral("Genre11") && !categories.last().isCurated);
}

void TrackSearchPolicyTest::minimumQueryLength()
{
    QCOMPARE(MinimumSearchQueryLength, qsizetype{1});
}

void TrackSearchPolicyTest::landingSkipsUncategorizedAndFoldsAsciiOnly()
{
    const QList<BrowseLandingCategory> categories = browseLandingCategories(
        {QStringLiteral("Uncategorized"), QStringLiteral("Various Artists"), QStringLiteral("Électro"),
         QStringLiteral("éLECTRO"), QStringLiteral("Unknown")});
    QCOMPARE(categories.size(), BrowseLandingCategoryLimit);
    // Folding changes A to Z only, as before, so the two spellings that differ
    // in an accented capital are both kept; the labels that are not genres are
    // gone, so the built-in shortcuts follow at once.
    QCOMPARE(categories[0].name, QStringLiteral("Électro"));
    QVERIFY(!categories[0].isCurated);
    QCOMPARE(categories[1].name, QStringLiteral("éLECTRO"));
    QVERIFY(!categories[1].isCurated);
    QCOMPARE(categories[2].name, QStringLiteral("Indie"));
    QVERIFY(categories[2].isCurated);
}

QTEST_GUILESS_MAIN(TrackSearchPolicyTest)
#include "tst_track_search_policy.moc"
