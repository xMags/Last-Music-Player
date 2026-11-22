#include <QByteArray>
#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "ArtworkFixtures.h"
#include "core/library/EmbeddedArtwork.h"

#include <cstdint>
#include <optional>
#include <vector>

using lmp::core::artworkFileExtension;
using lmp::core::EmbeddedArtwork;
using lmp::core::tryReadEmbeddedArtwork;
using lmp::core::tryReadEmbeddedArtworkFromFile;

namespace fx = artwork_fixtures;
using fx::Bytes;
using fx::FrameSizeEncoding;

namespace {

std::optional<EmbeddedArtwork> read(const Bytes& file)
{
    const QByteArray bytes = fx::toByteArray(file);
    return tryReadEmbeddedArtwork(bytes);
}

QByteArray bytesOf(const Bytes& bytes)
{
    return fx::toByteArray(bytes);
}

} // namespace

class EmbeddedArtworkTest : public QObject {
    Q_OBJECT

private slots:
    void id3v23Apic();
    void id3v24SyncSafeAndTrailingAudio();
    void id3v24PlainBigEndianFrameSize();
    void id3v23Unsynchronisation();
    void id3v24FrameLevelUnsynchronisation();
    void id3v24DataLengthIndicator();
    void id3v22Pic();
    void frontCoverWinsOverEarlierPicture();
    void nonFrontCoverIsStillUsable();
    void unicodeDescriptionIsSkipped();
    void urlPayloadIsRejected();
    void nonImagePayloadIsRejected();
    void oversizedAndTruncatedInputsAreRejected();
    void flacPictureBlock();
    void flacBehindId3Tag();
    void flacUrlPictureIsRejected();
    void mp4CoverAtom();
    void mp4MetaWithoutVersionFlags();
    void mp4LyingAtomSizeIsRejected();
    void artworkFileExtensionMapping();
    void everySniffedImageType();
    void moreContainerLayouts();
    void readsFromFile();
};

void EmbeddedArtworkTest::id3v23Apic()
{
    const Bytes picture = fx::makeJpeg(0xAA);
    const Bytes frames = fx::makeFrame("APIC", fx::makeApicContent("image/jpeg", 0x03, picture), 3);
    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(3, frames));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
    QCOMPARE(result->mimeType, QStringLiteral("image/jpeg"));
}

void EmbeddedArtworkTest::id3v24SyncSafeAndTrailingAudio()
{
    const Bytes picture = fx::makeJpeg(0xBB, 300);
    const Bytes frames = fx::makeFrame("APIC", fx::makeApicContent("image/jpeg", 0x03, picture), 4);
    // Real files continue into audio after the tag; the parse must stop at the
    // declared tag size.
    const Bytes audio(2048, 0xFF);
    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(4, frames, 0, audio));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
}

void EmbeddedArtworkTest::id3v24PlainBigEndianFrameSize()
{
    // Some taggers write v2.4 frame sizes as plain big-endian. A size above
    // 0x7F then decodes far too small as syncsafe.
    const Bytes picture = fx::makeJpeg(0xCC, 400);
    const Bytes content = fx::makeApicContent("image/jpeg", 0x03, picture);
    QVERIFY(content.size() > 0x7F);

    const Bytes frames = fx::makeFrame("APIC", content, 4, FrameSizeEncoding::BigEndian);
    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(4, frames));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
}

void EmbeddedArtworkTest::id3v23Unsynchronisation()
{
    // A JPEG with 0xFF bytes in its payload, which the tagger has to escape.
    const Bytes picture{0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0xFF, 0xFE, 0x01, 0x02};
    const Bytes frames = fx::makeFrame("APIC", fx::makeApicContent("image/jpeg", 0x03, picture), 3);

    // Up to v2.3 the finished frame block is unsynchronised as a whole, so the
    // frame sizes describe the decoded bytes.
    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(3, fx::unsynchronise(frames), 0x80));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
}

void EmbeddedArtworkTest::id3v24FrameLevelUnsynchronisation()
{
    const Bytes picture{0xFF, 0xD8, 0xFF, 0xE0, 0x22, 0xFF, 0x33};

    // In v2.4 unsynchronisation is per frame and the size counts encoded bytes.
    const Bytes encoded = fx::unsynchronise(fx::makeApicContent("image/jpeg", 0x03, picture));
    const Bytes frames = fx::makeFrame("APIC", encoded, 4, FrameSizeEncoding::SyncSafe, 0x0002);
    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(4, frames));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
}

void EmbeddedArtworkTest::id3v24DataLengthIndicator()
{
    const Bytes picture = fx::makeJpeg(0xDD);
    const Bytes content = fx::makeApicContent("image/jpeg", 0x03, picture);

    Bytes withIndicator;
    fx::appendSyncSafe32(withIndicator, static_cast<std::uint32_t>(content.size()));
    fx::append(withIndicator, content);

    const Bytes frames = fx::makeFrame("APIC", withIndicator, 4, FrameSizeEncoding::SyncSafe, 0x0001);
    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(4, frames));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
}

void EmbeddedArtworkTest::id3v22Pic()
{
    const Bytes picture = fx::makeJpeg(0xEE);
    const Bytes frames = fx::makeFrame("PIC", fx::makePicContent("JPG", 0x03, picture), 2);
    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(2, frames));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
}

void EmbeddedArtworkTest::frontCoverWinsOverEarlierPicture()
{
    const Bytes back = fx::makeJpeg(0x11);
    const Bytes front = fx::makePng();

    Bytes frames;
    fx::append(frames, fx::makeFrame("APIC", fx::makeApicContent("image/jpeg", 0x04, back), 3));
    fx::append(frames, fx::makeFrame("APIC", fx::makeApicContent("image/png", 0x03, front), 3));

    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(3, frames));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(front));
    QCOMPARE(result->mimeType, QStringLiteral("image/png"));
}

void EmbeddedArtworkTest::nonFrontCoverIsStillUsable()
{
    const Bytes other = fx::makeJpeg(0x77);
    const Bytes frames = fx::makeFrame("APIC", fx::makeApicContent("image/jpeg", 0x00, other), 3);
    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(3, frames));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(other));
}

void EmbeddedArtworkTest::unicodeDescriptionIsSkipped()
{
    const Bytes picture = fx::makeJpeg(0x55);
    // UTF-16 descriptions end with two zero bytes on an even boundary.
    Bytes content{0x01};
    fx::appendAscii(content, "image/jpeg");
    content.push_back(0x00);
    content.push_back(0x03);
    content.push_back(0xFF);
    content.push_back(0xFE); // byte order mark
    content.push_back('c');
    content.push_back(0x00);
    content.push_back(0x00);
    content.push_back(0x00); // terminator
    fx::append(content, picture);

    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(3, fx::makeFrame("APIC", content, 3)));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
}

void EmbeddedArtworkTest::urlPayloadIsRejected()
{
    Bytes url;
    fx::appendAscii(url, "https://media.example.test/cover.jpg");
    const Bytes frames = fx::makeFrame("APIC", fx::makeApicContent("-->", 0x03, url), 3);

    QVERIFY(!read(fx::makeId3File(3, frames)).has_value());
}

void EmbeddedArtworkTest::nonImagePayloadIsRejected()
{
    const Bytes payload(64, 0x42);
    const Bytes frames = fx::makeFrame("APIC", fx::makeApicContent("image/jpeg", 0x03, payload), 3);

    QVERIFY(!read(fx::makeId3File(3, frames)).has_value());
}

void EmbeddedArtworkTest::oversizedAndTruncatedInputsAreRejected()
{
    const Bytes picture = fx::makeJpeg(0x99);
    const Bytes content = fx::makeApicContent("image/jpeg", 0x03, picture);

    // A frame that claims far more bytes than the tag holds.
    Bytes lyingFrame;
    fx::appendAscii(lyingFrame, "APIC");
    fx::appendBigEndian32(lyingFrame, 0x00FFFFFFu);
    lyingFrame.push_back(0x00);
    lyingFrame.push_back(0x00);
    fx::append(lyingFrame, content);
    QVERIFY(!read(fx::makeId3File(3, lyingFrame)).has_value());

    // A tag header that claims more body than the file contains.
    Bytes truncated = fx::makeId3File(3, fx::makeFrame("APIC", content, 3));
    truncated.resize(truncated.size() / 2);
    QVERIFY(!read(truncated).has_value());

    // A tag whose declared size overruns the file entirely.
    Bytes overrun;
    fx::appendAscii(overrun, "ID3");
    overrun.push_back(0x03);
    overrun.push_back(0x00);
    overrun.push_back(0x00);
    fx::appendSyncSafe32(overrun, 0x0FFFFFFFu);
    QVERIFY(!read(overrun).has_value());

    QVERIFY(!tryReadEmbeddedArtwork(QByteArrayView()).has_value());
    QVERIFY(!read({}).has_value());
    QVERIFY(!read({'I', 'D', '3'}).has_value());
}

void EmbeddedArtworkTest::flacPictureBlock()
{
    const Bytes picture = fx::makeJpeg(0x31, 200);

    Bytes file;
    fx::appendAscii(file, "fLaC");
    fx::appendFlacBlock(file, 0, false, Bytes(34, 0x00)); // STREAMINFO
    fx::appendFlacBlock(file, 4, false, Bytes(16, 0x00)); // VORBIS_COMMENT
    fx::appendFlacBlock(file, 6, true, fx::makeFlacPictureBlock(3, "image/jpeg", picture));

    const std::optional<EmbeddedArtwork> result = read(file);

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
    QCOMPARE(result->mimeType, QStringLiteral("image/jpeg"));
}

void EmbeddedArtworkTest::flacBehindId3Tag()
{
    const Bytes picture = fx::makeJpeg(0x32, 120);

    Bytes stream;
    fx::appendAscii(stream, "fLaC");
    fx::appendFlacBlock(stream, 0, false, Bytes(34, 0x00));
    fx::appendFlacBlock(stream, 6, true, fx::makeFlacPictureBlock(3, "image/jpeg", picture));

    // An ID3v2 tag with no picture, followed by the real FLAC stream.
    const Bytes textFrame = fx::makeFrame("TIT2", Bytes{0x00, 'S', 'o', 'n', 'g'}, 3);
    const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(3, textFrame, 0, stream));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
}

void EmbeddedArtworkTest::flacUrlPictureIsRejected()
{
    Bytes url;
    fx::appendAscii(url, "https://media.example.test/cover.jpg");

    Bytes file;
    fx::appendAscii(file, "fLaC");
    fx::appendFlacBlock(file, 6, true, fx::makeFlacPictureBlock(3, "-->", url));

    QVERIFY(!read(file).has_value());
}

void EmbeddedArtworkTest::mp4CoverAtom()
{
    const Bytes picture = fx::makeJpeg(0x41, 256);
    const std::optional<EmbeddedArtwork> result = read(fx::makeMp4File(picture, 13, true));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
    QCOMPARE(result->mimeType, QStringLiteral("image/jpeg"));
}

void EmbeddedArtworkTest::mp4MetaWithoutVersionFlags()
{
    const Bytes picture = fx::makePng();
    const std::optional<EmbeddedArtwork> result = read(fx::makeMp4File(picture, 14, false));

    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
    QCOMPARE(result->mimeType, QStringLiteral("image/png"));
}

void EmbeddedArtworkTest::mp4LyingAtomSizeIsRejected()
{
    Bytes file = fx::makeMp4File(fx::makeJpeg(0x42, 128), 13, true);
    // Overstate the moov atom size so it runs past the end of the file.
    const std::size_t moovStart = fx::makeAtom("ftyp", Bytes(8, 0x00)).size();
    QVERIFY(file.size() > moovStart + 4);
    file[moovStart] = 0x7F;
    file[moovStart + 1] = 0xFF;

    QVERIFY(!read(file).has_value());
}

void EmbeddedArtworkTest::artworkFileExtensionMapping()
{
    QCOMPARE(artworkFileExtension(QStringLiteral("image/png")), QStringLiteral(".png"));
    QCOMPARE(artworkFileExtension(QStringLiteral("image/webp")), QStringLiteral(".webp"));
    QCOMPARE(artworkFileExtension(QStringLiteral("image/jpeg")), QStringLiteral(".jpg"));
    QCOMPARE(artworkFileExtension(QString()), QStringLiteral(".jpg"));
}

void EmbeddedArtworkTest::everySniffedImageType()
{
    const auto typeOf = [](const Bytes& picture) {
        const Bytes frames = fx::makeFrame("APIC", fx::makeApicContent("image/any", 0x03, picture), 3);
        const std::optional<EmbeddedArtwork> result = read(fx::makeId3File(3, frames));
        return result ? result->mimeType : QString();
    };
    QCOMPARE(typeOf({'G', 'I', 'F', '8', '9', 'a', 1}), QStringLiteral("image/gif"));
    QCOMPARE(typeOf({'G', 'I', 'F', '8', '7', 'a', 1}), QStringLiteral("image/gif"));
    QCOMPARE(typeOf({'B', 'M', 1}), QStringLiteral("image/bmp"));
    QCOMPARE(typeOf({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'}), QStringLiteral("image/webp"));
    QCOMPARE(typeOf({'I', 'I', 0x2A, 0x00}), QStringLiteral("image/tiff"));
    QCOMPARE(typeOf({'M', 'M', 0x00, 0x2A}), QStringLiteral("image/tiff"));
    QCOMPARE(typeOf({0x00, 0x00, 0x01, 0x00}), QStringLiteral("image/x-icon"));
    // RIFF without the WEBP form type is not an image.
    QCOMPARE(typeOf({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'}), QString());

    QCOMPARE(artworkFileExtension(QStringLiteral("image/gif")), QStringLiteral(".gif"));
    QCOMPARE(artworkFileExtension(QStringLiteral("image/bmp")), QStringLiteral(".bmp"));
    QCOMPARE(artworkFileExtension(QStringLiteral("image/tiff")), QStringLiteral(".tif"));
    QCOMPARE(artworkFileExtension(QStringLiteral("image/x-icon")), QStringLiteral(".ico"));
    QCOMPARE(artworkFileExtension(QStringLiteral("image/heic")), QStringLiteral(".jpg"));
}

void EmbeddedArtworkTest::moreContainerLayouts()
{
    // ID3v2.3 extended header (its size leaves out its own four bytes).
    const Bytes picture = fx::makeJpeg(0x61);
    Bytes extended;
    fx::appendBigEndian32(extended, 6);
    fx::append(extended, Bytes(6, 0x00));
    fx::append(extended, fx::makeFrame("APIC", fx::makeApicContent("image/jpeg", 0x03, picture), 3));
    std::optional<EmbeddedArtwork> result = read(fx::makeId3File(3, extended, 0x40));
    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));

    // A FLAC back cover is replaced by a later front cover.
    const Bytes back = fx::makeJpeg(0x62);
    const Bytes front = fx::makePng();
    Bytes flac;
    fx::appendAscii(flac, "fLaC");
    fx::appendFlacBlock(flac, 6, false, fx::makeFlacPictureBlock(4, "image/jpeg", back));
    fx::appendFlacBlock(flac, 6, true, fx::makeFlacPictureBlock(3, "image/png", front));
    result = read(flac);
    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(front));

    // An MP4 atom of size 0 extends to the end of its parent.
    const Bytes cover = fx::makeJpeg(0x63, 32);
    Bytes data;
    fx::appendBigEndian32(data, 0);
    fx::appendAscii(data, "data");
    fx::appendBigEndian32(data, 13);
    fx::appendBigEndian32(data, 0);
    fx::append(data, cover);
    Bytes mp4 = fx::makeAtom("ftyp", Bytes(8, 0x00));
    fx::append(mp4, fx::makeAtom("moov", fx::makeAtom("covr", data)));
    result = read(mp4);
    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(cover));
}

void EmbeddedArtworkTest::readsFromFile()
{
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const QString path = QDir(folder.path()).filePath(QStringLiteral("song.mp3"));
    const Bytes picture = fx::makeJpeg(0x71, 500);
    const Bytes frames = fx::makeFrame("APIC", fx::makeApicContent("image/jpeg", 0x03, picture), 4);
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = fx::toByteArray(fx::makeId3File(4, frames, 0, Bytes(64 * 1024, 0xFF)));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    const std::optional<EmbeddedArtwork> result = tryReadEmbeddedArtworkFromFile(path);
    QVERIFY(result.has_value());
    QCOMPARE(result->bytes, bytesOf(picture));
    QCOMPARE(result->mimeType, QStringLiteral("image/jpeg"));

    const QString empty = QDir(folder.path()).filePath(QStringLiteral("empty.mp3"));
    {
        QFile file(empty);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    QVERIFY(!tryReadEmbeddedArtworkFromFile(empty).has_value());
    QVERIFY(!tryReadEmbeddedArtworkFromFile(QDir(folder.path()).filePath(QStringLiteral("missing.mp3"))).has_value());
}

QTEST_GUILESS_MAIN(EmbeddedArtworkTest)
#include "tst_embedded_artwork.moc"
