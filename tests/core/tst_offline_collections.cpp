#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "core/downloads/OfflineCollections.h"

using lmp::core::Track;
namespace offline = lmp::core::offline;

namespace {

Track song(const QString& title, const QString& artist, const QString& album = {}, const QString& genre = {})
{
    Track track;
    track.title = title;
    track.artist = artist;
    track.album = album;
    track.genre = genre;
    track.filePath = QStringLiteral("C:/Music/") + title + QStringLiteral(".m4a");
    return track;
}

} // namespace

class OfflineCollectionsTest : public QObject {
    Q_OBJECT

private slots:
    void playlistsKeepTheirOrderAndCountEachSongOnce();
    void mainArtistIsTheFirstCreditedName();
    void albumsNeedTwoSongsAndDropTheSoundtrackTag();
    void artistsAreBiggestFirst();
    void genresLeaveOutSingleSongsWhileOthersHaveMore();
};

void OfflineCollectionsTest::playlistsKeepTheirOrderAndCountEachSongOnce()
{
    const QList<Track> downloaded{song(QStringLiteral("A"), QStringLiteral("X")), song(QStringLiteral("B"), QStringLiteral("X")),
                                  song(QStringLiteral("C"), QStringLiteral("Y"))};
    const QStringList keys{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
    const QList<Track> part = offline::downloadedPart(
        {QStringLiteral("c"), QStringLiteral("z"), QStringLiteral("a"), QStringLiteral("c"), QString()}, downloaded, keys);
    QCOMPARE(part.size(), 2);
    QCOMPARE(part.at(0).title, QStringLiteral("C"));
    QCOMPARE(part.at(1).title, QStringLiteral("A"));
    QCOMPARE(offline::playlistSubtitle(2, 4), QStringLiteral("2 of 4 songs"));
    QCOMPARE(offline::playlistSubtitle(1, 1), QStringLiteral("1 song"));
}

void OfflineCollectionsTest::mainArtistIsTheFirstCreditedName()
{
    QCOMPARE(offline::mainArtist(song(QStringLiteral("S"), QStringLiteral("Arijit Singh & Asees Kaur"))),
             QStringLiteral("Arijit Singh"));
    QCOMPARE(offline::mainArtist(song(QStringLiteral("S"), QStringLiteral("Pritam feat. KK"))), QStringLiteral("Pritam"));
    Track duo = song(QStringLiteral("S"), QStringLiteral("Vishal & Shekhar"));
    duo.primaryArtistName = QStringLiteral("Vishal & Shekhar");
    QCOMPARE(offline::mainArtist(duo), QStringLiteral("Vishal & Shekhar"));
}

void OfflineCollectionsTest::albumsNeedTwoSongsAndDropTheSoundtrackTag()
{
    const QList<Track> downloaded{
        song(QStringLiteral("One"), QStringLiteral("Pritam"), QStringLiteral("Film (Original Motion Picture Soundtrack)")),
        song(QStringLiteral("Two"), QStringLiteral("Pritam"), QStringLiteral("Film")),
        song(QStringLiteral("Alone"), QStringLiteral("KK"), QStringLiteral("Single")),
        song(QStringLiteral("Mix"), QStringLiteral("KK"), QStringLiteral("Road Trip")),
        song(QStringLiteral("Mix 2"), QStringLiteral("KK"), QStringLiteral("Road Trip")),
    };
    const QList<offline::OfflineGroup> albums = offline::albumGroups(downloaded, {QStringLiteral("road trip")});
    QCOMPARE(albums.size(), 1);
    QCOMPARE(albums.at(0).name, QStringLiteral("Film"));
    QCOMPARE(albums.at(0).subtitle, QStringLiteral("Pritam"));
    QCOMPARE(albums.at(0).tracks.size(), 2);
}

void OfflineCollectionsTest::artistsAreBiggestFirst()
{
    const QList<Track> downloaded{song(QStringLiteral("A"), QStringLiteral("Zed")), song(QStringLiteral("B"), QStringLiteral("Amy")),
                                  song(QStringLiteral("C"), QStringLiteral("zed, Amy")), song(QStringLiteral("D"), QStringLiteral("Unknown Artist"))};
    const QList<offline::OfflineGroup> artists = offline::artistGroups(downloaded);
    QCOMPARE(artists.size(), 2);
    QCOMPARE(artists.at(0).name, QStringLiteral("Zed"));
    QCOMPARE(artists.at(0).subtitle, QStringLiteral("2 songs"));
    QCOMPARE(artists.at(1).name, QStringLiteral("Amy"));
}

void OfflineCollectionsTest::genresLeaveOutSingleSongsWhileOthersHaveMore()
{
    QList<Track> downloaded{song(QStringLiteral("A"), QStringLiteral("X"), {}, QStringLiteral("Pop")),
                            song(QStringLiteral("B"), QStringLiteral("X"), {}, QStringLiteral("pop")),
                            song(QStringLiteral("C"), QStringLiteral("X"), {}, QStringLiteral("Rock"))};
    QList<offline::OfflineGroup> genres = offline::genreGroups(downloaded);
    QCOMPARE(genres.size(), 1);
    QCOMPARE(genres.at(0).name, QStringLiteral("Pop"));
    downloaded.removeAt(1);
    genres = offline::genreGroups(downloaded);
    QCOMPARE(genres.size(), 2);
}

QTEST_GUILESS_MAIN(OfflineCollectionsTest)
#include "tst_offline_collections.moc"
