#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <cmath>
#include <memory>

#include "../core/FakePlaybackClock.h"
#include "../core/ModeServiceFixture.h"
#include "core/remote/ArtworkService.h"
#include "ui/images/ArtworkImageProvider.h"
#include "ui/player/CoverSource.h"

using lmp::core::remote::ArtworkService;
using lmp::test::FakeHttpTransport;
using lmp::ui::ArtworkImageProvider;
using lmp::ui::CoverSource;

namespace {

QByteArray pngOf(QSize size)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// A cover-like JPEG with fine detail: thin diagonal lines over a gradient.
QByteArray detailedJpegOf(int side)
{
    QImage image(side, side, QImage::Format_RGB32);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const int line = ((x + y) / 3) % 2 == 0 ? 90 : 0;
            image.setPixel(x, y, qRgb((x * 255 / side + line) % 256, (y * 255 / side) % 256, (128 + line) % 256));
        }
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG", 95);
    return bytes;
}

// The mean difference in luma (0-255) between two pictures of one size.
double meanLumaDifference(const QImage& first, const QImage& second)
{
    const QImage a = first.convertToFormat(QImage::Format_RGB32);
    const QImage b = second.convertToFormat(QImage::Format_RGB32);
    double total = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            total += std::abs(qGray(a.pixel(x, y)) - qGray(b.pixel(x, y)));
        }
    }
    return total / (double(a.width()) * a.height());
}

} // namespace

// Remote covers through "image://artwork/...": the id, decoding at the
// requested size, and a whole request through the artwork service.
class ArtworkImageProviderTest : public QObject {
    Q_OBJECT

private slots:
    void remoteCoversUseTheProvider();
    void decodeScalesDownOnly();
    void decodeKeepsDetailLikeAFullSizeReduction();
    void responseDeliversTheDecodedPicture();
    void responseReportsAFailure();
    void aCancelledResponseFinishesOnceWithoutAPicture();
};

void ArtworkImageProviderTest::remoteCoversUseTheProvider()
{
    lmp::core::Track track;
    track.artworkUrl = QStringLiteral("https://images.example.test/a b.jpg?x=1");
    const QUrl display = CoverSource::forTrack(track).displayUrl();
    QCOMPARE(display.scheme(), QStringLiteral("image"));
    QCOMPARE(display.host(), QStringLiteral("artwork"));
    const std::optional<QString> url = CoverSource::remoteUrlFromProviderId(display.path().mid(1));
    QVERIFY(url.has_value());
    QCOMPARE(QUrl(*url), QUrl(track.artworkUrl));
    QVERIFY(!CoverSource::remoteUrlFromProviderId(u"not base64 !").has_value());
    const QString fileId = QString::fromLatin1(QByteArray("file:///C:/a.jpg").toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    QVERIFY(!CoverSource::remoteUrlFromProviderId(fileId).has_value());

    // An account cover link is a remote cover too.
    lmp::core::Track account;
    account.artworkUrl = QStringLiteral("/v1/music/art/AbCdEfGhIjKlMnOpQrSt_-");
    account.sourceKind = QStringLiteral("remote");
    const CoverSource cover = CoverSource::forTrack(account);
    QCOMPARE(cover.kind, CoverSource::Kind::Remote);
    QCOMPARE(CoverSource::remoteUrlFromProviderId(cover.displayUrl().path().mid(1)),
             std::optional<QString>(account.artworkUrl));
}

void ArtworkImageProviderTest::decodeScalesDownOnly()
{
    const QByteArray bytes = pngOf(QSize(800, 400));
    QCOMPARE(ArtworkImageProvider::decode(bytes, QSize(100, 100)).size(), QSize(200, 100));
    QCOMPARE(ArtworkImageProvider::decode(bytes, QSize(2000, 2000)).size(), QSize(800, 400));
    QCOMPARE(ArtworkImageProvider::decode(bytes, QSize()).size(), QSize(800, 400));
    QVERIFY(ArtworkImageProvider::decode("not a picture", QSize(10, 10)).isNull());
}

void ArtworkImageProviderTest::decodeKeepsDetailLikeAFullSizeReduction()
{
    // A tile's size at 125% scaling. Letting the JPEG decoder shrink the
    // picture itself differs from this reference by more than 1 level.
    const QByteArray bytes = detailedJpegOf(1200);
    const QSize fit(246, 246);
    const QImage reference = QImage::fromData(bytes).scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage decoded = ArtworkImageProvider::decode(bytes, fit);
    QCOMPARE(decoded.size(), fit);
    QVERIFY2(meanLumaDifference(decoded, reference) < 0.25,
             qPrintable(QString::number(meanLumaDifference(decoded, reference))));
}

void ArtworkImageProviderTest::responseDeliversTheDecodedPicture()
{
    lmp::test::ModeServiceFixture modes;
    (void)modes.modes.start();
    FakePlaybackClock clock;
    ArtworkService service({.client = modes.session.client,
                            .session = modes.session.service,
                            .mode = modes.modes,
                            .http = modes.session.transport,
                            .clock = clock});
    modes.session.transport.script(QStringLiteral("/cover.png"), FakeHttpTransport::ok(pngOf(QSize(600, 600))));

    lmp::core::Track track;
    track.artworkUrl = QStringLiteral("https://images.example.test/cover.png");
    ArtworkImageProvider provider(service);
    std::unique_ptr<QQuickImageResponse> response(
        provider.requestImageResponse(CoverSource::forTrack(track).displayUrl().path().mid(1), QSize(150, 150)));
    QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
    QVERIFY(finished.wait(3000));
    QVERIFY(response->errorString().isEmpty());
    std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
    QCOMPARE(texture->image().size(), QSize(150, 150));
}

void ArtworkImageProviderTest::responseReportsAFailure()
{
    lmp::test::ModeServiceFixture modes;
    (void)modes.modes.start();
    FakePlaybackClock clock;
    ArtworkService service({.client = modes.session.client,
                            .session = modes.session.service,
                            .mode = modes.modes,
                            .http = modes.session.transport,
                            .clock = clock});
    modes.session.transport.script(QStringLiteral("/gone.png"),
                                   FakeHttpTransport::failed(lmp::core::FailureKind::NotFound, 404));
    lmp::core::Track track;
    track.artworkUrl = QStringLiteral("https://images.example.test/gone.png");
    ArtworkImageProvider provider(service);
    std::unique_ptr<QQuickImageResponse> response(
        provider.requestImageResponse(CoverSource::forTrack(track).displayUrl().path().mid(1), QSize(64, 64)));
    QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
    QVERIFY(finished.wait(3000));
    QVERIFY(!response->errorString().isEmpty());
}

void ArtworkImageProviderTest::aCancelledResponseFinishesOnceWithoutAPicture()
{
    lmp::test::ModeServiceFixture modes;
    (void)modes.modes.start();
    FakePlaybackClock clock;
    ArtworkService service({.client = modes.session.client,
                            .session = modes.session.service,
                            .mode = modes.modes,
                            .http = modes.session.transport,
                            .clock = clock});
    lmp::core::Track track;
    track.artworkUrl = QStringLiteral("https://images.example.test/slow.png");
    ArtworkImageProvider provider(service);
    std::unique_ptr<QQuickImageResponse> response(
        provider.requestImageResponse(CoverSource::forTrack(track).displayUrl().path().mid(1), QSize(64, 64)));
    QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
    QTRY_COMPARE(modes.session.transport.waitingCount(QStringLiteral("/slow.png")), 1);

    // The engine gives up on it while the picture is still on its way: it
    // still has to finish, or the engine never deletes it.
    response->cancel();
    QVERIFY(finished.wait(3000));
    QVERIFY(!response->errorString().isEmpty());

    // The picture arriving afterwards is not decoded or delivered again.
    QVERIFY(modes.session.transport.answer(QStringLiteral("/slow.png"), FakeHttpTransport::ok(pngOf(QSize(600, 600)))));
    QTest::qWait(200);
    QCOMPARE(finished.count(), 1);
    QVERIFY(!response->errorString().isEmpty());
}

QTEST_MAIN(ArtworkImageProviderTest)
#include "tst_artwork_image_provider.moc"
