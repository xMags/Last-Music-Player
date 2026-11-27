// Writing a download's cover, lyrics and names into its file with TagLib,
// and reading saved lyrics back. The songs are made here: an MP4 with just
// the boxes a player needs to see one, and a few silent MP3 frames.

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <fileref.h>
#include <tpropertymap.h>
#include <tvariant.h>

#include "core/downloads/SongFileExtras.h"

namespace downloads = lmp::core::downloads;
namespace lyrics = lmp::core::lyrics;
using lmp::core::FailureKind;

namespace {

[[nodiscard]] QByteArray bigEndian(quint32 value)
{
    QByteArray bytes(4, '\0');
    qToBigEndian(value, bytes.data());
    return bytes;
}

[[nodiscard]] QByteArray box(const char* type, const QByteArray& body)
{
    return bigEndian(static_cast<quint32>(8 + body.size())) + QByteArray(type, 4) + body;
}

// ftyp, a moov with only its movie header, then the media data.
[[nodiscard]] QByteArray tinyMp4()
{
    QByteArray header;
    header += bigEndian(0);       // version and flags
    header += bigEndian(0);       // created
    header += bigEndian(0);       // modified
    header += bigEndian(1000);    // time scale
    header += bigEndian(1000);    // duration
    header += bigEndian(0x00010000); // rate
    header += QByteArray("\x01\x00", 2) + QByteArray(10, '\0'); // volume, reserved
    for (const quint32 cell : {0x00010000U, 0U, 0U, 0U, 0x00010000U, 0U, 0U, 0U, 0x40000000U}) {
        header += bigEndian(cell);
    }
    header += QByteArray(24, '\0'); // pre-defined
    header += bigEndian(2);         // next track
    const QByteArray ftyp = box("ftyp", QByteArray("M4A ") + bigEndian(0x200) + QByteArray("M4A mp42isom"));
    return ftyp + box("moov", box("mvhd", header)) + box("mdat", QByteArray(256, '\x55'));
}

// Twenty silent MPEG-1 Layer III frames, 128 kbit/s at 44.1 kHz.
[[nodiscard]] QByteArray tinyMp3()
{
    QByteArray frame(417, '\0');
    frame[0] = '\xFF';
    frame[1] = '\xFB';
    frame[2] = '\x90';
    frame[3] = '\x00';
    return frame.repeated(20);
}

[[nodiscard]] QByteArray jpeg()
{
    return QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray(64, '\x11') + QByteArray("\xFF\xD9", 2);
}

[[nodiscard]] QString write(const QTemporaryDir& folder, const QString& name, const QByteArray& bytes)
{
    const QString path = folder.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return {};
    }
    return path;
}

[[nodiscard]] TagLib::FileRef open(const QString& path)
{
    const std::wstring name = path.toStdWString();
    return TagLib::FileRef(name.c_str(), false);
}

[[nodiscard]] QString text(const TagLib::PropertyMap& properties, const char* key)
{
    const TagLib::StringList values = properties.value(key);
    return values.isEmpty() ? QString() : QString::fromStdWString(values.toString("\n").toWString());
}

[[nodiscard]] downloads::SongFileExtras everything()
{
    downloads::SongFileExtras extras;
    extras.title = QStringLiteral("Raag राग");
    extras.artist = QStringLiteral("Artist");
    extras.album = QStringLiteral("Album");
    extras.cover = jpeg();
    extras.plainLyrics = QStringLiteral("one\ntwo");
    extras.syncedLyrics = {{1000, QStringLiteral("one")}, {2500, QStringLiteral("two")}};
    return extras;
}

} // namespace

class SongFileExtrasTest : public QObject {
    Q_OBJECT

private slots:
    void writesEverythingIntoAnM4a();
    void writesEverythingIntoAnMp3();
    void plainLyricsAloneAreReadFromTheTag();
    void filesThatCannotHoldTagsAreLeftAlone();
    void aRealDownloadStaysPlayable();
};

void SongFileExtrasTest::writesEverythingIntoAnM4a()
{
    QTemporaryDir folder;
    const QString path = write(folder, QStringLiteral("song-1A2B3C4D.m4a"), tinyMp4());
    QVERIFY(!path.isEmpty());
    QVERIFY(downloads::writeSongFileExtras(path, everything()));

    const TagLib::FileRef file = open(path);
    QVERIFY(!file.isNull());
    const TagLib::PropertyMap properties = file.properties();
    QCOMPARE(text(properties, "TITLE"), everything().title);
    QCOMPARE(text(properties, "ARTIST"), QStringLiteral("Artist"));
    QCOMPARE(text(properties, "ALBUM"), QStringLiteral("Album"));
    QCOMPARE(text(properties, "LYRICS"), QStringLiteral("one\ntwo"));
    const TagLib::List<TagLib::VariantMap> pictures = file.complexProperties("PICTURE");
    QCOMPARE(pictures.size(), 1U);
    const TagLib::ByteVector data = pictures.front().value("data").toByteVector();
    QCOMPARE(QByteArray(data.data(), static_cast<qsizetype>(data.size())), jpeg());

    // Timed lines beside it, readable as the saved lyrics.
    QFile lrc(downloads::lrcPathFor(path));
    QCOMPARE(QFileInfo(lrc).fileName(), QStringLiteral("song-1A2B3C4D.lrc"));
    QVERIFY(lrc.open(QIODevice::ReadOnly));
    QCOMPARE(lrc.readAll(), QByteArray("\xEF\xBB\xBF[00:01.00]one\n[00:02.50]two\n"));
    const std::optional<lyrics::LyricsResult> saved = downloads::readSavedLyrics(path);
    QVERIFY(saved && saved->found);
    QCOMPARE(saved->synced.size(), 2);
    // Nothing left behind.
    QCOMPARE(QDir(folder.path()).entryList(QDir::Files).size(), 2);
}

void SongFileExtrasTest::writesEverythingIntoAnMp3()
{
    QTemporaryDir folder;
    const QString path = write(folder, QStringLiteral("song.mp3"), tinyMp3());
    QVERIFY(downloads::writeSongFileExtras(path, everything()));
    const TagLib::FileRef file = open(path);
    QVERIFY(!file.isNull());
    QCOMPARE(text(file.properties(), "TITLE"), everything().title);
    QCOMPARE(text(file.properties(), "LYRICS"), QStringLiteral("one\ntwo"));
    QCOMPARE(file.complexProperties("PICTURE").size(), 1U);
}

void SongFileExtrasTest::plainLyricsAloneAreReadFromTheTag()
{
    QTemporaryDir folder;
    const QString path = write(folder, QStringLiteral("song.m4a"), tinyMp4());
    downloads::SongFileExtras extras;
    extras.plainLyrics = QStringLiteral("only words");
    QVERIFY(downloads::writeSongFileExtras(path, extras));
    QVERIFY(!QFileInfo::exists(downloads::lrcPathFor(path)));
    const std::optional<lyrics::LyricsResult> saved = downloads::readSavedLyrics(path);
    QVERIFY(saved && saved->found);
    QVERIFY(saved->synced.isEmpty());
    QCOMPARE(saved->plain, QStringLiteral("only words"));

    const QString bare = write(folder, QStringLiteral("bare.m4a"), tinyMp4());
    QVERIFY(!downloads::readSavedLyrics(bare));
}

void SongFileExtrasTest::filesThatCannotHoldTagsAreLeftAlone()
{
    QTemporaryDir folder;
    const QByteArray noise(1000, '\x42');
    const QString path = write(folder, QStringLiteral("song.audio"), noise);
    const lmp::core::Result<void> written = downloads::writeSongFileExtras(path, everything());
    QVERIFY(!written);
    QCOMPARE(written.failure().kind, FailureKind::NotAvailable);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), noise);
    QCOMPARE(QDir(folder.path()).entryList(QDir::Files).size(), 1);

    const lmp::core::Result<void> gone = downloads::writeSongFileExtras(folder.filePath(QStringLiteral("x.m4a")), everything());
    QCOMPARE(gone.failure().kind, FailureKind::NotFound);
}

void SongFileExtrasTest::aRealDownloadStaysPlayable()
{
    // A real download is not kept in the repository; point LMP_SAMPLE_M4A
    // at one to check that its audio survives.
    const QString sample = qEnvironmentVariable("LMP_SAMPLE_M4A");
    if (sample.isEmpty()) {
        QSKIP("LMP_SAMPLE_M4A names no sample download");
    }
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("sample.m4a"));
    QVERIFY(QFile::copy(sample, path));
    const std::wstring name = path.toStdWString();
    const int before = TagLib::FileRef(name.c_str(), true).audioProperties()->lengthInMilliseconds();
    QVERIFY(before > 0);
    QVERIFY(downloads::writeSongFileExtras(path, everything()));
    const TagLib::FileRef after(name.c_str(), true);
    QCOMPARE(after.audioProperties()->lengthInMilliseconds(), before);
    QCOMPARE(after.complexProperties("PICTURE").size(), 1U);
    QCOMPARE(text(after.properties(), "LYRICS"), QStringLiteral("one\ntwo"));
}

QTEST_GUILESS_MAIN(SongFileExtrasTest)
#include "tst_song_file_extras.moc"
