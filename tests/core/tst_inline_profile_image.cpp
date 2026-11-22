#include <QObject>
#include <QString>
#include <QTest>

#include "core/net/InlineProfileImage.h"

using lmp::core::net::isInlineProfileImageData;
using lmp::core::net::isSafeInlineProfileImage;

class InlineProfileImageTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsStillImages();
    void refusesOtherShapes();
    void refusesMalformedPayloads();
    void refusesOversizedPictures();
    void tellsInlineDataFromAddresses();
};

void InlineProfileImageTest::acceptsStillImages()
{
    // A one-pixel GIF, the smallest payload that exercises the whole shape.
    QVERIFY(isSafeInlineProfileImage(
        QStringLiteral("data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")));
    // The scheme and media type are case-insensitive; the payload is not.
    QVERIFY(isSafeInlineProfileImage(QStringLiteral("DATA:IMAGE/PNG;BASE64,AAAA")));
    QVERIFY(isSafeInlineProfileImage(QStringLiteral("data:image/jpeg;base64,/9j/4AAQ")));
    QVERIFY(isSafeInlineProfileImage(QStringLiteral("data:image/webp;base64,AAA=")));
    QVERIFY(isSafeInlineProfileImage(QStringLiteral("data:image/bmp;base64,AA==")));
}

void InlineProfileImageTest::refusesOtherShapes()
{
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("https://cdn.example.test/a.png")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:image/svg+xml;base64,AAAA")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:text/html;base64,AAAA")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:application/octet-stream;base64,AAAA")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:image/png,AAAA")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:image/png;base64,")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:image/png;base64")));
    QVERIFY(!isSafeInlineProfileImage(QString()));
}

void InlineProfileImageTest::refusesMalformedPayloads()
{
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:image/png;base64,AA*A")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:image/png;base64,AA-_")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:image/png;base64,AAAAA")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:image/png;base64,A===")));
    QVERIFY(!isSafeInlineProfileImage(QStringLiteral("data:image/png;base64,====")));
}

void InlineProfileImageTest::refusesOversizedPictures()
{
    const QString header = QStringLiteral("data:image/png;base64,");
    const QString oversized = header + QString(1'500'004, QLatin1Char('A'));
    QVERIFY(!isSafeInlineProfileImage(oversized));
    // Just under the limit is still accepted.
    const qsizetype payload = (1'500'000 - header.size()) / 4 * 4;
    QVERIFY(isSafeInlineProfileImage(header + QString(payload, QLatin1Char('A'))));
}

void InlineProfileImageTest::tellsInlineDataFromAddresses()
{
    // An address is not inline data; the scheme is compared without case.
    QVERIFY(isInlineProfileImageData(
        QStringLiteral("data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")));
    QVERIFY(!isInlineProfileImageData(QStringLiteral("https://cdn.example.test/a.png")));
    QVERIFY(isInlineProfileImageData(QStringLiteral("DATA:")));
    QVERIFY(!isInlineProfileImageData(QStringLiteral("data")));
    QVERIFY(!isInlineProfileImageData(QString()));
}

QTEST_GUILESS_MAIN(InlineProfileImageTest)
#include "tst_inline_profile_image.moc"
