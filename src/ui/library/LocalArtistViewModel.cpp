#include "ui/library/LocalArtistViewModel.h"

#include <QHash>

#include "core/db/LibraryQueries.h"
#include "core/library/TrackActions.h"
#include "core/model/ArtworkPlaceholder.h"
#include "core/model/CatalogSourceKey.h"
#include "core/playback/PlayerController.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/player/CoverSource.h"
#include "ui/toasts/ToastQueue.h"
#include "ui/tracks/TrackMenuController.h"

namespace lmp::ui {

namespace {

QString songsText(int count)
{
    return count == 1 ? LocalArtistViewModel::tr("1 song") : LocalArtistViewModel::tr("%1 songs").arg(count);
}

} // namespace

LocalArtistViewModel::LocalArtistViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_library(parts.library)
    , m_actions(parts.actions)
    , m_player(parts.player)
    , m_trackMenu(parts.trackMenu)
    , m_navigator(parts.navigator)
    , m_follows(parts.follows)
    , m_toasts(parts.toasts)
{
    connect(&m_navigator, &Navigator::currentChanged, this, &LocalArtistViewModel::followNavigator);
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, &LocalArtistViewModel::followPlayer);
    connect(&m_actions, &core::TrackActions::likedChanged, this, [this](const QString& sourceKey, bool liked) {
        m_topSongs.setLiked(sourceKey, liked);
        for (core::Track& song : m_songs) {
            if (core::catalogSourceKey(song) == sourceKey) {
                song.liked = liked;
            }
        }
    });
}

void LocalArtistViewModel::followNavigator()
{
    const Route route = m_navigator.current();
    if (route.kind() != Route::Kind::Collection || route.param(QStringLiteral("kind")) != QLatin1StringView("artist")) {
        return;
    }
    m_publicId = route.param(QStringLiteral("artistId"));
    open(route.param(QStringLiteral("key")));
    emit followChanged();
}

core::FollowableArtist LocalArtistViewModel::followable() const
{
    core::FollowableArtist artist;
    artist.publicId = m_publicId;
    artist.identity.name = m_name;
    return artist;
}

bool LocalArtistViewModel::canFollow() const
{
    return !m_name.isEmpty() && m_follows.canFollow(followable());
}

bool LocalArtistViewModel::followed() const
{
    return canFollow() && m_follows.isFollowed(followable());
}

void LocalArtistViewModel::toggleFollow()
{
    if (m_followBusy || !canFollow()) {
        return;
    }
    const bool wanted = !followed();
    m_followBusy = true;
    emit followChanged();
    m_follows.setFollowed(followable(), wanted).then(this, [this, wanted](const core::Result<bool>& result) {
        m_followBusy = false;
        if (!result) {
            if (result.failure().kind != core::FailureKind::Cancelled) {
                m_toasts.show(tr("Could not update artist follow state."), true);
            }
        } else {
            m_toasts.show(wanted ? tr("Artist followed") : tr("Artist unfollowed"));
        }
        emit followChanged();
    });
}

void LocalArtistViewModel::open(const QString& name)
{
    m_name = name;
    m_showingAll = false;
    core::db::TrackQuery query;
    query.groupKind = QStringLiteral("artist");
    query.groupKey = name;
    query.sort = QStringLiteral("MostPlayed");
    m_songs = m_library.loadTracksForQuery(query);

    m_artworkUrl.clear();
    QList<CollectionCardModel::Card> albums;
    QHash<QString, qsizetype> albumRows;
    QList<int> albumCounts;
    for (const core::Track& song : m_songs) {
        if (m_artworkUrl.isEmpty()) {
            m_artworkUrl = CoverSource::forLibraryTrack(song).displayUrl();
        }
        const QString folded = song.album.trimmed().toCaseFolded();
        if (folded.isEmpty() || folded == QLatin1StringView("unknown album")) {
            continue;
        }
        const auto found = albumRows.constFind(folded);
        if (found != albumRows.cend()) {
            ++albumCounts[*found];
            if (albums[*found].artworkUrl.isEmpty()) {
                albums[*found].artworkUrl = CoverSource::forLibraryTrack(song).displayUrl();
            }
            continue;
        }
        albumRows.insert(folded, albums.size());
        albumCounts.append(1);
        albums.append({.kind = QStringLiteral("album"),
                       .key = song.album.trimmed(),
                       .title = song.album.trimmed(),
                       .artworkUrl = CoverSource::forLibraryTrack(song).displayUrl()});
    }
    for (qsizetype row = 0; row < albums.size(); ++row) {
        albums[row].subtitle = songsText(albumCounts.at(row));
    }
    m_albums.setCards(std::move(albums));
    showSongs();
    emit opened();
}

void LocalArtistViewModel::showSongs()
{
    m_topSongs.setTracks(m_showingAll ? m_songs : m_songs.mid(0, TopSongCount));
    followPlayer();
}

void LocalArtistViewModel::setShowingAll(bool all)
{
    if (all == m_showingAll) {
        return;
    }
    m_showingAll = all;
    showSongs();
    emit opened();
}

QString LocalArtistViewModel::artworkLabel() const
{
    return core::ArtworkPlaceholder::tileLabelFor(m_name);
}

QString LocalArtistViewModel::info() const
{
    const int albumCount = m_albums.count();
    const QString albums = albumCount == 1 ? tr("1 album") : tr("%1 albums").arg(albumCount);
    return albumCount == 0 ? songsText(static_cast<int>(m_songs.size()))
                           : songsText(static_cast<int>(m_songs.size())) + QStringLiteral(" · ") + albums;
}

void LocalArtistViewModel::play()
{
    for (const core::Track& song : m_songs) {
        if (m_actions.canPlay(song)) {
            m_actions.play(song, m_songs);
            return;
        }
    }
}

void LocalArtistViewModel::shuffle()
{
    m_actions.shufflePlay(m_songs);
}

void LocalArtistViewModel::playSong(int row)
{
    if (row >= 0 && row < m_topSongs.count()) {
        m_actions.play(m_topSongs.trackAt(row), m_songs);
    }
}

void LocalArtistViewModel::likeSong(int row)
{
    if (row >= 0 && row < m_topSongs.count()) {
        [[maybe_unused]] const std::optional<bool> liked = m_actions.toggleLike(m_topSongs.trackAt(row));
    }
}

void LocalArtistViewModel::prepareSongMenu(int row)
{
    if (row >= 0 && row < m_topSongs.count()) {
        m_trackMenu.prepare(m_topSongs.trackAt(row), m_songs);
    }
}

void LocalArtistViewModel::openAlbum(int row)
{
    if (row < 0 || row >= m_albums.count()) {
        return;
    }
    const CollectionCardModel::Card album = m_albums.cardAt(row);
    m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), QStringLiteral("album")},
                                                     {QStringLiteral("key"), album.key},
                                                     {QStringLiteral("title"), album.title}}));
}

void LocalArtistViewModel::followPlayer()
{
    const std::optional<core::Track>& current = m_player.currentTrack();
    m_topSongs.setPlayingKey(current ? core::catalogSourceKey(*current) : QString());
}

} // namespace lmp::ui
