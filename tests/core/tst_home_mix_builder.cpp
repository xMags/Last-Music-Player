#include <QObject>
#include <QSet>
#include <QString>
#include <QTest>

#include "core/library/HomeMixBuilder.h"
#include "core/playback/LastTrackIdentity.h"

using lmp::core::HomeMix;
using lmp::core::HomeMixBuilder;
using lmp::core::HomeMixInputs;
using lmp::core::Track;

namespace {

Track song(const QString& title, const QString& artist, const QString& genre = {}, double added = 0.0)
{
    Track track;
    track.title = title;
    track.artist = artist;
    track.genre = genre;
    track.sourceKind = QStringLiteral("local");
    track.filePath = QStringLiteral("C:/Music/%1.mp3").arg(title);
    track.dateAddedSortKey = added;
    return track;
}

QStringList titles(const QList<Track>& tracks)
{
    QStringList list;
    for (const Track& track : tracks) {
        list.append(track.title);
    }
    return list;
}

HomeMix mixNamed(const QList<HomeMix>& mixes, const QString& id)
{
    for (const HomeMix& mix : mixes) {
        if (mix.id == id) {
            return mix;
        }
    }
    return {};
}

} // namespace

// The current app's generated mixes, rule by rule, with the reference's names.
class HomeMixBuilderTest : public QObject {
    Q_OBJECT

private slots:
    void namesBadgesAndColoursAreTheReferences();
    void mixesAreShortUniqueAndPlayable();
    void dailyMixesTakeDistinctGenres();
    void dailyFallbacksFollowHistoryAndArtists();
    void onRepeatLeadsWithMostPlayed();
    void discoverLeadsWithUnfamiliarGenres();
    void timeCapsuleIsOldestDatedFirst();
    void freshFindsFollowTheCatalogue();
    void artistsAreRankedByPlays();
    void sameInputsSameMixes();
    void refreshKeyChangesDailyAndWithMusic();
};

void HomeMixBuilderTest::namesBadgesAndColoursAreTheReferences()
{
    QCOMPARE(HomeMixBuilder::ids(),
             (QStringList{QStringLiteral("daily1"), QStringLiteral("daily2"), QStringLiteral("daily3"),
                          QStringLiteral("daily4"), QStringLiteral("daily5"), QStringLiteral("repeat"),
                          QStringLiteral("discover"), QStringLiteral("timecapsule"), QStringLiteral("fresh")}));
    const HomeMix daily = HomeMixBuilder::blank(QStringLiteral("daily1"));
    QCOMPARE(daily.title, QStringLiteral("Daily Mix 1"));
    QCOMPARE(daily.badge, QStringLiteral("DAILY MIX 1"));
    QCOMPARE(daily.colorStart, 0xFF0097B2u);
    QCOMPARE(daily.colorEnd, 0xFF00B8D4u);
    QCOMPARE(HomeMixBuilder::blank(QStringLiteral("discover")).title, QStringLiteral("Discover Weekly"));
    QVERIFY(HomeMixBuilder::blank(QStringLiteral("nope")).id.isEmpty());
    // An empty library still names every mix, each with its theme line.
    const QList<HomeMix> empty = HomeMixBuilder::build({});
    QCOMPARE(empty.size(), 9);
    QCOMPARE(empty.at(0).description, QStringLiteral("Upbeat indie and pop you keep coming back to."));
    QVERIFY(empty.at(0).tracks.isEmpty());
}

void HomeMixBuilderTest::mixesAreShortUniqueAndPlayable()
{
    HomeMixInputs inputs;
    for (int index = 0; index < 40; ++index) {
        inputs.catalog.append(song(QStringLiteral("S%1").arg(index), QStringLiteral("A%1").arg(index % 7)));
    }
    Track noFile = song(QStringLiteral("Ghost"), QStringLiteral("A1"));
    noFile.filePath.clear();
    inputs.catalog.prepend(noFile);
    inputs.catalog.append(inputs.catalog.at(1));
    for (const HomeMix& mix : HomeMixBuilder::build(inputs)) {
        QCOMPARE(mix.tracks.size(), HomeMixBuilder::MixLength);
        QSet<QString> keys;
        for (const Track& track : mix.tracks) {
            QVERIFY(!track.filePath.isEmpty());
            keys.insert(lmp::core::homeHistoryKey(track));
        }
        QCOMPARE(keys.size(), mix.tracks.size());
    }
}

void HomeMixBuilderTest::dailyMixesTakeDistinctGenres()
{
    HomeMixInputs inputs;
    QList<Track> rock;
    QList<Track> jazz;
    for (int index = 0; index < 3; ++index) {
        rock.append(song(QStringLiteral("Rock %1").arg(index), QStringLiteral("Night Owls"), QStringLiteral("Rock")));
        jazz.append(song(QStringLiteral("Jazz %1").arg(index), QStringLiteral("Mira Sol"), QStringLiteral("Jazz")));
    }
    inputs.catalog = rock + jazz;
    inputs.rankedGenres = {QStringLiteral("Empty"), QStringLiteral("Rock"), QStringLiteral("Jazz")};
    inputs.genrePools.insert(QStringLiteral("Rock"), rock);
    inputs.genrePools.insert(QStringLiteral("Jazz"), jazz);
    const QList<HomeMix> mixes = HomeMixBuilder::build(inputs);
    // A genre with no songs is skipped; each genre drives one slot.
    QCOMPARE(mixes.at(0).genre, QStringLiteral("Rock"));
    QCOMPARE(titles(mixes.at(0).tracks).mid(0, 3),
             (QStringList{QStringLiteral("Rock 0"), QStringLiteral("Rock 1"), QStringLiteral("Rock 2")}));
    QCOMPARE(mixes.at(0).description, QStringLiteral("Night Owls"));
    QCOMPARE(mixes.at(1).genre, QStringLiteral("Jazz"));
    QCOMPARE(mixes.at(1).tracks.constFirst().title, QStringLiteral("Jazz 0"));
    QVERIFY(mixes.at(2).genre.isEmpty());
    // Topped up from the catalogue: every mix holds the six songs there are.
    QCOMPARE(mixes.at(0).tracks.size(), 6);
}

void HomeMixBuilderTest::dailyFallbacksFollowHistoryAndArtists()
{
    HomeMixInputs inputs;
    inputs.catalog = {song(QStringLiteral("a1"), QStringLiteral("Alpha")), song(QStringLiteral("b1"), QStringLiteral("Beta")),
                      song(QStringLiteral("a2"), QStringLiteral("Alpha")), song(QStringLiteral("c1"), QStringLiteral("Gamma")),
                      song(QStringLiteral("b2"), QStringLiteral("Beta"))};
    inputs.recentHistory = {song(QStringLiteral("c1"), QStringLiteral("Gamma"))};
    inputs.playCounts.insert(lmp::core::homeHistoryKey(inputs.catalog.at(1)), 9);
    const QList<HomeMix> mixes = HomeMixBuilder::build(inputs);
    // Daily Mix 1: the history first, then the top artist (Beta, most played).
    QCOMPARE(titles(mixes.at(0).tracks).mid(0, 3),
             (QStringList{QStringLiteral("c1"), QStringLiteral("b1"), QStringLiteral("b2")}));
    // Daily Mix 2: the second artist (Alpha) first.
    QCOMPARE(titles(mixes.at(1).tracks).mid(0, 2), (QStringList{QStringLiteral("a1"), QStringLiteral("a2")}));
    // Daily Mix 3: the third artist (Gamma) first.
    QCOMPARE(mixes.at(2).tracks.constFirst().title, QStringLiteral("c1"));
    // Two artists and others: "Beta, Alpha & more".
    QCOMPARE(mixes.at(0).description, QStringLiteral("Beta, Alpha & more"));
}

void HomeMixBuilderTest::onRepeatLeadsWithMostPlayed()
{
    HomeMixInputs inputs;
    inputs.catalog = {song(QStringLiteral("x"), QStringLiteral("A")), song(QStringLiteral("y"), QStringLiteral("A")),
                      song(QStringLiteral("z"), QStringLiteral("A"))};
    inputs.playCounts.insert(lmp::core::homeHistoryKey(inputs.catalog.at(2)), 5);
    inputs.playCounts.insert(lmp::core::homeHistoryKey(inputs.catalog.at(1)), 2);
    inputs.playCounts.insert(lmp::core::homeHistoryKey(inputs.catalog.at(0)), 2);
    inputs.lastPlayedOrder.insert(lmp::core::homeHistoryKey(inputs.catalog.at(0)), 10);
    inputs.lastPlayedOrder.insert(lmp::core::homeHistoryKey(inputs.catalog.at(1)), 3);
    const HomeMix repeat = mixNamed(HomeMixBuilder::build(inputs), QStringLiteral("repeat"));
    // Most played, then the more recently played of equals.
    QCOMPARE(titles(repeat.tracks), (QStringList{QStringLiteral("z"), QStringLiteral("x"), QStringLiteral("y")}));
}

void HomeMixBuilderTest::discoverLeadsWithUnfamiliarGenres()
{
    HomeMixInputs inputs;
    inputs.catalog = {song(QStringLiteral("rock"), QStringLiteral("A"), QStringLiteral("Rock"), 3),
                      song(QStringLiteral("folk-new"), QStringLiteral("B"), QStringLiteral("Folk"), 5),
                      song(QStringLiteral("folk-played"), QStringLiteral("B"), QStringLiteral("Folk"), 9),
                      song(QStringLiteral("folk-old"), QStringLiteral("B"), QStringLiteral("Folk"), 1)};
    inputs.rankedGenres = {QStringLiteral("rock")};
    inputs.playCounts.insert(lmp::core::homeHistoryKey(inputs.catalog.at(2)), 4);
    const HomeMix discover = mixNamed(HomeMixBuilder::build(inputs), QStringLiteral("discover"));
    // Outside the top genres first, least played, then newest; the top genre last.
    QCOMPARE(titles(discover.tracks), (QStringList{QStringLiteral("folk-new"), QStringLiteral("folk-old"),
                                                   QStringLiteral("folk-played"), QStringLiteral("rock")}));
}

void HomeMixBuilderTest::timeCapsuleIsOldestDatedFirst()
{
    HomeMixInputs inputs;
    inputs.catalog = {song(QStringLiteral("undated"), QStringLiteral("A"), {}, 0),
                      song(QStringLiteral("new"), QStringLiteral("A"), {}, 30),
                      song(QStringLiteral("old"), QStringLiteral("A"), {}, 10)};
    const HomeMix capsule = mixNamed(HomeMixBuilder::build(inputs), QStringLiteral("timecapsule"));
    QCOMPARE(titles(capsule.tracks), (QStringList{QStringLiteral("old"), QStringLiteral("new"), QStringLiteral("undated")}));
}

void HomeMixBuilderTest::freshFindsFollowTheCatalogue()
{
    HomeMixInputs inputs;
    inputs.catalog = {song(QStringLiteral("first"), QStringLiteral("A")), song(QStringLiteral("second"), QStringLiteral("B"))};
    const HomeMix fresh = mixNamed(HomeMixBuilder::build(inputs), QStringLiteral("fresh"));
    QCOMPARE(titles(fresh.tracks), (QStringList{QStringLiteral("first"), QStringLiteral("second")}));
    QCOMPARE(fresh.description, QStringLiteral("The latest additions to your library."));
}

void HomeMixBuilderTest::artistsAreRankedByPlays()
{
    const QList<Track> tracks{song(QStringLiteral("1"), QStringLiteral("Zed")), song(QStringLiteral("2"), QStringLiteral("Amy")),
                              song(QStringLiteral("3"), QStringLiteral("zed")), song(QStringLiteral("4"), QStringLiteral("Unknown Artist")),
                              song(QStringLiteral("5"), QString())};
    QHash<QString, quint32> plays;
    QCOMPARE(HomeMixBuilder::rankedArtists(tracks, plays), (QStringList{QStringLiteral("Zed"), QStringLiteral("Amy")}));
    plays.insert(lmp::core::homeHistoryKey(tracks.at(1)), 5);
    QCOMPARE(HomeMixBuilder::rankedArtists(tracks, plays), (QStringList{QStringLiteral("Amy"), QStringLiteral("Zed")}));
    QCOMPARE(HomeMixBuilder::artistLine(tracks, plays), QStringLiteral("Amy, Zed"));
}

void HomeMixBuilderTest::sameInputsSameMixes()
{
    HomeMixInputs inputs;
    for (int index = 0; index < 30; ++index) {
        inputs.catalog.append(song(QStringLiteral("S%1").arg(index), QStringLiteral("A%1").arg(index % 4), {}, index));
    }
    const QList<HomeMix> first = HomeMixBuilder::build(inputs);
    const QList<HomeMix> second = HomeMixBuilder::build(inputs);
    for (qsizetype index = 0; index < first.size(); ++index) {
        QCOMPARE(titles(first.at(index).tracks), titles(second.at(index).tracks));
    }
    // The daily fallbacks start at different places, so they differ.
    QVERIFY(titles(first.at(3).tracks) != titles(first.at(4).tracks));
}

void HomeMixBuilderTest::refreshKeyChangesDailyAndWithMusic()
{
    const QDate today(2026, 11, 23);
    QCOMPARE(HomeMixBuilder::refreshKey(today, true), HomeMixBuilder::refreshKey(today, true));
    QVERIFY(HomeMixBuilder::refreshKey(today, true) != HomeMixBuilder::refreshKey(today.addDays(1), true));
    QVERIFY(HomeMixBuilder::refreshKey(today, true) != HomeMixBuilder::refreshKey(today, false));
}

QTEST_GUILESS_MAIN(HomeMixBuilderTest)
#include "tst_home_mix_builder.moc"
