#include "ui/library/LibraryViewModel.h"

#include "ui/share/ShareController.h"

#include "core/mode/ModeService.h"

#include <QColor>
#include <QCollator>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <memory>

#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/library/CollectionActions.h"
#include "core/library/HomeMixService.h"
#include "core/library/LibraryScanner.h"
#include "core/library/TrackActions.h"
#include "core/model/CatalogSourceKey.h"
#include "core/playback/PlayerController.h"
#include "ui/library/SystemPlaylists.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/player/CoverSource.h"
#include "ui/tracks/QueryTrackSource.h"
#include "ui/tracks/TrackMenuController.h"

namespace lmp::ui {

namespace {

const QStringList kTabs{QStringLiteral("playlists"), QStringLiteral("history"), QStringLiteral("albums"),
                        QStringLiteral("artists"),   QStringLiteral("genres"),  QStringLiteral("songs")};

// Songs' sorts, as the current app offered them, and their query names.
const QStringList kSongSortKeys{QStringLiteral("DateAdded"), QStringLiteral("Title"), QStringLiteral("Artist"),
                                QStringLiteral("Duration"), QStringLiteral("MostPlayed")};

QString countLabel(int count)
{
    return count == 1 ? LibraryViewModel::tr("1 song") : LibraryViewModel::tr("%1 songs").arg(count);
}

// A card's cover by the same rules as a song's (CoverSource), so remote
// pictures and account cover links load the way the mode needs.
QUrl coverFor(const QString& artworkUrl)
{
    if (artworkUrl.isEmpty()) {
        return {};
    }
    core::Track cover;
    cover.artworkUrl = artworkUrl;
    cover.sourceKind = QStringLiteral("remote");
    return CoverSource::forTrack(cover).displayUrl();
}

} // namespace

LibraryViewModel::LibraryViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_library(parts.library)
    , m_groups(parts.groups)
    , m_actions(parts.actions)
    , m_mixes(parts.mixes)
    , m_player(parts.player)
    , m_trackMenu(parts.trackMenu)
    , m_navigator(parts.navigator)
    , m_mode(parts.mode)
    , m_share(parts.share)
    , m_download(std::move(parts.download))
{
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, &LibraryViewModel::followPlayer);
    connect(&m_actions, &core::TrackActions::likedChanged, this, &LibraryViewModel::onLiked);
    connect(&m_actions, &core::TrackActions::playlistsChanged, this, &LibraryViewModel::readPlaylists);
    connect(&m_actions, &core::TrackActions::playlistChanged, this, &LibraryViewModel::readPlaylists);
    connect(&parts.collections, &core::CollectionActions::playlistsChanged, this, &LibraryViewModel::readPlaylists);
    connect(&m_mixes, &core::HomeMixService::mixesChanged, this, &LibraryViewModel::readPlaylists);
    connect(&parts.collections, &core::CollectionActions::albumsChanged, this, &LibraryViewModel::readGroups);
    connect(&parts.scanner, &core::LibraryScanner::finished, this, &LibraryViewModel::refresh);
    connect(&parts.scanner, &core::LibraryScanner::pruned, this, &LibraryViewModel::refresh);
    connect(&m_navigator, &Navigator::currentChanged, this, &LibraryViewModel::followNavigator);
    // A song menu's Go to album and Go to artist open the library's pages.
    connect(&m_actions, &core::TrackActions::albumRequested, this, [this](const core::Track& track) {
        // Online albums open from the catalogue or the provider (CompositionRoot).
        if (m_actions.albumPageSource(track) != core::TrackActions::PageSource::Library) {
            return;
        }
        const QString album = track.album.isEmpty() ? tr("Unknown Album") : track.album;
        m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), QStringLiteral("album")},
                                                         {QStringLiteral("key"), album},
                                                         {QStringLiteral("title"), album}}));
    });
    connect(&m_actions, &core::TrackActions::artistRequested, this, [this](const core::Track& track) {
        // Online artists open from the catalogue or the provider (CompositionRoot).
        if (m_actions.artistPageSource(track) != core::TrackActions::PageSource::Library) {
            return;
        }
        const QString artist = track.artist.isEmpty() ? tr("Unknown Artist") : track.artist;
        m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), QStringLiteral("artist")},
                                                         {QStringLiteral("key"), artist},
                                                         {QStringLiteral("title"), artist}}));
    });
}

void LibraryViewModel::followNavigator()
{
    if (m_navigator.current().kind() != Route::Kind::Library) {
        return;
    }
    // Each history entry keeps its own tab.
    const QString tab = m_navigator.current().param(QStringLiteral("tab"));
    const QString wanted = kTabs.contains(tab) ? tab : QStringLiteral("playlists");
    if (wanted != m_tab) {
        m_tab = wanted;
        emit tabChanged();
    }
    // openAutoMixes: Home's "See all" on Your mixes opens the Auto mixes.
    const QString filter = m_navigator.current().param(QStringLiteral("filter"));
    if (!filter.isEmpty() && filter != m_playlistFilter) {
        m_playlistFilter = filter == QLatin1StringView("auto") ? QStringLiteral("auto") : QStringLiteral("manual");
        emit playlistFilterChanged();
    }
    refresh();
}

void LibraryViewModel::setTab(const QString& tab)
{
    if (!kTabs.contains(tab) || tab == m_tab) {
        return;
    }
    m_tab = tab;
    m_navigator.setCurrentParam(QStringLiteral("tab"), tab);
    emit tabChanged();
}

QVariantList LibraryViewModel::tabs() const
{
    const QStringList labels{tr("Playlists"), tr("History"), tr("Albums"), tr("Artists"), tr("Genres"), tr("Songs")};
    QVariantList list;
    for (qsizetype i = 0; i < kTabs.size(); ++i) {
        list.append(QVariantMap{{QStringLiteral("value"), kTabs.at(i)}, {QStringLiteral("label"), labels.at(i)}});
    }
    return list;
}

QStringList LibraryViewModel::historySorts() const
{
    return {tr("Relevance"), tr("Title"), tr("Artist"), tr("Duration")};
}

QStringList LibraryViewModel::songsSorts() const
{
    return {tr("Date added"), tr("Title"), tr("Artist"), tr("Duration"), tr("Most played")};
}

void LibraryViewModel::setHistorySort(int sort)
{
    if (sort < 0 || sort >= historySorts().size() || sort == m_historySort) {
        return;
    }
    m_historySort = sort;
    emit historySortChanged();
    readHistory();
}

void LibraryViewModel::setSongsSort(int sort)
{
    if (sort < 0 || sort >= kSongSortKeys.size() || sort == m_songsSort) {
        return;
    }
    m_songsSort = sort;
    emit songsChanged();
    readSongs();
}

void LibraryViewModel::setSongsFilter(const QString& filter)
{
    if (filter == m_songsFilter) {
        return;
    }
    m_songsFilter = filter;
    emit songsChanged();
    readSongs();
}

void LibraryViewModel::refresh()
{
    // Today's mixes (made again once a day).
    m_mixes.refresh();
    readPlaylists();
    readGroups();
    readHistory();
    readSongs();
    const bool empty = m_songs.totalCount() == 0 && m_songsFilter.isEmpty();
    if (empty != m_empty) {
        m_empty = empty;
    }
    emit contentChanged();
}

void LibraryViewModel::setPlaylistFilter(const QString& filter)
{
    const QString chosen = filter == QLatin1StringView("auto") ? QStringLiteral("auto") : QStringLiteral("manual");
    if (chosen == m_playlistFilter) {
        return;
    }
    m_playlistFilter = chosen;
    // Each history entry keeps its own filter, as it keeps its tab.
    if (m_navigator.current().kind() == Route::Kind::Library) {
        m_navigator.setCurrentParam(QStringLiteral("filter"), chosen);
    }
    emit playlistFilterChanged();
    readPlaylists();
}

void LibraryViewModel::readPlaylists()
{
    QList<CollectionCardModel::Card> cards;
    if (m_playlistFilter == QLatin1StringView("auto")) {
        for (const core::HomeMix& mix : m_mixes.mixes()) {
            if (mix.tracks.isEmpty()) {
                continue;
            }
            cards.append({.kind = QStringLiteral("mix"),
                          .key = mix.id,
                          .title = mix.title,
                          .subtitle = tr("%1 · Auto mix").arg(countLabel(static_cast<int>(mix.tracks.size()))),
                          .gradientStart = QColor::fromRgba(mix.colorStart),
                          .gradientEnd = QColor::fromRgba(mix.colorEnd),
                          .iconName = QStringLiteral("sparkle"),
                          .badge = mix.badge});
        }
        m_playlists.setCards(std::move(cards));
        return;
    }
    for (const SystemPlaylist& system : systemPlaylists()) {
        core::db::TrackQuery probe = system.query;
        probe.limit = 1;
        const int count = m_library.loadTrackPage(probe).totalCount;
        // Most Played and Recently Added fill themselves, so an empty one is
        // only a dead card; Favourites stays, since its page says how to fill it.
        if (count == 0 && system.key != FavouritesKey) {
            continue;
        }
        cards.append({.kind = QStringLiteral("system"),
                      .key = system.key,
                      .title = system.title,
                      .subtitle = countLabel(count),
                      .gradientStart = system.gradientStart,
                      .gradientEnd = system.gradientEnd,
                      .iconName = system.iconName});
    }
    for (const core::Track& playlist : m_actions.playlists()) {
        cards.append({.kind = QStringLiteral("playlist"),
                      .key = playlist.sourceUrl,
                      .title = playlist.title,
                      .subtitle = countLabel(playlist.trackCount),
                      .artworkUrl = coverFor(playlist.artworkUrl)});
    }
    m_playlists.setCards(std::move(cards));
}

void LibraryViewModel::readGroups()
{
    QList<CollectionCardModel::Card> albums;
    for (const core::Track& album : m_groups.loadAlbums()) {
        // An album the user made opens as one ("album-collection"), with its own order.
        albums.append({.kind = album.sourceKind.isEmpty() ? QStringLiteral("album") : album.sourceKind,
                       .key = album.sourceUrl.isEmpty() ? album.title : album.sourceUrl,
                       .title = album.title,
                       .subtitle = album.artist.isEmpty() ? countLabel(album.trackCount) : album.artist,
                       .artworkUrl = coverFor(album.artworkUrl)});
    }
    m_albums.setCards(std::move(albums));

    QList<CollectionCardModel::Card> artists;
    for (const core::Track& artist : m_groups.loadArtists()) {
        artists.append({.kind = QStringLiteral("artist"),
                        .key = artist.sourceUrl.isEmpty() ? artist.title : artist.sourceUrl,
                        .title = artist.title,
                        .subtitle = artist.artist,
                        .artworkUrl = coverFor(artist.artworkUrl)});
    }
    m_artists.setCards(std::move(artists));

    QList<CollectionCardModel::Card> genres;
    for (const core::Track& genre : m_groups.loadGenres()) {
        genres.append({.kind = QStringLiteral("genre"),
                       .key = genre.sourceUrl.isEmpty() ? genre.title : genre.sourceUrl,
                       .title = genre.title,
                       .subtitle = countLabel(genre.trackCount)});
    }
    m_genres.setCards(std::move(genres));
}

void LibraryViewModel::readHistory()
{
    QList<core::Track> tracks = m_library.loadHistoryTracks();
    if (m_historySort != 0) {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        const int sort = m_historySort;
        std::stable_sort(tracks.begin(), tracks.end(), [&collator, sort](const core::Track& a, const core::Track& b) {
            if (sort == 3) {
                return a.durationSeconds < b.durationSeconds;
            }
            const QString& left = sort == 2 ? a.artist : a.title;
            const QString& right = sort == 2 ? b.artist : b.title;
            return collator.compare(left, right) < 0;
        });
    }
    m_history.setTracks(std::move(tracks));
    followPlayer();
}

void LibraryViewModel::readSongs()
{
    core::db::TrackQuery query;
    query.sort = kSongSortKeys.value(m_songsSort, QStringLiteral("DateAdded"));
    query.searchText = m_songsFilter.trimmed();
    // Local files only, as the current app's Songs tab: it answers "what is
    // on this disk"; streamed songs reach the library through the other tabs.
    query.includeRemote = false;
    m_songs.setSource(std::make_shared<QueryTrackSource>(m_library, query));
    followPlayer();
}

TrackListModel* LibraryViewModel::listNamed(const QString& list)
{
    if (list == QLatin1StringView("history")) {
        return &m_history;
    }
    if (list == QLatin1StringView("songs")) {
        return &m_songs;
    }
    return nullptr;
}

void LibraryViewModel::openCard(const QString& tab, int row)
{
    CollectionCardModel* model = tab == QLatin1StringView("playlists") ? &m_playlists
        : tab == QLatin1StringView("albums")                         ? &m_albums
        : tab == QLatin1StringView("artists")                        ? &m_artists
        : tab == QLatin1StringView("genres")                         ? &m_genres
                                                                     : nullptr;
    if (model == nullptr || row < 0 || row >= model->count()) {
        return;
    }
    const CollectionCardModel::Card card = model->cardAt(row);
    m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), card.kind},
                                                     {QStringLiteral("key"), card.key},
                                                     {QStringLiteral("title"), card.title}}));
}

bool LibraryViewModel::prepareCardMenu(const QString& tab, int row)
{
    CollectionCardModel* model = tab == QLatin1StringView("playlists") ? &m_playlists
        : tab == QLatin1StringView("albums")                         ? &m_albums
        : tab == QLatin1StringView("genres")                         ? &m_genres
                                                                     : nullptr;
    if (model == nullptr || row < 0 || row >= model->count()) {
        return false;
    }
    const CollectionCardModel::Card card = model->cardAt(row);
    m_menuKind = card.kind;
    m_menuKey = card.key;
    m_menuTitle = card.title;
    const bool songs = !tracksOf(card.kind, card.key).isEmpty();
    QList<MenuItemListModel::Item> items{
        {.id = QStringLiteral("play"), .label = tr("Play now"), .iconName = QStringLiteral("play"), .enabled = songs},
        {.id = QStringLiteral("next"), .label = tr("Play next"), .iconName = QStringLiteral("play-next"), .enabled = songs},
        {.id = QStringLiteral("queue"), .label = tr("Add to queue"), .iconName = QStringLiteral("queue-add"), .enabled = songs},
        {.id = QStringLiteral("shuffle"), .label = tr("Shuffle"), .iconName = QStringLiteral("shuffle"), .enabled = songs}};
    // collectionDownloadAction: Download in its own group where songs can
    // be kept on this PC; then Share in a group of its own.
    if (m_download && songs && m_mode != nullptr && m_mode->capabilities().downloadSongs) {
        items.append({.id = QStringLiteral("download"), .label = tr("Download"), .iconName = QStringLiteral("download"),
                      .separatorBefore = true});
    }
    if (m_share != nullptr && m_share->available()) {
        items.append({.id = QStringLiteral("share"), .label = tr("Share"), .iconName = QStringLiteral("share"),
                      .enabled = songs, .separatorBefore = true});
    }
    m_cardMenu.setItems(std::move(items));
    return true;
}

bool LibraryViewModel::prepareArtistMenu(const QString& name, bool followed)
{
    if (name.trimmed().isEmpty()) {
        return false;
    }
    m_menuKind = QStringLiteral("artist");
    m_menuKey = name;
    m_menuTitle = name;
    const bool songs = !tracksOf(m_menuKind, m_menuKey).isEmpty();
    QList<MenuItemListModel::Item> items{
        {.id = QStringLiteral("play"), .label = tr("Play saved tracks"), .iconName = QStringLiteral("play"), .enabled = songs},
        {.id = QStringLiteral("next"), .label = tr("Play saved tracks next"), .iconName = QStringLiteral("play-next"),
         .enabled = songs},
        {.id = QStringLiteral("queue"), .label = tr("Add saved tracks to queue"), .iconName = QStringLiteral("queue-add"),
         .enabled = songs},
        {.id = QStringLiteral("shuffle"), .label = tr("Shuffle saved tracks"), .iconName = QStringLiteral("shuffle"),
         .enabled = songs}};
    if (m_download && songs && m_mode != nullptr && m_mode->capabilities().downloadSongs) {
        items.append({.id = QStringLiteral("download"), .label = tr("Download saved tracks"),
                      .iconName = QStringLiteral("download")});
    }
    items.append(followed ? MenuItemListModel::Item{.id = QStringLiteral("follow"),
                                                    .label = tr("Unfollow artist"),
                                                    .iconName = QStringLiteral("person-minus"),
                                                    .danger = true,
                                                    .separatorBefore = true}
                          : MenuItemListModel::Item{.id = QStringLiteral("follow"),
                                                    .label = tr("Follow artist"),
                                                    .iconName = QStringLiteral("person-plus"),
                                                    .separatorBefore = true});
    items.append({.id = QStringLiteral("details"), .label = tr("View artist"), .iconName = QStringLiteral("person")});
    m_cardMenu.setItems(std::move(items));
    return true;
}

void LibraryViewModel::triggerCardMenu(const QString& itemId)
{
    if (m_menuKey.isEmpty()) {
        return;
    }
    const QList<core::Track> tracks = tracksOf(m_menuKind, m_menuKey);
    if (tracks.isEmpty()) {
        return;
    }
    if (itemId == QLatin1StringView("play")) {
        for (const core::Track& track : tracks) {
            if (m_actions.canPlay(track)) {
                m_actions.play(track, tracks);
                break;
            }
        }
    } else if (itemId == QLatin1StringView("next")) {
        // Each goes right after the current song, so the list goes in backwards.
        for (auto it = tracks.crbegin(); it != tracks.crend(); ++it) {
            m_actions.playNext(*it);
        }
    } else if (itemId == QLatin1StringView("queue")) {
        m_actions.addAllToQueue(tracks);
    } else if (itemId == QLatin1StringView("shuffle")) {
        m_actions.shufflePlay(tracks);
    } else if (itemId == QLatin1StringView("download") && m_download) {
        (void)m_download(m_menuTitle, {}, tracks.first().artworkUrl, tracks);
    } else if (itemId == QLatin1StringView("share") && m_share != nullptr) {
        m_share->share({.kind = core::share::ShareKind::List,
                        .title = m_menuTitle,
                        .artworkUrl = tracks.first().artworkUrl,
                        .tracks = tracks},
                       tr("Could not share this playlist."));
    }
}

QList<core::Track> LibraryViewModel::tracksOf(const QString& kind, const QString& key)
{
    if (kind == QLatin1StringView("mix")) {
        const std::optional<core::HomeMix> mix = m_mixes.mix(key);
        return mix ? mix->tracks : QList<core::Track>{};
    }
    if (kind == QLatin1StringView("system")) {
        const std::optional<SystemPlaylist> system = systemPlaylist(key);
        return system ? m_library.loadTracksForQuery(system->query) : QList<core::Track>{};
    }
    if (kind == QLatin1StringView("artist")) {
        return m_library.loadTracksForGroup(kind, key);
    }
    // Playlists, albums and genres: the songs their page lists, in its order.
    core::db::TrackQuery query;
    query.groupKind = kind;
    query.groupKey = key;
    return m_library.loadTracksForQuery(query);
}

void LibraryViewModel::playSong(const QString& list, int row)
{
    TrackListModel* model = listNamed(list);
    if (model == nullptr || row < 0 || row >= model->count()) {
        return;
    }
    // The whole list is the context, as the current app queued the visible list.
    model->fetchAll();
    m_actions.play(model->trackAt(row), model->tracks());
}

void LibraryViewModel::likeSong(const QString& list, int row)
{
    TrackListModel* model = listNamed(list);
    if (model != nullptr && row >= 0 && row < model->count()) {
        [[maybe_unused]] const std::optional<bool> liked = m_actions.toggleLike(model->trackAt(row));
    }
}

void LibraryViewModel::prepareSongMenu(const QString& list, int row)
{
    TrackListModel* model = listNamed(list);
    if (model == nullptr || row < 0 || row >= model->count()) {
        return;
    }
    m_trackMenu.prepare(model->trackAt(row), model->tracks());
}

void LibraryViewModel::followPlayer()
{
    const std::optional<core::Track>& current = m_player.currentTrack();
    const QString key = current ? core::catalogSourceKey(*current) : QString();
    m_history.setPlayingKey(key);
    m_songs.setPlayingKey(key);
}

void LibraryViewModel::onLiked(const QString& sourceKey, bool liked)
{
    m_history.setLiked(sourceKey, liked);
    m_songs.setLiked(sourceKey, liked);
    readPlaylists();
}

} // namespace lmp::ui
