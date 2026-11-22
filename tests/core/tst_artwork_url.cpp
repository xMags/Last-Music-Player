#include <QObject>
#include <QString>
#include <QTest>

#include "core/net/ArtworkUrl.h"

using lmp::core::net::durableArtworkUrl;
using lmp::core::net::normalizeArtworkUrlForDisplay;

class ArtworkUrlTest : public QObject {
    Q_OBJECT

private slots:
    void displaySizeIsRaised();
    void durableAddressesAreKeptOrReduced();
    void unsafeAddressesAreRefused();
};

void ArtworkUrlTest::displaySizeIsRaised()
{
    QCOMPARE(normalizeArtworkUrlForDisplay(QStringLiteral(" https://images.example.test/cover=w60-h60-l90-rj ")),
             QStringLiteral("https://images.example.test/cover=w1200-h1200-l90-rj"));
    // Inside a relay's encoded field, without disturbing its other fields.
    QCOMPARE(normalizeArtworkUrlForDisplay(QStringLiteral(
                 "https://provider.example/v1/artwork?url=https%3A%2F%2Fimages.example.test%2Fcover%3Dw120-h120-l90-rj"
                 "&media_token=artwork.1.signature")),
             QStringLiteral(
                 "https://provider.example/v1/artwork?url=https%3A%2F%2Fimages.example.test%2Fcover%3Dw1200-h1200-l90-rj"
                 "&media_token=artwork.1.signature"));
    QCOMPARE(normalizeArtworkUrlForDisplay(
                 QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Music211/v4/cover.jpg/100x100bb.jpg")),
             QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Music211/v4/cover.jpg/1200x1200bb.jpg"));
    QCOMPARE(normalizeArtworkUrlForDisplay(QStringLiteral(
                 "https://relay.example/v1/artwork?url=https%3A%2F%2Fis1-ssl.mzstatic.com%2Fimage%2Fthumb%2Fcover%2F300x300cc.webp&x=1")),
             QStringLiteral(
                 "https://relay.example/v1/artwork?url=https%3A%2F%2Fis1-ssl.mzstatic.com%2Fimage%2Fthumb%2Fcover%2F1200x1200cc.webp&x=1"));
    // Another image host keeps its size segment.
    QCOMPARE(normalizeArtworkUrlForDisplay(QStringLiteral("https://images.example.test/100x100bb.jpg")),
             QStringLiteral("https://images.example.test/100x100bb.jpg"));
    // A size marker without both numbers is left alone.
    QCOMPARE(normalizeArtworkUrlForDisplay(QStringLiteral("https://images.example.test/a=w-h60")),
             QStringLiteral("https://images.example.test/a=w-h60"));
    QCOMPARE(normalizeArtworkUrlForDisplay(QStringLiteral("   ")), QString());
}

void ArtworkUrlTest::durableAddressesAreKeptOrReduced()
{
    QCOMPARE(durableArtworkUrl(QStringLiteral("https://images.example.test/cover.jpg")),
             QStringLiteral("https://images.example.test/cover.jpg"));
    QCOMPARE(durableArtworkUrl(QStringLiteral(
                 "https://provider.example.test/v1/artwork?url=https%3A%2F%2Fimages.example.test%2Fcover%3Dw120-h120-l90-rj"
                 "&media_token=artwork.1.signature")),
             QStringLiteral("https://images.example.test/cover=w1200-h1200-l90-rj"));
    QCOMPARE(durableArtworkUrl(QStringLiteral(
                 "https://provider.example.test/prefix/v1/artwork?url=https%3A%2F%2Fimages.example.test%2Fcover.jpg"
                 "&media_token=artwork.1.signature")),
             QStringLiteral("https://images.example.test/cover.jpg"));
}

void ArtworkUrlTest::unsafeAddressesAreRefused()
{
    QCOMPARE(durableArtworkUrl(QStringLiteral(
                 "https://provider.example.test/v1/artwork?url=https%3A%2F%2Fimages.example.test%2Fcover.jpg%3Faccess_token%3Dsecret"
                 "&media_token=artwork.1.signature")),
             QString());
    QCOMPARE(durableArtworkUrl(QStringLiteral("https://provider.example.test/v1/artwork?media_token=artwork.1.signature")),
             QString());
    QCOMPARE(durableArtworkUrl(QStringLiteral(
                 "https://provider.example.test/v1/stream/track?url=https%3A%2F%2Fimages.example.test%2Fcover.jpg"
                 "&media_token=stream.1.signature")),
             QString());
    QCOMPARE(durableArtworkUrl(QStringLiteral("file:///v1/artwork?url=https%3A%2F%2Fimages.example.test%2Fcover.jpg")),
             QString());
    QCOMPARE(durableArtworkUrl(QStringLiteral("https://images.example.test/cover.jpg?signature=x")), QString());
    QCOMPARE(durableArtworkUrl(QString()), QString());
}

QTEST_GUILESS_MAIN(ArtworkUrlTest)
#include "tst_artwork_url.moc"
