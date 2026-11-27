#include "ui/search/SearchViewModel.h"

#include "core/net/UserMessages.h"

#include <QCollator>
#include <QColor>
#include <QPointer>
#include <QSet>

#include <algorithm>
#include <utility>

#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/library/BrowseGroupingPolicy.h"
#include "core/library/RecentSearchStore.h"
#include "core/library/TrackActions.h"
#include "core/library/TrackSearchPolicy.h"
#include "core/model/CatalogSourceKey.h"
#include "core/model/DisplayTitle.h"
#include "core/playback/PlayerController.h"
#include "core/settings/SettingsStore.h"
#include "core/catalog/CatalogService.h"
#include "ui/catalog/CatalogRoutes.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/player/CoverSource.h"
#include "ui/tracks/TrackMenuController.h"

namespace lmp::ui {

namespace {

// views/search.ts categoryColors.
constexpr QRgb kCategoryColors[] = {0xFFF0806A, 0xFF8B7BE8, 0xFF00A7C4, 0xFF1F9D6B,
                                    0xFFD4A03C, 0xFFC96FA8, 0xFF5B8DEF, 0xFF7A9E4F};

QString songCount(qsizetype count)
{
    return count == 1 ? SearchViewModel::tr("1 song") : SearchViewModel::tr("%1 songs").arg(count);
}

} // namespace

SearchViewModel::SearchViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_source(parts.source)
    , m_library(parts.library)
    , m_groups(parts.groups)
    , m_actions(parts.actions)
    , m_player(parts.player)
    , m_settings(parts.settings)
    , m_trackMenu(parts.trackMenu)
    , m_navigator(parts.navigator)
    , m_accountLibrary(std::move(parts.accountLibrary))
    , m_catalog(parts.catalog)
    , m_online(std::move(parts.online))
    , m_shareCode(std::move(parts.shareCode))
    , m_remoteSongs(std::move(parts.remoteSongs))
{
    m_typing.setSingleShot(true);
    m_typing.setInterval(TypingDelayMs);
    connect(&m_typing, &QTimer::timeout, this, &SearchViewModel::run);
    m_dwell.setSingleShot(true);
    m_dwell.setInterval(RecentDwellMs);
    connect(&m_dwell, &QTimer::timeout, this, [this] {
        // Only a query that stayed on screen joins the recent searches, so
        // the prefixes of one word typed along the way do not.
        if (m_state != QLatin1StringView("landing") && m_shownQuery == m_query.trimmed()) {
            record(m_shownQuery);
        }
    });
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, &SearchViewModel::followPlayer);
    connect(&m_actions, &core::TrackActions::likedChanged, this, [this](const QString& sourceKey, bool liked) {
        m_topSongs.setLiked(sourceKey, liked);
        m_songs.setLiked(sourceKey, liked);
        if (m_topSong && core::catalogSourceKey(*m_topSong) == sourceKey && m_topSong->liked != liked) {
            m_topSong->liked = liked;
            emit resultsChanged();
        }
    });
    m_recent = core::RecentSearchStore::load(m_settings);
}

void SearchViewModel::setQuery(const QString& query)
{
    if (query == m_query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    if (m_query.trimmed().size() < core::MinimumSearchQueryLength) {
        // Back to the landing at once; any answer still on its way is stale.
        m_typing.stop();
        m_dwell.stop();
        ++m_generation;
        m_shownQuery.clear();
        setState(QStringLiteral("landing"));
        return;
    }
    m_typing.start();
}

void SearchViewModel::searchNow()
{
    m_typing.stop();
    const QString query = m_query.trimmed();
    if (query.size() < core::MinimumSearchQueryLength) {
        return;
    }
    record(query);
    run();
}

void SearchViewModel::run()
{
    const QString query = m_query.trimmed();
    if (query.size() < core::MinimumSearchQueryLength) {
        return;
    }
    const quint64 generation = ++m_generation;
    // The last results stay on screen until this search answers.
    if (m_state == QLatin1StringView("landing") || m_state == QLatin1StringView("empty")
        || m_state == QLatin1StringView("error")) {
        setState(QStringLiteral("searching"));
    }
    if (m_catalog != nullptr && m_online && m_online() && m_shareCode) {
        if (const std::optional<QString> code = m_shareCode(query)) {
            setState(QStringLiteral("landing"));
            m_navigator.push(Route(Route::Kind::Shared, {{QStringLiteral("code"), *code}}));
            return;
        }
    }
    if (m_catalog != nullptr && m_online && m_online()) {
        m_catalog->searchAll(query).then(this, [this, generation, query](const core::Result<core::catalog::SearchAll>& found) {
            if (generation != m_generation) {
                return;
            }
            if (!found) {
                // "Search unavailable" with why and Retry, not "No results".
                m_shownQuery = query;
                m_errorText = core::net::userMessageOr(found.failure(), tr("Search is unavailable right now. Try again."));
                setState(QStringLiteral("error"));
                return;
            }
            applyOnline(generation, query, found.value());
        });
        return;
    }
    QPointer<SearchViewModel> self(this);
    m_source.search(query, [self, generation, query](SearchHits hits) {
        if (!self) {
            return;
        }
        self->apply(generation, query, hits);
        if (self->m_remoteSongs && generation == self->m_generation) {
            self->joinRemote(generation, query, hits);
        }
    });
}

void SearchViewModel::joinRemote(quint64 generation, const QString& query, const SearchHits& local)
{
    m_remoteSongs(query).then(this, [this, generation, query, local](const core::Result<QList<core::Track>>& found) {
        if (generation != m_generation || !found || found.value().isEmpty()) {
            return;
        }
        SearchHits joined = local;
        QSet<QString> seen;
        for (const core::Track& song : local.songs) {
            seen.insert(core::catalogSourceKey(song));
        }
        for (const core::Track& song : found.value()) {
            const QString key = core::catalogSourceKey(song);
            if (!key.isEmpty() && !seen.contains(key)) {
                seen.insert(key);
                joined.songs.append(song);
            }
        }
        apply(generation, query, std::move(joined));
    });
}

void SearchViewModel::apply(quint64 generation, const QString& query, SearchHits hits)
{
    // A newer search has started since: this answer is stale.
    if (generation != m_generation) {
        return;
    }
    m_shownQuery = query;
    m_dwell.start();

    QList<core::BrowseResultInput> inputs;
    for (const core::Track& song : hits.songs) {
        inputs.append({.kind = core::BrowseResultKind::Song,
                       .title = song.title,
                       .artist = song.artist,
                       .album = song.album,
                       .year = {},
                       .duration = song.durationText,
                       .key = core::catalogSourceKey(song),
                       .count = 0});
    }
    const qsizetype songInputs = inputs.size();
    for (const core::Track& playlist : hits.playlists) {
        inputs.append({.kind = core::BrowseResultKind::Playlist,
                       .title = playlist.title,
                       .artist = {},
                       .album = {},
                       .year = {},
                       .duration = {},
                       .key = playlist.sourceUrl,
                       .count = playlist.trackCount});
    }
    const core::GroupedBrowseResults grouped = core::buildGroupedBrowseResults(inputs, query, false);

    m_hits.clear();
    QList<CollectionCardModel::Card> artists;
    QList<CollectionCardModel::Card> albums;
    QList<CollectionCardModel::Card> playlists;
    auto coverOf = [&hits, songInputs](const core::BrowseResultEntity& entity) {
        for (const qsizetype index : entity.memberIndices) {
            if (index < songInputs) {
                const QUrl url = CoverSource::forLibraryTrack(hits.songs.at(index)).displayUrl();
                if (!url.isEmpty()) {
                    return url;
                }
            }
        }
        return QUrl();
    };
    for (const core::BrowseResultGroup& group : grouped.groups) {
        for (const core::BrowseResultEntity& entity : group.items) {
            switch (group.kind) {
            case core::BrowseResultKind::Song:
                if (entity.representativeIndex < songInputs) {
                    m_hits.append(hits.songs.at(entity.representativeIndex));
                }
                break;
            case core::BrowseResultKind::Album:
                albums.append({.kind = QStringLiteral("album"),
                               .key = entity.title,
                               .title = entity.title,
                               .subtitle = entity.subtitle,
                               .artworkUrl = coverOf(entity)});
                break;
            case core::BrowseResultKind::Artist:
                artists.append({.kind = QStringLiteral("artist"),
                                .key = entity.title,
                                .title = entity.title,
                                .subtitle = songCount(entity.memberIndices.size()),
                                .artworkUrl = coverOf(entity)});
                break;
            case core::BrowseResultKind::Playlist: {
                const qsizetype index = entity.representativeIndex - songInputs;
                if (index >= 0 && index < hits.playlists.size()) {
                    const core::Track& playlist = hits.playlists.at(index);
                    playlists.append({.kind = QStringLiteral("playlist"),
                                      .key = playlist.sourceUrl,
                                      .title = playlist.title,
                                      .subtitle = songCount(playlist.trackCount)});
                }
                break;
            }
            }
        }
    }

    // The top result: an artist or album named exactly as searched, else the
    // first song (the reference's comes from its server). An online search's
    // results go with it: they are not this search's.
    m_topKind.clear();
    m_topSong.reset();
    m_topKey.clear();
    m_onlineTop.reset();
    m_onlineCards.clear();
    auto takeTop = [this, &query](const QList<CollectionCardModel::Card>& cards, const QString& kind,
                                  const QString& label) {
        for (const CollectionCardModel::Card& card : cards) {
            if (card.title.compare(query, Qt::CaseInsensitive) == 0) {
                m_topKind = kind;
                m_topKey = card.key;
                m_topTitle = card.title;
                m_topPill = label;
                m_topDetail = kind == QLatin1StringView("album") ? card.subtitle : QString();
                m_topArtworkUrl = card.artworkUrl;
                m_topArtworkId = kind + QLatin1Char(':') + card.key;
                return true;
            }
        }
        return false;
    };
    if (!takeTop(artists, QStringLiteral("artist"), tr("Artist"))
        && !takeTop(albums, QStringLiteral("album"), tr("Album")) && !m_hits.isEmpty()) {
        const core::Track& song = m_hits.constFirst();
        m_topKind = QStringLiteral("song");
        m_topSong = song;
        m_topTitle = core::displayTitle(song.title);
        m_topPill = tr("Song");
        m_topDetail = QStringList{song.artist, song.durationText}.join(QStringLiteral(" · "));
        m_topArtworkUrl = CoverSource::forLibraryTrack(song).displayUrl();
        m_topArtworkId = CoverSource::placeholderIdFor(song);
    }

    m_artists.setCards(std::move(artists));
    m_albums.setCards(std::move(albums));
    m_playlists.setCards(std::move(playlists));
    m_topSongs.setTracks(m_hits.mid(0, TopSongCount));
    showSongs();
    followPlayer();
    emit resultsChanged();
    const bool any = !m_hits.isEmpty() || m_playlists.count() > 0;
    setState(any ? QStringLiteral("results") : QStringLiteral("empty"));
}

void SearchViewModel::applyOnline(quint64 generation, const QString& query, const core::catalog::SearchAll& found)
{
    if (generation != m_generation) {
        return;
    }
    m_shownQuery = query;
    m_dwell.start();
    m_hits = found.songs;
    m_onlineCards.clear();
    const auto cards = [this](const QString& group, const QList<core::catalog::CatalogItem>& items) {
        m_onlineCards.insert(group, items);
        QList<CollectionCardModel::Card> list;
        for (const core::catalog::CatalogItem& item : items) {
            core::Track cover;
            cover.artworkUrl = item.artworkUrl;
            cover.sourceKind = QStringLiteral("remote");
            list.append({.kind = QStringLiteral("catalog"),
                         .key = item.catalogId,
                         .title = item.title,
                         .subtitle = item.subtitle,
                         .artworkUrl = CoverSource::forTrack(cover).displayUrl(),
                         // .catalog-card.artist .art is round.
                         .round = group == QLatin1StringView("artists")});
        }
        return list;
    };
    m_artists.setCards(cards(QStringLiteral("artists"), found.artists));
    m_albums.setCards(cards(QStringLiteral("albums"), found.albums));
    m_playlists.setCards(cards(QStringLiteral("playlists"), found.playlists));

    // The service's own top result.
    m_topKind.clear();
    m_topSong.reset();
    m_topKey.clear();
    m_onlineTop.reset();
    if (found.topKind == QLatin1StringView("song") && found.topSongIndex >= 0 && found.topSongIndex < m_hits.size()) {
        const core::Track& song = m_hits.at(found.topSongIndex);
        m_topKind = QStringLiteral("song");
        m_topSong = song;
        m_topTitle = core::displayTitle(song.title);
        m_topPill = tr("Song");
        m_topDetail = QStringList{song.artist, song.durationText}.join(QStringLiteral(" · "));
        m_topArtworkUrl = CoverSource::forTrack(song).displayUrl();
        m_topArtworkId = CoverSource::placeholderIdFor(song);
    } else if (found.topItem) {
        const core::catalog::CatalogItem& item = *found.topItem;
        core::Track cover;
        cover.artworkUrl = item.artworkUrl;
        cover.sourceKind = QStringLiteral("remote");
        m_onlineTop = item;
        m_topKind = found.topKind;
        m_topKey = item.catalogId;
        m_topTitle = item.title;
        // topDetail: "Artist"; a single or EP names its own kind first
        // ("Single · Artist"); otherwise "Album" or "Playlist", then who.
        if (found.topKind == QLatin1StringView("artist")) {
            m_topPill = tr("Artist");
            m_topDetail.clear();
        } else if (item.releaseKind != core::catalog::ReleaseKind::None && !item.subtitle.isEmpty()) {
            QStringList parts = item.subtitle.split(QStringLiteral(" · "));
            m_topPill = parts.takeFirst();
            m_topDetail = parts.join(QStringLiteral(" · "));
        } else {
            m_topPill = found.topKind == QLatin1StringView("album") ? tr("Album") : tr("Playlist");
            m_topDetail = item.subtitle;
        }
        m_topArtworkUrl = CoverSource::forTrack(cover).displayUrl();
        m_topArtworkId = found.topKind + QLatin1Char(':') + item.title;
    }
    m_topSongs.setTracks(m_hits.mid(0, TopSongCount));
    showSongs();
    followPlayer();
    emit resultsChanged();
    const bool any = !m_hits.isEmpty() || m_artists.count() > 0 || m_albums.count() > 0 || m_playlists.count() > 0;
    setState(any ? QStringLiteral("results") : QStringLiteral("empty"));
}

QStringList SearchViewModel::songSorts() const
{
    return {tr("Relevance"), tr("Title"), tr("Artist"), tr("Duration")};
}

void SearchViewModel::setSongSort(int sort)
{
    if (sort < 0 || sort >= songSorts().size() || sort == m_songSort) {
        return;
    }
    m_songSort = sort;
    showSongs();
    emit resultsChanged();
}

void SearchViewModel::showSongs()
{
    QList<core::Track> songs = m_hits;
    if (m_songSort != 0) {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        const int sort = m_songSort;
        std::stable_sort(songs.begin(), songs.end(), [&collator, sort](const core::Track& a, const core::Track& b) {
            if (sort == 3) {
                return a.durationSeconds < b.durationSeconds;
            }
            return collator.compare(sort == 2 ? a.artist : a.title, sort == 2 ? b.artist : b.title) < 0;
        });
    }
    m_songs.setTracks(std::move(songs));
    followPlayer();
}

void SearchViewModel::setState(const QString& state)
{
    if (state != m_state) {
        m_state = state;
        emit stateChanged();
    }
}

void SearchViewModel::record(const QString& query)
{
    const QStringList next = core::RecentSearchStore::record(m_recent, query);
    if (next == m_recent) {
        return;
    }
    m_recent = next;
    core::RecentSearchStore::save(m_settings, m_recent);
    emit recentSearchesChanged();
}

void SearchViewModel::useRecent(int row)
{
    if (row < 0 || row >= m_recent.size()) {
        return;
    }
    setQuery(m_recent.at(row));
    searchNow();
}

void SearchViewModel::removeRecent(int row)
{
    if (row < 0 || row >= m_recent.size()) {
        return;
    }
    m_recent.removeAt(row);
    core::RecentSearchStore::save(m_settings, m_recent);
    emit recentSearchesChanged();
}

void SearchViewModel::clearRecents()
{
    if (m_recent.isEmpty()) {
        return;
    }
    m_recent.clear();
    core::RecentSearchStore::clear(m_settings);
    emit recentSearchesChanged();
}

void SearchViewModel::reloadForSource()
{
    refreshLanding();
    if (m_state == QLatin1StringView("landing")) {
        return;
    }
    m_typing.stop();
    run();
}

void SearchViewModel::refreshLanding()
{
    if (const std::optional<core::home::HomeLibrary> account = m_accountLibrary ? m_accountLibrary() : std::nullopt) {
        refreshAccountLanding(*account);
        return;
    }
    const QList<core::BrowseLandingCategory> categories =
        core::browseLandingCategories(m_groups.loadTopGenres(static_cast<int>(core::BrowseLandingCategoryLimit)));
    QList<CollectionCardModel::Card> tiles;
    // One of the user's own covers per genre tile, never the same one twice
    // (views/search.ts categoryCovers); shortcuts keep the plain shape.
    QSet<QUrl> usedCovers;
    for (const core::BrowseLandingCategory& category : categories) {
        if (tiles.size() >= CategoryTileCount) {
            break;
        }
        QUrl cover;
        if (!category.isCurated) {
            for (const core::Track& song : m_library.loadTracksForGroup(QStringLiteral("genre"), category.name)) {
                const QUrl url = CoverSource::forLibraryTrack(song).displayUrl();
                if (!url.isEmpty() && !usedCovers.contains(url)) {
                    cover = url;
                    usedCovers.insert(url);
                    break;
                }
            }
        }
        const QColor color = QColor::fromRgba(kCategoryColors[tiles.size() % std::size(kCategoryColors)]);
        tiles.append({.kind = category.isCurated ? QStringLiteral("shortcut") : QStringLiteral("genre"),
                      .key = category.name,
                      .title = category.name,
                      .artworkUrl = cover,
                      .gradientStart = color,
                      .gradientEnd = color});
    }
    m_categories.setCards(std::move(tiles));
}

void SearchViewModel::refreshAccountLanding(const core::home::HomeLibrary& library)
{
    // views/search.ts renderBrowseLanding: the library's genres as the
    // reference ranks them (plays and likes count), else its artists, then
    // the default categories to fill two rows; every tile searches.
    static const QStringList defaults{QStringLiteral("Pop"),   QStringLiteral("Hip-Hop"), QStringLiteral("Electronic"),
                                      QStringLiteral("Chill"), QStringLiteral("Bollywood"), QStringLiteral("Rock"),
                                      QStringLiteral("Indie"), QStringLiteral("R&B"),       QStringLiteral("Jazz"),
                                      QStringLiteral("Lo-Fi")};
    const QList<core::Track> catalog = core::home::deriveHomeModel(library).catalog;
    const QStringList genres = core::home::rankedValues(catalog, true);
    const bool byGenre = !genres.isEmpty();
    const QStringList titles = core::home::fillLabels(byGenre ? genres : core::home::rankedValues(catalog, false),
                                                      defaults, CategoryTileCount);
    QList<CollectionCardModel::Card> tiles;
    QSet<QString> usedSongs;
    for (const QString& title : titles) {
        // One of the listener's own covers, never the same song twice.
        QUrl cover;
        const QString wanted = title.trimmed().toLower();
        for (const core::Track& song : catalog) {
            const QString value = (byGenre ? song.genre : song.artist).trimmed().toLower();
            if (song.artworkUrl.isEmpty() || value != wanted || usedSongs.contains(song.remoteId)) {
                continue;
            }
            usedSongs.insert(song.remoteId);
            cover = CoverSource::forTrack(song).displayUrl();
            break;
        }
        const QColor color = QColor::fromRgba(kCategoryColors[tiles.size() % std::size(kCategoryColors)]);
        tiles.append({.kind = QStringLiteral("shortcut"),
                      .key = title,
                      .title = title,
                      .artworkUrl = cover,
                      .gradientStart = color,
                      .gradientEnd = color});
    }
    m_categories.setCards(std::move(tiles));
}

void SearchViewModel::openCategory(int row)
{
    if (row < 0 || row >= m_categories.count()) {
        return;
    }
    const CollectionCardModel::Card tile = m_categories.cardAt(row);
    if (tile.kind == QLatin1StringView("genre")) {
        m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), QStringLiteral("genre")},
                                                         {QStringLiteral("key"), tile.key},
                                                         {QStringLiteral("title"), tile.title}}));
        return;
    }
    setQuery(tile.title);
    searchNow();
}

void SearchViewModel::openTop()
{
    if (m_topKind == QLatin1StringView("song") && m_topSong) {
        m_actions.play(*m_topSong, m_hits);
        return;
    }
    if (m_topKind.isEmpty()) {
        return;
    }
    if (m_onlineTop) {
        if (const std::optional<Route> route = catalog_routes::routeForItem(*m_onlineTop)) {
            m_navigator.push(*route);
        }
        return;
    }
    m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), m_topKind},
                                                     {QStringLiteral("key"), m_topKey},
                                                     {QStringLiteral("title"), m_topTitle}}));
}

void SearchViewModel::openCard(const QString& group, int row)
{
    CollectionCardModel* model = group == QLatin1StringView("artists") ? &m_artists
        : group == QLatin1StringView("albums")                         ? &m_albums
        : group == QLatin1StringView("playlists")                      ? &m_playlists
                                                                       : nullptr;
    if (model == nullptr || row < 0 || row >= model->count()) {
        return;
    }
    // Opening a result means the search found what was wanted.
    record(m_shownQuery);
    const CollectionCardModel::Card card = model->cardAt(row);
    if (card.kind == QLatin1StringView("catalog")) {
        const QList<core::catalog::CatalogItem> items = m_onlineCards.value(group);
        if (row < items.size()) {
            if (const std::optional<Route> route = catalog_routes::routeForItem(items.at(row))) {
                m_navigator.push(*route);
            }
        }
        return;
    }
    m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), card.kind},
                                                     {QStringLiteral("key"), card.key},
                                                     {QStringLiteral("title"), card.title}}));
}

TrackListModel* SearchViewModel::listNamed(const QString& list)
{
    if (list == QLatin1StringView("top")) {
        return &m_topSongs;
    }
    return list == QLatin1StringView("all") ? &m_songs : nullptr;
}

void SearchViewModel::playSong(const QString& list, int row)
{
    TrackListModel* model = listNamed(list);
    if (model == nullptr || row < 0 || row >= model->count()) {
        return;
    }
    record(m_shownQuery);
    // Just the clicked song: results are not a playlist, so autoplay fills
    // Up Next after it (the reference's play: "single").
    m_actions.play(model->trackAt(row));
}

void SearchViewModel::likeTop()
{
    if (m_topSong) {
        [[maybe_unused]] const std::optional<bool> liked = m_actions.toggleLike(*m_topSong);
    }
}

void SearchViewModel::likeSong(const QString& list, int row)
{
    TrackListModel* model = listNamed(list);
    if (model != nullptr && row >= 0 && row < model->count()) {
        [[maybe_unused]] const std::optional<bool> liked = m_actions.toggleLike(model->trackAt(row));
    }
}

void SearchViewModel::prepareSongMenu(const QString& list, int row)
{
    TrackListModel* model = listNamed(list);
    if (model != nullptr && row >= 0 && row < model->count()) {
        m_trackMenu.prepare(model->trackAt(row), model->tracks());
    }
}

void SearchViewModel::followPlayer()
{
    const std::optional<core::Track>& current = m_player.currentTrack();
    const QString key = current ? core::catalogSourceKey(*current) : QString();
    m_topSongs.setPlayingKey(key);
    m_songs.setPlayingKey(key);
}

} // namespace lmp::ui
