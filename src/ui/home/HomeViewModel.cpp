#include "ui/home/HomeViewModel.h"

#include "ui/share/ShareController.h"

#include <QColor>
#include <chrono>

#include "core/account/AccountSessionService.h"
#include "core/account/MusicSyncService.h"
#include "core/db/LibraryQueries.h"
#include "core/home/HomeLayout.h"
#include "core/mode/ModeService.h"
#include "core/library/CollectionActions.h"
#include "core/library/HomeMixService.h"
#include "core/library/LibraryScanner.h"
#include "core/library/TrackActions.h"
#include "core/model/ArtworkPlaceholder.h"
#include "core/model/CatalogSourceKey.h"
#include "core/model/DisplayTitle.h"
#include "core/playback/PlayerController.h"
#include "ui/library/SystemPlaylists.h"
#include "ui/catalog/CatalogRoutes.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/player/CoverSource.h"
#include "ui/tracks/TrackMenuController.h"

namespace lmp::ui {

namespace {

// A chart row offers "See all" only past this many charts (CHART_SEE_ALL_MIN).
constexpr int ChartSeeAllMin = 4;

constexpr auto kGreetingCheck = std::chrono::minutes(1);

} // namespace

HomeViewModel::HomeViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_library(parts.library)
    , m_actions(parts.actions)
    , m_player(parts.player)
    , m_mixService(parts.mixes)
    , m_trackMenu(parts.trackMenu)
    , m_navigator(parts.navigator)
    , m_now(std::move(parts.now))
    , m_mode(parts.mode)
    , m_sync(parts.sync)
    , m_session(parts.session)
    , m_share(parts.share)
    , m_download(std::move(parts.download))
{
    if (parts.catalog != nullptr && m_mode != nullptr && m_sync != nullptr && m_session != nullptr) {
        m_feed = new HomeCatalogFeed(*parts.catalog, this);
        connect(m_feed, &HomeCatalogFeed::changed, this, [this]() {
            readSections();
            emit catalogChanged();
        });
        connect(m_mode, &core::mode::ModeService::changed, this, [this]() {
            if (!accountHome()) {
                m_feed->reset();
            }
            refresh();
        });
        // A new library may bring a new layout, mixes and shelves.
        connect(m_sync, &core::account::MusicSyncService::libraryChanged, this, &HomeViewModel::readSections);
    }
    connect(&m_mixService, &core::HomeMixService::mixesChanged, this, &HomeViewModel::readMixes);
    connect(&parts.scanner, &core::LibraryScanner::finished, this, &HomeViewModel::refresh);
    connect(&parts.scanner, &core::LibraryScanner::pruned, this, &HomeViewModel::refresh);
    connect(&parts.collections, &core::CollectionActions::playlistsChanged, this, &HomeViewModel::readQuickTiles);
    connect(&m_actions, &core::TrackActions::playlistChanged, this, &HomeViewModel::readQuickTiles);
    connect(&m_actions, &core::TrackActions::likedChanged, this, [this](const QString& sourceKey, bool liked) {
        for (TrackListModel* shelf : {&m_listenAgain, &m_mostPlayed, &m_liked, &m_recentlyAdded}) {
            shelf->setLiked(sourceKey, liked);
        }
    });
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, &HomeViewModel::followPlayer);
    m_greetingTimer.setInterval(kGreetingCheck);
    connect(&m_greetingTimer, &QTimer::timeout, this, &HomeViewModel::refreshGreeting);
    m_greetingTimer.start();
    refreshGreeting();
}

QString HomeViewModel::greetingTimeFor(int hour)
{
    if (hour < 4 || hour >= 22) {
        return QStringLiteral("moon");
    }
    if (hour < 12) {
        return QStringLiteral("sunrise");
    }
    return hour < 17 ? QStringLiteral("sun") : QStringLiteral("sunset");
}

void HomeViewModel::refreshGreeting()
{
    const QString time = greetingTimeFor(m_now().time().hour());
    if (time != m_greetingTime) {
        m_greetingTime = time;
        emit greetingChanged();
    }
}

QString HomeViewModel::salutation() const
{
    if (m_greetingTime == QLatin1StringView("sunrise")) {
        return tr("Good morning");
    }
    if (m_greetingTime == QLatin1StringView("sun")) {
        return tr("Good afternoon");
    }
    return m_greetingTime == QLatin1StringView("sunset") ? tr("Good evening") : tr("Good night");
}

QString HomeViewModel::heroTitle() const
{
    return m_hero ? core::displayTitle(m_hero->title) : QString();
}

QString HomeViewModel::heroArtist() const
{
    return m_hero ? m_hero->artist : QString();
}

QUrl HomeViewModel::heroArtworkUrl() const
{
    return m_hero ? CoverSource::forLibraryTrack(*m_hero).displayUrl() : QUrl();
}

QString HomeViewModel::heroArtworkId() const
{
    return m_hero ? CoverSource::placeholderIdFor(*m_hero) : QString();
}

QString HomeViewModel::heroArtworkLabel() const
{
    return m_hero ? core::ArtworkPlaceholder::tileLabelFor(m_hero->title) : QString();
}

void HomeViewModel::refresh()
{
    core::db::TrackQuery probe;
    probe.limit = 1;
    m_empty = m_library.loadTrackPage(probe).totalCount == 0;

    // The newest play leads; Listen Again holds the rest, so the hero song
    // is not shown twice.
    m_history = m_library.loadHistoryTracks().mid(0, ShelfLength + 1);
    m_hero = m_history.isEmpty() ? std::nullopt : std::optional<core::Track>(m_history.constFirst());
    m_listenAgain.setTracks(m_history.mid(1, ShelfLength));

    m_mostPlayed.setTracks(m_library.loadMostPlayedTracks().mid(0, ShelfLength));
    core::db::TrackQuery liked;
    liked.filter = QStringLiteral("Liked");
    liked.sort = QStringLiteral("DateAdded");
    liked.limit = ShelfLength;
    m_liked.setTracks(m_library.loadTrackPage(liked).tracks);
    m_recentlyAdded.setTracks(m_library.loadRecentlyAddedTracks(ShelfLength));

    m_mixService.refresh();
    readMixes();
    if (accountHome()) {
        m_feed->load();
    }
    readQuickTiles();
    followPlayer();
    readSections();
    emit contentChanged();
}

void HomeViewModel::readMixes()
{
    QList<HomeMixListModel::Card> cards;
    bool anySongs = false;
    for (const core::HomeMix& mix : m_mixService.mixes()) {
        anySongs = anySongs || !mix.tracks.isEmpty();
        cards.append({.id = mix.id,
                      .title = mix.title,
                      .badge = mix.badge,
                      .description = mix.description,
                      .colorStart = QColor::fromRgba(mix.colorStart),
                      .colorEnd = QColor::fromRgba(mix.colorEnd),
                      .empty = mix.tracks.isEmpty()});
    }
    // No songs in any mix: the row is left out (home.ts hasMixes).
    m_mixCards.setCards(anySongs ? std::move(cards) : QList<HomeMixListModel::Card>{});
    readSections();
}

void HomeViewModel::readQuickTiles()
{
    // Shortcuts to the user's own collections (home.ts quickTiles): the
    // system playlists with songs first, then the playlists with songs,
    // cut to whole rows of three; one or two show as they are.
    QList<CollectionCardModel::Card> tiles;
    for (const SystemPlaylist& system : systemPlaylists()) {
        core::db::TrackQuery probe = system.query;
        probe.limit = 1;
        if (m_library.loadTrackPage(probe).totalCount == 0) {
            continue;
        }
        tiles.append({.kind = QStringLiteral("system"),
                      .key = system.key,
                      .title = system.title,
                      .gradientStart = system.gradientStart,
                      .gradientEnd = system.gradientEnd,
                      .iconName = system.iconName});
    }
    for (const core::Track& playlist : m_actions.playlists()) {
        if (tiles.size() >= QuickTileLimit) {
            break;
        }
        if (playlist.trackCount <= 0) {
            continue;
        }
        // One cover, not a collage: at tile size a collage's cells are too small.
        QUrl cover;
        for (const core::Track& song : m_library.loadTracksForGroup(QStringLiteral("playlist"), playlist.sourceUrl)) {
            cover = CoverSource::forLibraryTrack(song).displayUrl();
            if (!cover.isEmpty()) {
                break;
            }
        }
        tiles.append({.kind = QStringLiteral("playlist"),
                      .key = playlist.sourceUrl,
                      .title = playlist.title,
                      .artworkUrl = cover});
    }
    const qsizetype count = std::min<qsizetype>(tiles.size(), QuickTileLimit);
    const qsizetype whole = count < QuickTileColumns ? count : count - (count % QuickTileColumns);
    m_quickTiles.setCards(tiles.mid(0, whole));
}

bool HomeViewModel::accountHome() const
{
    return m_feed != nullptr && m_mode->capabilities().showCatalogueHome;
}

bool HomeViewModel::catalogLoading() const
{
    return m_feed != nullptr && m_feed->loading();
}

QString HomeViewModel::catalogError() const
{
    return m_feed != nullptr ? m_feed->error() : QString();
}

bool HomeViewModel::catalogSkeleton() const
{
    return accountHome() && m_feed->showSkeleton();
}

bool HomeViewModel::catalogCached() const
{
    return m_feed != nullptr && m_feed->showingCached();
}

QString HomeViewModel::regionName() const
{
    return m_feed != nullptr ? m_feed->regionName() : QString();
}

CatalogCardModel* HomeViewModel::catalogShelf(const QString& key)
{
    return m_feed != nullptr ? m_feed->shelf(key) : nullptr;
}

ChartCardModel* HomeViewModel::chartRow(const QString& key)
{
    return m_feed != nullptr ? m_feed->charts(key) : nullptr;
}

void HomeViewModel::playCatalogCard(const QString& key, int row)
{
    CatalogCardModel* shelf = catalogShelf(key);
    const core::catalog::CatalogItem* item = shelf != nullptr ? shelf->itemAt(row) : nullptr;
    if (item == nullptr) {
        return;
    }
    if (item->track) {
        m_actions.play(*item->track, shelf->tracks());
        return;
    }
    if (const std::optional<Route> route = catalog_routes::routeForItem(*item)) {
        m_navigator.push(*route);
    }
}

void HomeViewModel::openChart(const QString& key, int row)
{
    ChartCardModel* charts = chartRow(key);
    const core::catalog::ChartDescriptor* chart = charts != nullptr ? charts->chartAt(row) : nullptr;
    if (chart != nullptr) {
        m_navigator.push(catalog_routes::routeForChart(chart->ref));
    }
}

void HomeViewModel::retryCatalog()
{
    if (accountHome()) {
        m_feed->load(true);
    }
}

void HomeViewModel::leave()
{
    if (m_feed != nullptr) {
        m_feed->pause();
    }
}

void HomeViewModel::readAccountSections()
{
    using Section = HomeSectionListModel::Section;
    const QString local = QStringLiteral("local");
    const QString catalogue = QStringLiteral("catalogue");
    const std::optional<core::account::AccountOperationContext> context = m_session->captureOperation();
    // The service's layout for this account, else the built-in one.
    const core::home::HomeLayoutView layout(
        core::home::normalizeHomeLayout(m_sync->homeLayout(context ? context->ownerId : QString())));
    static const QHash<QString, QString> tierLabels{{QStringLiteral("you"), tr("For you")},
                                                    {QStringLiteral("new"), tr("New and trending")},
                                                    {QStringLiteral("library"), tr("From your library")},
                                                    {QStringLiteral("explore"), tr("Explore")}};
    QList<Section> sections;
    if (m_hero || m_quickTiles.count() > 0) {
        sections.append({.key = QStringLiteral("hero"), .title = {}, .tier = {}, .capability = local, .kind = QStringLiteral("hero")});
    }
    const QStringList topKeys{QStringLiteral("topSongs"), QStringLiteral("topAlbums"), QStringLiteral("topPlaylists")};
    const QHash<QString, QString> topShelfIds{{QStringLiteral("topSongs"), QStringLiteral("catalog:top-songs")},
                                              {QStringLiteral("topAlbums"), QStringLiteral("catalog:top-albums")},
                                              {QStringLiteral("topPlaylists"), QStringLiteral("catalog:top-playlists")}};
    // With no mixes and nothing played, For you is a welcome card instead of
    // two empty shelves, whatever its shelves are set to.
    const bool newListener = m_mixCards.count() == 0 && !m_hero;
    for (const core::home::HomeLayoutTier& tier : layout.tiers()) {
        QString label = layout.tierTitle(tier.key, tierLabels.value(tier.key));
        const auto add = [&sections, &label](Section section) {
            section.tier = label;
            label.clear();
            sections.append(std::move(section));
        };
        if (tier.key == QLatin1StringView("you") && newListener) {
            add({.key = QStringLiteral("welcome"), .title = {}, .tier = {}, .capability = local,
                 .kind = QStringLiteral("welcome")});
            continue;
        }
        QStringList tierTops;
        for (const core::home::HomeLayoutShelf& shelf : tier.shelves) {
            if (topKeys.contains(shelf.key)) {
                tierTops.append(shelf.key);
            }
        }
        for (const core::home::HomeLayoutShelf& shelf : tier.shelves) {
            const QString& key = shelf.key;
            // The catalogue's status stands where the first Top shelf does,
            // shown or hidden, so it never goes away with a hidden shelf.
            if (!tierTops.isEmpty() && key == tierTops.first()
                && (m_feed->showSkeleton() || !m_feed->error().isEmpty() || m_feed->showingCached())) {
                add({.key = QStringLiteral("catalogStatus"), .title = {}, .tier = {}, .capability = catalogue,
                     .kind = QStringLiteral("status")});
            }
            if (shelf.enabled) {
                if (key == QLatin1StringView("mixes") && m_mixCards.count() > 0) {
                    add({.key = key, .title = layout.shelfTitle(key, tr("Your mixes")), .tier = {}, .capability = local,
                         .kind = QStringLiteral("mixes"), .link = true});
                } else if (key == QLatin1StringView("listenAgain") && m_listenAgain.count() > 0) {
                    add({.key = key, .title = layout.shelfTitle(key, tr("Listen Again")), .tier = {}, .capability = local,
                         .kind = QStringLiteral("songs"), .link = true});
                } else if (key == QLatin1StringView("mostPlayed") && m_mostPlayed.count() >= MostPlayedMinimum) {
                    add({.key = key, .title = layout.shelfTitle(key, tr("Most Played")), .tier = {}, .capability = local,
                         .kind = QStringLiteral("songs"), .link = true});
                } else if (key == QLatin1StringView("liked") && m_liked.count() > 0) {
                    add({.key = key, .title = layout.shelfTitle(key, tr("Your Liked Songs")), .tier = {},
                         .capability = local, .kind = QStringLiteral("songs"), .link = true});
                } else if (key == QLatin1StringView("recentlyAdded") && m_recentlyAdded.count() > 0) {
                    add({.key = key, .title = layout.shelfTitle(key, tr("Recently Added")), .tier = {},
                         .capability = local, .kind = QStringLiteral("songs"), .link = true});
                } else if (topShelfIds.contains(key)) {
                    const QString id = topShelfIds.value(key);
                    if (m_feed->shelf(id)->count() > 0) {
                        add({.key = id, .title = layout.shelfTitleOverride(key).value_or(m_feed->shelfTitle(id)),
                             .tier = {}, .capability = catalogue, .kind = QStringLiteral("catalog"),
                             .link = m_feed->shelfHasChart(id)});
                    }
                } else if (key == QLatin1StringView("genreCharts") || key == QLatin1StringView("cityCharts")) {
                    if (m_feed->charts(key)->count() > 0) {
                        add({.key = key, .title = layout.shelfTitleOverride(key).value_or(m_feed->chartsTitle(key)),
                             .tier = {}, .capability = catalogue, .kind = QStringLiteral("charts"),
                             // CHART_SEE_ALL_MIN: "See all" once a row holds more than 4.
                             .link = m_feed->charts(key)->count() > ChartSeeAllMin});
                    }
                } else if (m_feed->shelf(key)->count() > 0) {
                    // newSongs, newAlbums, biggestHits, popularArtists, moods.
                    add({.key = key, .title = layout.shelfTitle(key, m_feed->shelfTitle(key)), .tier = {},
                         .capability = catalogue, .kind = QStringLiteral("catalog"), .link = true});
                }
            }
            // Any other shelf the service sends follows the last Top shelf.
            if (!tierTops.isEmpty() && key == tierTops.last()) {
                for (const QString& id : m_feed->discoveryShelfKeys()) {
                    if (topShelfIds.values().contains(id) || m_feed->shelf(id)->count() == 0) {
                        continue;
                    }
                    add({.key = id, .title = m_feed->shelfTitle(id), .tier = {}, .capability = catalogue,
                         .kind = QStringLiteral("catalog"), .link = m_feed->shelfHasChart(id)});
                }
            }
        }
    }
    m_sections.setSections(std::move(sections));
}

void HomeViewModel::readSections()
{
    using Section = HomeSectionListModel::Section;
    if (accountHome()) {
        readAccountSections();
        return;
    }
    QList<Section> sections;
    const QString local = QStringLiteral("local");
    if (m_empty) {
        m_sections.setSections({});
        return;
    }
    if (m_hero || m_quickTiles.count() > 0) {
        sections.append({.key = QStringLiteral("hero"), .title = {}, .tier = {}, .capability = local, .kind = QStringLiteral("hero")});
    }
    // For you: the mixes and the rest of the history.
    QString tier = tr("For you");
    if (m_mixCards.count() > 0) {
        sections.append({.key = QStringLiteral("mixes"), .title = tr("Your mixes"), .tier = tier, .capability = local,
                         .kind = QStringLiteral("mixes"), .link = true});
        tier.clear();
    }
    if (m_listenAgain.count() > 0) {
        sections.append(
            {.key = QStringLiteral("listenAgain"), .title = tr("Listen Again"), .tier = tier, .capability = local,
             .kind = QStringLiteral("songs"), .link = true});
    }
    // From your library.
    tier = tr("From your library");
    if (m_mostPlayed.count() >= MostPlayedMinimum) {
        sections.append(
            {.key = QStringLiteral("mostPlayed"), .title = tr("Most Played"), .tier = tier, .capability = local,
             .kind = QStringLiteral("songs"), .link = true});
        tier.clear();
    }
    if (m_liked.count() > 0) {
        sections.append(
            {.key = QStringLiteral("liked"), .title = tr("Your Liked Songs"), .tier = tier, .capability = local,
             .kind = QStringLiteral("songs"), .link = true});
        tier.clear();
    }
    if (m_recentlyAdded.count() > 0) {
        sections.append(
            {.key = QStringLiteral("recentlyAdded"), .title = tr("Recently Added"), .tier = tier, .capability = local,
             .kind = QStringLiteral("songs"), .link = true});
    }
    m_sections.setSections(std::move(sections));
}

void HomeViewModel::followPlayer()
{
    const std::optional<core::Track>& current = m_player.currentTrack();
    const QString key = current ? core::catalogSourceKey(*current) : QString();
    for (TrackListModel* shelf : {&m_listenAgain, &m_mostPlayed, &m_liked, &m_recentlyAdded}) {
        shelf->setPlayingKey(key);
    }
}

TrackListModel* HomeViewModel::shelfNamed(const QString& shelf)
{
    if (shelf == QLatin1StringView("listenAgain")) {
        return &m_listenAgain;
    }
    if (shelf == QLatin1StringView("mostPlayed")) {
        return &m_mostPlayed;
    }
    if (shelf == QLatin1StringView("liked")) {
        return &m_liked;
    }
    if (shelf == QLatin1StringView("recentlyAdded")) {
        return &m_recentlyAdded;
    }
    return nullptr;
}

void HomeViewModel::playHero()
{
    if (m_hero) {
        // The whole history is the context, as Listen Again plays.
        m_actions.play(*m_hero, m_history);
    }
}

void HomeViewModel::startHeroRadio()
{
    if (m_hero) {
        m_actions.startMix(*m_hero);
    }
}

void HomeViewModel::prepareHeroMenu()
{
    if (m_hero) {
        m_trackMenu.prepare(*m_hero, m_history);
    }
}

void HomeViewModel::playSong(const QString& shelf, int row)
{
    TrackListModel* model = shelfNamed(shelf);
    if (model != nullptr && row >= 0 && row < model->count()) {
        m_actions.play(model->trackAt(row), model->tracks());
    }
}

void HomeViewModel::prepareSongMenu(const QString& shelf, int row)
{
    TrackListModel* model = shelfNamed(shelf);
    if (model != nullptr && row >= 0 && row < model->count()) {
        m_trackMenu.prepare(model->trackAt(row), model->tracks());
    }
}

void HomeViewModel::openCollection(const QString& kind, const QString& key, const QString& title)
{
    m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), kind},
                                                     {QStringLiteral("key"), key},
                                                     {QStringLiteral("title"), title}}));
}

void HomeViewModel::seeAll(const QString& shelf)
{
    // home.ts bindHome's "See all": the matching playlist page, or Library.
    if (shelf == QLatin1StringView("mixes")) {
        m_navigator.push(Route(Route::Kind::Library, {{QStringLiteral("tab"), QStringLiteral("playlists")}}));
    } else if (shelf == QLatin1StringView("listenAgain")) {
        m_navigator.push(Route(Route::Kind::Library, {{QStringLiteral("tab"), QStringLiteral("history")}}));
    } else if (shelf == QLatin1StringView("liked")) {
        openCollection(QStringLiteral("system"), QStringLiteral("smart-liked"), {});
    } else if (shelf == QLatin1StringView("mostPlayed")) {
        openCollection(QStringLiteral("system"), QStringLiteral("smart-most"), {});
    } else if (shelf == QLatin1StringView("recentlyAdded")) {
        openCollection(QStringLiteral("system"), QStringLiteral("smart-recent"), {});
    } else if (shelf == QLatin1StringView("genreCharts") || shelf == QLatin1StringView("cityCharts")) {
        // Every chart of the row: the Charts page lists them (P08 later
        // parts); until then the first chart opens.
        openChart(shelf, 0);
    } else if (m_feed != nullptr) {
        m_navigator.push(catalog_routes::routeForShelf(shelf));
    }
}

QList<core::Track> HomeViewModel::tracksOf(const QString& kind, const QString& key)
{
    if (kind == QLatin1StringView("mix")) {
        const std::optional<core::HomeMix> mix = m_mixService.mix(key);
        return mix ? mix->tracks : QList<core::Track>{};
    }
    if (kind == QLatin1StringView("system")) {
        const std::optional<SystemPlaylist> system = systemPlaylist(key);
        return system ? m_library.loadTracksForQuery(system->query) : QList<core::Track>{};
    }
    return m_library.loadTracksForGroup(QStringLiteral("playlist"), key);
}

void HomeViewModel::playMix(int row)
{
    const QList<core::Track> tracks = tracksOf(QStringLiteral("mix"), m_mixCards.idAt(row));
    for (const core::Track& track : tracks) {
        if (m_actions.canPlay(track)) {
            m_actions.play(track, tracks);
            return;
        }
    }
}

void HomeViewModel::playQuickTile(int row)
{
    if (row < 0 || row >= m_quickTiles.count()) {
        return;
    }
    const CollectionCardModel::Card tile = m_quickTiles.cardAt(row);
    const QList<core::Track> tracks = tracksOf(tile.kind, tile.key);
    for (const core::Track& track : tracks) {
        if (m_actions.canPlay(track)) {
            m_actions.play(track, tracks);
            return;
        }
    }
}

void HomeViewModel::prepareMixMenu(int row)
{
    const QString id = m_mixCards.idAt(row);
    if (id.isEmpty()) {
        return;
    }
    m_menuKind = QStringLiteral("mix");
    m_menuKey = id;
    m_menuTitle = core::HomeMixBuilder::blank(id).title;
    // home.ts openMixMenu: Play now, Play next, Add to queue, Shuffle,
    // Download, then Share.
    const bool songs = !tracksOf(m_menuKind, id).isEmpty();
    QList<MenuItemListModel::Item> items{
        {.id = QStringLiteral("play"), .label = tr("Play now"), .iconName = QStringLiteral("play"), .enabled = songs},
        {.id = QStringLiteral("next"), .label = tr("Play next"), .iconName = QStringLiteral("play-next"), .enabled = songs},
        {.id = QStringLiteral("queue"), .label = tr("Add to queue"), .iconName = QStringLiteral("queue-add"), .enabled = songs},
        {.id = QStringLiteral("shuffle"), .label = tr("Shuffle"), .iconName = QStringLiteral("shuffle"), .enabled = songs}};
    appendCollectionItems(items, songs);
    m_cardMenu.setItems(std::move(items));
}

void HomeViewModel::prepareQuickTileMenu(int row)
{
    if (row < 0 || row >= m_quickTiles.count()) {
        return;
    }
    const CollectionCardModel::Card tile = m_quickTiles.cardAt(row);
    m_menuKind = tile.kind;
    m_menuKey = tile.key;
    m_menuTitle = tile.title;
    // home.ts openQuickTileMenu: Open playlist, then Play now, Shuffle, Play
    // next, Add to queue, Download, then Share.
    const bool songs = !tracksOf(m_menuKind, m_menuKey).isEmpty();
    QList<MenuItemListModel::Item> items{
        {.id = QStringLiteral("open"), .label = tr("Open playlist"), .iconName = QStringLiteral("open")},
        {.id = QStringLiteral("play"), .label = tr("Play now"), .iconName = QStringLiteral("play"), .enabled = songs,
         .separatorBefore = true},
        {.id = QStringLiteral("shuffle"), .label = tr("Shuffle"), .iconName = QStringLiteral("shuffle"), .enabled = songs},
        {.id = QStringLiteral("next"), .label = tr("Play next"), .iconName = QStringLiteral("play-next"), .enabled = songs},
        {.id = QStringLiteral("queue"), .label = tr("Add to queue"), .iconName = QStringLiteral("queue-add"), .enabled = songs}};
    appendCollectionItems(items, songs);
    m_cardMenu.setItems(std::move(items));
}

void HomeViewModel::appendCollectionItems(QList<MenuItemListModel::Item>& items, bool songs) const
{
    // collectionDownloadAction: Download, in its own group, where songs can
    // be kept on this PC; then Share in a group of its own.
    if (m_download && songs && m_mode != nullptr && m_mode->capabilities().downloadSongs) {
        items.append({.id = QStringLiteral("download"), .label = tr("Download"), .iconName = QStringLiteral("download"),
                      .separatorBefore = true});
    }
    if (m_share != nullptr && m_share->available()) {
        items.append({.id = QStringLiteral("share"), .label = tr("Share"), .iconName = QStringLiteral("share"),
                      .enabled = songs, .separatorBefore = true});
    }
}

void HomeViewModel::triggerCardMenu(const QString& itemId)
{
    if (m_menuKey.isEmpty()) {
        return;
    }
    const QList<core::Track> tracks = tracksOf(m_menuKind, m_menuKey);
    if (itemId == QLatin1StringView("open")) {
        openCollection(m_menuKind, m_menuKey, m_menuTitle);
    } else if (itemId == QLatin1StringView("play")) {
        for (const core::Track& track : tracks) {
            if (m_actions.canPlay(track)) {
                m_actions.play(track, tracks);
                break;
            }
        }
    } else if (itemId == QLatin1StringView("shuffle")) {
        m_actions.shufflePlay(tracks);
    } else if (itemId == QLatin1StringView("next")) {
        // Each goes right after the current song, so the list goes in backwards.
        for (auto it = tracks.crbegin(); it != tracks.crend(); ++it) {
            m_actions.playNext(*it);
        }
    } else if (itemId == QLatin1StringView("queue")) {
        m_actions.addAllToQueue(tracks);
    } else if (itemId == QLatin1StringView("download") && m_download && !tracks.isEmpty()) {
        (void)m_download(m_menuTitle, {}, tracks.first().artworkUrl, tracks);
    } else if (itemId == QLatin1StringView("share") && m_share != nullptr && !tracks.isEmpty()) {
        m_share->share({.kind = core::share::ShareKind::List,
                        .title = m_menuTitle,
                        .artworkUrl = tracks.first().artworkUrl,
                        .tracks = tracks},
                       tr("Could not share this playlist."));
    }
}

} // namespace lmp::ui
