#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

#include "../core/ArtworkFixtures.h"
#include "../core/PlayerFixture.h"
#include "core/playback/ArtworkAccent.h"
#include "ui/player/AmbientAccentSampler.h"
#include "ui/player/CoverSource.h"
#include "ui/theme/Theme.h"

using lmp::core::AccentRgb;
using lmp::core::ArtworkAccent;
using lmp::core::Track;
using lmp::ui::AmbientAccentSampler;

namespace {

QColor toColor(AccentRgb rgb)
{
    return QColor(rgb.red, rgb.green, rgb.blue);
}

QByteArray pngOf(const QColor& color, QSize size = QSize(64, 48))
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace

// The panel tint against a real player controller, with covers generated as
// picture files and as pictures inside generated audio files.
class AmbientAccentSamplerTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void nothingPlayingUsesTheThemeDefault();
    void aCoverFileTintsWithItsAccent();
    void anEmbeddedCoverTintsWithItsAccent();
    void aSongWithoutACoverKeepsItsPlaceholderTint();
    void onlyTheNewestSongsCoverIsApplied();
    void theThemeFollows();
    void samplingStretchesThePictureToTheSquare();

private:
    void play(const Track& track);

    QTemporaryDir m_dir;
    std::unique_ptr<PlayerFixture> m_fixture;
    std::unique_ptr<lmp::core::PlayerController> m_player;
    std::unique_ptr<AmbientAccentSampler> m_sampler;
};

void AmbientAccentSamplerTest::init()
{
    QVERIFY(m_dir.isValid());
    m_fixture = std::make_unique<PlayerFixture>();
    QVERIFY(m_fixture->ok());
    m_player = m_fixture->makeController();
    m_sampler = std::make_unique<AmbientAccentSampler>(*m_player);
}

void AmbientAccentSamplerTest::cleanup()
{
    m_sampler.reset();
    m_player.reset();
    m_fixture.reset();
}

void AmbientAccentSamplerTest::play(const Track& track)
{
    m_player->playTrack(track);
    settle();
}

void AmbientAccentSamplerTest::nothingPlayingUsesTheThemeDefault()
{
    QVERIFY(!m_sampler->ambient().isValid());
}

void AmbientAccentSamplerTest::aCoverFileTintsWithItsAccent()
{
    const QString cover = m_dir.filePath(QStringLiteral("red.png"));
    QVERIFY(writeFile(cover, pngOf(QColor(200, 40, 40))));
    Track track = m_fixture->song(QStringLiteral("Red"));
    track.artworkUrl = QUrl::fromLocalFile(cover).toString();
    play(track);
    QTRY_COMPARE(m_sampler->ambient(), toColor(ArtworkAccent::ambientFor({200, 40, 40})));
}

void AmbientAccentSamplerTest::anEmbeddedCoverTintsWithItsAccent()
{
    namespace fx = artwork_fixtures;
    const QByteArray png = pngOf(QColor(40, 40, 200));
    const fx::Bytes picture(png.begin(), png.end());
    const fx::Bytes file = fx::makeId3File(3, fx::makeFrame("APIC", fx::makeApicContent("image/png", 3, picture), 3));
    const QString path = m_dir.filePath(QStringLiteral("blue.mp3"));
    QVERIFY(writeFile(path, fx::toByteArray(file)));

    Track track = m_fixture->song(QStringLiteral("Blue"));
    track.filePath = path;
    play(track);
    QTRY_COMPARE(m_sampler->ambient(), toColor(AccentRgb{40, 40, 199}));

    // The same picture through the image provider's path.
    const lmp::ui::CoverSource source = lmp::ui::CoverSource::forTrack(track);
    QCOMPARE(source.kind, lmp::ui::CoverSource::Kind::Embedded);
    const QString id = source.displayUrl().path().mid(1);
    QCOMPARE(lmp::ui::CoverSource::embeddedPathFromProviderId(id), std::optional<QString>(path));
    QCOMPARE(source.load().size(), QSize(64, 48));
}

void AmbientAccentSamplerTest::aSongWithoutACoverKeepsItsPlaceholderTint()
{
    // Its file does not exist, so nothing can be read from it.
    const Track track = m_fixture->song(QStringLiteral("Plain"));
    play(track);
    const QColor placeholder = toColor(ArtworkAccent::ambientFor(
        AmbientAccentSampler::placeholderAccent(lmp::ui::CoverSource::placeholderIdFor(track))));
    QCOMPARE(m_sampler->ambient(), placeholder);
    QTest::qWait(200);
    QCOMPARE(m_sampler->ambient(), placeholder);
}

void AmbientAccentSamplerTest::onlyTheNewestSongsCoverIsApplied()
{
    const QString red = m_dir.filePath(QStringLiteral("red.png"));
    const QString green = m_dir.filePath(QStringLiteral("green.png"));
    QVERIFY(writeFile(red, pngOf(QColor(200, 40, 40), QSize(1600, 1600))));
    QVERIFY(writeFile(green, pngOf(QColor(30, 180, 90))));
    Track first = m_fixture->song(QStringLiteral("First"));
    first.artworkUrl = QUrl::fromLocalFile(red).toString();
    Track second = m_fixture->song(QStringLiteral("Second"));
    second.artworkUrl = QUrl::fromLocalFile(green).toString();
    play(first);
    play(second);
    const QColor expected = toColor(ArtworkAccent::ambientFor({30, 180, 90}));
    QTRY_COMPARE(m_sampler->ambient(), expected);
    QTest::qWait(300);
    QCOMPARE(m_sampler->ambient(), expected);
}

void AmbientAccentSamplerTest::theThemeFollows()
{
    lmp::ui::Theme theme;
    const QColor themeDefault = theme.ambient();
    m_sampler->setTheme(&theme);
    QCOMPARE(theme.ambient(), themeDefault);
    const QString cover = m_dir.filePath(QStringLiteral("red.png"));
    QVERIFY(writeFile(cover, pngOf(QColor(200, 40, 40))));
    Track track = m_fixture->song(QStringLiteral("Red"));
    track.artworkUrl = QUrl::fromLocalFile(cover).toString();
    play(track);
    QTRY_COMPARE(theme.ambient(), toColor(ArtworkAccent::ambientFor({200, 40, 40})));
    m_sampler.reset(); // before the theme goes
}

void AmbientAccentSamplerTest::samplingStretchesThePictureToTheSquare()
{
    // A wide picture keeps its proportions of colour once drawn into the
    // square: the left quarter blue, the rest red.
    QImage image(400, 100, QImage::Format_RGB32);
    image.fill(QColor(200, 40, 40));
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < 100; ++x) {
            image.setPixelColor(x, y, QColor(40, 40, 200));
        }
    }
    // Red wins on count.
    QCOMPARE(AmbientAccentSampler::sampleImage(image), std::optional<AccentRgb>(AccentRgb{200, 40, 40}));
    QVERIFY(!AmbientAccentSampler::sampleImage(QImage()));
}

QTEST_MAIN(AmbientAccentSamplerTest)
#include "tst_ambient_accent_sampler.moc"
