#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include <algorithm>

#include "core/library/LikedIndex.h"
#include "core/library/SameSong.h"

using lmp::core::isSameSong;
using lmp::core::LikedIndex;
using lmp::core::SongIdentity;

// The shared same-song cases every client passes (in both directions), and
// the reference client's liked-index cases. Ids are neutral: only whether
// they are equal matters.
class SameSongTest : public QObject {
    Q_OBJECT

private slots:
    void sharedCases_data();
    void sharedCases();
    void otherVersionsOfALikedSongReadAsLiked();
    void everySavedCopyIsFound();
    void comparableTextFoldsAccentsAndCase();
};

namespace {

SongIdentity song(const QString& id, const QString& title, const QString& artist)
{
    return SongIdentity{id, title, artist};
}

} // namespace

void SameSongTest::sharedCases_data()
{
    QTest::addColumn<SongIdentity>("a");
    QTest::addColumn<SongIdentity>("b");
    QTest::addColumn<bool>("same");
    const auto row = [](const char* why, const SongIdentity& a, const SongIdentity& b, bool same) {
        QTest::newRow(why) << a << b << same;
    };
    row("credits in a different order",
        song("a1", "Sita's Aspiration", "A.R. Ameen, Vaishali Samant, Rakshita Suresh & Antara Nandy"),
        song("b1", "Sita's Aspiration",
             "A.R. Rahman, A.R. Ameen, Vaishali Samant, Rakshita Suresh, Antra Nandy & Kumar Vishwas"),
        true);
    row("same id", song("same", "One", "Someone"), song("same", "Two", "Else"), true);
    row("film prefix and pipe credits",
        song("a3", "KING: Tu Mil Jaaye | Shah Rukh Khan, Deepika | Sachin-Jigar, Arijit, Ed Sheeran, Madhubanti, Kumaar",
             "T-Series"),
        song("b3", "Tu Mil Jaaye (From \"KING\")", "Sachin-Jigar, Arijit Singh, Ed Sheeran, Madhubanti Bagchi & Kumaar"),
        true);
    row("dubbed-language suffix",
        song("a4", "Ramayana: Judi Jodi (Song) | Namit M, Nitesh T | Ranbir K, Sai P | AR Rahman, Shreya G, Kumar V",
             "T-Series"),
        song("b4", "Judi Jodi (From \"Ramayana\") -  HINDI", "A.R. Rahman, Shreya Ghoshal & Kumar Vishwas"), true);
    row("upload decoration and a dash credit", song("a5", "GEET (Official Music Video) - TALWIINDER X NDS", "Talwiinder"),
        song("b5", "Geet", "Talwiinder & NDS"), true);
    row("emoji prefix and singers after the dash",
        song("a6", QStringLiteral("\U0001F1EE\U0001F1F3\U0001F1F3\U0001F1F5Payal - Tony Kakkar, Neha Kakkar, ST MAN"),
             "Tony Kakkar"),
        song("b6", "Payal", "Tony Kakkar, Neha Kakkar & ST MAN"), true);
    row("lyric video against the release",
        song("a7", "Ruku Ruku Rukmini - Lyrical | Alias Rukmini | Nithiin | Aafiya", "Saregama Telugu"),
        song("b7", "Ruku Ruku Rukmini (From \"Alias Rukmini\")", "Anurag Kulkarni, Vijai Bulganin & Purna Chary"),
        false);
    row("accents and case", song("a8", "Despacito", "Luis Fonsi"),
        song("b8", "DESPACITO", QStringLiteral("Luis Fonsí & Daddy Yankee")), true);
    row("a remix is another recording", song("a9", "Kesariya (Remix)", "Arijit Singh"),
        song("b9", "Kesariya (From \"Brahmastra\")", "Pritam & Arijit Singh"), false);
    row("a live take is another recording", song("a10", "Tum Hi Ho - Live", "Arijit Singh"),
        song("b10", "Tum Hi Ho", "Arijit Singh"), false);
    row("both live", song("a11", "Tum Hi Ho (Live)", "Arijit Singh"),
        song("b11", "Tum Hi Ho (Live Version)", "Arijit Singh"), true);
    row("a cover by someone else", song("a12", "Aur Es Dil Mein (Cover)", "Kabul Bukhari"),
        song("b12", "Aur Es Dil Mein", "Kumar Sanu & Alka Yagnik"), false);
    row("same title, different artists", song("a13", "Hello", "Adele"), song("b13", "Hello", "Lionel Richie"), false);
    row("one title contains the other", song("a14", "Something", "Arijit Singh"),
        song("b14", "Something Else Entirely", "Arijit Singh"), false);
    row("generic words are not a shared artist", song("a15", "Hello (Official Music Video)", "T-Series Music"),
        song("b15", "Hello", "Official Music Collective"), false);
    row("unknown artist never matches", song("a16", "Intro", "Unknown artist"), song("b16", "Intro", "Unknown artist"),
        false);
    row("a remastered release keeps its title", song("a17", "Hey Jude - Remastered 2015", "The Beatles"),
        song("b17", "Hey Jude", "The Beatles"), false);
    row("featured artist in the title", song("a18", "Levitating (feat. DaBaby)", "Dua Lipa"),
        song("b18", "Levitating (feat. DaBaby)", "Dua Lipa"), true);
    row("empty titles never match", song("a19", "", "Arijit Singh"), song("b19", "", "Arijit Singh"), false);
}

void SameSongTest::sharedCases()
{
    QFETCH(SongIdentity, a);
    QFETCH(SongIdentity, b);
    QFETCH(bool, same);
    QCOMPARE(isSameSong(a, b), same);
    QCOMPARE(isSameSong(b, a), same);
}

void SameSongTest::otherVersionsOfALikedSongReadAsLiked()
{
    const SongIdentity release = song("release:1", "Sita's Aspiration", "A.R. Rahman, A.R. Ameen, Vaishali Samant");
    const SongIdentity upload = song("upload:1", "Sita's Aspiration", "A.R. Ameen, Vaishali Samant");
    const SongIdentity remix = song("upload:remix", "Sita's Aspiration (Remix)", "A.R. Ameen");
    const SongIdentity unrelated = song("upload:other", "Jai Jai Ram", "A.R. Rahman");

    const LikedIndex liked({release});
    QVERIFY(liked.has(release.id));
    QVERIFY(!liked.has(upload.id));
    QVERIFY(liked.hasTrack(upload));
    QCOMPARE(liked.copyOf(upload)->id, release.id);
    QVERIFY(!liked.hasTrack(remix));
    QVERIFY(!liked.hasTrack(unrelated));
}

void SameSongTest::everySavedCopyIsFound()
{
    const SongIdentity release = song("release:1", "Sita's Aspiration", "A.R. Rahman, A.R. Ameen, Vaishali Samant");
    const SongIdentity upload = song("upload:1", "Sita's Aspiration", "A.R. Ameen, Vaishali Samant");
    const SongIdentity remix = song("upload:remix", "Sita's Aspiration (Remix)", "A.R. Ameen");
    const SongIdentity unrelated = song("upload:other", "Jai Jai Ram", "A.R. Rahman");

    // Duplicates saved before same-song likes existed.
    const LikedIndex liked({upload, release, unrelated});
    const auto ids = [](const QList<SongIdentity>& songs) {
        QStringList list;
        for (const SongIdentity& item : songs) {
            list.append(item.id);
        }
        list.sort();
        return list;
    };
    const QStringList both = ids({release, upload});
    QCOMPARE(ids(liked.copiesOf(release)), both);
    QCOMPARE(ids(liked.copiesOf(upload)), both);
    QCOMPARE(liked.copyOf(upload)->id, upload.id);
    QVERIFY(liked.copiesOf(remix).isEmpty());
}

void SameSongTest::comparableTextFoldsAccentsAndCase()
{
    QCOMPARE(lmp::core::comparableText(QStringLiteral("  Fonsí -- DÉJÀ vu!! ")), QStringLiteral("fonsi deja vu"));
    QCOMPARE(lmp::core::comparableText(QString()), QString());
}

QTEST_GUILESS_MAIN(SameSongTest)
#include "tst_same_song.moc"
