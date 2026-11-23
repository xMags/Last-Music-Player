#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "core/account/WelcomeModel.h"

namespace account = lmp::core::account;
using account::ArtistStepStatus;
using account::ArtistWaiver;
using account::WelcomeArtist;
using account::WelcomeMode;

namespace {

WelcomeArtist artist(const QString& name, const QString& catalogId = QString())
{
    WelcomeArtist item;
    item.title = name;
    item.identity = account::LibraryArtistIdentity{catalogId.isEmpty() ? QStringLiteral("UC-") + name : catalogId,
                                                   QString(), name, QStringLiteral("source-a")};
    return item;
}

QStringList names(const QList<WelcomeArtist>& items)
{
    QStringList result;
    for (const WelcomeArtist& item : items) {
        result.append(item.title);
    }
    return result;
}

QStringList list(std::initializer_list<const char*> codes)
{
    QStringList result;
    for (const char* code : codes) {
        result.append(QString::fromLatin1(code));
    }
    return result;
}

} // namespace

// The welcome flow's rules, ported from the reference client's cases.
class WelcomeModelTest : public QObject {
    Q_OBJECT

private slots:
    void onlyANewAccountOwesTheFlow();
    void firstStepStartsFromTheListenersOwnList();
    void serverRegionTableDecidesWhenThere();
    void aLanguageIsRequiredAndNeverMoreThanTen();
    void threeArtistsUnlessWaived();
    void anArtistHasOneKey();
    void similarArtistsFollowThePick();
    void reopenedFlowLeadsWithFollowed();
    void catalogueAndSuggestionsAreRead();
};

void WelcomeModelTest::onlyANewAccountOwesTheFlow()
{
    QVERIFY(account::needsWelcome(false, QString()));
    QVERIFY(!account::needsWelcome(false, QStringLiteral("2026-11-12T00:00:00Z")));
    QVERIFY(!account::needsWelcome(true, QString()));
}

void WelcomeModelTest::firstStepStartsFromTheListenersOwnList()
{
    const std::optional<account::RegionLanguageTable> none;
    QCOMPARE(account::initialLanguages(std::nullopt, QStringLiteral("in"), none),
             list({"hi", "bn", "ta", "te", "pa", "mr", "en"}));
    QCOMPARE(account::initialLanguages(account::ContentLanguages{QStringLiteral("custom"), list({"ta", "en"})},
                                       QStringLiteral("in"), none),
             list({"ta", "en"}));
    // The automatic list is the service's guess, not the listener's choice.
    QCOMPARE(account::initialLanguages(account::ContentLanguages{QStringLiteral("auto"), list({"ko"})},
                                       QStringLiteral("gb"), none),
             list({"en"}));
    QCOMPARE(account::initialLanguages(account::ContentLanguages{QStringLiteral("custom"), {}}, QStringLiteral("us"), none),
             list({"en", "es"}));
    // A region without suggestions starts empty.
    QVERIFY(account::initialLanguages(std::nullopt, QStringLiteral("zz"), none).isEmpty());
}

void WelcomeModelTest::serverRegionTableDecidesWhenThere()
{
    account::RegionLanguageTable regions;
    regions.insert(QStringLiteral("in"), list({"ta", "en"}));
    regions.insert(QStringLiteral("us"), {});
    QCOMPARE(account::initialLanguages(std::nullopt, QStringLiteral("in"), regions), list({"ta", "en"}));
    QVERIFY(account::initialLanguages(std::nullopt, QStringLiteral("us"), regions).isEmpty());
    QVERIFY(account::initialLanguages(std::nullopt, QStringLiteral("gb"), regions).isEmpty());
    QCOMPARE(account::initialLanguages(std::nullopt, QStringLiteral("in"), std::nullopt),
             list({"hi", "bn", "ta", "te", "pa", "mr", "en"}));
    QCOMPARE(account::initialLanguages(account::ContentLanguages{QStringLiteral("custom"), list({"ko"})},
                                       QStringLiteral("in"), regions),
             list({"ko"}));
    QCOMPARE(account::regionLanguages(QStringLiteral("IN"), regions), list({"ta", "en"}));
}

void WelcomeModelTest::aLanguageIsRequiredAndNeverMoreThanTen()
{
    QVERIFY(!account::languagesReady(0, WelcomeMode::FirstRun));
    QVERIFY(!account::languagesReady(0, WelcomeMode::Tune));
    QVERIFY(account::languagesReady(1, WelcomeMode::FirstRun));
    QVERIFY(!account::languagesReady(11, WelcomeMode::FirstRun));
}

void WelcomeModelTest::threeArtistsUnlessWaived()
{
    QCOMPARE(account::artistStepStatus(0, WelcomeMode::FirstRun, ArtistWaiver::None), (ArtistStepStatus{false, 3}));
    QCOMPARE(account::artistStepStatus(2, WelcomeMode::FirstRun, ArtistWaiver::None), (ArtistStepStatus{false, 1}));
    QCOMPARE(account::artistStepStatus(3, WelcomeMode::FirstRun, ArtistWaiver::None), (ArtistStepStatus{true, 0}));
    QCOMPARE(account::artistStepStatus(7, WelcomeMode::FirstRun, ArtistWaiver::None), (ArtistStepStatus{true, 0}));
    QCOMPARE(account::artistStepStatus(0, WelcomeMode::FirstRun, ArtistWaiver::SuggestionsUnavailable),
             (ArtistStepStatus{true, 0}));
    QCOMPARE(account::artistStepStatus(1, WelcomeMode::FirstRun, ArtistWaiver::FollowNotAllowed),
             (ArtistStepStatus{true, 0}));
    QCOMPARE(account::artistStepStatus(0, WelcomeMode::Tune, ArtistWaiver::None), (ArtistStepStatus{true, 0}));
}

void WelcomeModelTest::anArtistHasOneKey()
{
    QCOMPARE(account::artistKey({QStringLiteral("UC1"), {}, QStringLiteral("A"), QStringLiteral("Source-A")}),
             QStringLiteral("source-a:UC1"));
    QCOMPARE(account::artistKey({{}, QStringLiteral("https://x/UC1"), QStringLiteral("A"), QStringLiteral("source-a")}),
             QStringLiteral("source-a:https://x/UC1"));
    QCOMPARE(account::artistKey({{}, {}, QStringLiteral(" Some One "), QStringLiteral("source-a")}),
             QStringLiteral("source-a:name:some one"));
    WelcomeArtist unfollowable = artist(QStringLiteral("X"));
    unfollowable.identity.reset();
    QVERIFY(account::artistItemKey(unfollowable).isEmpty());
    QCOMPARE(names(account::uniqueArtists({artist(QStringLiteral("A")), unfollowable, artist(QStringLiteral("A")),
                                           artist(QStringLiteral("B"))})),
             list({"A", "B"}));
}

void WelcomeModelTest::similarArtistsFollowThePick()
{
    const QList<WelcomeArtist> grid{artist(QStringLiteral("A")), artist(QStringLiteral("B")), artist(QStringLiteral("C"))};
    const QList<WelcomeArtist> similar{artist(QStringLiteral("B")), artist(QStringLiteral("X")), artist(QStringLiteral("Y")),
                                       artist(QStringLiteral("X")), artist(QStringLiteral("Z")), artist(QStringLiteral("W"))};
    QCOMPARE(names(account::insertSimilarArtists(grid, account::artistItemKey(grid.at(0)), similar)),
             list({"A", "X", "Y", "Z", "B", "C"}));
    QCOMPARE(names(account::insertSimilarArtists(grid, account::artistItemKey(grid.at(2)), similar, 1)),
             list({"A", "B", "C", "X"}));
    QCOMPARE(names(account::insertSimilarArtists(grid, account::artistItemKey(grid.at(0)),
                                                 {artist(QStringLiteral("B")), artist(QStringLiteral("C"))})),
             list({"A", "B", "C"}));
    QCOMPARE(names(account::insertSimilarArtists(grid, QStringLiteral("source-a:missing"), similar)), list({"A", "B", "C"}));
}

void WelcomeModelTest::reopenedFlowLeadsWithFollowed()
{
    const QList<WelcomeArtist> suggested{artist(QStringLiteral("A")), artist(QStringLiteral("B")), artist(QStringLiteral("C"))};
    QCOMPARE(names(account::withFollowedFirst(suggested, {artist(QStringLiteral("Q")), artist(QStringLiteral("B"))})),
             list({"Q", "B", "A", "C"}));
    QCOMPARE(names(account::withFollowedFirst(suggested, {})), list({"A", "B", "C"}));
}

void WelcomeModelTest::catalogueAndSuggestionsAreRead()
{
    const QJsonDocument catalog = QJsonDocument::fromJson(
        "{\"languages\":[{\"code\":\"HI\",\"name\":\"Hindi\",\"nativeName\":\"x\"},{\"code\":\"hi\"},"
        "{\"code\":\"english\"},{\"code\":\"en\"}],\"regions\":{\"IN\":[\"hi\",\"zz\",\"hi\",\"en\"],\"india\":[\"hi\"]}}");
    const QList<account::LanguageInfo> languages =
        account::normalizeLanguageCatalog(catalog.object().value(QLatin1StringView("languages")));
    QCOMPARE(languages.size(), 2);
    QCOMPARE(languages.at(0).code, QStringLiteral("hi"));
    QCOMPARE(languages.at(1).name, QStringLiteral("EN"));
    const std::optional<account::RegionLanguageTable> regions =
        account::normalizeRegionLanguages(catalog.object().value(QLatin1StringView("regions")), languages);
    QVERIFY(regions.has_value());
    QCOMPARE(regions->size(), 1);
    QCOMPARE(regions->value(QStringLiteral("in")), list({"hi", "en"}));

    const QList<WelcomeArtist> suggested = account::parseSuggestedArtists(
        "{\"artists\":[{\"name\":\"Mira Sol\",\"provider\":\"source-a\",\"catalogId\":\"c1\",\"followed\":true,"
        "\"libraryArtistId\":\"la1\",\"languages\":[\"hi\"]},{\"name\":\"No Id\",\"provider\":\"source-a\"}]}");
    QCOMPARE(suggested.size(), 1);
    QCOMPARE(suggested.first().followedId, QStringLiteral("la1"));
    QCOMPARE(suggested.first().languages, list({"hi"}));
}

QTEST_GUILESS_MAIN(WelcomeModelTest)
#include "tst_welcome_model.moc"
