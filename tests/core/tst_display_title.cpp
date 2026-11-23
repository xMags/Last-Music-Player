#include <QObject>
#include <QString>
#include <QTest>

#include "core/model/DisplayTitle.h"

using lmp::core::displayAlbumTitle;
using lmp::core::displayTitle;

// The reference client's display-title.test.ts, case for case.
class DisplayTitleTest : public QObject {
    Q_OBJECT

private slots:
    void titles_data();
    void titles();
    void albums_data();
    void albums();
};

void DisplayTitleTest::titles_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    const auto row = [](const QString& input, const QString& expected) {
        QTest::newRow(input.toUtf8().constData()) << input << expected;
    };
    // Strips upload decoration.
    row(QStringLiteral("GEET (Official Music Video)"), QStringLiteral("GEET"));
    row(QStringLiteral("Patar Bashori | Coke Studio Bangla | Season 3"), QStringLiteral("Patar Bashori"));
    row(QStringLiteral("KING: Tu Mil Jaaye | Shah Rukh Khan | T-Series"), QStringLiteral("KING: Tu Mil Jaaye"));
    row(QStringLiteral("Kesariya [Lyrical Video] #trending #love"), QStringLiteral("Kesariya"));
    row(QStringLiteral("Raataan Lambiyan - Official Video"), QStringLiteral("Raataan Lambiyan"));
    row(QStringLiteral("Apna Bana Le (Full Song) [4K]"), QStringLiteral("Apna Bana Le"));
    row(QStringLiteral("Tum Hi Ho (Audio)"), QStringLiteral("Tum Hi Ho"));
    row(QStringLiteral("Song Name (Video Song) | Movie"), QStringLiteral("Song Name"));
    row(QStringLiteral("  Spaced   Out   "), QStringLiteral("Spaced Out"));
    // Film credits.
    row(QStringLiteral("Jhoome Jo Pathaan (From \"Pathaan\")"), QStringLiteral("Jhoome Jo Pathaan"));
    row(QStringLiteral("Ucha Lamba Kad Forever (From \"Welcome To The Jungle\")"), QStringLiteral("Ucha Lamba Kad Forever"));
    row(QStringLiteral("Aankhon Se Tune 2.0 (From \"Bhai Tera Star Hai\")"), QStringLiteral("Aankhon Se Tune 2.0"));
    row(QStringLiteral("Saanson Ko (From \"Zid\") (Official Video)"), QStringLiteral("Saanson Ko"));
    row(QStringLiteral("Barbaad (Movie: Saiyaara)"), QStringLiteral("Barbaad"));
    row(QStringLiteral("Tum Se - From \"Teri Baaton Mein Aisa Uljha Jiya\""), QStringLiteral("Tum Se"));
    row(QStringLiteral("Kesariya (From \"Brahmastra\") (Tu Fuljhadiyo Wale Tyohaaro Jesi - Happy Diwali)"),
        QStringLiteral("Kesariya (Tu Fuljhadiyo Wale Tyohaaro Jesi - Happy Diwali)"));
    row(QStringLiteral("Apna Bana Le (From \"Bhediya\") (Remix)"), QStringLiteral("Apna Bana Le (Remix)"));
    row(QStringLiteral("(From \"Pathaan\")"), QStringLiteral("(From \"Pathaan\")"));
    // Keeps information about the recording.
    row(QStringLiteral("Aur Es Dil Mein (Cover)"), QStringLiteral("Aur Es Dil Mein (Cover)"));
    row(QStringLiteral("Great Feat of Strength"), QStringLiteral("Great Feat of Strength"));
    row(QStringLiteral("Tum Hi Ho (Live Version) [Official Video]"), QStringLiteral("Tum Hi Ho (Live Version)"));
    row(QStringLiteral("Kesariya (Remix) - Lyrical"), QStringLiteral("Kesariya (Remix)"));
    row(QStringLiteral("Blinding Lights"), QStringLiteral("Blinding Lights"));
    row(QStringLiteral("Hey Jude - Remastered 2015"), QStringLiteral("Hey Jude - Remastered 2015"));
    row(QStringLiteral("Video Killed the Radio Star"), QStringLiteral("Video Killed the Radio Star"));
    // Featured artists, producers and soundtrack labels.
    row(QStringLiteral("Levitating (feat. DaBaby)"), QStringLiteral("Levitating"));
    row(QStringLiteral("DARKHAAST (feat. Sunidhi Chauhan & Arijit Singh)"), QStringLiteral("DARKHAAST"));
    row(QStringLiteral("Perfect ft. Ed Sheeran"), QStringLiteral("Perfect"));
    row(QStringLiteral("Mann Mera ft. Gajendra Verma - Lyrical"), QStringLiteral("Mann Mera"));
    row(QStringLiteral("Tere Hawale [feat. Shreya Ghoshal] (Remix)"), QStringLiteral("Tere Hawale (Remix)"));
    row(QStringLiteral("Tum Hi Ho (Original Motion Picture Soundtrack)"), QStringLiteral("Tum Hi Ho"));
    row(QStringLiteral("Kal Ho Naa Ho (OST)"), QStringLiteral("Kal Ho Naa Ho"));
    row(QStringLiteral("Brown Munde (Prod. by Gminxr)"), QStringLiteral("Brown Munde"));
    row(QStringLiteral("Lost in the Fire (with The Weeknd)"), QStringLiteral("Lost in the Fire"));
    // Titles that only start like upload wording, and numbers written with #.
    row(QStringLiteral("The Buggles - Video Killed The Radio Star (Official Music Video)"),
        QStringLiteral("The Buggles - Video Killed The Radio Star"));
    row(QStringLiteral("Lana Del Rey - Video Games"), QStringLiteral("Lana Del Rey - Video Games"));
    row(QStringLiteral("Cassie - Official Girl"), QStringLiteral("Cassie - Official Girl"));
    row(QStringLiteral("Raataan Lambiyan - Official Music Video HD"), QStringLiteral("Raataan Lambiyan"));
    row(QStringLiteral("Tum Hi Ho - Full Video Song"), QStringLiteral("Tum Hi Ho"));
    row(QStringLiteral("Kesariya - Lyrical #trending #love"), QStringLiteral("Kesariya"));
    row(QStringLiteral("Apna Bana Le - Official Lyric Video 2022"), QStringLiteral("Apna Bana Le"));
    row(QStringLiteral("Love Potion #9"), QStringLiteral("Love Potion #9"));
    row(QStringLiteral("#9 Dream"), QStringLiteral("#9 Dream"));
    row(QStringLiteral("Symphony #5 in C Minor"), QStringLiteral("Symphony #5 in C Minor"));
    row(QStringLiteral("Kesariya #1hit"), QStringLiteral("Kesariya"));
    // Never empty for a real title.
    row(QStringLiteral("(Official Video)"), QStringLiteral("(Official Video)"));
    row(QStringLiteral("#hashtag"), QStringLiteral("#hashtag"));
    row(QStringLiteral("| |"), QStringLiteral("| |"));
    QTest::newRow("empty") << QString() << QString();
}

void DisplayTitleTest::titles()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(displayTitle(input), expected);
}

void DisplayTitleTest::albums_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("soundtrack") << QStringLiteral("Aashiqui 2 (Original Motion Picture Soundtrack)")
                                << QStringLiteral("Aashiqui 2");
    QTest::newRow("film and single") << QStringLiteral("Tu Mil Jaaye (From \"KING\") - Single")
                                     << QStringLiteral("Tu Mil Jaaye");
    QTest::newRow("EP") << QStringLiteral("Brahmastra - EP") << QStringLiteral("Brahmastra - EP");
    QTest::newRow("edition") << QStringLiteral("÷ (Deluxe Edition)") << QStringLiteral("÷ (Deluxe Edition)");
    QTest::newRow("original soundtrack") << QStringLiteral("Rockstar (Original Soundtrack)")
                                         << QStringLiteral("Rockstar");
    QTest::newRow("empty") << QString() << QString();
}

void DisplayTitleTest::albums()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(displayAlbumTitle(input), expected);
}

QTEST_GUILESS_MAIN(DisplayTitleTest)
#include "tst_display_title.moc"
