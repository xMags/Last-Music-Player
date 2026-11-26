#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include <algorithm>

#include "CatalogFixtures.h"
#include "core/home/HomeLayout.h"
#include "core/home/HomeModel.h"

namespace home = lmp::core::home;
using lmp::core::HomeMix;
using lmp::core::Track;
using lmp::test::json;

namespace {

Track track(const QString& id, const QString& artist = QStringLiteral("Artist A"))
{
    Track result;
    result.remoteId = id;
    result.title = QStringLiteral("Track ") + id;
    result.artist = artist;
    result.sourceUrl = QStringLiteral("https://music.example.test/") + id;
    return result;
}

QStringList ids(const QList<Track>& tracks)
{
    QStringList result;
    for (const Track& item : tracks) {
        result.append(item.remoteId);
    }
    return result;
}

const HomeMix& mix(const home::HomeModel& model, const char* id)
{
    return *std::find_if(model.mixes.cbegin(), model.mixes.cend(),
                         [id](const HomeMix& entry) { return entry.id == QLatin1StringView(id); });
}

QJsonValue value(const char* text)
{
    return QJsonDocument::fromJson(json(text)).isObject() ? QJsonValue(QJsonDocument::fromJson(json(text)).object())
                                                          : QJsonValue(QJsonDocument::fromJson(json(text)).array());
}

QStringList order(const home::HomeLayout& layout)
{
    QStringList result;
    for (const home::HomeLayoutTier& tier : layout.tiers) {
        QStringList shelves;
        for (const home::HomeLayoutShelf& shelf : tier.shelves) {
            shelves.append(shelf.key);
        }
        result.append(tier.key + QStringLiteral(": ") + shelves.join(QLatin1Char(',')));
    }
    return result;
}

const QStringList kDefaultOrder{
    QStringLiteral("you: mixes,listenAgain"),
    QStringLiteral("new: newSongs,newAlbums,biggestHits,popularArtists,topSongs,topAlbums,topPlaylists"),
    QStringLiteral("library: mostPlayed,liked,recentlyAdded"),
    QStringLiteral("explore: moods,genreCharts,cityCharts"),
};

} // namespace

// The reference client's home model and home layout cases.
class HomeModelTest : public QObject {
    Q_OBJECT

private slots:
    void buildsShelvesAndNineMixes();
    void listenAgainNeverSubstitutesLikes();
    void mixKeyChangesWithTheDayOnly();
    void dailyProviderFallbacksFanOut();
    void dailyMixesStayDistinctWithoutGenres();
    void dailyMixesFillFromASmallLibrary();
    void fillLabelsPadsWithUnseenFallbacks();
    void starterMixesDealFollowedArtists();
    void starterMixInterleavesSongs();
    void sortsByPlaysDatesAndGenres();

    void builtInLayoutIsTodaysHome();
    void noLayoutGivesTheBuiltInOne();
    void layoutOrderWinsAndTheRestFollows();
    void unknownKeysAndRepeatsAreIgnored();
    void onlyEnabledFalseHides();
    void titlesAreCheckedTextOfSixtyCharacters();
    void aCompleteLayoutReadsBackTheSame();
    void phoneFormPutsListenAgainFirst();
    void listedLayoutKeepsOnlyWhatWasNamed();
};

// Every date-ordered list, ties kept in library order.
void HomeModelTest::sortsByPlaysDatesAndGenres()
{
    const auto song = [](const char* id, int plays, const char* lastPlayed, const char* added, const char* genre) {
        Track result = track(QString::fromLatin1(id));
        result.playCount = plays;
        result.lastPlayedAt = QString::fromLatin1(lastPlayed);
        result.addedAt = QString::fromLatin1(added);
        result.genre = QString::fromLatin1(genre);
        return result;
    };
    home::HomeLibrary library;
    // c has no added date, so its last play stands in for it.
    library.playlistTracks = {
        song("a", 3, "2026-01-02T00:00:00Z", "2026-01-01T00:00:00Z", "Pop"),
        song("b", 3, "2026-01-05T00:00:00Z", "2025-12-01T00:00:00Z", "Pop"),
        song("c", 1, "2026-01-03T00:00:00Z", "", "Pop"),
        song("d", 0, "", "", ""),
        song("e", 3, "2026-01-05T00:00:00Z", "2026-01-01T00:00:00Z", ""),
    };
    const home::HomeModel model = home::deriveHomeModel(library);

    QCOMPARE(ids(model.mostPlayed), QStringList({"b", "e", "a", "c"}));
    QCOMPARE(ids(model.recentlyAdded), QStringList({"c", "a", "e", "b"}));
    QCOMPARE(ids(mix(model, "repeat").tracks), QStringList({"b", "e", "a", "c", "d"}));
    QCOMPARE(ids(mix(model, "fresh").tracks), QStringList({"c", "a", "e", "b", "d"}));
    QCOMPARE(ids(mix(model, "timecapsule").tracks), QStringList({"b", "a", "e", "c", "d"}));
    QCOMPARE(ids(mix(model, "discover").tracks), QStringList({"d", "e", "c", "a", "b"}));
}

void HomeModelTest::buildsShelvesAndNineMixes()
{
    Track fresh = track(QStringLiteral("new"));
    fresh.genre = QStringLiteral("Pop");
    fresh.playCount = 2;
    fresh.firstPlayedAt = QStringLiteral("2026-06-03T00:00:00Z");
    fresh.lastPlayedAt = QStringLiteral("2026-06-05T00:00:00Z");
    fresh.addedAt = QStringLiteral("2026-06-03T00:00:00Z");
    Track repeat = track(QStringLiteral("repeat"), QStringLiteral("Artist B"));
    repeat.genre = QStringLiteral("Rock");
    repeat.playCount = 8;
    repeat.firstPlayedAt = QStringLiteral("2026-06-02T00:00:00Z");
    repeat.lastPlayedAt = QStringLiteral("2026-06-04T00:00:00Z");
    repeat.addedAt = QStringLiteral("2026-06-02T00:00:00Z");
    Track old = track(QStringLiteral("old"), QStringLiteral("Artist C"));
    old.genre = QStringLiteral("Jazz");
    old.playCount = 1;
    old.firstPlayedAt = QStringLiteral("2026-06-01T00:00:00Z");
    old.lastPlayedAt = QStringLiteral("2026-06-01T00:00:00Z");
    old.addedAt = QStringLiteral("2026-06-01T00:00:00Z");
    Track liked = fresh;
    liked.liked = true;
    const home::HomeModel model = home::deriveHomeModel({{fresh, repeat, old}, {liked}, {}});
    QCOMPARE(model.listenAgain.size(), 3);
    QCOMPARE(model.mixes.size(), 9);
    QCOMPARE(mix(model, "repeat").tracks.first().remoteId, QStringLiteral("repeat"));
    QCOMPARE(mix(model, "timecapsule").tracks.first().remoteId, QStringLiteral("old"));
    QCOMPARE(mix(model, "fresh").tracks.first().remoteId, QStringLiteral("new"));
    QCOMPARE(model.mostPlayed.size(), 3);
    QCOMPARE(model.recentlyAdded.size(), 3);
    QCOMPARE(ids(model.catalog).count(QStringLiteral("new")), 1);
    // The first Daily Mix takes the top genre and says so.
    QCOMPARE(home::mixCaption(mix(model, "daily1")), QStringLiteral("Rock"));
}

void HomeModelTest::listenAgainNeverSubstitutesLikes()
{
    Track liked = track(QStringLiteral("liked"));
    liked.liked = true;
    liked.likedAt = QStringLiteral("2026-06-01T00:00:00Z");
    liked.addedAt = liked.likedAt;
    const home::HomeModel model = home::deriveHomeModel({{}, {liked}, {}});
    QVERIFY(model.listenAgain.isEmpty());
    QCOMPARE(model.liked.size(), 1);
    QCOMPARE(ids(home::mergeHomeMixTracks({liked}, {liked, track(QStringLiteral("remote"))})),
             (QStringList{QStringLiteral("liked"), QStringLiteral("remote")}));
    QCOMPARE(ids(home::mergeHomeMixTracks({liked}, {track(QStringLiteral("remote"))}, true)),
             (QStringList{QStringLiteral("remote"), QStringLiteral("liked")}));
}

void HomeModelTest::mixKeyChangesWithTheDayOnly()
{
    const QDate day(2026, 10, 14);
    Track one = track(QStringLiteral("one"));
    one.playCount = 1;
    const home::HomeLibrary first{{one}, {}, {}};
    Track again = one;
    again.playCount = 2;
    const home::HomeLibrary replayed{{track(QStringLiteral("two")), again}, {}, {}};
    QCOMPARE(home::homeMixKey(first, day), home::homeMixKey(replayed, day));
    QVERIFY(home::homeMixKey(first, day) != home::homeMixKey(first, day.addDays(1)));
    QVERIFY(home::homeMixKey({}, day) != home::homeMixKey(first, day));
}

void HomeModelTest::dailyProviderFallbacksFanOut()
{
    Track a = track(QStringLiteral("a"), QStringLiteral("Artist A"));
    a.playCount = 5;
    Track b = track(QStringLiteral("b"), QStringLiteral("Artist B"));
    b.playCount = 4;
    Track c = track(QStringLiteral("c"), QStringLiteral("Artist C"));
    c.playCount = 3;
    const home::HomeModel model = home::deriveHomeModel({{a, b, c}, {}, {}});
    QStringList queries;
    for (int index = 0; index < 5; ++index) {
        queries.append(home::providerQueryForMix(model.mixes.at(index), model.catalog));
    }
    QCOMPARE(queries, (QStringList{QStringLiteral("Artist A"), QStringLiteral("Artist B"), QStringLiteral("Artist C"),
                                   QStringLiteral("top hits"), QStringLiteral("feel good songs")}));
}

void HomeModelTest::dailyMixesStayDistinctWithoutGenres()
{
    QList<Track> history;
    for (int index = 0; index < 80; ++index) {
        history.append(track(QStringLiteral("s%1").arg(index), QStringLiteral("Artist %1").arg(index % 40)));
    }
    const home::HomeModel model = home::deriveHomeModel({history, {}, {}});
    const QList<HomeMix> daily = model.mixes.mid(0, 5);
    for (const HomeMix& entry : daily) {
        QCOMPARE(entry.tracks.size(), 12);
    }
    for (qsizetype left = 0; left < daily.size(); ++left) {
        for (qsizetype right = left + 1; right < daily.size(); ++right) {
            const QStringList leftIds = ids(daily.at(left).tracks);
            const qsizetype shared = std::count_if(daily.at(right).tracks.cbegin(), daily.at(right).tracks.cend(),
                                                   [&leftIds](const Track& item) { return leftIds.contains(item.remoteId); });
            QVERIFY2(shared <= 2, qPrintable(daily.at(left).id + QStringLiteral(" and ") + daily.at(right).id));
        }
    }
}

void HomeModelTest::dailyMixesFillFromASmallLibrary()
{
    const home::HomeModel model = home::deriveHomeModel(
        {{track(QStringLiteral("a"), QStringLiteral("One")), track(QStringLiteral("b"), QStringLiteral("Two")),
          track(QStringLiteral("c"), QStringLiteral("Three"))},
         {},
         {}});
    for (int index = 0; index < 5; ++index) {
        QCOMPARE(model.mixes.at(index).tracks.size(), 3);
    }
}

void HomeModelTest::fillLabelsPadsWithUnseenFallbacks()
{
    const QStringList fallback{QStringLiteral("Pop"),       QStringLiteral("Hip-Hop"), QStringLiteral("Electronic"),
                               QStringLiteral("Chill"),     QStringLiteral("Bollywood"), QStringLiteral("Rock"),
                               QStringLiteral("Indie"),     QStringLiteral("R&B")};
    QCOMPARE(home::fillLabels({}, fallback, 8), fallback);
    QCOMPARE(home::fillLabels({QStringLiteral("Ambient"), QStringLiteral("pop"), QStringLiteral("Dance")}, fallback, 8),
             (QStringList{QStringLiteral("Ambient"), QStringLiteral("pop"), QStringLiteral("Dance"), QStringLiteral("Hip-Hop"),
                          QStringLiteral("Electronic"), QStringLiteral("Chill"), QStringLiteral("Bollywood"),
                          QStringLiteral("Rock")}));
    const QStringList many{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("E"),
                           QStringLiteral("F"), QStringLiteral("G"), QStringLiteral("H"), QStringLiteral("I")};
    QCOMPARE(home::fillLabels(many, fallback, 8), many.mid(0, 8));
    QCOMPARE(home::fillLabels({QStringLiteral("Rock"), QStringLiteral("rock"), QStringLiteral(" ")}, {QStringLiteral("ROCK")}, 8),
             QStringList{QStringLiteral("Rock")});
}

void HomeModelTest::starterMixesDealFollowedArtists()
{
    QVERIFY(home::planStarterMixes({}).isEmpty());
    const QList<home::StarterMixPlan> three = home::planStarterMixes(
        {QStringLiteral("Singer One"), QStringLiteral(" Singer Two "), QStringLiteral("singer one"), QStringLiteral("Singer Three")});
    QCOMPARE(three.size(), 3);
    QCOMPARE(three.at(0).mix.id, QStringLiteral("daily1"));
    QCOMPARE(three.at(0).artists, QStringList{QStringLiteral("Singer One")});
    QCOMPARE(three.at(1).artists, QStringList{QStringLiteral("Singer Two")});
    QCOMPARE(three.at(2).artists, QStringList{QStringLiteral("Singer Three")});
    QCOMPARE(three.at(0).mix.title, QStringLiteral("Daily Mix 1"));
    QCOMPARE(three.at(0).mix.description, QStringLiteral("Singer One"));
    QCOMPARE(home::mixCaption(three.at(0).mix), QStringLiteral("From artists you follow"));
    QVERIFY(three.at(0).mix.tracks.isEmpty());
    QStringList twenty;
    for (int index = 0; index < 20; ++index) {
        twenty.append(QStringLiteral("Artist %1").arg(index));
    }
    const QList<home::StarterMixPlan> many = home::planStarterMixes(twenty);
    QCOMPARE(many.size(), 5);
    QCOMPARE(many.at(0).artists,
             (QStringList{QStringLiteral("Artist 0"), QStringLiteral("Artist 5"), QStringLiteral("Artist 10")}));
    QCOMPARE(many.at(4).artists,
             (QStringList{QStringLiteral("Artist 4"), QStringLiteral("Artist 9"), QStringLiteral("Artist 14")}));
    QCOMPARE(many.at(0).mix.description, QStringLiteral("Artist 0, Artist 5 & more"));
}

void HomeModelTest::starterMixInterleavesSongs()
{
    const QList<Track> a{track(QStringLiteral("a1")), track(QStringLiteral("a2")), track(QStringLiteral("a3"))};
    const QList<Track> b{track(QStringLiteral("b1")), track(QStringLiteral("a2"))};
    QCOMPARE(ids(home::interleaveMixTracks({a, b})),
             (QStringList{QStringLiteral("a1"), QStringLiteral("b1"), QStringLiteral("a2"), QStringLiteral("a3")}));
    QVERIFY(home::interleaveMixTracks({}).isEmpty());
    QList<Track> longList;
    for (int index = 0; index < 30; ++index) {
        longList.append(track(QStringLiteral("long%1").arg(index)));
    }
    QCOMPARE(home::interleaveMixTracks({longList}).size(), 12);
}

void HomeModelTest::builtInLayoutIsTodaysHome()
{
    const home::HomeLayout layout = home::defaultHomeLayout();
    QCOMPARE(order(layout), kDefaultOrder);
    int shelves = 0;
    for (const home::HomeLayoutTier& tier : layout.tiers) {
        QVERIFY(!tier.title);
        for (const home::HomeLayoutShelf& shelf : tier.shelves) {
            QVERIFY(!shelf.title && shelf.enabled);
            ++shelves;
        }
    }
    QCOMPARE(shelves, 15);
}

void HomeModelTest::noLayoutGivesTheBuiltInOne()
{
    for (const char* raw : {"null", "\"\"", "\"tiers\"", "7", "true", "[]", "[{'key':'you'}]", "{}", "{'tiers':null}",
                            "{'tiers':{}}", "{'tiers':'you'}"}) {
        const QJsonValue parsed = QJsonDocument::fromJson(QByteArray("[") + json(raw) + "]").array().at(0);
        QVERIFY2(!home::readHomeLayout(parsed), raw);
        QCOMPARE(home::normalizeHomeLayout(parsed), home::defaultHomeLayout());
    }
    QCOMPARE(home::normalizeHomeLayout(value("{'tiers':[null,3,'new',[],{'key':'nope'},{'title':'No key'}]}")),
             home::defaultHomeLayout());
}

void HomeModelTest::layoutOrderWinsAndTheRestFollows()
{
    const home::HomeLayout layout = home::normalizeHomeLayout(value(
        "{'tiers':[{'key':'explore','title':null,'shelves':[{'key':'cityCharts','title':null,'enabled':true}]},"
        "{'key':'new','title':null,'shelves':[{'key':'topPlaylists'},{'key':'popularArtists'}]}]}"));
    QCOMPARE(order(layout),
             (QStringList{QStringLiteral("explore: cityCharts,moods,genreCharts"),
                          QStringLiteral("new: topPlaylists,popularArtists,newSongs,newAlbums,biggestHits,topSongs,topAlbums"),
                          QStringLiteral("you: mixes,listenAgain"), QStringLiteral("library: mostPlayed,liked,recentlyAdded")}));
}

void HomeModelTest::unknownKeysAndRepeatsAreIgnored()
{
    const home::HomeLayout layout = home::normalizeHomeLayout(value(
        "{'tiers':[{'key':'radio','shelves':[{'key':'mixes'}]},"
        "{'key':'library','title':'Yours','shelves':[{'key':'mixes','title':'Moved','enabled':false},{'key':'podcasts'},"
        "{'key':'liked','title':'First','enabled':false},{'key':'liked','title':'Second','enabled':true},'liked',null]},"
        "{'key':'library','title':'Again','shelves':[{'key':'recentlyAdded','enabled':false}]}]}"));
    QCOMPARE(order(layout),
             (QStringList{QStringLiteral("library: liked,mostPlayed,recentlyAdded"), QStringLiteral("you: mixes,listenAgain"),
                          QStringLiteral("new: newSongs,newAlbums,biggestHits,popularArtists,topSongs,topAlbums,topPlaylists"),
                          QStringLiteral("explore: moods,genreCharts,cityCharts")}));
    const home::HomeLayoutView view(layout);
    QCOMPARE(view.tierTitle(QStringLiteral("library"), QStringLiteral("From your library")), QStringLiteral("Yours"));
    QCOMPARE(view.shelfTitle(QStringLiteral("liked"), QStringLiteral("Your Liked Songs")), QStringLiteral("First"));
    QVERIFY(!view.shelfEnabled(QStringLiteral("liked")));
    QVERIFY(view.shelfEnabled(QStringLiteral("recentlyAdded")));
    QVERIFY(view.shelfEnabled(QStringLiteral("mixes")));
    QCOMPARE(view.shelfTitle(QStringLiteral("mixes"), QStringLiteral("Your mixes")), QStringLiteral("Your mixes"));
}

void HomeModelTest::onlyEnabledFalseHides()
{
    const home::HomeLayoutView view(home::normalizeHomeLayout(value(
        "{'tiers':[{'key':'library','shelves':[{'key':'mostPlayed','enabled':false},{'key':'liked','enabled':'false'},"
        "{'key':'recentlyAdded','enabled':0}]}]}")));
    QVERIFY(!view.shelfEnabled(QStringLiteral("mostPlayed")));
    QVERIFY(view.shelfEnabled(QStringLiteral("liked")));
    QVERIFY(view.shelfEnabled(QStringLiteral("recentlyAdded")));
    QVERIFY(view.shelfEnabled(QStringLiteral("topSongs")));
}

void HomeModelTest::titlesAreCheckedTextOfSixtyCharacters()
{
    QCOMPARE(home::normalizeLayoutTitle(QJsonValue(QStringLiteral("  Fresh  "))), std::optional<QString>(QStringLiteral("Fresh")));
    QCOMPARE(home::normalizeLayoutTitle(QJsonValue(QStringLiteral("<b>Bold</b>"))),
             std::optional<QString>(QStringLiteral("<b>Bold</b>")));
    QCOMPARE(home::normalizeLayoutTitle(QJsonValue(QString(60, QLatin1Char('x')))), std::optional<QString>(QString(60, QLatin1Char('x'))));
    QVERIFY(!home::normalizeLayoutTitle(QJsonValue(QString(61, QLatin1Char('x')))));
    // Counted in characters, so sixty characters outside the BMP still fit.
    QString notes;
    for (int index = 0; index < 60; ++index) {
        notes += QString::fromUcs4(U"\U0001F3B5", 1);
    }
    QCOMPARE(home::normalizeLayoutTitle(QJsonValue(notes)), std::optional<QString>(notes));
    for (const QJsonValue& raw : {QJsonValue(), QJsonValue(QString()), QJsonValue(QStringLiteral("   ")), QJsonValue(42),
                                  QJsonValue(true), QJsonValue(QJsonObject())}) {
        QVERIFY(!home::normalizeLayoutTitle(raw));
    }
    const home::HomeLayoutView view(home::normalizeHomeLayout(value(
        "{'tiers':[{'key':'new','title':'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx',"
        "'shelves':[{'key':'topSongs','title':'Chart toppers'},{'key':'newSongs','title':5}]}]}")));
    QCOMPARE(view.tierTitle(QStringLiteral("new"), QStringLiteral("New and trending")), QStringLiteral("New and trending"));
    QCOMPARE(view.shelfTitleOverride(QStringLiteral("topSongs")), std::optional<QString>(QStringLiteral("Chart toppers")));
    QVERIFY(!view.shelfTitleOverride(QStringLiteral("newSongs")));
}

void HomeModelTest::aCompleteLayoutReadsBackTheSame()
{
    const home::HomeLayout layout = home::normalizeHomeLayout(
        value("{'tiers':[{'key':'explore','title':'Discover','shelves':[{'key':'moods','enabled':false}]}]}"));
    QCOMPARE(home::readHomeLayout(home::homeLayoutJson(layout)), std::optional<home::HomeLayout>(layout));
}

void HomeModelTest::phoneFormPutsListenAgainFirst()
{
    QCOMPARE(home::defaultHomeLayout().tiers.at(0).shelves.at(0).key, QStringLiteral("mixes"));
    QCOMPARE(home::defaultHomeLayout(true).tiers.at(0).shelves.at(0).key, QStringLiteral("listenAgain"));
    QCOMPARE(home::defaultHomeLayout(true).tiers.mid(1), home::defaultHomeLayout().tiers.mid(1));
    const std::optional<home::HomeLayout> listed =
        home::listedHomeLayout(value("{'tiers':[{'key':'you','shelves':[{'key':'mixes'},{'key':'listenAgain'}]}]}"));
    for (const bool phone : {false, true}) {
        QCOMPARE(home::normalizeHomeLayout(home::homeLayoutJson(*listed), phone).tiers.at(0).shelves.at(0).key,
                 QStringLiteral("mixes"));
    }
    const std::optional<home::HomeLayout> renamed = home::listedHomeLayout(value("{'tiers':[{'key':'you','title':'Hi'}]}"));
    QCOMPARE(home::normalizeHomeLayout(home::homeLayoutJson(*renamed), true).tiers.at(0).shelves.at(0).key,
             QStringLiteral("listenAgain"));
}

void HomeModelTest::listedLayoutKeepsOnlyWhatWasNamed()
{
    const std::optional<home::HomeLayout> listed = home::listedHomeLayout(value(
        "{'tiers':[{'key':'explore','title':'Discover','shelves':[{'key':'moods','enabled':false},{'key':'moods'},"
        "{'key':'mixes'},{'key':'nope'}]},{'key':'explore'},{'key':'nope'}]}"));
    QVERIFY(listed);
    QCOMPARE(listed->tiers.size(), 1);
    QCOMPARE(listed->tiers.at(0).title, std::optional<QString>(QStringLiteral("Discover")));
    QCOMPARE(listed->tiers.at(0).shelves.size(), 1);
    QCOMPARE(listed->tiers.at(0).shelves.at(0), (home::HomeLayoutShelf{QStringLiteral("moods"), std::nullopt, false}));
    QCOMPARE(home::listedHomeLayout(home::homeLayoutJson(*listed)), listed);
    QVERIFY(!home::listedHomeLayout(QJsonValue(QStringLiteral("layout"))));
    QVERIFY(!home::listedHomeLayout(value("{'tiers':'no'}")));
}

QTEST_GUILESS_MAIN(HomeModelTest)
#include "tst_home_model.moc"
