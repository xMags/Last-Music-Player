#include "ui/shell/SidebarViewModel.h"

#include "core/library/CollectionActions.h"
#include "core/library/TrackActions.h"
#include "ui/navigation/Route.h"

namespace lmp::ui {

namespace {

QList<SidebarNavListModel::Item> mainViews()
{
    return {
        {static_cast<int>(Route::Kind::Home), Navigator::tr("Home"), QStringLiteral("home")},
        {static_cast<int>(Route::Kind::Browse), Navigator::tr("Browse"), QStringLiteral("browse")},
        {static_cast<int>(Route::Kind::Library), Navigator::tr("Library"), QStringLiteral("library")},
        // The Windows app's Offline page: the songs downloaded on this PC.
        {static_cast<int>(Route::Kind::Offline), Navigator::tr("Offline"), QStringLiteral("download")},
        {static_cast<int>(Route::Kind::Settings), Navigator::tr("Settings"),
         QStringLiteral("settings")},
    };
}

} // namespace

SidebarViewModel::SidebarViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_navigator(parts.navigator)
    , m_actions(parts.actions)
    , m_navItems(mainViews())
    , m_activeKind(static_cast<int>(Route::Kind::Home))
{
    connect(&m_navigator, &Navigator::currentChanged, this, &SidebarViewModel::followNavigator);
    connect(&parts.collections, &core::CollectionActions::playlistsChanged, this,
            &SidebarViewModel::refreshPlaylists);
    connect(&m_actions, &core::TrackActions::playlistsChanged, this, &SidebarViewModel::refreshPlaylists);
    followNavigator();
}

void SidebarViewModel::refreshPlaylists()
{
    QList<SidebarPlaylistListModel::Item> items;
    for (const core::Track& playlist : m_actions.playlists()) {
        if (items.size() == PlaylistLimit) {
            break;
        }
        items.append({.id = playlist.sourceUrl, .name = playlist.title});
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
                                                     {QStringLiteral("title"), tr("Favourites")}}));
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
