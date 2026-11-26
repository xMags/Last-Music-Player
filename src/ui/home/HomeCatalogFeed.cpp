#include "ui/home/HomeCatalogFeed.h"
#include "core/app/PerfLog.h"

#include "core/catalog/CatalogPresentation.h"
#include "core/catalog/CatalogService.h"

#include <QSet>

#include <algorithm>

namespace lmp::ui {

namespace {

namespace catalog = core::catalog;

const QString kCatalogPrefix = QStringLiteral("catalog:");

} // namespace

std::chrono::milliseconds nextCatalogRetryDelay(std::chrono::milliseconds current)
{
    if (current.count() <= 0) {
        return CatalogRetryMin;
    }
    return std::min<std::chrono::milliseconds>(current * 2, CatalogRetryMax);
}

HomeCatalogFeed::HomeCatalogFeed(catalog::CatalogService& catalog, QObject* parent)
    : QObject(parent)
    , m_catalog(catalog)
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this]() { load(true); });
    connect(&m_catalog, &catalog::CatalogService::cleared, this, [this]() {
        // Another account or region: what is shown belongs to the old one.
        const bool wasShown = !m_paused;
        reset();
        if (wasShown) {
            load();
        }
    });
    connect(&m_catalog, &catalog::CatalogService::refreshed, this, [this](const QString& key) {
        if (key == catalog::discoveryKey(m_catalog.region()) && !m_paused) {
            load();
        }
    });
}

void HomeCatalogFeed::reset()
{
    ++m_request;
    m_retry.stop();
    m_retryDelay = std::chrono::milliseconds(0);
    m_discovery.reset();
    m_cached = false;
    m_loading = false;
    m_error.clear();
    for (CatalogCardModel* model : std::as_const(m_shelves)) {
        model->setItems({});
    }
    for (ChartCardModel* model : std::as_const(m_charts)) {
        model->setCharts({});
    }
    m_titles.clear();
    emit changed();
}

void HomeCatalogFeed::pause()
{
    m_paused = true;
}

void HomeCatalogFeed::load(bool force)
{
    m_paused = false;
    if (m_loading || (!force && m_retry.isActive())) {
        return;
    }
    if (!m_discovery) {
        // The copy kept from the last launch shows while the live one loads.
        if (std::optional<catalog::Discovery> stored = m_catalog.storedDiscovery()) {
            apply(std::move(*stored), true);
        }
    }
    const std::uint64_t request = ++m_request;
    m_loading = true;
    emit changed();
    m_catalog.discovery().then(this, [this, request](const core::Result<catalog::Discovery>& result) {
        if (request != m_request) {
            return;
        }
        m_loading = false;
        if (!result) {
            if (result.failure().kind == core::FailureKind::Cancelled) {
                emit changed();
                return;
            }
            fail(tr("Music discovery is unavailable right now. Try again shortly."));
            return;
        }
        m_retryDelay = std::chrono::milliseconds(0);
        m_error.clear();
        apply(result.value(), false);
    });
}

void HomeCatalogFeed::fail(const QString& message)
{
    m_retryDelay = nextCatalogRetryDelay(m_retryDelay);
    // An older copy stays on screen quietly; with nothing shown the error does.
    if (!m_discovery) {
        m_error = message;
    }
    if (!m_paused) {
        m_retry.start(m_retryDelay);
    }
    emit changed();
}

QString HomeCatalogFeed::regionName() const
{
    return m_discovery ? m_discovery->regionName : m_catalog.region().toUpper();
}

void HomeCatalogFeed::apply(catalog::Discovery discovery, bool cached)
{
    core::PerfScope perf("home feed applied");
    m_cached = cached;
    m_titles.clear();
    const auto set = [this](const QString& key, const std::optional<catalog::Shelf>& shelf, const QString& fallback,
                            int limit) {
        QList<catalog::CatalogItem> items = shelf ? shelf->items : QList<catalog::CatalogItem>();
        if (limit > 0) {
            items = items.mid(0, limit);
        }
        shelfModel(key)->setItems(std::move(items));
        m_titles.insert(key, shelf && !shelf->title.isEmpty() ? shelf->title : fallback);
    };
    set(QStringLiteral("newSongs"), discovery.newReleaseSongsShelf, tr("New Songs"), 0);
    set(QStringLiteral("newAlbums"), discovery.newReleasesShelf, tr("New Albums"), 0);
    set(QStringLiteral("biggestHits"), discovery.hitsShelf, tr("Biggest hits"), 0);
    set(QStringLiteral("popularArtists"), discovery.popularArtistsShelf, tr("Popular artists"), PopularArtistsLimit);
    set(QStringLiteral("moods"), discovery.moodShelf, tr("Moods & Activities"), MoodLimit);
    // The discovery's own shelves; the Top Songs one names the region.
    QSet<QString> current;
    for (const catalog::Shelf& shelf : discovery.shelves) {
        const QString key = kCatalogPrefix + shelf.id;
        current.insert(key);
        shelfModel(key)->setItems(shelf.items);
        m_titles.insert(key, shelf.id == QLatin1StringView("top-songs")
                                 ? tr("%1 in %2").arg(shelf.title, discovery.regionName)
                                 : shelf.title);
    }
    for (auto it = m_shelves.cbegin(); it != m_shelves.cend(); ++it) {
        if (it.key().startsWith(kCatalogPrefix) && !current.contains(it.key())) {
            it.value()->setItems({});
        }
    }
    QList<catalog::ChartDescriptor> explore;
    QList<catalog::ChartDescriptor> cities;
    for (const catalog::ChartSection& section : catalog::buildChartSections(discovery)) {
        const QList<catalog::ChartDescriptor> preview =
            section.charts.mid(0, catalog::chartPreviewCount(section.charts.size()));
        if (section.id == QLatin1StringView("explore")) {
            explore = preview;
            m_titles.insert(QStringLiteral("genreCharts"), section.title);
        } else {
            cities = preview;
            m_titles.insert(QStringLiteral("cityCharts"), section.title);
        }
    }
    charts(QStringLiteral("genreCharts"))->setCharts(explore);
    charts(QStringLiteral("cityCharts"))->setCharts(cities);
    m_discovery = std::move(discovery);
    emit changed();
    if (perf.enabled()) {
        int cards = 0;
        for (const CatalogCardModel* shelf : std::as_const(m_shelves)) {
            cards += shelf->count();
        }
        perf.done(QStringLiteral("%1 shelves, %2 cards, %3 charts, %4")
                      .arg(m_shelves.size())
                      .arg(cards)
                      .arg(explore.size() + cities.size())
                      .arg(cached ? QStringLiteral("cached") : QStringLiteral("fresh")));
    }
}

QStringList HomeCatalogFeed::discoveryShelfKeys() const
{
    QStringList keys;
    if (m_discovery) {
        for (const catalog::Shelf& shelf : m_discovery->shelves) {
            keys.append(kCatalogPrefix + shelf.id);
        }
    }
    return keys;
}

QString HomeCatalogFeed::shelfTitle(const QString& key) const
{
    return m_titles.value(key);
}

bool HomeCatalogFeed::shelfHasChart(const QString& key) const
{
    const std::optional<catalog::Shelf> shelf = discoveryShelf(key);
    return shelf && shelf->chart.has_value();
}

std::optional<catalog::Shelf> HomeCatalogFeed::discoveryShelf(const QString& key) const
{
    if (!m_discovery || !key.startsWith(kCatalogPrefix)) {
        return std::nullopt;
    }
    const QString id = key.mid(kCatalogPrefix.size());
    const auto found = std::find_if(m_discovery->shelves.cbegin(), m_discovery->shelves.cend(),
                                    [&id](const catalog::Shelf& shelf) { return shelf.id == id; });
    return found != m_discovery->shelves.cend() ? std::optional(*found) : std::nullopt;
}

CatalogCardModel* HomeCatalogFeed::shelf(const QString& key)
{
    return shelfModel(key);
}

CatalogCardModel* HomeCatalogFeed::shelfModel(const QString& key)
{
    CatalogCardModel*& model = m_shelves[key];
    if (model == nullptr) {
        model = new CatalogCardModel(this);
    }
    return model;
}

ChartCardModel* HomeCatalogFeed::charts(const QString& key)
{
    ChartCardModel*& model = m_charts[key];
    if (model == nullptr) {
        model = new ChartCardModel(this);
    }
    return model;
}

QString HomeCatalogFeed::chartsTitle(const QString& key) const
{
    return m_titles.value(key);
}

} // namespace lmp::ui
