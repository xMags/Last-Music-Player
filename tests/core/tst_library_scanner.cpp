#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimeZone>
#include <QUrl>

#include <atomic>
#include <memory>

#include "FakeTagReader.h"
#include "TempLibrary.h"
#include "core/db/LibraryQueries.h"
#include "core/db/TrackRepository.h"
#include "core/library/ArtworkCache.h"
#include "core/library/LibraryScanner.h"
#include "core/library/UserDataOperationGate.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/ITagReader.h"

using lmp::core::ArtworkCache;
using lmp::core::LibraryScanner;
using lmp::test::FakeTagReader;
namespace keys = lmp::core::settings::keys;



// The folder scan: what it reads, what it writes, and that it never deletes.
class LibraryScannerTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void extensionsReadLikeEarlierVersions();
    void tagsFallBackLikeEarlierVersions();
    void scanWritesEverySongWithItsSourceKey();
    void rescanDeactivatesMissingSongsWithoutDeleting();
    void cancelLeavesTheDatabaseAlone();
    void oneScanAtATime();
    void closedGateRefusesTheScan();
    void pruneDeactivatesMissingFiles();
    void artworkCacheNamesAndEmbeddedPictures();

private:
    void touch(const QString& relative);
    [[nodiscard]] LibraryScanner::Outcome runScan(int* count = nullptr);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<TempLibrary> m_library;
    std::unique_ptr<lmp::core::SettingsStore> m_settings;
    std::unique_ptr<lmp::core::db::TrackRepository> m_tracks;
    std::unique_ptr<lmp::core::db::LibraryQueries> m_queries;
    std::unique_ptr<FakeTagReader> m_tags;
    std::unique_ptr<ArtworkCache> m_artwork;
    std::unique_ptr<lmp::core::UserDataOperationGate> m_gate;
    std::unique_ptr<LibraryScanner> m_scanner;
    QString m_music;
};

void LibraryScannerTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_music = m_dir->filePath(QStringLiteral("Music"));
    QVERIFY(QDir().mkpath(m_music));
    m_library = std::make_unique<TempLibrary>();
    QVERIFY(m_library->ok());
    m_settings = std::make_unique<lmp::core::SettingsStore>(m_dir->filePath(QStringLiteral("Settings.json")));
    m_tracks = std::make_unique<lmp::core::db::TrackRepository>(m_library->database());
    m_queries = std::make_unique<lmp::core::db::LibraryQueries>(m_library->database());
    m_tags = std::make_unique<FakeTagReader>();
    m_artwork = std::make_unique<ArtworkCache>(m_dir->filePath(QStringLiteral("thumbs")), nullptr);
    m_gate = std::make_unique<lmp::core::UserDataOperationGate>();
    m_scanner = std::make_unique<LibraryScanner>(LibraryScanner::Dependencies{
        *m_tags, *m_artwork, *m_tracks, *m_queries, *m_settings, *m_gate});
}

void LibraryScannerTest::cleanup()
{
    m_scanner.reset();
    m_gate.reset();
    m_artwork.reset();
    m_tags.reset();
    m_queries.reset();
    m_tracks.reset();
    m_settings.reset();
    m_library.reset();
    m_dir.reset();
}

void LibraryScannerTest::touch(const QString& relative)
{
    const QString path = QDir(m_music).filePath(relative);
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not really audio");
}

LibraryScanner::Outcome LibraryScannerTest::runScan(int* count)
{
    QSignalSpy finished(m_scanner.get(), &LibraryScanner::finished);
    if (!m_scanner->start(m_music)) {
        return LibraryScanner::Outcome::Failed;
    }
    if (!finished.wait(10000)) {
        return LibraryScanner::Outcome::Failed;
    }
    if (count != nullptr) {
        *count = finished.first().at(1).toInt();
    }
    return finished.first().at(0).value<LibraryScanner::Outcome>();
}

void LibraryScannerTest::extensionsReadLikeEarlierVersions()
{
    QCOMPARE(LibraryScanner::scanExtensions(QStringLiteral(".mp3,.flac")),
             (QStringList{QStringLiteral(".mp3"), QStringLiteral(".flac")}));
    QCOMPARE(LibraryScanner::scanExtensions(QStringLiteral(" MP3 , .Wav,,ogg ")),
             (QStringList{QStringLiteral(".mp3"), QStringLiteral(".wav"), QStringLiteral(".ogg")}));
    QCOMPARE(LibraryScanner::scanExtensions(QStringLiteral(" , ")),
             (QStringList{QStringLiteral(".mp3"), QStringLiteral(".flac")}));
    QCOMPARE(LibraryScanner::formatDuration(0), QStringLiteral("0:00"));
    QCOMPARE(LibraryScanner::formatDuration(125.9), QStringLiteral("2:05"));
    QCOMPARE(LibraryScanner::formatDuration(3725), QStringLiteral("62:05"));
}

void LibraryScannerTest::tagsFallBackLikeEarlierVersions()
{
    lmp::platform::TrackTags empty;
    const lmp::core::Track track =
        LibraryScanner::trackFromTags(QStringLiteral("C:\\Music\\my.song.mp3"), empty, QString());
    QCOMPARE(track.title, QStringLiteral("my.song"));
    QCOMPARE(track.artist, QStringLiteral("Unknown Artist"));
    QCOMPARE(track.album, QStringLiteral("Unknown Album"));
    QCOMPARE(track.genre, QStringLiteral("Unknown Genre"));
    QCOMPARE(track.sourceKind, QStringLiteral("local"));
    QCOMPARE(track.sourceLabel, QStringLiteral("Local"));
    QCOMPARE(track.dateAddedSortKey, 0.0);

    lmp::platform::TrackTags dated;
    dated.created = QDateTime(QDate(2025, 6, 1), QTime(0, 0), QTimeZone::UTC);
    dated.modified = QDateTime(QDate(2024, 6, 1), QTime(0, 0), QTimeZone::UTC);
    const lmp::core::Track newer = LibraryScanner::trackFromTags(QStringLiteral("C:\\a.mp3"), dated, QString());
    QCOMPARE(newer.dateAddedSortKey, static_cast<double>(dated.created.toSecsSinceEpoch()));
    QVERIFY(newer.dateAdded.contains(QStringLiteral("2025")));
}

void LibraryScannerTest::scanWritesEverySongWithItsSourceKey()
{
    touch(QStringLiteral("A - One.mp3"));
    touch(QStringLiteral("Sub/B - Two.FLAC"));
    touch(QStringLiteral("untagged.mp3"));
    touch(QStringLiteral("unreadable.mp3"));
    touch(QStringLiteral("skip.wav"));
    touch(QStringLiteral("cover.jpg"));

    QSignalSpy progress(m_scanner.get(), &LibraryScanner::progress);
    int count = 0;
    QCOMPARE(runScan(&count), LibraryScanner::Outcome::Completed);
    QCOMPARE(count, 3);
    QCOMPARE(m_tags->reads.load(), 4);
    QVERIFY(progress.count() >= 2);
    QCOMPARE(progress.last().at(0).toInt(), 4);

    QCOMPARE(m_library->number("SELECT COUNT(*) FROM Tracks WHERE IsActive=1;"), 3);
    const QString expectedKey = QStringLiteral("local|")
        + QDir::toNativeSeparators(QDir(m_music).filePath(QStringLiteral("Sub/B - Two.FLAC"))).toLower();
    const QByteArray sql = "SELECT Title FROM Tracks WHERE SourceKey='" + expectedKey.toUtf8() + "';";
    QCOMPARE(m_library->text(sql.constData()), QStringLiteral("Two"));
    QCOMPARE(m_library->text("SELECT Artist FROM Tracks WHERE Title='untagged';"), QStringLiteral("Unknown Artist"));

    // Only the formats in ScanFormats.
    m_settings->setString(keys::ScanFormats, QStringLiteral(".wav"));
    QCOMPARE(runScan(&count), LibraryScanner::Outcome::Completed);
    QCOMPARE(count, 1);
}

void LibraryScannerTest::rescanDeactivatesMissingSongsWithoutDeleting()
{
    touch(QStringLiteral("A - One.mp3"));
    touch(QStringLiteral("A - Two.mp3"));
    QCOMPARE(runScan(), LibraryScanner::Outcome::Completed);
    QVERIFY(m_tracks->setLiked(
        QStringLiteral("local|") + QDir::toNativeSeparators(QDir(m_music).filePath(QStringLiteral("A - Two.mp3"))).toLower(),
        true));

    QVERIFY(QFile::remove(QDir(m_music).filePath(QStringLiteral("A - Two.mp3"))));
    QCOMPARE(runScan(), LibraryScanner::Outcome::Completed);
    QCOMPARE(m_library->number("SELECT COUNT(*) FROM Tracks;"), 2);
    QCOMPARE(m_library->number("SELECT COUNT(*) FROM Tracks WHERE IsActive=1;"), 1);

    // Back again: active and still liked.
    touch(QStringLiteral("A - Two.mp3"));
    QCOMPARE(runScan(), LibraryScanner::Outcome::Completed);
    QCOMPARE(m_library->number("SELECT COUNT(*) FROM Tracks WHERE IsActive=1;"), 2);
    QCOMPARE(m_library->number("SELECT IsLiked FROM Tracks WHERE Title='Two';"), 1);
}

void LibraryScannerTest::cancelLeavesTheDatabaseAlone()
{
    for (int i = 0; i < 60; ++i) {
        touch(QStringLiteral("A - Song %1.mp3").arg(i));
    }
    m_tags->slow = true;
    QSignalSpy finished(m_scanner.get(), &LibraryScanner::finished);
    QSignalSpy progress(m_scanner.get(), &LibraryScanner::progress);
    QVERIFY(m_scanner->start(m_music));
    QVERIFY(progress.wait(5000));
    m_scanner->cancel();
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.first().at(0).value<LibraryScanner::Outcome>(), LibraryScanner::Outcome::Cancelled);
    QVERIFY(m_tags->reads.load() < 60);
    QCOMPARE(m_library->number("SELECT COUNT(*) FROM Tracks;"), 0);
    QVERIFY(!m_scanner->isRunning());
}

void LibraryScannerTest::oneScanAtATime()
{
    touch(QStringLiteral("A - One.mp3"));
    m_tags->slow = true;
    QSignalSpy finished(m_scanner.get(), &LibraryScanner::finished);
    QVERIFY(m_scanner->start(m_music));
    QVERIFY(!m_scanner->start(m_music));
    QVERIFY(!m_scanner->pruneMissingTracks());
    QVERIFY(finished.wait(10000));
    QVERIFY(!m_scanner->start(m_dir->filePath(QStringLiteral("missing folder"))));
}

void LibraryScannerTest::closedGateRefusesTheScan()
{
    touch(QStringLiteral("A - One.mp3"));
    QVERIFY(m_gate->closeAdmissions());
    QVERIFY(!m_scanner->start(m_music));
    QVERIFY(!m_scanner->isRunning());
}

void LibraryScannerTest::pruneDeactivatesMissingFiles()
{
    touch(QStringLiteral("A - One.mp3"));
    touch(QStringLiteral("A - Two.mp3"));
    QCOMPARE(runScan(), LibraryScanner::Outcome::Completed);
    QVERIFY(QFile::remove(QDir(m_music).filePath(QStringLiteral("A - One.mp3"))));

    QSignalSpy pruned(m_scanner.get(), &LibraryScanner::pruned);
    QVERIFY(m_scanner->pruneMissingTracks());
    QVERIFY(pruned.wait(5000));
    QCOMPARE(pruned.first().at(0).toInt(), 1);
    QCOMPARE(m_library->number("SELECT COUNT(*) FROM Tracks WHERE IsActive=1;"), 1);
    QCOMPARE(m_library->number("SELECT COUNT(*) FROM Tracks;"), 2);
    QTRY_VERIFY(!m_scanner->isRunning());
}

void LibraryScannerTest::artworkCacheNamesAndEmbeddedPictures()
{
    // SHA-1 of "C:\\Music\\a.mp3", first 16 hex digits, then the generation.
    const QString key = ArtworkCache::cacheKey(QStringLiteral("C:\\Music\\a.mp3"));
    QCOMPARE(key.size(), 16 + 3);
    QVERIFY(key.endsWith(QStringLiteral("-v2")));
    QCOMPARE(ArtworkCache::cacheKey(QStringLiteral("C:\\Music\\a.mp3")), key);
    QVERIFY(ArtworkCache::cacheKey(QStringLiteral("C:\\Music\\b.mp3")) != key);

    // A file with no picture and no fallback has none.
    touch(QStringLiteral("plain.mp3"));
    QCOMPARE(m_artwork->ensure(QDir(m_music).filePath(QStringLiteral("plain.mp3"))), QString());

    // An ID3 picture is copied out as it is.
    const QByteArray png = QByteArray::fromHex("89504E470D0A1A0A") + QByteArray(32, 'x');
    QByteArray frameBody;
    frameBody.append('\0');
    frameBody.append("image/png");
    frameBody.append('\0');
    frameBody.append('\3');
    frameBody.append('\0');
    frameBody.append(png);
    QByteArray frame("APIC");
    const auto size = static_cast<quint32>(frameBody.size());
    frame.append(static_cast<char>((size >> 24) & 0xFF));
    frame.append(static_cast<char>((size >> 16) & 0xFF));
    frame.append(static_cast<char>((size >> 8) & 0xFF));
    frame.append(static_cast<char>(size & 0xFF));
    frame.append(QByteArray(2, '\0'));
    frame.append(frameBody);
    QByteArray tag("ID3");
    tag.append('\3');
    tag.append('\0');
    tag.append('\0');
    const auto tagSize = static_cast<quint32>(frame.size());
    tag.append(static_cast<char>((tagSize >> 21) & 0x7F));
    tag.append(static_cast<char>((tagSize >> 14) & 0x7F));
    tag.append(static_cast<char>((tagSize >> 7) & 0x7F));
    tag.append(static_cast<char>(tagSize & 0x7F));
    tag.append(frame);
    const QString withCover = QDir(m_music).filePath(QStringLiteral("cover.mp3"));
    QFile file(withCover);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(tag);
    file.close();

    const QString address = m_artwork->ensure(withCover);
    QVERIFY(address.startsWith(QStringLiteral("file:///")));
    QVERIFY(address.endsWith(ArtworkCache::cacheKey(withCover) + QStringLiteral(".png")));
    QFile cached(QUrl(address).toLocalFile());
    QVERIFY(cached.open(QIODevice::ReadOnly));
    QCOMPARE(cached.readAll(), png);
    // A second call finds the cached file.
    QCOMPARE(m_artwork->ensure(withCover), address);
}

QTEST_GUILESS_MAIN(LibraryScannerTest)
#include "tst_library_scanner.moc"
