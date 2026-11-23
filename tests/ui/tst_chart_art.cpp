#include <QDir>
#include <QImage>
#include <QObject>
#include <QString>
#include <QSvgRenderer>
#include <QTest>

#include "core/catalog/CatalogPresentation.h"
#include "core/catalog/ChartArt.h"
#include "ui/images/ChartArtImageProvider.h"

namespace catalog = lmp::core::catalog;
using catalog::ChartDescriptor;
using catalog::ChartKind;
using catalog::ResourceType;

namespace {

ChartDescriptor chart(ChartKind kind, const QString& id, const QString& name, const QString& region = QStringLiteral("in"))
{
    ChartDescriptor descriptor;
    descriptor.ref = {region, kind, id, ResourceType::Song};
    descriptor.name = name;
    descriptor.title = QStringLiteral("Top Songs in ") + name;
    return descriptor;
}

std::optional<catalog::GenreArt> art(const char* name)
{
    return catalog::resolveGenreArt(QString::fromUtf8(name));
}

catalog::GenreArt expected(const char* motif, const char* tone)
{
    return {QString::fromLatin1(motif), QString::fromLatin1(tone)};
}

} // namespace

// The reference client's chart card cases, and the drawn covers themselves.
class ChartArtTest : public QObject {
    Q_OBJECT

private slots:
    void cityChartsNeverShareASection();
    void eachGenreFamilyDrawsItsOwnCover();
    void identityIsTheNavigationIdentity();
    void cityAliasesResolveToLandmarks();
    void visualsStayDistinctAndDeterministic();
    void hashMatchesTheReference();
    void everyCoverDraws();
    void paletteFillsTheTemplate();
};

void ChartArtTest::cityChartsNeverShareASection()
{
    catalog::Discovery discovery;
    discovery.charts = {chart(ChartKind::Global, QStringLiteral("top-songs"), QStringLiteral("India")),
                        chart(ChartKind::Genre, QStringLiteral("14"), QStringLiteral("Pop")),
                        chart(ChartKind::City, QStringLiteral("city-mumbai"), QStringLiteral("Mumbai"))};
    discovery.charts[0].ref.resourceType = ResourceType::Song;
    catalog::CityChartGroups groups;
    groups.indian.charts = {chart(ChartKind::City, QStringLiteral("city-mumbai"), QStringLiteral("Mumbai"))};
    groups.international.charts = {chart(ChartKind::City, QStringLiteral("city-london"), QStringLiteral("London"), "gb")};
    groups.international.partial = true;
    discovery.cityChartGroups = groups;
    const QList<catalog::ChartSection> grouped = catalog::buildChartSections(discovery);
    QCOMPARE(grouped.size(), 2);
    QCOMPARE(grouped.at(0).title, QStringLiteral("Top charts"));
    QCOMPARE(grouped.at(0).charts.size(), 2);
    QCOMPARE(grouped.at(1).title, QStringLiteral("Top 25 by city"));
    QCOMPARE(grouped.at(1).charts.at(1).ref.id, QStringLiteral("city-london"));
    QVERIFY(grouped.at(1).partial);
    discovery.cityChartGroups.reset();
    const QList<catalog::ChartSection> flat = catalog::buildChartSections(discovery);
    QCOMPARE(flat.at(1).charts.size(), 1);
    QVERIFY(!flat.at(1).partial);
    catalog::Discovery onlyCity;
    onlyCity.charts = {chart(ChartKind::City, QStringLiteral("city-mumbai"), QStringLiteral("Mumbai"))};
    QCOMPARE(catalog::buildChartSections(onlyCity).size(), 1);
    QCOMPARE(catalog::buildChartSections(onlyCity).at(0).id, QStringLiteral("cities"));
}

void ChartArtTest::eachGenreFamilyDrawsItsOwnCover()
{
    QCOMPARE(art("Bollywood"), expected("marquee", "bollywood"));
    QCOMPARE(art("Devotional & Spiritual"), expected("lotus", "devotional"));
    QCOMPARE(art("Hip-Hop/Rap"), expected("cassette", "hiphop"));
    QCOMPARE(art("Christmas"), expected("bauble", "christmas"));
    QCOMPARE(art("Indian Classical"), expected("sitar", "raga"));
    QCOMPARE(art("Classical")->motif, QStringLiteral("piano"));
    QVERIFY(art("Indian Classical") != art("Classical"));
    QCOMPARE(art("Pop")->tone, QStringLiteral("pop"));
    QCOMPARE(art("Indian Pop")->tone, QStringLiteral("indian-pop"));
    QVERIFY(art("Indian Pop")->motif != art("Pop")->motif);
    QCOMPARE(art("R&B/Soul")->motif, QStringLiteral("ribbon"));
    QCOMPARE(art("Punjabi")->motif, QStringLiteral("dhol"));
    QCOMPARE(art("Hard Rock"), expected("amp", "alternative"));
    QCOMPARE(art("Alternative"), expected("guitar", "alternative"));
    QCOMPARE(art("Heavy Metal"), expected("pick", "alternative"));
    QCOMPARE(art("Opera"), expected("violin", "classical"));
    QCOMPARE(art("New Age"), expected("harp", "classical"));
    QCOMPARE(art("Holiday: Christmas Classics"), expected("bauble", "christmas"));
    QVERIFY(!art("Fjordcore"));
    const catalog::ChartVisual christmas =
        catalog::resolveChartVisual(chart(ChartKind::Genre, QStringLiteral("4"), QStringLiteral("Christmas")));
    QCOMPARE(christmas.tone, QStringLiteral("christmas"));
    const catalog::ChartVisual unknown =
        catalog::resolveChartVisual(chart(ChartKind::Genre, QStringLiteral("99"), QStringLiteral("Fjordcore")));
    QVERIFY(unknown.tone.isEmpty());
    QVERIFY(unknown.paletteIndex >= 0 && unknown.paletteIndex <= 5);
    QCOMPARE(unknown.visualKey, QStringLiteral("genre-%1").arg(unknown.patternIndex));
}

void ChartArtTest::identityIsTheNavigationIdentity()
{
    const ChartDescriptor mumbai = chart(ChartKind::City, QStringLiteral("city-mumbai"), QStringLiteral("Mumbai"));
    QCOMPARE(catalog::chartIdentity(mumbai.ref), QStringLiteral("in:city:song:city-mumbai"));
    QVERIFY(catalog::chartIdentity(mumbai.ref)
            != catalog::chartIdentity(chart(ChartKind::City, QStringLiteral("city-mumbai"), QStringLiteral("Mumbai"), "gb").ref));
}

void ChartArtTest::cityAliasesResolveToLandmarks()
{
    QCOMPARE(catalog::resolveCityLandmark(QStringLiteral("Mumbai")), QStringLiteral("mumbai"));
    QCOMPARE(catalog::resolveCityLandmark(QStringLiteral("Bombay")), QStringLiteral("mumbai"));
    QCOMPARE(catalog::resolveCityLandmark(QStringLiteral("New Delhi")), QStringLiteral("delhi"));
    QCOMPARE(catalog::resolveCityLandmark(QStringLiteral("Bangalore")), QStringLiteral("bengaluru"));
    QCOMPARE(catalog::resolveCityLandmark(QStringLiteral("New York City")), QStringLiteral("new-york"));
    QCOMPARE(catalog::resolveCityLandmark(QStringLiteral("LA")), QStringLiteral("los-angeles"));
    QCOMPARE(catalog::resolveCityLandmark(QString::fromUtf8("S\xc3\xa3o Paulo")), QStringLiteral("generic"));
}

void ChartArtTest::visualsStayDistinctAndDeterministic()
{
    const ChartDescriptor mumbai = chart(ChartKind::City, QStringLiteral("city-mumbai"), QStringLiteral("Mumbai"));
    const catalog::ChartVisual city = catalog::resolveChartVisual(mumbai);
    QCOMPARE(catalog::resolveChartVisual(mumbai), city);
    QCOMPARE(city.family, catalog::ChartFamily::City);
    QCOMPARE(city.visualKey, QStringLiteral("mumbai"));
    QCOMPARE(city.kindLabel, QStringLiteral("City chart"));
    QCOMPARE(catalog::chartCardSubtitle(mumbai, city), mumbai.title);
    QCOMPARE(catalog::resolveChartVisual(chart(ChartKind::City, QStringLiteral("city-x"), QStringLiteral("Somewhere"))).visualKey,
             QStringLiteral("generic"));
    ChartDescriptor region = chart(ChartKind::Global, QStringLiteral("top-songs"), QStringLiteral("India"));
    const catalog::ChartVisual global = catalog::resolveChartVisual(region);
    QCOMPARE(global.visualKey, QStringLiteral("region"));
    QCOMPARE(global.kindLabel, QStringLiteral("Top songs"));
    QVERIFY(catalog::chartCardSubtitle(region, global).isEmpty());
    const catalog::ChartVisual genre =
        catalog::resolveChartVisual(chart(ChartKind::Genre, QStringLiteral("14"), QStringLiteral("Pop")));
    QCOMPARE(genre.visualKey, QStringLiteral("genre-bubbles"));
    QVERIFY(global.family != city.family && genre.family != city.family);
}

void ChartArtTest::hashMatchesTheReference()
{
    // FNV-1a over UTF-16 units, as Math.imul and charCodeAt compute it.
    QCOMPARE(catalog::chartStableHash(QString()), 2166136261U);
    QCOMPARE(catalog::chartStableHash(QStringLiteral("a")), 0xe40c292cU);
    QCOMPARE(catalog::chartStableHash(QStringLiteral("foobar")), 0xbf9cf968U);
}

void ChartArtTest::everyCoverDraws()
{
    const catalog::ChartPalette palette{QStringLiteral("#087e9b"), QStringLiteral("#12345b"), QStringLiteral("#8ce8f3"),
                                        QStringLiteral("#ffd36e")};
    const QStringList keys = QDir(QStringLiteral(":/chartart")).entryList({QStringLiteral("*.svg")});
    QCOMPARE(keys.size(), 35);
    for (const QString& file : keys) {
        const QString key = file.chopped(4);
        const QByteArray svg = lmp::ui::ChartArtImageProvider::svgFor(key, palette);
        QVERIFY2(!svg.contains('{'), qPrintable(key));
        QSvgRenderer renderer(svg);
        QVERIFY2(renderer.isValid(), qPrintable(key));
        lmp::ui::ChartArtImageProvider provider;
        QSize size;
        const QImage image = provider.requestImage(key + QStringLiteral("/087e9b/12345b/8ce8f3/ffd36e"), &size, QSize(92, 92));
        QCOMPARE(size, QSize(92, 92));
        // The background is opaque: the corner takes the first colour's side.
        QCOMPARE(image.pixelColor(2, 2).alpha(), 255);
    }
}

void ChartArtTest::paletteFillsTheTemplate()
{
    lmp::ui::ChartArtImageProvider provider;
    QSize size;
    QVERIFY(provider.requestImage(QStringLiteral("../etc/087e9b/12345b/8ce8f3/ffd36e"), &size, {}).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("region/#87e9b/12345b/8ce8f3/ffd36e"), &size, {}).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("nothing/087e9b/12345b/8ce8f3/ffd36e"), &size, {}).isNull());
    const catalog::ChartVisual visual = catalog::resolveChartVisual(chart(ChartKind::Genre, QStringLiteral("4"), QStringLiteral("Christmas")));
    QCOMPARE(lmp::ui::ChartArtImageProvider::sourceFor(visual),
             QStringLiteral("image://chartart/genre-bauble/17734d/10303a/ffd9a0/ffe7a3"));
    const catalog::ChartVisual city =
        catalog::resolveChartVisual(chart(ChartKind::City, QStringLiteral("city-mumbai"), QStringLiteral("Mumbai")));
    QVERIFY(lmp::ui::ChartArtImageProvider::sourceFor(city).startsWith(QStringLiteral("image://chartart/city-mumbai/")));
}

QTEST_MAIN(ChartArtTest)
#include "tst_chart_art.moc"
