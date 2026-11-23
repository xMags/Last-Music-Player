#include "core/catalog/CatalogService.h"

#include "core/account/AccountSessionService.h"
#include "core/account/PortablePreferenceSettings.h"
#include "core/catalog/CatalogDetailParser.h"
#include "core/catalog/CatalogParser.h"
#include "core/db/AccountRepository.h"
#include "core/playback/PlaybackClock.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "core/text/CaseFold.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>

#include <utility>

namespace lmp::core::catalog {

namespace {

[[nodiscard]] Failure signedOut()
{
    return Failure::of(FailureKind::Unauthorized, QStringLiteral("Sign in to see the music catalog."));
}

[[nodiscard]] Failure cancelled()
{
    return Failure::of(FailureKind::Cancelled, QStringLiteral("The account or region changed."));
}

template <typename T>
[[nodiscard]] QFuture<Result<T>> ready(Result<T> value)
{
    return QtFuture::makeReadyValueFuture(std::move(value));
}

[[nodiscard]] bool hasContent(const Discovery& discovery)
{
    return !discovery.shelves.isEmpty() || !discovery.charts.isEmpty() || discovery.newReleasesShelf
        || discovery.newReleaseSongsShelf || discovery.hitsShelf || discovery.popularArtistsShelf;
}

[[nodiscard]] QString identityKey(const account::LibraryArtistIdentity& identity)
{
    return identity.provider + QLatin1Char('\n') + identity.catalogId + QLatin1Char('\n') + identity.sourceUrl
        + QLatin1Char('\n') + identity.name;
}

} // namespace

CatalogAge catalogAge(std::uint64_t savedAtMs, std::uint64_t nowMs, std::chrono::milliseconds fresh)
{
    if (nowMs < savedAtMs) {
        return CatalogAge::Stale;
    }
    const auto age = std::chrono::milliseconds(static_cast<qint64>(nowMs - savedAtMs));
    if (age <= fresh) {
        return CatalogAge::Fresh;
    }
    return age <= CatalogMaxAge ? CatalogAge::Stale : CatalogAge::Expired;
}

QString discoveryKey(const QString& region)
{
    return QStringLiteral("discovery:") + region;
}

CatalogService::CatalogService(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(dependencies))
{
    connect(&m_deps.session, &account::AccountSessionService::ownerChanged, this, &CatalogService::onScopeChanged);
    connect(&m_deps.settings, &SettingsStore::changed, this, [this](const QString& key) {
        if (key.isEmpty() || key == settings::keys::CatalogStorefront) {
            onScopeChanged();
        }
    });
    const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
    m_scopeOwner = context ? context->ownerId : QString();
    m_scopeRegion = region();
}

QString CatalogService::region() const
{
    const QString stored = m_deps.settings.string(settings::keys::CatalogStorefront,
                                                  QString(account::DefaultCatalogStorefront));
    return normalizeRegionCode(stored).value_or(QString(account::DefaultCatalogStorefront));
}

void CatalogService::onScopeChanged()
{
    const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
    const QString owner = context ? context->ownerId : QString();
    const QString currentRegion = region();
    if (owner == m_scopeOwner && currentRegion == m_scopeRegion) {
        return;
    }
    m_scopeOwner = owner;
    m_scopeRegion = currentRegion;
    clear();
}

void CatalogService::clear()
{
    ++m_generation;
    m_entries.clear();
    m_loading.clear();
    m_revalidating.clear();
    m_artistMisses.clear();
    emit cleared();
}

template <typename T>
QFuture<Result<T>> CatalogService::uncached(Loader load, Parser<T> parse)
{
    const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
    if (!context) {
        return ready<T>(signedOut());
    }
    const std::uint64_t generation = m_generation;
    return load(context->bearerSession)
        .then(this, [this, context = *context, generation, parse = std::move(parse)](
                        const Result<QByteArray>& answer) -> Result<T> {
            if (!m_deps.session.isCurrent(context) || generation != m_generation) {
                return cancelled();
            }
            if (!answer) {
                if (answer.failure().kind == FailureKind::Unauthorized) {
                    m_deps.session.handleUnauthorized(context);
                }
                return answer.failure();
            }
            return parse(answer.value());
        });
}

template <typename T>
QFuture<Result<T>> CatalogService::cached(const QString& key, Loader load, Parser<T> parse, bool force,
                                          std::chrono::milliseconds fresh)
{
    if (force) {
        m_entries.remove(key);
        m_loading.remove(key);
    }
    std::optional<T> fallback;
    const auto found = m_entries.constFind(key);
    if (found != m_entries.cend()) {
        const CatalogAge age = catalogAge(found->savedAtMs, m_deps.clock.nowMilliseconds(), fresh);
        if (age != CatalogAge::Expired) {
            if (age == CatalogAge::Stale) {
                revalidate<T>(key, load, parse);
            }
            return ready<T>(std::any_cast<T>(found->value));
        }
        // Too old to show, unless the network fails.
        fallback = std::any_cast<T>(found->value);
    }
    const auto loading = m_loading.constFind(key);
    if (loading != m_loading.cend() && loading->second.has_value()) {
        return std::any_cast<QFuture<Result<T>>>(loading->second);
    }
    const std::uint64_t generation = m_generation;
    const std::uint64_t token = ++m_loadToken;
    // Registered before the load starts: an answer can arrive (and its
    // continuation run) before the future below is stored.
    m_loading.insert(key, {token, std::any()});
    QFuture<Result<T>> future =
        uncached<T>(std::move(load), std::move(parse))
            .then(this, [this, key, generation, token, fallback](const Result<T>& result) -> Result<T> {
                if (generation != m_generation) {
                    return cancelled();
                }
                const auto current = m_loading.constFind(key);
                const bool newest = current != m_loading.cend() && current->first == token;
                if (newest) {
                    m_loading.remove(key);
                }
                if (!result) {
                    // Offline or failing: an old page beats an error.
                    if (fallback && result.failure().kind != FailureKind::Cancelled) {
                        return *fallback;
                    }
                    return result;
                }
                if (newest) {
                    m_entries.insert(key, Entry{result.value(), m_deps.clock.nowMilliseconds()});
                }
                return result;
            });
    const auto registered = m_loading.find(key);
    if (registered != m_loading.end() && registered->first == token) {
        registered->second = future;
    }
    return future;
}

template <typename T>
void CatalogService::revalidate(const QString& key, const Loader& load, const Parser<T>& parse)
{
    if (m_revalidating.contains(key)) {
        return;
    }
    m_revalidating.insert(key);
    const std::uint64_t generation = m_generation;
    (void)uncached<T>(load, parse).then(this, [this, key, generation](const Result<T>& result) {
        if (generation != m_generation) {
            return;
        }
        m_revalidating.remove(key);
        if (!result) {
            return; // the copy on screen stays; the next visit tries again
        }
        m_entries.insert(key, Entry{result.value(), m_deps.clock.nowMilliseconds()});
        emit refreshed(key);
    });
}

QFuture<Result<QList<Region>>> CatalogService::regions(bool force)
{
    return cached<QList<Region>>(
        QStringLiteral("storefronts"), [this](const QString& bearer) { return m_deps.client.regions(bearer); },
        [](const QByteArray& payload) -> Result<QList<Region>> { return parseRegions(payload); }, force, CatalogFresh);
}

QFuture<Result<Discovery>> CatalogService::discovery(bool force)
{
    const QString code = region();
    const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
    const QString owner = context ? context->ownerId : QString();
    const auto found = m_entries.constFind(QStringLiteral("storefronts"));
    const QList<Region> known = found != m_entries.cend() ? std::any_cast<QList<Region>>(found->value) : QList<Region>();
    return cached<Discovery>(
        discoveryKey(code), [this, code](const QString& bearer) { return m_deps.client.discovery(bearer, code); },
        [this, code, known, owner](const QByteArray& payload) -> Result<Discovery> {
            Result<Discovery> parsed = parseDiscovery(payload, code, known, m_deps.tags);
            if (parsed && hasContent(parsed.value()) && !owner.isEmpty()) {
                // Kept for the next launch; losing it only costs a cold start.
                (void)m_deps.accounts.saveCatalogDiscoveryPayload(owner, code, QString::fromUtf8(payload));
            }
            return parsed;
        },
        force, DiscoveryFresh);
}

std::optional<Discovery> CatalogService::storedDiscovery() const
{
    const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
    if (!context) {
        return std::nullopt;
    }
    const QString code = region();
    const QString payload = m_deps.accounts.loadCatalogDiscoveryPayload(context->ownerId, code);
    if (payload.isEmpty()) {
        return std::nullopt;
    }
    Result<Discovery> parsed = parseDiscovery(payload.toUtf8(), code, {}, m_deps.tags);
    if (!parsed || !hasContent(parsed.value())) {
        return std::nullopt;
    }
    return std::move(parsed).value();
}

std::optional<ChartDescriptor> CatalogService::knownDescriptor(const ChartRef& chart) const
{
    const auto found = m_entries.constFind(discoveryKey(chart.region));
    if (found == m_entries.cend()) {
        return std::nullopt;
    }
    const Discovery& discovery = std::any_cast<const Discovery&>(found->value);
    QList<ChartDescriptor> charts = discovery.charts;
    if (discovery.cityChartGroups) {
        charts += discovery.cityChartGroups->indian.charts + discovery.cityChartGroups->international.charts;
    }
    for (const ChartDescriptor& descriptor : charts) {
        if (descriptor.ref == chart) {
            return descriptor;
        }
    }
    return std::nullopt;
}

QFuture<Result<Chart>> CatalogService::chart(const ChartRef& chart, bool force)
{
    if (!isValidChartRef(chart)) {
        return ready<Chart>(Failure::of(FailureKind::Unknown, QStringLiteral("Invalid catalog chart.")));
    }
    const QString key = QStringLiteral("chart:") + chart.region + QLatin1Char(':') + resourceTypeName(chart.resourceType)
        + QLatin1Char(':') + chartKindName(chart.kind) + QLatin1Char(':') + chart.id;
    const std::optional<ChartDescriptor> known = knownDescriptor(chart);
    return cached<Chart>(
        key, [this, chart](const QString& bearer) { return m_deps.client.chart(bearer, chart, 0, ChartFirstPage); },
        [this, chart, known](const QByteArray& payload) { return parseChart(payload, chart, known, m_deps.tags); },
        force, CatalogFresh);
}

QFuture<Result<ItemPage>> CatalogService::chartPage(const ChartRef& chart, int offset)
{
    if (!isValidChartRef(chart) || offset <= 0) {
        return ready<ItemPage>(Failure::of(FailureKind::Unknown, QStringLiteral("Invalid catalog chart page.")));
    }
    return uncached<ItemPage>(
        [this, chart, offset](const QString& bearer) {
            return m_deps.client.chart(bearer, chart, offset, CatalogMorePage);
        },
        [this, chart](const QByteArray& payload) { return parseChartPage(payload, chart, m_deps.tags); });
}

QFuture<Result<Detail>> CatalogService::detail(ResourceType type, const QString& catalogId, bool force)
{
    const QString code = region();
    const QString id = catalogId.trimmed();
    // Artist pages carry single and EP marks; the "v2" keeps them apart from
    // copies without.
    const QString key = QStringLiteral("detail:") + (type == ResourceType::Artist ? QStringLiteral("v2:") : QString())
        + code + QLatin1Char(':') + resourceTypeName(type) + QLatin1Char(':') + id;
    return cached<Detail>(
        key, [this, code, type, id](const QString& bearer) { return m_deps.client.detail(bearer, code, type, id); },
        [this, type](const QByteArray& payload) { return parseCatalogDetail(payload, type, m_deps.tags); }, force,
        CatalogFresh);
}

QFuture<Result<Shelf>> CatalogService::artistSection(const QString& artistId, ArtistSection section,
                                                     const QString& title, bool force)
{
    const QString code = region();
    const QString id = artistId.trimmed();
    const QString key = QStringLiteral("artist-section:v2:") + code + QLatin1Char(':') + id + QLatin1Char(':')
        + artistSectionId(section);
    return cached<Shelf>(
        key,
        [this, code, id, section](const QString& bearer) {
            return m_deps.client.catalogArtistSection(bearer, code, id, section, 0, ChartFirstPage);
        },
        [this, section, title](const QByteArray& payload) -> Result<Shelf> {
            const QJsonDocument document = QJsonDocument::fromJson(payload);
            if (!document.isObject()) {
                return Failure::of(FailureKind::InvalidResponse,
                                   QStringLiteral("This artist section could not be loaded. Try again."));
            }
            const QJsonObject root = document.object();
            Shelf shelf;
            shelf.id = artistSectionId(section);
            shelf.title = title;
            for (const QJsonValue& entry : root.value(QLatin1StringView("items")).toArray()) {
                if (std::optional<CatalogItem> item = parseCatalogItem(entry, m_deps.tags)) {
                    shelf.items.append(std::move(*item));
                }
            }
            if (section == ArtistSection::Singles) {
                shelf.items = asSinglesAndEps(std::move(shelf.items));
            }
            shelf.type = parseResourceType(root.value(QLatin1StringView("resourceType")).toString());
            if (shelf.type == ResourceType::Unknown) {
                shelf.type = shelf.items.isEmpty() ? ResourceType::Song : shelf.items.first().type;
            }
            shelf.nextOffset = parseNextOffset(root.value(QLatin1StringView("nextOffset")));
            return shelf;
        },
        force, CatalogFresh);
}

QFuture<Result<ItemPage>> CatalogService::artistSectionPage(const QString& artistId, ArtistSection section, int offset)
{
    const QString code = region();
    const QString id = artistId.trimmed();
    if (id.isEmpty() || offset <= 0) {
        return ready<ItemPage>(Failure::of(FailureKind::Unknown, QStringLiteral("Invalid artist section page.")));
    }
    return uncached<ItemPage>(
        [this, code, id, section, offset](const QString& bearer) {
            return m_deps.client.catalogArtistSection(bearer, code, id, section, offset, CatalogMorePage);
        },
        [this, section](const QByteArray& payload) -> Result<ItemPage> {
            const QJsonObject root = QJsonDocument::fromJson(payload).object();
            ItemPage page;
            for (const QJsonValue& entry : root.value(QLatin1StringView("items")).toArray()) {
                if (std::optional<CatalogItem> item = parseCatalogItem(entry, m_deps.tags)) {
                    page.items.append(std::move(*item));
                }
            }
            if (section == ArtistSection::Singles) {
                page.items = asSinglesAndEps(std::move(page.items));
            }
            page.nextOffset = parseNextOffset(root.value(QLatin1StringView("nextOffset")));
            return page;
        });
}

QFuture<Result<Detail>> CatalogService::providerArtist(const account::LibraryArtistIdentity& identity, bool force)
{
    return cached<Detail>(
        QStringLiteral("provider-artist:") + identityKey(identity),
        [this, identity](const QString& bearer) { return m_deps.client.providerArtist(bearer, identity); },
        [identity](const QByteArray& payload) { return parseProviderArtistDetail(payload, identity); }, force,
        CatalogFresh);
}

QFuture<Result<Shelf>> CatalogService::providerArtistSection(const account::LibraryArtistIdentity& identity,
                                                             ArtistSection section, const QString& title, bool force)
{
    return cached<Shelf>(
        QStringLiteral("provider-artist-section:") + artistSectionId(section) + QLatin1Char('\n') + identityKey(identity),
        [this, identity, section](const QString& bearer) {
            return m_deps.client.providerArtistSection(bearer, identity, section);
        },
        [section, title](const QByteArray& payload) { return parseProviderArtistSection(payload, section, title); },
        force, CatalogFresh);
}

QFuture<Result<Detail>> CatalogService::providerAlbum(const account::LibraryArtistIdentity& identity,
                                                      const QString& artist, bool force)
{
    return cached<Detail>(
        QStringLiteral("provider-album:") + identityKey(identity) + QLatin1Char('\n') + artist,
        [this, identity, artist](const QString& bearer) { return m_deps.client.providerAlbum(bearer, identity, artist); },
        [identity](const QByteArray& payload) { return parseProviderAlbumDetail(payload, identity); }, force,
        CatalogFresh);
}

QFuture<Result<Detail>> CatalogService::externalPlaylist(const QString& id, bool force)
{
    if (!m_deps.tags.isExternalPlaylistId(id)) {
        return ready<Detail>(Failure::of(FailureKind::Unknown, QStringLiteral("Invalid playlist.")));
    }
    return cached<Detail>(
        QStringLiteral("external-playlist:") + id,
        [this, id](const QString& bearer) { return m_deps.client.externalPlaylist(bearer, id, m_deps.tags); },
        [this, id](const QByteArray& payload) { return parseExternalPlaylistDetail(payload, id, m_deps.tags); }, force,
        CatalogFresh);
}

QFuture<Result<ItemPage>> CatalogService::newReleasesPage(NewReleasesKind kind, int offset)
{
    const QString code = region();
    if (offset < 0) {
        return ready<ItemPage>(Failure::of(FailureKind::Unknown, QStringLiteral("Invalid new releases page.")));
    }
    return uncached<ItemPage>(
        [this, code, kind, offset](const QString& bearer) {
            return m_deps.client.newReleases(bearer, code, kind, offset);
        },
        [this, kind](const QByteArray& payload) -> Result<ItemPage> {
            const QJsonObject root = QJsonDocument::fromJson(payload).object();
            const QJsonObject wrapped{{QStringLiteral("items"), root.value(QLatin1StringView("items"))}};
            const std::optional<Shelf> shelf = kind == NewReleasesKind::Songs
                ? parseNewReleaseSongsShelf(wrapped, m_deps.tags)
                : parseNewReleasesShelf(wrapped, m_deps.tags);
            return ItemPage{shelf ? shelf->items : QList<CatalogItem>(),
                            parseNextOffset(root.value(QLatin1StringView("nextOffset")))};
        });
}

QFuture<std::optional<AlbumTarget>> CatalogService::songAlbum(const Track& track)
{
    QUrlQuery fields;
    const auto add = [&fields](const char* key, const QString& value) {
        if (!value.trimmed().isEmpty()) {
            fields.addQueryItem(QString::fromLatin1(key), value.trimmed());
        }
    };
    add("provider", track.sourceTag);
    add("catalogId", track.catalogId);
    add("sourceUrl", track.sourceUrl);
    add("title", track.title);
    add("artist", track.artist == QLatin1StringView("Unknown artist") ? QString() : track.artist);
    add("album", track.album);
    add("albumCatalogId", track.albumCatalogId);
    add("durationMs", track.durationSeconds > 0 ? QString::number(qRound64(track.durationSeconds * 1000)) : QString());
    if (!fields.hasQueryItem(QStringLiteral("title")) && !fields.hasQueryItem(QStringLiteral("catalogId"))
        && !fields.hasQueryItem(QStringLiteral("sourceUrl"))) {
        return QtFuture::makeReadyValueFuture(std::optional<AlbumTarget>());
    }
    const QString code = region();
    // Bump the version when the service changes which album it picks.
    const QString key = QStringLiteral("song-album:v2:") + code + QLatin1Char(':') + fields.toString(QUrl::FullyEncoded);
    return cached<AlbumTarget>(
               key, [this, code, fields](const QString& bearer) { return m_deps.client.songAlbum(bearer, code, fields); },
               [this](const QByteArray& payload) -> Result<AlbumTarget> {
                   std::optional<AlbumTarget> target = parseSongAlbum(payload, m_deps.tags);
                   if (!target) {
                       return Failure::of(FailureKind::NotFound, QStringLiteral("No album for that song."));
                   }
                   return std::move(*target);
               },
               false, CatalogFresh)
        .then([](const Result<AlbumTarget>& result) {
            return result ? std::optional<AlbumTarget>(result.value()) : std::nullopt;
        });
}

QFuture<Result<std::optional<CatalogItem>>> CatalogService::resolveArtist(const QString& name)
{
    using Answer = Result<std::optional<CatalogItem>>;
    const QString code = region();
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return ready<std::optional<CatalogItem>>(std::optional<CatalogItem>());
    }
    const QString key = QStringLiteral("artist-resolve:v2:") + code + QLatin1Char(':') + trimmed.toLower();
    const auto miss = m_artistMisses.constFind(key);
    if (miss != m_artistMisses.cend()) {
        if (*miss > m_deps.clock.nowMilliseconds()) {
            return ready<std::optional<CatalogItem>>(std::optional<CatalogItem>());
        }
        m_artistMisses.remove(key);
    }
    const std::uint64_t generation = m_generation;
    return cached<CatalogItem>(
               key, [this, code, trimmed](const QString& bearer) {
                   return m_deps.client.resolveArtist(bearer, code, trimmed);
               },
               [this](const QByteArray& payload) -> Result<CatalogItem> {
                   std::optional<CatalogItem> item = parseCatalogItem(
                       QJsonDocument::fromJson(payload).object().value(QLatin1StringView("artist")), m_deps.tags);
                   if (!item) {
                       return Failure::of(FailureKind::NotFound, QStringLiteral("No catalogue artist by that name."));
                   }
                   return std::move(*item);
               },
               false, CatalogFresh)
        .then(this, [this, key, generation](const Result<CatalogItem>& result) -> Answer {
            if (result) {
                return std::optional<CatalogItem>(result.value());
            }
            if (result.failure().kind != FailureKind::NotFound) {
                return result.failure();
            }
            if (generation == m_generation) {
                m_artistMisses.insert(key, m_deps.clock.nowMilliseconds()
                                               + static_cast<std::uint64_t>(
                                                   std::chrono::milliseconds(ArtistResolveMissMemory).count()));
            }
            return std::optional<CatalogItem>();
        });
}

} // namespace lmp::core::catalog
