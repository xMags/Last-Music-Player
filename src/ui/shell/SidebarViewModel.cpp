#include "ui/shell/SidebarViewModel.h"
#include "core/app/PerfLog.h"

#include "core/db/DatabaseReader.h"
#include "core/library/CollectionActions.h"
#include "core/library/TrackActions.h"
#include "ui/library/PlaylistCoverArt.h"
#include "ui/navigation/Route.h"

namespace lmp::ui {

namespace {

QList<SidebarNavListModel::Item> mainViews(bool showsOffline)
{
    QList<SidebarNavListModel::Item> views{
        {static_cast<int>(Route::Kind::Home), Navigator::tr("Home"), QStringLiteral("home")},
        {static_cast<int>(Route::Kind::Browse), Navigator::tr("Browse"), QStringLiteral("browse")},
        {static_cast<int>(Route::Kind::Library), Navigator::tr("Library"), QStringLiteral("library")},
    };
    if (showsOffline) {
        // The Windows app's Offline page: the songs downloaded on this PC.
        views.append({static_cast<int>(Route::Kind::Offline), Navigator::tr("Offline"), QStringLiteral("download")});
    }
    views.append({static_cast<int>(Route::Kind::Settings), Navigator::tr("Settings"), QStringLiteral("settings")});
    return views;
}

} // namespace

SidebarViewModel::SidebarViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_navigator(parts.navigator)
    , m_reader(parts.reader)
    , m_actions(parts.actions)
    , m_navItems(mainViews(true))
    , m_activeKind(static_cast<int>(Route::Kind::Home))
{
    connect(&m_navigator, &Navigator::currentChanged, this, &SidebarViewModel::followNavigator);
    connect(&parts.collections, &core::CollectionActions::playlistsChanged, this,
            &SidebarViewModel::refreshPlaylists);
    connect(&m_actions, &core::TrackActions::playlistsChanged, this, &SidebarViewModel::refreshPlaylists);
    // A playlist's songs changed: its cover may have.
    connect(&m_actions, &core::TrackActions::playlistChanged, this, &SidebarViewModel::refreshPlaylists);
    followNavigator();
}

void SidebarViewModel::setShowsOffline(bool shows)
{
    m_navItems.setItems(mainViews(shows));
}

void SidebarViewModel::refreshPlaylists()
{
    if (m_playlistsReading) {
        // The read under way is out of date: its answer is dropped, and one
        // more read starts when it ends.
        ++m_playlistsGeneration;
        m_playlistsDirty = true;
        return;
    }
    startPlaylistsRead();
}

void SidebarViewModel::startPlaylistsRead()
{
    const std::uint64_t generation = ++m_playlistsGeneration;
    m_playlistsReading = true;
    const bool accountPlaylists = m_actions.accountPlaylistsActive();
    m_reader
        .read<SidebarPlaylistRead>([accountPlaylists](core::db::Database& database) {
            return readSidebarPlaylists(database, accountPlaylists, PlaylistLimit, CoverSongLimit);
        })
        .then(this, [this, generation](const SidebarPlaylistRead& read) {
            m_playlistsReading = false;
            if (generation == m_playlistsGeneration) {
                applyPlaylists(read);
            }
            if (m_playlistsDirty) {
                m_playlistsDirty = false;
                startPlaylistsRead();
            }
        });
}

void SidebarViewModel::applyPlaylists(const SidebarPlaylistRead& read)
{
    core::PerfScope perf("sidebar playlists applied");
    QList<SidebarPlaylistListModel::Item> items;
    for (qsizetype index = 0; index < read.playlists.size() && index < read.songs.size(); ++index) {
        const core::Track& playlist = read.playlists.at(index);
        const PlaylistCoverArt art = playlistCoverArt(read.songs.at(index), true);
        items.append({.id = playlist.sourceUrl,
                      .name = playlist.title,
                      .coverStyle = art.style,
                      .artworkUrl = art.image,
                      .collage = art.collage});
    }
    m_playlists.setItems(std::move(items));
}

void SidebarViewModel::openPlaylist(int row)
{
    const QModelIndex index = m_playlists.index(row);
    if (!index.isValid()) {
        return;
    }
    m_navigator.push(Route(Route::Kind::Collection,
                           {{QStringLiteral("kind"), QStringLiteral("playlist")},
                            {QStringLiteral("key"), index.data(SidebarPlaylistListModel::PlaylistIdRole).toString()},
                            {QStringLiteral("title"), index.data(SidebarPlaylistListModel::NameRole).toString()}}));
}

void SidebarViewModel::openLiked()
{
    m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), QStringLiteral("system")},
                                                     {QStringLiteral("key"), QStringLiteral("smart-liked")},
                                                     {QStringLiteral("title"), tr("Liked Songs")}}));
}

void SidebarViewModel::openAllPlaylists()
{
    m_navigator.push(Route(Route::Kind::Library, {{QStringLiteral("tab"), QStringLiteral("playlists")}}));
}


void SidebarViewModel::openView(int kind)
{
    m_navigator.openView(kind);
}


void SidebarViewModel::followNavigator()
{
    const Route route = m_navigator.current();
    if (!route.isMainView()) {
        return;
    }
    const int kind = static_cast<int>(route.kind());
    if (kind != m_activeKind) {
        m_activeKind = kind;
        emit activeKindChanged();
    }
}

} // namespace lmp::ui
