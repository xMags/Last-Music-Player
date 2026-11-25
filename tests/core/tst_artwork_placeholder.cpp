#include <QObject>
#include <QString>
#include <QTest>

#include "core/model/ArtworkPlaceholder.h"

using lmp::core::ArtworkPlaceholder;

// The expected values were worked out by hand from the hash: start at 0, for
// each UTF-16 unit take hash * 31 + unit, keep the low 32 bits, and finally
// take the remainder by the number of gradients (9).
class ArtworkPlaceholderTest : public QObject {
    Q_OBJECT

private slots:
    void gradientIndexMatchesHandValues();
    void gradientIndexWrapsAtThirtyTwoBits();
    void gradientIndexHandlesEdgeCases();
    void initialsTakeTheFirstLetterOfTwoWords();
    void initialsOfBlankNameIsL();
    void initialsAreCutBeforeUpperCasing();
    void tileLabelIsTheFirstCharacter();
    void charactersOutsideTheBasicPlaneStayWhole();
};

void ArtworkPlaceholderTest::gradientIndexMatchesHandValues()
{
    // "a": 97; 97 % 9 = 7.
    QCOMPARE(ArtworkPlaceholder::gradientIndexForId(QStringLiteral("a"), 9), 7);
    // "ab": 97 * 31 + 98 = 3105; 3105 = 9 * 345, so 0.
    QCOMPARE(ArtworkPlaceholder::gradientIndexForId(QStringLiteral("ab"), 9), 0);
    // "track": 110621003; 110621003 % 9 = 5.
    QCOMPARE(ArtworkPlaceholder::gradientIndexForId(QStringLiteral("track"), 9), 5);
}

void ArtworkPlaceholderTest::gradientIndexWrapsAtThirtyTwoBits()
{
    // "Daft Punk" passes 2^31: hash 2585154423, % 9 = 3. A signed 32-bit hash
    // would give a negative number here.
    QCOMPARE(ArtworkPlaceholder::gradientIndexForId(QStringLiteral("Daft Punk"), 9), 3);
    // Twenty "x" wrap several times: hash 845403648, % 9 = 6.
    QCOMPARE(ArtworkPlaceholder::gradientIndexForId(QString(20, QLatin1Char('x')), 9), 6);
}

void ArtworkPlaceholderTest::gradientIndexHandlesEdgeCases()
{
    QCOMPARE(ArtworkPlaceholder::gradientIndexForId(QString(), 9), 0);
    QCOMPARE(ArtworkPlaceholder::gradientIndexForId(QStringLiteral("a"), 0), 0);
}

void ArtworkPlaceholderTest::initialsTakeTheFirstLetterOfTwoWords()
{
    QCOMPARE(ArtworkPlaceholder::initialsFor(QStringLiteral("  daft   punk ")), QStringLiteral("DP"));
    QCOMPARE(ArtworkPlaceholder::initialsFor(QStringLiteral("a b c")), QStringLiteral("AB"));
    QCOMPARE(ArtworkPlaceholder::initialsFor(QStringLiteral("Adele")), QStringLiteral("A"));
    // Tabs and line breaks separate words too.
    QCOMPARE(ArtworkPlaceholder::initialsFor(QStringLiteral("x\ty\nz")), QStringLiteral("XY"));
}

void ArtworkPlaceholderTest::initialsOfBlankNameIsL()
{
    QCOMPARE(ArtworkPlaceholder::initialsFor(QString()), QStringLiteral("L"));
    QCOMPARE(ArtworkPlaceholder::initialsFor(QStringLiteral("   ")), QStringLiteral("L"));
}

void ArtworkPlaceholderTest::initialsAreCutBeforeUpperCasing()
{
    // U+00DF (sharp s) upper-cases to "SS"; the cut to two happens first.
    QCOMPARE(ArtworkPlaceholder::initialsFor(QStringLiteral("ßa yo")), QStringLiteral("SSY"));
}

void ArtworkPlaceholderTest::tileLabelIsTheFirstCharacter()
{
    QCOMPARE(ArtworkPlaceholder::tileLabelFor(QStringLiteral("  hello")), QStringLiteral("h"));
    QCOMPARE(ArtworkPlaceholder::tileLabelFor(QString()), QStringLiteral("M"));
    QCOMPARE(ArtworkPlaceholder::tileLabelFor(QStringLiteral(" ")), QStringLiteral("M"));
}

void ArtworkPlaceholderTest::charactersOutsideTheBasicPlaneStayWhole()
{
    using lmp::core::ArtworkPlaceholder;
    const QString note = QString(QChar(0xD83C)) + QChar(0xDFB5); // a musical note emoji, two UTF-16 units
    QCOMPARE(ArtworkPlaceholder::tileLabelFor(note + QStringLiteral(" Song")), note);
    QCOMPARE(ArtworkPlaceholder::initialsFor(note + QStringLiteral(" band")), note + QStringLiteral("B"));
}

QTEST_GUILESS_MAIN(ArtworkPlaceholderTest)
#include "tst_artwork_placeholder.moc"
