#include "ui/catalog/CatalogPageViewModel.h"

#include "core/account/AccountLibraryEditor.h"
#include "core/catalog/CatalogPresentation.h"
#include "core/catalog/CatalogService.h"
#include "core/catalog/ChartArt.h"
#include "core/catalog/CollectionSourceKey.h"
#include "core/library/TrackActions.h"
#include "ui/catalog/CatalogRoutes.h"
#include "ui/share/ShareController.h"
#include "ui/images/ChartArtImageProvider.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/player/CoverSource.h"
#include "ui/tracks/TrackMenuController.h"

#include <QLatin1StringView>

#include <cmath>

namespace lmp::ui {

namespace catalog = core::catalog;
namespace routes = catalog_routes;

namespace {

[[nodiscard]] QUrl coverUrl(const QString& artworkUrl)
{
    if (artworkUrl.isEmpty()) {
        return {};
    }
    core::Track cover;
    cover.artworkUrl = artworkUrl;
    cover.sourceKind = QStringLiteral("remote");
    return CoverSource::forTrack(cover).displayUrl();
}

[[nodiscard]] QString itemKey(const catalog::CatalogItem& item)
{
    return catalog::resourceTypeName(item.type) + QLatin1Char(':') + item.catalogId;
}

[[nodiscard]] QList<core::Track> songsOf(const QList<catalog::CatalogItem>& items)
{
    QList<core::Track> songs;
    for (const catalog::CatalogItem& item : items) {
        if (item.track) {
            songs.append(*item.track);
        }
    }
    return songs;
}

} // namespace

CatalogPageViewModel::CatalogPageViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_catalog(parts.catalog)
    , m_editor(parts.editor)
    , m_actions(parts.actions)
    , m_trackMenu(parts.trackMenu)
    , m_navigator(parts.navigator)
    , m_share(parts.share)
    , m_providerPages(std::move(parts.providerPages))
    , m_providerAlbum(std::move(parts.providerAlbum))
    , m_download(std::move(parts.download))
    , m_singleSongs({.catalog = parts.catalog,
                     .providerAlbum =
                         [this](const core::account::LibraryArtistIdentity& identity, const QString& artist) {
                             // As load() opens a SourceAlbum route.
                             return m_providerPages && m_providerPages() && m_providerAlbum
                                 ? m_providerAlbum(identity, artist)
                                 : m_catalog.providerAlbum(identity, artist);
                         }})
{
    connect(&m_navigator, &Navigator::currentChanged, this, &CatalogPageViewModel::followNavigator);
    // The release rows follow the cards.
    connect(&m_cards, &CatalogCardModel::itemsChanged, this, [this]() {
        QList<core::Track> rows;
        if (core::catalog::isReleaseList(m_cards.items())) {
            rows.reserve(m_cards.items().size());
            for (const core::catalog::CatalogItem& item : m_cards.items()) {
                core::Track row;
                row.title = item.title;
                row.artist = item.subtitle;
                row.album = core::catalog::releaseDateLabel(item.releaseDate);
                row.durationText = item.trackCount > 0 ? QString::number(item.trackCount) : QString();
                row.artworkUrl = item.artworkUrl;
                row.sourceKind = QStringLiteral("remote");
                rows.append(row);
            }
        }
        m_releases.setTracks(std::move(rows));
    });
    // A saved copy made or removed elsewhere changes the Save button.
    connect(&m_editor, &core::account::AccountLibraryEditor::playlistsChanged, this, &CatalogPageViewModel::changed);
    connect(&m_actions, &core::TrackActions::likedChanged, this, [this](const QString& sourceKey, bool liked) {
        m_tracks.setLiked(sourceKey, liked);
    });
}

bool CatalogPageViewModel::handles(const Route& route)
{
    switch (route.kind()) {
    case Route::Kind::CatalogDetail: {
        const QString type = route.param(QStringLiteral("type"));
        return type == QLatin1StringView("album") || type == QLatin1StringView("playlist");
    }
    case Route::Kind::SourceAlbum:
    case Route::Kind::ExternalPlaylist:
    case Route::Kind::Chart:
    case Route::Kind::Shelf:
    case Route::Kind::ArtistSection:
    case Route::Kind::Shared:
        return true;
    default:
        return false;
    }
}

void CatalogPageViewModel::followNavigator()
{
    // Another page (or this one again): a single still being looked up must
    // neither play nor open its page now.
    m_singleSongs.forget();
    const Route& current = m_navigator.current();
    if (handles(current)) {
        load(current, false);
        return;
    }
    // Another page: whatever is still on its way for this one is not wanted
    // (its answer would replace the page the user went to).
    ++m_request;
}

void CatalogPageViewModel::replaceCurrent(const Route& next)
{
    // Only over the page the request was made for.
    if (m_navigator.current().kind() != m_requestRoute.kind()) {
        return;
    }
    m_navigator.replace(next);
}

void CatalogPageViewModel::retry()
{
    if (handles(m_navigator.current())) {
        load(m_navigator.current(), true);
    }
}

void CatalogPageViewModel::reset(const QString& kind)
{
    m_kind = kind;
    m_loading = true;
    m_finding = false;
    m_error.clear();
    m_eyebrow.clear();
    m_title.clear();
    m_byline.clear();
    m_infoSuffix.clear();
    m_shelfType = core::catalog::ResourceType::Unknown;
    m_singlesShelf = false;
    m_description.clear();
    m_artworkUrl.clear();
    m_shareArtwork.clear();
    m_sourceUrl.clear();
    m_showsCards = false;
    m_saving = false;
    m_pager.reset();
    m_chart.reset();
    m_shelfKey.clear();
    m_artistId.clear();
    m_artistSection.reset();
    m_tracks.setTracks({});
    m_cards.setItems({});
}

void CatalogPageViewModel::fail(const QString& message)
{
    m_loading = false;
    m_finding = false;
    m_error = message;
    emit changed();
}

void CatalogPageViewModel::load(const Route& route, bool force)
{
    const std::uint64_t request = ++m_request;
    m_requestRoute = route;
    const auto current = [this, request]() { return request == m_request; };
    const QString title = route.param(QStringLiteral("title"));

    if (route.kind() == Route::Kind::Shared) {
        reset(QStringLiteral("playlist"));
        emit changed();
        m_catalog.sharedItem(route.param(QStringLiteral("code")))
            .then(this, [this, current](const core::Result<core::share::SharedItem>& result) {
                if (!current()) {
                    return;
                }
                if (!result) {
                    fail(result.failure().kind == core::FailureKind::NotFound
                             ? tr("This share link is not available.")
                             : tr("This page could not be loaded. Try again."));
                    return;
                }
                const core::share::SharedItem& item = result.value();
                // An artist link shows that artist's own page, in place of this one.
                if (item.kind == core::share::ShareKind::Artist && item.artist) {
                    QMap<QString, QString> params{{QStringLiteral("provider"), item.artist->provider},
                                                  {QStringLiteral("name"), item.artist->name},
                                                  {QStringLiteral("title"), item.artist->name}};
                    if (!item.artist->catalogId.isEmpty()) {
                        params.insert(QStringLiteral("catalogId"), item.artist->catalogId);
                    }
                    if (!item.artist->sourceUrl.isEmpty()) {
                        params.insert(QStringLiteral("sourceUrl"), item.artist->sourceUrl);
                    }
                    replaceCurrent(Route(Route::Kind::SourceArtist, params));
                    return;
                }
                m_loading = false;
                m_kind = QStringLiteral("playlist");
                m_eyebrow = item.kind == core::share::ShareKind::Song ? tr("SONG") : tr("PLAYLIST");
                m_title = item.title;
                m_byline = item.subtitle;
                m_artworkUrl = coverUrl(item.artworkUrl);
                m_shareArtwork = item.artworkUrl;
                m_showsCards = false;
                m_tracks.setTracks(item.tracks);
                emit changed();
            });
        return;
    }

    if (route.kind() == Route::Kind::CatalogDetail) {
        const QString type = route.param(QStringLiteral("type"));
        reset(type);
        m_title = title;
        emit changed();
        m_catalog.detail(catalog::parseResourceType(type), route.param(QStringLiteral("id")), force)
            .then(this, [this, current, type](const core::Result<catalog::Detail>& result) {
                if (!current()) {
                    return;
                }
                if (!result) {
                    fail(result.failure().kind == core::FailureKind::NotFound
                             ? tr("This is not available in your music region.")
                             : tr("This page could not be loaded. Try again."));
                    return;
                }
                showDetail(result.value(), type);
            });
        return;
    }

    if (route.kind() == Route::Kind::SourceAlbum && route.param(QStringLiteral("finding")) == QLatin1StringView("1")) {
        reset(QStringLiteral("album"));
        m_finding = true;
        m_title = route.param(QStringLiteral("album"));
        emit changed();
        core::Track song;
        song.sourceTag = route.param(QStringLiteral("provider"));
        song.catalogId = route.param(QStringLiteral("catalogId"));
        song.sourceUrl = route.param(QStringLiteral("sourceUrl"));
        song.title = route.param(QStringLiteral("title"));
        song.artist = route.param(QStringLiteral("artist"));
        song.album = route.param(QStringLiteral("album"));
        song.albumCatalogId = route.param(QStringLiteral("albumCatalogId"));
        song.durationSeconds = route.param(QStringLiteral("durationMs")).toDouble() / 1000.0;
        m_catalog.songAlbum(song).then(this, [this, current](const std::optional<catalog::AlbumTarget>& target) {
            if (!current()) {
                return;
            }
            if (!target) {
                fail(tr("This song's album could not be found."));
                return;
            }
            // The pending page gives way to the album, so Back skips it.
            if (!target->catalogAlbumId.isEmpty()) {
                replaceCurrent(Route(Route::Kind::CatalogDetail, {{QStringLiteral("type"), QStringLiteral("album")},
                                                                       {QStringLiteral("id"), target->catalogAlbumId}}));
            } else {
                replaceCurrent(routes::routeForAlbumIdentity(target->identity, target->artist));
            }
        });
        return;
    }

    if (route.kind() == Route::Kind::SourceAlbum) {
        reset(QStringLiteral("album"));
        m_title = title;
        emit changed();
        const bool fromProvider = m_providerPages && m_providerPages() && m_providerAlbum;
        (fromProvider ? m_providerAlbum(routes::identityFromRoute(route), route.param(QStringLiteral("artist")))
                      : m_catalog.providerAlbum(routes::identityFromRoute(route), route.param(QStringLiteral("artist")),
                                                force))
            .then(this, [this, current](const core::Result<catalog::Detail>& result) {
                if (!current()) {
                    return;
                }
                if (!result) {
                    fail(tr("This album could not be loaded. Try again."));
                    return;
                }
                showDetail(result.value(), QStringLiteral("album"));
            });
        return;
    }

    if (route.kind() == Route::Kind::ExternalPlaylist) {
        reset(QStringLiteral("playlist"));
        m_title = title;
        emit changed();
        m_catalog.externalPlaylist(route.param(QStringLiteral("id")), force)
            .then(this, [this, current](const core::Result<catalog::Detail>& result) {
                if (!current()) {
                    return;
                }
                if (!result) {
                    fail(tr("This playlist could not be loaded. Try again."));
                    return;
                }
                showDetail(result.value(), QStringLiteral("playlist"));
            });
        return;
    }

    if (route.kind() == Route::Kind::Chart) {
        reset(QStringLiteral("chart"));
        const std::optional<catalog::ChartRef> chart = routes::chartFromRoute(route);
        if (!chart) {
            fail(tr("This chart could not be loaded."));
            return;
        }
        m_chart = chart;
        emit changed();
        m_catalog.chart(*chart, force).then(this, [this, current](const core::Result<catalog::Chart>& result) {
            if (!current()) {
                return;
            }
            if (!result) {
                fail(tr("This chart could not be loaded. Try again."));
                return;
            }
            const catalog::Chart& value = result.value();
            m_loading = false;
            m_eyebrow = value.descriptor.ref.kind == catalog::ChartKind::City    ? tr("CITY CHART")
                : value.descriptor.ref.kind == catalog::ChartKind::Genre ? tr("GENRE CHART")
                                                                         : tr("CHART");
            m_title = value.descriptor.title.isEmpty() ? value.shelf.title : value.descriptor.title;
            // chartMeta: songs · length · subtitle, no byline.
            m_infoSuffix = value.descriptor.subtitle;
            m_shareArtwork = value.descriptor.artworkUrl;
            m_artworkUrl = value.descriptor.artworkUrl.isEmpty()
                ? QUrl(ChartArtImageProvider::sourceFor(catalog::resolveChartVisual(value.descriptor)))
                : coverUrl(value.descriptor.artworkUrl);
            m_showsCards = value.shelf.type != catalog::ResourceType::Song;
            startPager(value.shelf.items, value.shelf.nextOffset, false);
        });
        return;
    }

    if (route.kind() == Route::Kind::ArtistSection) {
        reset(QStringLiteral("shelf"));
        const std::optional<catalog::ArtistSection> section =
            catalog::parseArtistSectionId(route.param(QStringLiteral("section")));
        if (!section) {
            fail(tr("This list could not be loaded."));
            return;
        }
        m_title = route.param(QStringLiteral("title"));
        m_eyebrow = route.param(QStringLiteral("artistName")).toUpper();
        m_singlesShelf = *section == catalog::ArtistSection::Singles;
        emit changed();
        const auto show = [this, current](const core::Result<catalog::Shelf>& result) {
            if (!current()) {
                return;
            }
            if (!result) {
                fail(tr("This list could not be loaded. Try again."));
                return;
            }
            m_loading = false;
            m_showsCards = result.value().type != catalog::ResourceType::Song;
            startPager(result.value().items, result.value().nextOffset, false);
        };
        const QString artistId = route.param(QStringLiteral("catalogArtistId"));
        if (!artistId.isEmpty()) {
            // The catalogue's own sections page; the provider's arrive whole.
            m_artistId = artistId;
            m_artistSection = section;
            m_catalog.artistSection(artistId, *section, m_title, force).then(this, show);
        } else {
            m_catalog.providerArtistSection(routes::identityFromRoute(route), *section, m_title, force).then(this, show);
        }
        return;
    }

    // A Home shelf's "See all".
    reset(QStringLiteral("shelf"));
    m_shelfKey = route.param(QStringLiteral("key"));
    emit changed();
    m_catalog.discovery().then(this, [this, current](const core::Result<catalog::Discovery>& result) {
        if (!current()) {
            return;
        }
        if (!result) {
            fail(tr("This list could not be loaded. Try again."));
            return;
        }
        const catalog::Discovery& discovery = result.value();
        std::optional<catalog::Shelf> shelf;
        if (m_shelfKey == QLatin1StringView("newSongs")) {
            shelf = discovery.newReleaseSongsShelf;
        } else if (m_shelfKey == QLatin1StringView("newAlbums")) {
            shelf = discovery.newReleasesShelf;
        } else if (m_shelfKey == QLatin1StringView("biggestHits")) {
            shelf = discovery.hitsShelf;
        } else if (m_shelfKey == QLatin1StringView("popularArtists")) {
            shelf = discovery.popularArtistsShelf;
        } else if (m_shelfKey == QLatin1StringView("moods")) {
            shelf = discovery.moodShelf;
        } else if (m_shelfKey.startsWith(QLatin1StringView("catalog:"))) {
            const QString id = m_shelfKey.mid(8);
            for (const catalog::Shelf& candidate : discovery.shelves) {
                if (candidate.id == id) {
                    shelf = candidate;
                }
            }
        }
        if (!shelf) {
            fail(tr("This list is not available right now."));
            return;
        }
        // A shelf with a chart behind it opens the chart.
        if (shelf->chart) {
            replaceCurrent(routes::routeForChart(*shelf->chart));
            return;
        }
        m_loading = false;
        // CATALOG_SHELF_PAGES: New Songs and New Albums by name, the rest
        // under the shelf's own title; Moods is playlists, the rest sit
        // under New and trending.
        m_title = m_shelfKey == QLatin1StringView("newSongs")    ? tr("New Songs")
            : m_shelfKey == QLatin1StringView("newAlbums")        ? tr("New Albums")
                                                                   : shelf->title;
        m_eyebrow = m_shelfKey == QLatin1StringView("moods") ? tr("COLLECTIONS") : tr("NEW AND TRENDING");
        m_shelfType = shelf->type;
        m_showsCards = shelf->type != catalog::ResourceType::Song;
        // New Songs and New Albums page in full; Home's row shows at once
        // while the first page loads.
        const bool paged = m_shelfKey == QLatin1StringView("newSongs") || m_shelfKey == QLatin1StringView("newAlbums");
        startPager(shelf->items, paged ? std::optional<int>(0) : std::nullopt, paged);
        if (paged) {
            loadMore();
        }
    });
}

void CatalogPageViewModel::showDetail(const catalog::Detail& detail, const QString& kind)
{
    catalog::Detail shown = detail;
    catalog::applyAlbumArtworkToTracks(shown);
    m_loading = false;
    m_kind = kind;
    m_eyebrow = kind == QLatin1StringView("album") ? tr("ALBUM") : tr("PLAYLIST");
    m_title = shown.item.title;
    m_byline = shown.item.subtitle;
    m_description = shown.item.description;
    m_artworkUrl = coverUrl(shown.item.artworkUrl);
    m_shareArtwork = shown.item.artworkUrl;
    m_sourceUrl = shown.item.sourceUrl;
    m_showsCards = false;
    m_tracks.setTracks(shown.tracks);
    emit changed();
}

void CatalogPageViewModel::startPager(QList<catalog::CatalogItem> items, std::optional<int> nextOffset, bool seed)
{
    m_pager.emplace(std::move(items), nextOffset, &itemKey, seed);
    applyPagerItems();
}

void CatalogPageViewModel::applyPagerItems()
{
    if (!m_pager) {
        return;
    }
    if (m_showsCards) {
        m_cards.setItems(m_pager->items());
    } else {
        m_tracks.setTracks(songsOf(m_pager->items()));
    }
    emit changed();
}

bool CatalogPageViewModel::canShare() const
{
    return !m_loading && !m_showsCards && m_tracks.count() > 0 && !m_title.isEmpty();
}

void CatalogPageViewModel::download()
{
    if (m_download && canShare()) {
        (void)m_download(m_title, m_byline, m_shareArtwork, m_tracks.tracks());
    }
}

void CatalogPageViewModel::share()
{
    if (m_share == nullptr || !canShare()) {
        return;
    }
    const QList<core::Track>& songs = m_tracks.tracks();
    if (songs.size() == 1 && m_eyebrow == tr("SONG")) {
        m_share->share({.kind = core::share::ShareKind::Song, .track = songs.first()}, tr("Could not share this song."));
        return;
    }
    m_share->share({.kind = core::share::ShareKind::List,
                    .title = m_title,
                    .subtitle = m_byline,
                    .artworkUrl = m_shareArtwork,
                    .tracks = songs},
                   m_chart ? tr("Could not share this chart.") : tr("Could not share this item."));
}

void CatalogPageViewModel::loadMore()
{
    if (!m_pager) {
        return;
    }
    const std::optional<catalog::PageRequest> request = m_pager->begin();
    if (!request) {
        return;
    }
    emit changed();
    QFuture<core::Result<catalog::ItemPage>> page;
    if (m_chart) {
        page = m_catalog.chartPage(*m_chart, request->offset);
    } else if (m_artistSection && !m_artistId.isEmpty()) {
        page = m_catalog.artistSectionPage(m_artistId, *m_artistSection, request->offset);
    } else if (m_shelfKey == QLatin1StringView("newSongs") || m_shelfKey == QLatin1StringView("newAlbums")) {
        page = m_catalog.newReleasesPage(m_shelfKey == QLatin1StringView("newSongs") ? catalog::NewReleasesKind::Songs
                                                                                     : catalog::NewReleasesKind::Albums,
                                         request->offset);
    } else {
        m_pager->cancel();
        return;
    }
    const std::uint64_t owner = m_request;
    page.then(this, [this, owner, request = *request](const core::Result<catalog::ItemPage>& result) {
        if (owner != m_request || !m_pager) {
            return;
        }
        if (!result) {
            (void)m_pager->fail(request);
            emit changed();
            return;
        }
        if (m_pager->complete(request, result.value())) {
            applyPagerItems();
        }
    });
}

QString CatalogPageViewModel::pagingState() const
{
    if (!m_pager) {
        return QStringLiteral("done");
    }
    switch (m_pager->status()) {
    case catalog::PagerStatus::Idle:
        return QStringLiteral("idle");
    case catalog::PagerStatus::Loading:
        return QStringLiteral("loading");
    case catalog::PagerStatus::Failed:
        return QStringLiteral("failed");
    case catalog::PagerStatus::Done:
        break;
    }
    return QStringLiteral("done");
}

QString CatalogPageViewModel::info() const
{
    const QList<core::Track>& songs = m_tracks.tracks();
    QStringList parts;
    if (!m_showsCards && !songs.isEmpty()) {
        double seconds = 0;
        for (const core::Track& song : songs) {
            seconds += std::max(0.0, song.durationSeconds);
        }
        parts.append(songs.size() == 1 ? tr("1 song") : tr("%1 songs").arg(songs.size()));
        // totalDurationLabel: at least a minute; "N hr" with no minutes left.
        if (seconds > 0) {
            const qint64 minutes = std::max<qint64>(1, std::llround(seconds / 60.0));
            const qint64 rest = minutes % 60;
            parts.append(minutes < 60 ? tr("%1 min").arg(minutes)
                         : rest > 0   ? tr("%1 hr %2 min").arg(minutes / 60).arg(rest)
                                      : tr("%1 hr").arg(minutes / 60));
        }
    }
    // shelfCountLabel: what a shelf page holds so far ("8 releases").
    if (m_kind == QLatin1StringView("shelf") && m_showsCards && m_cards.count() > 0) {
        const int count = m_cards.count();
        if (m_shelfType == catalog::ResourceType::Artist) {
            parts.append(count == 1 ? tr("1 artist") : tr("%1 artists").arg(count));
        } else if (m_shelfKey == QLatin1StringView("moods") || m_shelfKey == QLatin1StringView("biggestHits")) {
            parts.append(count == 1 ? tr("1 playlist") : tr("%1 playlists").arg(count));
        } else {
            parts.append(count == 1 ? tr("1 release") : tr("%1 releases").arg(count));
        }
    }
    if (!m_infoSuffix.isEmpty()) {
        parts.append(m_infoSuffix);
    }
    return parts.join(QStringLiteral(" · "));
}

bool CatalogPageViewModel::canSave() const
{
    return (m_kind == QLatin1StringView("album") || m_kind == QLatin1StringView("playlist")) && !m_loading
        && m_tracks.count() > 0 && !catalog::canonicalSourceKey(m_sourceUrl).isEmpty();
}

bool CatalogPageViewModel::saved() const
{
    return canSave() && m_editor.savedCollectionKey(m_sourceUrl).has_value();
}

void CatalogPageViewModel::toggleSave()
{
    if (!canSave() || m_saving) {
        return;
    }
    m_saving = true;
    emit changed();
    const auto done = [this]() {
        m_saving = false;
        emit changed();
    };
    if (saved()) {
        m_editor.removeSavedCollection(m_sourceUrl).then(this, [done](const core::Result<void>&) { done(); });
        return;
    }
    m_editor.saveCollection(m_title, m_description, m_tracks.tracks(), m_sourceUrl)
        .then(this, [done](const core::Result<QString>&) { done(); });
}

void CatalogPageViewModel::playRow(int row)
{
    if (row >= 0 && row < m_tracks.count()) {
        m_actions.play(m_tracks.trackAt(row), m_tracks.tracks());
    }
}

void CatalogPageViewModel::play()
{
    for (const core::Track& track : m_tracks.tracks()) {
        if (m_actions.canPlay(track)) {
            m_actions.play(track, m_tracks.tracks());
            return;
        }
    }
}

void CatalogPageViewModel::shuffle()
{
    m_actions.shufflePlay(m_tracks.tracks());
}

void CatalogPageViewModel::playNextAll()
{
    // Each goes right after the current song, so the list goes in backwards.
    const QList<core::Track>& tracks = m_tracks.tracks();
    for (auto it = tracks.crbegin(); it != tracks.crend(); ++it) {
        m_actions.playNext(*it);
    }
}

void CatalogPageViewModel::addAllToQueue()
{
    m_actions.addAllToQueue(m_tracks.tracks());
}

void CatalogPageViewModel::prepareSongMenu(int row)
{
    if (row >= 0 && row < m_tracks.count()) {
        m_trackMenu.prepare(m_tracks.trackAt(row), m_tracks.tracks());
    }
}

void CatalogPageViewModel::openCard(int row)
{
    const catalog::CatalogItem* item = m_cards.itemAt(row);
    if (item == nullptr) {
        return;
    }
    if (item->track) {
        m_singleSongs.forget();
        m_actions.play(*item->track, m_cards.tracks());
        return;
    }
    // A single plays as its one song; anything else opens its page.
    if (SingleSongLookup::isCandidate(*item, m_singlesShelf)) {
        const catalog::CatalogItem release = *item;
        (void)m_singleSongs.lookUp(release, [this, release](const std::optional<core::Track>& song) {
            if (song) {
                m_actions.play(*song, {*song});
                return;
            }
            openRelease(release);
        });
        return;
    }
    openRelease(*item);
}

void CatalogPageViewModel::openRelease(const catalog::CatalogItem& item)
{
    if (const std::optional<Route> route = routes::routeForItem(item)) {
        m_navigator.push(*route);
    }
}

bool CatalogPageViewModel::lookUpCardSong(int row)
{
    const catalog::CatalogItem* item = m_cards.itemAt(row);
    if (item == nullptr || !SingleSongLookup::isCandidate(*item, m_singlesShelf)) {
        return false;
    }
    // A click on the same card while this loads takes its answer instead.
    (void)m_singleSongs.lookUp(*item, [this, row](const std::optional<core::Track>& song) {
        if (song) {
            m_trackMenu.prepare(*song, {*song});
        }
        emit cardSongLooked(row, song.has_value());
    });
    return true;
}

void CatalogPageViewModel::openAlbumOf(const core::Track& track)
{
    m_navigator.push(routes::routeFindingAlbum(track));
}

void CatalogPageViewModel::openProviderAlbumOf(const core::Track& track)
{
    core::account::LibraryArtistIdentity identity;
    identity.catalogId = track.albumCatalogId.trimmed();
    identity.name = track.album.trimmed();
    identity.provider = track.sourceTag.trimmed();
    const QString artist = track.artist.trimmed();
    m_navigator.push(routes::routeForAlbumIdentity(identity, artist));
}

} // namespace lmp::ui
