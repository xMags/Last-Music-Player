#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <optional>

#include "core/catalog/CatalogModels.h"
#include "ui/catalog/CatalogCardModel.h"

namespace lmp::core::catalog {
class CatalogService;
}

namespace lmp::ui {

// The first retry of a failed catalogue load, and the longest wait.
inline constexpr std::chrono::seconds CatalogRetryMin{30};
inline constexpr std::chrono::minutes CatalogRetryMax{5};

// The wait before the next retry after a failure: 30 s, doubling to 5 minutes.
[[nodiscard]] std::chrono::milliseconds nextCatalogRetryDelay(std::chrono::milliseconds current);

// Home's catalogue: the region's discovery page as shelves of cards and rows
// of chart cards. It shows the copy kept from the last launch at once, loads
// the live one, retries a failure by itself with back-off while Home is open,
// and reloads when the catalogue is cleared (account or region change) or
// refreshed in the background.
//
// Shelf keys: "newSongs", "newAlbums", "biggestHits", "popularArtists",
// "moods" and "catalog:<shelf id>" for the discovery's own shelves
// ("catalog:top-songs" and so on); chart rows "genreCharts" and "cityCharts".
//
// GUI thread only.
class HomeCatalogFeed final : public QObject {
    Q_OBJECT

public:
    // Preview lengths on Home.
    static constexpr int ChartRowLimit = 18;
    static constexpr int MoodLimit = 12;
    static constexpr int PopularArtistsLimit = 20;

    // `catalog` outlives this object.
    explicit HomeCatalogFeed(core::catalog::CatalogService& catalog, QObject* parent = nullptr);

    // Loads when due: nothing yet, a failure whose back-off is over, or
    // `force`. Home calls it when it opens.
    void load(bool force = false);
    // Forgets everything (leaving Account mode).
    void reset();
    // Home is not shown: a pending retry waits for the next load().
    void pause();

    [[nodiscard]] bool hasDiscovery() const { return m_discovery.has_value(); }
    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] QString error() const { return m_error; }
    // Nothing to show yet and no error: the loading skeleton.
    [[nodiscard]] bool showSkeleton() const { return !m_discovery && m_error.isEmpty(); }
    // The live load failed while an older copy is shown.
    [[nodiscard]] bool showingCached() const { return m_discovery && m_cached; }
    [[nodiscard]] QString regionName() const;

    // The keys of the discovery's own shelves, in its order.
    [[nodiscard]] QStringList discoveryShelfKeys() const;
    [[nodiscard]] QString shelfTitle(const QString& key) const;
    [[nodiscard]] bool shelfHasChart(const QString& key) const;
    // Always a model; empty when the shelf has nothing.
    [[nodiscard]] CatalogCardModel* shelf(const QString& key);
    [[nodiscard]] ChartCardModel* charts(const QString& key);
    [[nodiscard]] QString chartsTitle(const QString& key) const;
    [[nodiscard]] std::optional<core::catalog::Shelf> discoveryShelf(const QString& key) const;

signals:
    void changed();

private:
    void apply(core::catalog::Discovery discovery, bool cached);
    void fail(const QString& message);
    [[nodiscard]] CatalogCardModel* shelfModel(const QString& key);

    core::catalog::CatalogService& m_catalog;
    std::optional<core::catalog::Discovery> m_discovery;
    bool m_cached = false;
    bool m_loading = false;
    QString m_error;
    std::uint64_t m_request = 0;
    std::chrono::milliseconds m_retryDelay{0};
    QTimer m_retry;
    bool m_paused = true;
    QHash<QString, CatalogCardModel*> m_shelves;
    QHash<QString, ChartCardModel*> m_charts;
    QHash<QString, QString> m_titles;
};

} // namespace lmp::ui
