#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/library/GenreArtworkRecipe.h"

using lmp::core::allGenreArtworkRecipes;
using lmp::core::GenreArtworkPrimitive;
using lmp::core::GenreArtworkPrimitiveKind;
using lmp::core::GenreArtworkRecipe;
using lmp::core::normalizeGenreArtworkName;
using lmp::core::resolveGenreArtworkRecipe;

class GenreArtworkRecipeTest : public QObject {
    Q_OBJECT

private slots:
    void genreArtworkRecipe();
    void builderGeometry();
    void normalisedNames();
};

void GenreArtworkRecipeTest::genreArtworkRecipe()
{
    const QList<QString> expectedNames{
        QStringLiteral("Pop"),        QStringLiteral("Hip-Hop"),      QStringLiteral("Electronic"),
        QStringLiteral("Chill"),      QStringLiteral("Rock"),         QStringLiteral("Metal"),
        QStringLiteral("Jazz"),       QStringLiteral("Blues"),        QStringLiteral("Classical"),
        QStringLiteral("Country"),    QStringLiteral("R&B"),          QStringLiteral("Soul"),
        QStringLiteral("Folk"),       QStringLiteral("Reggae"),       QStringLiteral("Punk"),
        QStringLiteral("Indie"),      QStringLiteral("Dance"),        QStringLiteral("House"),
        QStringLiteral("Techno"),     QStringLiteral("Ambient"),      QStringLiteral("Lo-fi"),
        QStringLiteral("Soundtrack"), QStringLiteral("World"),        QStringLiteral("Latin"),
        QStringLiteral("Bollywood"),  QStringLiteral("Ghazal"),       QStringLiteral("Unknown Genre"),
    };
    const QList<GenreArtworkRecipe>& recipes = allGenreArtworkRecipes();
    QCOMPARE(recipes.size(), expectedNames.size());
    for (qsizetype index = 0; index < expectedNames.size(); ++index) {
        QCOMPARE(recipes[index].name, expectedNames[index]);
        QVERIFY(!recipes[index].primitives.isEmpty());
        const QString padded = QStringLiteral(" \t") + expectedNames[index] + QStringLiteral("\r\n");
        QCOMPARE(resolveGenreArtworkRecipe(padded).name, expectedNames[index]);
    }

    QCOMPARE(resolveGenreArtworkRecipe(QStringLiteral("hIp-HoP")).name, QStringLiteral("Hip-Hop"));
    QCOMPARE(resolveGenreArtworkRecipe(QString()).name, QStringLiteral("Unknown Genre"));
    QCOMPARE(resolveGenreArtworkRecipe(QStringLiteral("Shoegaze")).name, QStringLiteral("Unknown Genre"));

    const GenreArtworkRecipe& pop = resolveGenreArtworkRecipe(QStringLiteral("Pop"));
    QVERIFY(pop.gradientStart == 0xFFFF5FBF && pop.gradientEnd == 0xFFA855F7);
    QVERIFY(pop.primitives.size() == 2 && pop.primitives[0].kind == GenreArtworkPrimitiveKind::Ring
            && pop.primitives[0].x == 11.0 && pop.primitives[0].y == 11.0 && pop.primitives[0].width == 74.0
            && pop.primitives[0].strokeWidth == 3.0);

    const GenreArtworkRecipe& folk = resolveGenreArtworkRecipe(QStringLiteral("Folk"));
    QVERIFY(folk.primitives.size() == 1 && folk.primitives[0].kind == GenreArtworkPrimitiveKind::Triangle
            && folk.primitives[0].x == 6.0 && folk.primitives[0].y == 14.0 && folk.primitives[0].width == 84.0
            && folk.primitives[0].height == 70.0);

    const GenreArtworkRecipe& unknown = resolveGenreArtworkRecipe(QStringLiteral("Unknown Genre"));
    QVERIFY(unknown.gradientStart == 0xFFCBD5E1 && unknown.gradientEnd == 0xFF64748B
            && unknown.primitives[0].dashed);
}

void GenreArtworkRecipeTest::builderGeometry()
{
    // Electronic: columns on the y = 84 baseline, rounded by half their width.
    const GenreArtworkPrimitive& column = resolveGenreArtworkRecipe(QStringLiteral("Electronic")).primitives[1];
    QVERIFY(column.kind == GenreArtworkPrimitiveKind::Column);
    QCOMPARE(column.x, 29.0);
    QCOMPARE(column.y, 24.0);
    QCOMPARE(column.width, 10.0);
    QCOMPARE(column.height, 60.0);
    QCOMPARE(column.cornerRadius, 5.0);

    // Hip-Hop: a rotated bar keeps its corner position and gets a pill radius.
    const GenreArtworkPrimitive& bar = resolveGenreArtworkRecipe(QStringLiteral("Hip-Hop")).primitives[2];
    QVERIFY(bar.kind == GenreArtworkPrimitiveKind::Bar);
    QCOMPARE(bar.x, 30.0);
    QCOMPARE(bar.y, 10.0);
    QCOMPARE(bar.cornerRadius, 4.0);
    QCOMPARE(bar.rotation, 34.0);

    // Ambient: ovals are centred and rounded by half their height.
    const GenreArtworkPrimitive& oval = resolveGenreArtworkRecipe(QStringLiteral("Ambient")).primitives[0];
    QVERIFY(oval.kind == GenreArtworkPrimitiveKind::Oval);
    QCOMPARE(oval.x, 2.0);
    QCOMPARE(oval.y, 25.0);
    QCOMPARE(oval.cornerRadius, 23.0);

    const GenreArtworkPrimitive& arch = resolveGenreArtworkRecipe(QStringLiteral("Country")).primitives[0];
    QVERIFY(arch.kind == GenreArtworkPrimitiveKind::Arch);
    QVERIFY(arch.x == 15.0 && arch.y == 14.0 && arch.width == 60.0 && arch.strokeWidth == 3.0
            && arch.cornerRadius == 33.0);

    const GenreArtworkPrimitive& square = resolveGenreArtworkRecipe(QStringLiteral("Metal")).primitives[1];
    QVERIFY(square.kind == GenreArtworkPrimitiveKind::FilledSquare);
    QVERIFY(square.x == 36.0 && square.y == 36.0 && square.width == 24.0 && square.cornerRadius == 3.0
            && square.rotation == 45.0);
    QCOMPARE(resolveGenreArtworkRecipe(QStringLiteral("Techno")).primitives.size(), qsizetype{9});
}

void GenreArtworkRecipeTest::normalisedNames()
{
    QCOMPARE(normalizeGenreArtworkName(QStringLiteral("  Lo-FI ")), QStringLiteral("lo-fi"));
    QCOMPARE(normalizeGenreArtworkName(QStringLiteral(" \t ")), QString());
    QCOMPARE(resolveGenreArtworkRecipe(QStringLiteral("r&b")).name, QStringLiteral("R&B"));
}

QTEST_GUILESS_MAIN(GenreArtworkRecipeTest)
#include "tst_genre_artwork_recipe.moc"
