#pragma once

#include "core/account/AccountSessionState.h"
#include "core/account/LibraryArtistContract.h"
#include "core/async/Result.h"
#include "core/catalog/CatalogClient.h"
#include "core/catalog/CatalogModels.h"

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <any>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace lmp::core {
class SettingsStore;
namespace account {
class AccountSessionService;
}
namespace db {
class AccountRepository;
}
class PlaybackClock;
} // namespace lmp::core

namespace lmp::core::catalog {

// A copy newer than this is used as it is.
inline constexpr std::chrono::minutes CatalogFresh{30};
// Home's discovery is personal and changes often, so it is checked again on
// almost every visit.
inline constexpr std::chrono::minutes DiscoveryFresh{2};
// A copy older than this is not shown, unless the network fails.
inline constexpr std::chrono::hours CatalogMaxAge{7 * 24};
// How long an artist name the catalogue does not know is not asked again.
inline constexpr std::chrono::minutes ArtistResolveMissMemory{5};

enum class CatalogAge {
    Fresh,
    Stale,
    Expired,
};

// The age class of a copy saved at `savedAtMs`, as the reference client
// decides it: a clock that went backwards counts as stale.
[[nodiscard]] CatalogAge catalogAge(std::uint64_t savedAtMs, std::uint64_t nowMs, std::chrono::milliseconds fresh);

// The catalogue for the signed-in account and its region, with an in-memory
// cache. A copy up to its fresh age is used as it is; an older one is shown
// at once while a fresh copy loads in the background (refreshed() names its
// key when it lands); one older than CatalogMaxAge is fetched again and shown
// only when that fails. `force` skips every copy. Requests for the same key
// share one load. A change of account or region bumps the generation and
// clears the cache, so nothing loaded before it can fill the cache again.
//
// Home's discovery is also kept in the database per account and region, so
// a launch can draw it before the network answers (storedDiscovery).
//
// Every call needs a session; without one it fails with Unauthorized. An
// answer that arrives after the session changed fails with Cancelled.
//
// GUI thread only. Futures finish on the GUI thread.
class CatalogService final : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        CatalogClient& client;
        account::AccountSessionService& session;
        db::AccountRepository& accounts;
        SettingsStore& settings;
        PlaybackClock& clock;
        SourceTags tags;
    };

    // Everything in `dependencies` outlives this object.
    explicit CatalogService(Dependencies dependencies, QObject* parent = nullptr);

    // The region in Settings (CatalogStorefront), else "in".
    [[nodiscard]] QString region() const;
    [[nodiscard]] const SourceTags& tags() const { return m_deps.tags; }
    [[nodiscard]] std::uint64_t generation() const { return m_generation; }

    [[nodiscard]] QFuture<Result<QList<Region>>> regions(bool force = false);
    // Home's discovery for the current region.
    [[nodiscard]] QFuture<Result<Discovery>> discovery(bool force = false);
    // The copy kept in the database for the signed-in account and the
    // current region, when it still reads as a page with something on it.
    [[nodiscard]] std::optional<Discovery> storedDiscovery() const;
    // A chart's first page; a global chart without its own descriptor uses
    // the one the cached discovery holds.
    [[nodiscard]] QFuture<Result<Chart>> chart(const ChartRef& chart, bool force = false);
    // A later page of a chart (not kept).
    [[nodiscard]] QFuture<Result<ItemPage>> chartPage(const ChartRef& chart, int offset);
    [[nodiscard]] QFuture<Result<Detail>> detail(ResourceType type, const QString& catalogId, bool force = false);
    // A catalogue artist's section in full, and its later pages (not kept).
    [[nodiscard]] QFuture<Result<Shelf>> artistSection(const QString& artistId, ArtistSection section,
                                                       const QString& title, bool force = false);
    [[nodiscard]] QFuture<Result<ItemPage>> artistSectionPage(const QString& artistId, ArtistSection section,
                                                              int offset);
    [[nodiscard]] QFuture<Result<Detail>> providerArtist(const account::LibraryArtistIdentity& identity,
                                                         bool force = false);
    [[nodiscard]] QFuture<Result<Shelf>> providerArtistSection(const account::LibraryArtistIdentity& identity,
                                                               ArtistSection section, const QString& title,
                                                               bool force = false);
    [[nodiscard]] QFuture<Result<Detail>> providerAlbum(const account::LibraryArtistIdentity& identity,
                                                        const QString& artist, bool force = false);
    [[nodiscard]] QFuture<Result<Detail>> externalPlaylist(const QString& id, bool force = false);
    // A New Songs or New Albums page (not kept).
    [[nodiscard]] QFuture<Result<ItemPage>> newReleasesPage(NewReleasesKind kind, int offset);
    // The catalogue artist a name stands for, or nothing. A miss is not
    // cached; it is only remembered for ArtistResolveMissMemory.
    [[nodiscard]] QFuture<Result<std::optional<CatalogItem>>> resolveArtist(const QString& name);

    // Forgets every copy and bumps the generation.
    void clear();

signals:
    // A background refresh replaced the copy under `key`.
    void refreshed(const QString& key);
    // The cache was cleared (account or region changed, or clear()).
    void cleared();

private:
    using Loader = std::function<QFuture<Result<QByteArray>>(const QString& bearer)>;

    struct Entry {
        std::any value;
        std::uint64_t savedAtMs = 0;
    };

    template <typename T>
    using Parser = std::function<Result<T>(const QByteArray& payload)>;

    template <typename T>
    QFuture<Result<T>> cached(const QString& key, Loader load, Parser<T> parse, bool force,
                              std::chrono::milliseconds fresh);
    template <typename T>
    void revalidate(const QString& key, const Loader& load, const Parser<T>& parse);
    template <typename T>
    QFuture<Result<T>> uncached(Loader load, Parser<T> parse);

    void onScopeChanged();
    [[nodiscard]] std::optional<ChartDescriptor> knownDescriptor(const ChartRef& chart) const;

    Dependencies m_deps;
    std::uint64_t m_generation = 0;
    QHash<QString, Entry> m_entries;
    // Loads under way: a token and the QFuture<Result<T>>. A forced load
    // replaces the one before it, whose answer is then not kept.
    QHash<QString, std::pair<std::uint64_t, std::any>> m_loading;
    std::uint64_t m_loadToken = 0;
    QSet<QString> m_revalidating;
    QHash<QString, std::uint64_t> m_artistMisses;
    QString m_scopeOwner;
    QString m_scopeRegion;
};

// The cache key of a region's discovery.
[[nodiscard]] QString discoveryKey(const QString& region);

} // namespace lmp::core::catalog
