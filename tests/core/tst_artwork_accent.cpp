#include <QObject>
#include <QTest>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/playback/ArtworkAccent.h"

using lmp::core::AccentRgb;
using lmp::core::ArtworkAccent;

namespace {

using Pixels = std::vector<std::uint8_t>;

// Appends `count` pixels of one colour: a generated picture, row by row.
void paint(Pixels& pixels, int count, std::array<std::uint8_t, 4> rgba)
{
    for (int index = 0; index < count; ++index) {
        pixels.insert(pixels.end(), rgba.begin(), rgba.end());
    }
}

// A 32 x 32 picture of one colour.
Pixels solid(std::array<std::uint8_t, 4> rgba)
{
    Pixels pixels;
    paint(pixels, ArtworkAccent::SampleSize * ArtworkAccent::SampleSize, rgba);
    return pixels;
}

std::optional<AccentRgb> some(int red, int green, int blue)
{
    return AccentRgb{red, green, blue};
}

std::optional<AccentRgb> vibrant(const Pixels& pixels)
{
    return ArtworkAccent::vibrantColour(std::span<const std::uint8_t>(pixels));
}

} // namespace

// The expected colours were worked out with the design's own arithmetic.
class ArtworkAccentTest : public QObject {
    Q_OBJECT

private slots:
    void solidColourIsItsOwnAccent();
    void nearlyWhiteBlackOrGreyPicturesHaveNone();
    void anEmptyPictureHasNone();
    void aTieGoesToTheColourSeenFirst();
    void aGroupAveragesItsPixels();
    void manyDullPixelsCanOutscoreFewVividOnes();
    void mostlyTransparentPixelsAreSkipped();
    void middleBrightnessScoresHigher();
    void glowToneKeepsTheHueAtTheGivenBrightness();
    void glowToneOfGreyUsesTheMinimumSaturation();
    void ambientIsTheGlowToneAtSeventyEightPercent();
};

void ArtworkAccentTest::solidColourIsItsOwnAccent()
{
    QCOMPARE(vibrant(solid({200, 30, 30, 255})), some(200, 30, 30));
}

void ArtworkAccentTest::nearlyWhiteBlackOrGreyPicturesHaveNone()
{
    // Brightest channel above 0.97, below 0.12, or saturation below 0.18.
    QVERIFY(!vibrant(solid({255, 0, 0, 255})));
    QVERIFY(!vibrant(solid({20, 5, 5, 255})));
    QVERIFY(!vibrant(solid({120, 110, 110, 255})));
}

void ArtworkAccentTest::anEmptyPictureHasNone()
{
    QVERIFY(!vibrant({}));
    QVERIFY(!vibrant(solid({200, 30, 30, 0})));
}

void ArtworkAccentTest::aTieGoesToTheColourSeenFirst()
{
    Pixels pixels;
    paint(pixels, 512, {40, 40, 200, 255});
    paint(pixels, 512, {200, 40, 40, 255});
    QCOMPARE(vibrant(pixels), some(40, 40, 200));
}

void ArtworkAccentTest::aGroupAveragesItsPixels()
{
    // Both colours fall in the same three-bit group.
    Pixels pixels;
    paint(pixels, 512, {200, 40, 40, 255});
    paint(pixels, 512, {210, 50, 40, 255});
    QCOMPARE(vibrant(pixels), some(205, 45, 40));
}

void ArtworkAccentTest::manyDullPixelsCanOutscoreFewVividOnes()
{
    Pixels pixels;
    paint(pixels, 700, {150, 120, 100, 255});
    paint(pixels, 324, {30, 180, 90, 255});
    QCOMPARE(vibrant(pixels), some(150, 120, 100));
}

void ArtworkAccentTest::mostlyTransparentPixelsAreSkipped()
{
    // 127 of 255 is under half opacity; 128 is not.
    Pixels pixels;
    paint(pixels, 900, {200, 40, 40, 127});
    paint(pixels, 124, {40, 40, 200, 128});
    QCOMPARE(vibrant(pixels), some(40, 40, 200));
}

void ArtworkAccentTest::middleBrightnessScoresHigher()
{
    // Equally saturated and equally many; the one nearer 62% brightness wins.
    Pixels pixels;
    paint(pixels, 512, {60, 12, 12, 255});
    paint(pixels, 512, {12, 158, 12, 255});
    QCOMPARE(vibrant(pixels), some(12, 158, 12));
}

void ArtworkAccentTest::glowToneKeepsTheHueAtTheGivenBrightness()
{
    QCOMPARE(ArtworkAccent::glowTone({0, 151, 178}, 0.5), (AccentRgb{13, 110, 128}));
    QCOMPARE(ArtworkAccent::glowTone({200, 40, 40}, 0.5), (AccentRgb{128, 25, 25}));
    QCOMPARE(ArtworkAccent::glowTone({40, 200, 40}, 0.78), (AccentRgb{40, 199, 40}));
    QCOMPARE(ArtworkAccent::glowTone({200, 40, 200}, 0.78), (AccentRgb{199, 40, 199}));
    QCOMPARE(ArtworkAccent::glowTone({255, 200, 0}, 0.78), (AccentRgb{199, 160, 20}));
    QCOMPARE(ArtworkAccent::glowTone({30, 180, 90}, 0.5), (AccentRgb{21, 128, 64}));
}

void ArtworkAccentTest::glowToneOfGreyUsesTheMinimumSaturation()
{
    // No hue (0, red) and saturation raised to 0.45.
    QCOMPARE(ArtworkAccent::glowTone({120, 120, 120}, 0.78), (AccentRgb{199, 109, 109}));
    QCOMPARE(ArtworkAccent::glowTone({0, 0, 0}, 0.5), (AccentRgb{128, 70, 70}));
}

void ArtworkAccentTest::ambientIsTheGlowToneAtSeventyEightPercent()
{
    QCOMPARE(ArtworkAccent::ambientFor(ArtworkAccent::IdleAccent), (AccentRgb{20, 172, 199}));
    QCOMPARE(ArtworkAccent::ambientFor({200, 40, 40}), (AccentRgb{199, 40, 40}));
    QCOMPARE(ArtworkAccent::ambientFor({30, 180, 90}), (AccentRgb{33, 199, 99}));
}

QTEST_GUILESS_MAIN(ArtworkAccentTest)
#include "tst_artwork_accent.moc"
