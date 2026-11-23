#include <QCoreApplication>
#include <QDateTime>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QStringList>
#include <QTest>

#include <memory>
#include <optional>
#include <vector>

#include "CatalogFixtures.h"
#include "FakePlaybackClock.h"
#include "core/catalog/ContentLanguageController.h"
#include "core/catalog/ContentLanguages.h"

namespace catalog = lmp::core::catalog;
namespace account = lmp::core::account;
using catalog::LanguageInfo;
using catalog::LanguageMode;
using catalog::LanguageSetting;
using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::test::jsonValue;

namespace {

QStringList codes(std::initializer_list<const char*> values)
{
    QStringList result;
    for (const char* value : values) {
        result.append(QString::fromLatin1(value));
    }
    return result;
}

const QStringList kTen = codes({"hi", "bn", "pa", "ta", "te", "ml", "kn", "mr", "gu", "or"});

LanguageSetting setting(LanguageMode mode, QStringList languages, std::optional<QStringList> inferred = std::nullopt,
                        QString inferredAt = {})
{
    return LanguageSetting{mode, std::move(languages), std::move(inferred), std::move(inferredAt)};
}

using Reply = QPromise<Result<LanguageSetting>>;

// The controller over a stored value, with replies the test sends.
struct Harness {
    explicit Harness(std::optional<LanguageSetting> initial, QStringList fallback = {})
        : stored(std::move(initial))
    {
        catalog::ContentLanguageController::Dependencies deps;
        deps.read = [this]() { return stored; };
        deps.write = [this](const std::optional<LanguageSetting>& value) { stored = value; };
        deps.save = [this](const LanguageSetting& payload) {
            saves.push_back({payload, std::make_shared<Reply>()});
            saves.back().reply->start();
            return saves.back().reply->future();
        };
        deps.infer = [this]() {
            infers.push_back(std::make_shared<Reply>());
            infers.back()->start();
            return infers.back()->future();
        };
        deps.fallbackLanguages = [fallback]() { return fallback; };
        deps.saveFailed = [this](const Failure& failure) { failures.append(failure); };
        deps.changed = [this]() { ++changes; };
        deps.clock = &clock;
        controller = std::make_unique<catalog::ContentLanguageController>(std::move(deps));
    }

    static void answer(const std::shared_ptr<Reply>& reply, Result<LanguageSetting> value)
    {
        reply->addResult(std::move(value));
        reply->finish();
        settle();
    }

    static void settle()
    {
        for (int round = 0; round < 5; ++round) {
            QCoreApplication::processEvents();
        }
    }

    void flushTimers() { clock.advance(catalog::LanguageSaveDelayMs); }

    struct Save {
        LanguageSetting payload;
        std::shared_ptr<Reply> reply;
    };

    std::optional<LanguageSetting> stored;
    std::vector<Save> saves;
    std::vector<std::shared_ptr<Reply>> infers;
    QList<Failure> failures;
    int changes = 0;
    FakePlaybackClock clock;
    std::unique_ptr<catalog::ContentLanguageController> controller;
};

Failure failure(int status)
{
    return Failure::of(FailureKind::Server, QStringLiteral("down"), status);
}

const QList<LanguageInfo> kCatalog{
    {QStringLiteral("hi"), QStringLiteral("Hindi"), QString::fromUtf8("\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d\xe0\xa4\xa6\xe0\xa5\x80")},
    {QStringLiteral("en"), QStringLiteral("English"), QStringLiteral("English")},
    {QStringLiteral("bn"), QStringLiteral("Bengali"), QString::fromUtf8("\xe0\xa6\xac\xe0\xa6\xbe\xe0\xa6\x82\xe0\xa6\xb2\xe0\xa6\xbe")},
    {QStringLiteral("ko"), QStringLiteral("Korean"), QString::fromUtf8("\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4")},
    {QStringLiteral("bho"), QStringLiteral("Bhojpuri"), QStringLiteral("Bhojpuri")},
    {QStringLiteral("es"), QStringLiteral("Spanish"), QString::fromUtf8("Espa\xc3\xb1ol")},
    {QStringLiteral("ro"), QStringLiteral("Romanian"), QString::fromUtf8("Rom\xc3\xa2n\xc4\x83")},
    {QStringLiteral("ar"), QStringLiteral("Arabic"), QStringLiteral("Arabic")},
};

QList<QStringList> codesOf(const QList<catalog::LanguageGroup>& groups)
{
    QList<QStringList> result;
    for (const catalog::LanguageGroup& group : groups) {
        QStringList list;
        for (const LanguageInfo& info : group.languages) {
            list.append(info.code);
        }
        result.append(list);
    }
    return result;
}

QStringList idsOf(const QList<catalog::LanguageGroup>& groups)
{
    QStringList result;
    for (const catalog::LanguageGroup& group : groups) {
        result.append(group.id);
    }
    return result;
}

std::optional<account::RegionLanguageTable> table(const char* json, const QList<LanguageInfo>& languages)
{
    return account::normalizeRegionLanguages(jsonValue(json), languages);
}

} // namespace

// The reference client's content-language cases, one slot each.
class ContentLanguagesTest : public QObject {
    Q_OBJECT

private slots:
    void serverDataIsNormalized();
    void eachModeSendsTheWholeObject();
    void effectiveAndDetectedFallBackToInferred();
    void aCustomListKeepsOneToTen();
    void detectionRunsByItselfOnlyOnAutomatic();
    void detectErrorsReadAsShortMessages();
    void automaticAndOffSaveAtOnce();
    void choosePreselectsTheEffectiveList();
    void chooseWithNothingDetectedStartsFromTheRegion();
    void chooseWithNothingToPreselectStaysUnsaved();
    void leavingAnUnsavedChooseSavesNothing();
    void rapidChipTapsSendOneSave();
    void refusedTogglesChangeNothing();
    void anOlderReplyNeverOverwrites();
    void aReplyDuringAWaitingTapDoesNotUndoIt();
    void theNewestFailedSaveReverts();
    void anOlderFailureIsIgnored();
    void aFailedFirstSaveRestoresAbsence();
    void autoDetectRunsOncePerSession();
    void autoDetectIsNotRetriedAfterAFailure();
    void autoDetectDoesNotRunOtherwise();
    void aDetectionDuringASaveKeepsTheChoice();
    void resetDropsLateReplies();
    void withoutARegionTheCatalogSplitsByArea();
    void aRegionLeadsWithItsOwnLanguages();
    void aRegionWithoutSuggestionsKeepsTheAreaOrder();
    void theServerRegionTableIsReadDefensively();
    void theServerTableDecidesSuggestions();
    void emptyGroupsAreLeftOutAndUnknownCodesGoUnderMore();
    void aRegionNameFallsBackToTheCountryName();
    void searchMatchesNamesIgnoringCase();
    void filteringDropsGroupsWithoutAMatch();
    void searchKnowsOtherNamesAndIgnoresAccents();
    void theDetectionTimeReadsAsAPhrase();
    void chooseOpensTheEditorOnlyWithoutOwnList();
};

void ContentLanguagesTest::serverDataIsNormalized()
{
    QCOMPARE(catalog::normalizeLanguageSetting(QJsonValue()), setting(LanguageMode::Auto, {}));
    QCOMPARE(catalog::normalizeLanguageSetting(jsonValue("{'mode':'weird','languages':['HI','hi','x',3,'bn']}")),
             setting(LanguageMode::Auto, codes({"hi", "bn"})));
    QCOMPARE(catalog::normalizeLanguageSetting(
                 jsonValue("{'mode':'custom','languages':['hi','bn','pa','ta','te','ml','kn','mr','gu','or','en','ko'],"
                           "'inferred':['en'],'inferredAt':'2026-11-01T00:00:00Z'}")),
             setting(LanguageMode::Custom, kTen, codes({"en"}), QStringLiteral("2026-11-01T00:00:00Z")));
    const QJsonObject body = catalog::languageSettingJson(setting(LanguageMode::Off, codes({"hi"}), codes({"en"}), "t"));
    QCOMPARE(QJsonDocument(body).toJson(QJsonDocument::Compact), QByteArray("{\"languages\":[\"hi\"],\"mode\":\"off\"}"));
}

void ContentLanguagesTest::eachModeSendsTheWholeObject()
{
    QCOMPARE(catalog::autoPayload(), setting(LanguageMode::Auto, {}));
    QCOMPARE(catalog::offPayload(setting(LanguageMode::Custom, codes({"hi", "bn"}), codes({"en"}))),
             setting(LanguageMode::Off, codes({"hi", "bn"})));
    QCOMPARE(catalog::customPayload(codes({"ta"})), setting(LanguageMode::Custom, codes({"ta"})));
}

void ContentLanguagesTest::effectiveAndDetectedFallBackToInferred()
{
    QCOMPARE(catalog::effectiveLanguages(setting(LanguageMode::Auto, {}, codes({"hi", "en"}))), codes({"hi", "en"}));
    QCOMPARE(catalog::effectiveLanguages(setting(LanguageMode::Custom, codes({"bn"}), codes({"hi"}))), codes({"bn"}));
    QCOMPARE(catalog::detectedLanguages(setting(LanguageMode::Auto, codes({"hi"}))), codes({"hi"}));
    QVERIFY(catalog::detectedLanguages(setting(LanguageMode::Custom, codes({"bn"}))).isEmpty());
}

void ContentLanguagesTest::aCustomListKeepsOneToTen()
{
    QCOMPARE(catalog::toggleLanguage(codes({"hi"}), QStringLiteral("hi")).refusal, catalog::ToggleRefusal::Min);
    QCOMPARE(catalog::toggleLanguage(codes({"hi", "bn"}), QStringLiteral("hi")).languages, codes({"bn"}));
    QCOMPARE(catalog::toggleLanguage(kTen, QStringLiteral("en")).refusal, catalog::ToggleRefusal::Max);
    QCOMPARE(catalog::toggleLanguage(kTen, QStringLiteral("hi")).languages, kTen.mid(1));
    QCOMPARE(catalog::toggleLanguage({}, QStringLiteral("en")).languages, codes({"en"}));
    QCOMPARE(kTen.size(), account::MaxContentLanguages);
}

void ContentLanguagesTest::detectionRunsByItselfOnlyOnAutomatic()
{
    QVERIFY(catalog::needsAutoDetect(std::nullopt));
    QVERIFY(catalog::needsAutoDetect(setting(LanguageMode::Auto, {})));
    QVERIFY(!catalog::needsAutoDetect(setting(LanguageMode::Auto, codes({"hi"}), codes({"hi"}), "2026-11-01T00:00:00Z")));
    QVERIFY(!catalog::needsAutoDetect(setting(LanguageMode::Custom, codes({"hi"}))));
    QVERIFY(!catalog::needsAutoDetect(setting(LanguageMode::Off, {})));
}

void ContentLanguagesTest::detectErrorsReadAsShortMessages()
{
    QCOMPARE(catalog::inferErrorMessage(429), QString::fromUtf8(catalog::language_text::DetectRateLimited));
    QCOMPARE(catalog::inferErrorMessage(503), QString::fromUtf8(catalog::language_text::DetectUnavailable));
    QCOMPARE(catalog::inferErrorMessage(0), QString::fromUtf8(catalog::language_text::DetectUnavailable));
}

void ContentLanguagesTest::automaticAndOffSaveAtOnce()
{
    Harness h(setting(LanguageMode::Custom, codes({"hi", "bn"}), codes({"en"}), "t"));
    h.controller->selectMode(LanguageMode::Off);
    QCOMPARE(h.saves.at(0).payload, setting(LanguageMode::Off, codes({"hi", "bn"})));
    QCOMPARE(h.stored->mode, LanguageMode::Off);
    Harness::answer(h.saves.at(0).reply, setting(LanguageMode::Off, codes({"hi", "bn"}), codes({"en"}), "t"));
    h.controller->selectMode(LanguageMode::Auto);
    QCOMPARE(h.saves.at(1).payload, setting(LanguageMode::Auto, {}));
    // Automatic shows the detected list until the service answers.
    QCOMPARE(*h.stored, setting(LanguageMode::Auto, codes({"en"}), codes({"en"}), "t"));
    Harness::answer(h.saves.at(1).reply, setting(LanguageMode::Auto, codes({"en"}), codes({"en"}), "t"));
    QCOMPARE(h.saves.size(), std::size_t{2});
}

void ContentLanguagesTest::choosePreselectsTheEffectiveList()
{
    Harness h(setting(LanguageMode::Auto, codes({"hi", "en"}), codes({"hi", "en"}), "t"));
    h.controller->selectMode(LanguageMode::Custom);
    QCOMPARE(h.saves.at(0).payload, setting(LanguageMode::Custom, codes({"hi", "en"})));
    QCOMPARE(h.controller->current().mode, LanguageMode::Custom);
}

void ContentLanguagesTest::chooseWithNothingDetectedStartsFromTheRegion()
{
    Harness h(setting(LanguageMode::Auto, {}, QStringList(), "t"), codes({"ro", "en"}));
    h.controller->selectMode(LanguageMode::Custom);
    QCOMPARE(h.saves.at(0).payload, setting(LanguageMode::Custom, codes({"ro", "en"})));
    Harness detected(setting(LanguageMode::Auto, codes({"hi"}), codes({"hi"}), "t"), codes({"ro", "en"}));
    detected.controller->selectMode(LanguageMode::Custom);
    QCOMPARE(detected.saves.at(0).payload, setting(LanguageMode::Custom, codes({"hi"})));
}

void ContentLanguagesTest::chooseWithNothingToPreselectStaysUnsaved()
{
    Harness h(std::nullopt);
    h.controller->selectMode(LanguageMode::Custom);
    QVERIFY(h.saves.empty());
    QCOMPARE(h.controller->current(), setting(LanguageMode::Custom, {}));
    QVERIFY(!h.stored);
    h.controller->toggle(QStringLiteral("bn"));
    QVERIFY(h.saves.empty()); // chip saves wait
    h.flushTimers();
    QCOMPARE(h.saves.at(0).payload, setting(LanguageMode::Custom, codes({"bn"})));
    Harness::answer(h.saves.at(0).reply, setting(LanguageMode::Custom, codes({"bn"})));
    QCOMPARE(*h.stored, setting(LanguageMode::Custom, codes({"bn"})));
}

void ContentLanguagesTest::leavingAnUnsavedChooseSavesNothing()
{
    Harness h(std::nullopt);
    h.controller->selectMode(LanguageMode::Custom);
    h.controller->selectMode(LanguageMode::Auto);
    QVERIFY(h.saves.empty());
    QCOMPARE(h.controller->current().mode, LanguageMode::Auto);
}

void ContentLanguagesTest::rapidChipTapsSendOneSave()
{
    Harness h(setting(LanguageMode::Custom, codes({"hi"})));
    h.controller->toggle(QStringLiteral("bn"));
    h.controller->toggle(QStringLiteral("en"));
    h.controller->toggle(QStringLiteral("hi"));
    QVERIFY(h.saves.empty());
    QCOMPARE(h.stored->languages, codes({"bn", "en"})); // shown at once
    h.clock.advance(catalog::LanguageSaveDelayMs - 1);
    QVERIFY(h.saves.empty());
    h.clock.advance(1);
    QCOMPARE(h.saves.size(), std::size_t{1});
    QCOMPARE(h.saves.at(0).payload, setting(LanguageMode::Custom, codes({"bn", "en"})));
}

void ContentLanguagesTest::refusedTogglesChangeNothing()
{
    Harness h(setting(LanguageMode::Custom, codes({"hi"})));
    h.controller->toggle(QStringLiteral("hi"));
    QCOMPARE(*h.stored, setting(LanguageMode::Custom, codes({"hi"})));
    Harness full(setting(LanguageMode::Custom, kTen));
    full.controller->toggle(QStringLiteral("en"));
    QCOMPARE(full.stored->languages, kTen);
    h.flushTimers();
    full.flushTimers();
    QVERIFY(h.saves.empty() && full.saves.empty());
}

void ContentLanguagesTest::anOlderReplyNeverOverwrites()
{
    Harness h(setting(LanguageMode::Custom, codes({"hi"})));
    h.controller->toggle(QStringLiteral("bn"));
    h.flushTimers();
    h.controller->toggle(QStringLiteral("en"));
    h.flushTimers();
    QCOMPARE(h.saves.size(), std::size_t{2});
    Harness::answer(h.saves.at(0).reply, setting(LanguageMode::Custom, codes({"hi", "bn"})));
    QCOMPARE(h.stored->languages, codes({"hi", "bn", "en"}));
    Harness::answer(h.saves.at(1).reply, setting(LanguageMode::Custom, codes({"hi", "bn", "en"})));
    QCOMPARE(h.stored->languages, codes({"hi", "bn", "en"}));
}

void ContentLanguagesTest::aReplyDuringAWaitingTapDoesNotUndoIt()
{
    Harness h(setting(LanguageMode::Custom, codes({"hi"})));
    h.controller->toggle(QStringLiteral("bn"));
    h.flushTimers();
    h.controller->toggle(QStringLiteral("en"));
    Harness::answer(h.saves.at(0).reply, setting(LanguageMode::Custom, codes({"hi", "bn"})));
    QCOMPARE(h.stored->languages, codes({"hi", "bn", "en"}));
}

void ContentLanguagesTest::theNewestFailedSaveReverts()
{
    Harness h(setting(LanguageMode::Custom, codes({"hi"})));
    h.controller->toggle(QStringLiteral("bn"));
    h.flushTimers();
    Harness::answer(h.saves.at(0).reply, setting(LanguageMode::Custom, codes({"hi", "bn"})));
    h.controller->selectMode(LanguageMode::Off);
    Harness::answer(h.saves.at(1).reply, failure(500));
    QCOMPARE(*h.stored, setting(LanguageMode::Custom, codes({"hi", "bn"})));
    QCOMPARE(h.failures.size(), 1);
}

void ContentLanguagesTest::anOlderFailureIsIgnored()
{
    Harness h(setting(LanguageMode::Custom, codes({"hi"})));
    h.controller->toggle(QStringLiteral("bn"));
    h.flushTimers();
    h.controller->toggle(QStringLiteral("en"));
    h.flushTimers();
    Harness::answer(h.saves.at(0).reply, failure(500));
    QVERIFY(h.failures.isEmpty());
    QCOMPARE(h.stored->languages, codes({"hi", "bn", "en"}));
}

void ContentLanguagesTest::aFailedFirstSaveRestoresAbsence()
{
    Harness h(std::nullopt);
    h.controller->selectMode(LanguageMode::Off);
    Harness::answer(h.saves.at(0).reply, failure(500));
    QVERIFY(!h.stored);
    QCOMPARE(h.failures.size(), 1);
}

void ContentLanguagesTest::autoDetectRunsOncePerSession()
{
    Harness h(std::nullopt);
    QVERIFY(h.controller->autoDetectIfNeeded());
    QVERIFY(h.controller->detecting());
    QVERIFY(!h.controller->autoDetectIfNeeded());
    Harness::answer(h.infers.at(0), setting(LanguageMode::Auto, codes({"hi", "bn"}), codes({"hi", "bn"}), "t"));
    QVERIFY(!h.controller->detecting());
    QCOMPARE(*h.stored, setting(LanguageMode::Auto, codes({"hi", "bn"}), codes({"hi", "bn"}), "t"));
    QVERIFY(!h.controller->autoDetectIfNeeded());
    QCOMPARE(h.infers.size(), std::size_t{1});
}

void ContentLanguagesTest::autoDetectIsNotRetriedAfterAFailure()
{
    Harness h(setting(LanguageMode::Auto, {}));
    (void)h.controller->autoDetectIfNeeded();
    Harness::answer(h.infers.at(0), failure(503));
    QCOMPARE(h.controller->detectError(), QString::fromUtf8(catalog::language_text::DetectUnavailable));
    QVERIFY(!h.controller->autoDetectIfNeeded());
    QCOMPARE(h.infers.size(), std::size_t{1});
    // Detect again still works, and clears the error while it runs.
    h.controller->detect();
    QVERIFY(h.controller->detectError().isEmpty());
    Harness::answer(h.infers.at(1), failure(429));
    QCOMPARE(h.controller->detectError(), QString::fromUtf8(catalog::language_text::DetectRateLimited));
}

void ContentLanguagesTest::autoDetectDoesNotRunOtherwise()
{
    for (const LanguageSetting& value : {setting(LanguageMode::Custom, codes({"hi"})), setting(LanguageMode::Off, {}),
                                         setting(LanguageMode::Auto, {}, QStringList(), "t")}) {
        Harness h(value);
        QVERIFY(!h.controller->autoDetectIfNeeded());
        QVERIFY(h.infers.empty());
    }
}

void ContentLanguagesTest::aDetectionDuringASaveKeepsTheChoice()
{
    Harness h(setting(LanguageMode::Auto, {}));
    h.controller->detect();
    h.controller->selectMode(LanguageMode::Off);
    Harness::answer(h.infers.at(0), setting(LanguageMode::Auto, codes({"hi"}), codes({"hi"}), "t"));
    QCOMPARE(*h.stored, setting(LanguageMode::Off, {}, codes({"hi"}), "t"));
}

void ContentLanguagesTest::resetDropsLateReplies()
{
    Harness h(setting(LanguageMode::Custom, codes({"hi"})));
    h.controller->toggle(QStringLiteral("bn"));
    h.controller->reset();
    h.flushTimers();
    QVERIFY(h.saves.empty()); // the waiting save was cancelled
    h.controller->detect();
    h.controller->reset();
    Harness::answer(h.infers.at(0), setting(LanguageMode::Auto, codes({"ko"}), codes({"ko"}), "t"));
    QCOMPARE(*h.stored, setting(LanguageMode::Custom, codes({"hi", "bn"})));
    QVERIFY(!h.controller->detecting());
}

void ContentLanguagesTest::withoutARegionTheCatalogSplitsByArea()
{
    const QList<catalog::LanguageGroup> groups = catalog::groupLanguageCatalog(kCatalog, std::nullopt, std::nullopt);
    QCOMPARE(idsOf(groups), codes({"europe", "asia", "middle-east-africa"}));
    QCOMPARE(groups.at(0).label, QStringLiteral("Europe & the Americas"));
    QCOMPARE(codesOf(groups), (QList<QStringList>{codes({"en", "es", "ro"}), codes({"ko", "hi", "bn", "bho"}), codes({"ar"})}));
}

void ContentLanguagesTest::aRegionLeadsWithItsOwnLanguages()
{
    const QList<catalog::LanguageGroup> romania =
        catalog::groupLanguageCatalog(kCatalog, catalog::LanguageRegion{QStringLiteral("ro"), QStringLiteral("Romania")},
                                      std::nullopt);
    QCOMPARE(romania.at(0).label, QStringLiteral("Suggested for Romania"));
    QCOMPARE(codesOf(romania),
             (QList<QStringList>{codes({"ro", "en"}), codes({"es"}), codes({"ko", "hi", "bn", "bho"}), codes({"ar"})}));
    const QList<catalog::LanguageGroup> india =
        catalog::groupLanguageCatalog(kCatalog, catalog::LanguageRegion{QStringLiteral("IN"), QStringLiteral("India")},
                                      std::nullopt);
    QCOMPARE(idsOf(india), codes({"suggested", "indian", "europe", "asia", "middle-east-africa"}));
    QCOMPARE(codesOf(india), (QList<QStringList>{codes({"hi", "bn", "en"}), codes({"bho"}), codes({"es", "ro"}),
                                                 codes({"ko"}), codes({"ar"})}));
    const QList<catalog::LanguageGroup> us = catalog::groupLanguageCatalog(
        kCatalog, catalog::LanguageRegion{QStringLiteral("us"), QStringLiteral("United States")}, std::nullopt);
    QCOMPARE(codesOf(us).mid(0, 2), (QList<QStringList>{codes({"en", "es"}), codes({"ro"})}));
    const QList<catalog::LanguageGroup> korea = catalog::groupLanguageCatalog(
        kCatalog, catalog::LanguageRegion{QStringLiteral("kr"), QStringLiteral("Korea")}, std::nullopt);
    QCOMPARE(idsOf(korea), codes({"suggested", "asia", "europe", "middle-east-africa"}));
    QCOMPARE(codesOf(korea).at(1), codes({"hi", "bn", "bho"}));
}

void ContentLanguagesTest::aRegionWithoutSuggestionsKeepsTheAreaOrder()
{
    QCOMPARE(codesOf(catalog::groupLanguageCatalog(
                 kCatalog, catalog::LanguageRegion{QStringLiteral("zw"), QStringLiteral("Zimbabwe")}, std::nullopt)),
             codesOf(catalog::groupLanguageCatalog(kCatalog, std::nullopt, std::nullopt)));
    QVERIFY(account::suggestedLanguages(QStringLiteral("zw"), std::nullopt).isEmpty());
    QCOMPARE(account::suggestedLanguages(QStringLiteral(" RO "), std::nullopt), codes({"ro", "en"}));
    QCOMPARE(account::suggestedLanguages(QStringLiteral("us"), std::nullopt), codes({"en", "es"}));
}

void ContentLanguagesTest::theServerRegionTableIsReadDefensively()
{
    QVERIFY(!account::normalizeRegionLanguages(QJsonValue(), kCatalog));
    QVERIFY(!account::normalizeRegionLanguages(QJsonValue(QStringLiteral("in")), kCatalog));
    QVERIFY(!account::normalizeRegionLanguages(QJsonDocument::fromJson("[\"in\"]").array(), kCatalog));
    const std::optional<account::RegionLanguageTable> regions =
        table("{'in':['bn','hi',7,null,'hi','xx',' EN '],'US':['es','en'],'usa':['en'],'1n':['en'],'ro':'ro','kr':[]}",
              kCatalog);
    QVERIFY(regions);
    QCOMPARE(regions->size(), 4);
    QCOMPARE(regions->value(QStringLiteral("in")), codes({"bn", "hi", "en"}));
    QCOMPARE(regions->value(QStringLiteral("us")), codes({"es", "en"}));
    QVERIFY(regions->contains(QStringLiteral("ro")) && regions->value(QStringLiteral("ro")).isEmpty());
    QList<LanguageInfo> eleven;
    for (const QString& code : kTen + codes({"en"})) {
        eleven.append({code, code, {}});
    }
    const std::optional<account::RegionLanguageTable> tooMany =
        table("{'in':['hi','bn','pa','ta','te','ml','kn','mr','gu','or','en']}", eleven);
    QCOMPARE(tooMany->value(QStringLiteral("in")), kTen); // cut, not extended
}

void ContentLanguagesTest::theServerTableDecidesSuggestions()
{
    const std::optional<account::RegionLanguageTable> regions = table("{'in':['bn','en'],'ro':[]}", kCatalog);
    QCOMPARE(account::suggestedLanguages(QStringLiteral(" IN "), regions), codes({"bn", "en"}));
    QVERIFY(account::suggestedLanguages(QStringLiteral("ro"), regions).isEmpty());
    QVERIFY(account::suggestedLanguages(QStringLiteral("us"), regions).isEmpty());
    const QList<catalog::LanguageGroup> india = catalog::groupLanguageCatalog(
        kCatalog, catalog::LanguageRegion{QStringLiteral("in"), QStringLiteral("India")}, regions);
    QCOMPARE(india.at(0).label, QStringLiteral("Suggested for India"));
    QCOMPARE(codesOf(india).at(0), codes({"bn", "en"}));
    QCOMPARE(idsOf(india), codes({"suggested", "indian", "europe", "asia", "middle-east-africa"}));
    QCOMPARE(codesOf(catalog::groupLanguageCatalog(
                 kCatalog, catalog::LanguageRegion{QStringLiteral("ro"), QStringLiteral("Romania")}, regions)),
             codesOf(catalog::groupLanguageCatalog(kCatalog, std::nullopt, std::nullopt)));
}

void ContentLanguagesTest::emptyGroupsAreLeftOutAndUnknownCodesGoUnderMore()
{
    QCOMPARE(idsOf(catalog::groupLanguageCatalog({kCatalog.at(1)}, std::nullopt, std::nullopt)), codes({"europe"}));
    QVERIFY(catalog::groupLanguageCatalog({}, std::nullopt, std::nullopt).isEmpty());
    QList<LanguageInfo> withUnknown = kCatalog;
    withUnknown.append({QStringLiteral("xx"), QStringLiteral("Newish"), {}});
    const QList<catalog::LanguageGroup> groups = catalog::groupLanguageCatalog(withUnknown, std::nullopt, std::nullopt);
    QCOMPARE(groups.last().id, QStringLiteral("other"));
    QCOMPARE(groups.last().label, QStringLiteral("More"));
    QCOMPARE(codesOf(groups).last(), codes({"xx"}));
    QCOMPARE(catalog::languageArea(QStringLiteral("fil")), catalog::LanguageArea::Asia);
}

void ContentLanguagesTest::aRegionNameFallsBackToTheCountryName()
{
    QCOMPARE(catalog::regionDisplayName(QStringLiteral("ro")), QStringLiteral("Romania"));
    QCOMPARE(catalog::regionDisplayName(QStringLiteral("zz")), QStringLiteral("ZZ"));
}

void ContentLanguagesTest::searchMatchesNamesIgnoringCase()
{
    const LanguageInfo& bengali = kCatalog.at(2);
    QVERIFY(catalog::languageMatchesQuery(bengali, QStringLiteral("ben")));
    QVERIFY(catalog::languageMatchesQuery(bengali, QStringLiteral("  BEN ")));
    QVERIFY(catalog::languageMatchesQuery(bengali, QString::fromUtf8("\xe0\xa6\xac\xe0\xa6\xbe\xe0\xa6\x82")));
    QVERIFY(!catalog::languageMatchesQuery(bengali, QStringLiteral("hin")));
    QVERIFY(catalog::languageMatchesQuery(bengali, QString()));
    QVERIFY(catalog::languageMatchesQuery(kCatalog.at(5), QString::fromUtf8("espa\xc3\xb1")));
    QVERIFY(catalog::languageMatchesQuery(kCatalog.at(5), QString::fromUtf8("ESPA\xc3\x91")));
    QVERIFY(!catalog::languageMatchesQuery(kCatalog.at(0), QStringLiteral("bn"))); // codes are not searched
}

void ContentLanguagesTest::filteringDropsGroupsWithoutAMatch()
{
    const QList<catalog::LanguageGroup> groups = catalog::groupLanguageCatalog(kCatalog, std::nullopt, std::nullopt);
    QCOMPARE(codesOf(catalog::filterLanguageGroups(groups, QStringLiteral("ben"))), QList<QStringList>{codes({"bn"})});
    QCOMPARE(idsOf(catalog::filterLanguageGroups(groups, QStringLiteral("kor"))), codes({"asia"}));
    QCOMPARE(codesOf(catalog::filterLanguageGroups(groups, QStringLiteral("i"))),
             (QList<QStringList>{codes({"en", "es", "ro"}), codes({"hi", "bn", "bho"}), codes({"ar"})}));
    QVERIFY(catalog::filterLanguageGroups(groups, QStringLiteral("zzz")).isEmpty());
    QCOMPARE(codesOf(catalog::filterLanguageGroups(groups, QString())), codesOf(groups));
}

void ContentLanguagesTest::searchKnowsOtherNamesAndIgnoresAccents()
{
    QVERIFY(catalog::languageMatchesQuery({QStringLiteral("fa"), QStringLiteral("Persian"), {}}, QStringLiteral("farsi")));
    QVERIFY(catalog::languageMatchesQuery({QStringLiteral("fil"), QStringLiteral("Filipino"), QStringLiteral("Filipino")},
                                          QStringLiteral("Taga")));
    QVERIFY(catalog::languageMatchesQuery({QStringLiteral("bn"), QStringLiteral("Bengali"), {}}, QStringLiteral("bangla")));
    QVERIFY(catalog::languageMatchesQuery({QStringLiteral("zh"), QStringLiteral("Chinese"), {}}, QStringLiteral("mandarin")));
    QVERIFY(catalog::languageMatchesQuery({QStringLiteral("es"), QStringLiteral("Spanish"), QString::fromUtf8("Espa\xc3\xb1ol")},
                                          QStringLiteral("espanol")));
    QVERIFY(catalog::languageMatchesQuery({QStringLiteral("cs"), QStringLiteral("Czech"), QString::fromUtf8("\xc4\x8c" "e\xc5\xa1tina")},
                                          QStringLiteral("cestina")));
    QVERIFY(!catalog::languageMatchesQuery({QStringLiteral("ro"), QStringLiteral("Romanian"), {}}, QStringLiteral("farsi")));
}

void ContentLanguagesTest::theDetectionTimeReadsAsAPhrase()
{
    const std::int64_t now = QDateTime::fromString(QStringLiteral("2026-11-07T12:00:00Z"), Qt::ISODate).toMSecsSinceEpoch();
    QCOMPARE(catalog::detectedAgo(QStringLiteral("2026-11-07T01:00:00Z"), now), QStringLiteral("today"));
    QCOMPARE(catalog::detectedAgo(QStringLiteral("2026-11-06T11:00:00Z"), now), QStringLiteral("yesterday"));
    QCOMPARE(catalog::detectedAgo(QStringLiteral("2026-11-04T12:00:00Z"), now), QStringLiteral("3 days ago"));
    QCOMPARE(catalog::detectedAgo(QStringLiteral("2026-10-17T12:00:00Z"), now), QStringLiteral("3 weeks ago"));
    QCOMPARE(catalog::detectedAgo(QStringLiteral("2026-11-08T12:00:00Z"), now), QStringLiteral("today"));
    QVERIFY(catalog::detectedAgo(QString(), now).isEmpty());
    QVERIFY(catalog::detectedAgo(QStringLiteral("soon"), now).isEmpty());
}

void ContentLanguagesTest::chooseOpensTheEditorOnlyWithoutOwnList()
{
    QVERIFY(catalog::opensEditorOnChoose(setting(LanguageMode::Auto, codes({"hi"}))));
    QVERIFY(catalog::opensEditorOnChoose(setting(LanguageMode::Auto, {})));
    QVERIFY(catalog::opensEditorOnChoose(setting(LanguageMode::Off, {})));
    QVERIFY(!catalog::opensEditorOnChoose(setting(LanguageMode::Off, codes({"hi", "bn"}))));
    QVERIFY(!catalog::opensEditorOnChoose(setting(LanguageMode::Custom, {})));
}

QTEST_GUILESS_MAIN(ContentLanguagesTest)
#include "tst_content_languages.moc"
