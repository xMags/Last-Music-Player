#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "ui/navigation/Navigator.h"
#include "ui/shell/SidebarListModels.h"

namespace lmp::core::db {
class LibraryQueries;
} // namespace lmp::core::db

namespace lmp::core {
class CollectionActions;
class TrackActions;
} // namespace lmp::core

namespace lmp::ui {

// The left sidebar (`App.sidebar`): navigation items with the active one and
// the playlists section (the profile card is App.profile). The active item
// is the last main view opened; catalogue pages leave it as it was.
//
// The playlists section (main.ts renderSidebarPlaylists): Liked Songs, then
// the user's playlists on this PC (the account's join with account sync),
// at most 50; the sidebar shows those that fit and an "All playlists" row
// that opens Library. It follows playlists as they are made, renamed and
// deleted, and each row shows the playlist's cover mosaic from its first
// songs, read again when its songs change.
class SidebarViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.sidebar")

    Q_PROPERTY(lmp::ui::SidebarNavListModel* navItems READ navItems CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::SidebarPlaylistListModel* playlists READ playlists CONSTANT FINAL)
    Q_PROPERTY(int activeKind READ activeKind NOTIFY activeKindChanged FINAL)


public:
    struct Parts {
        Navigator& navigator;
        core::db::LibraryQueries& library;
        core::CollectionActions& collections;
        core::TrackActions& actions;
    };

    // More playlists than this never reach the sidebar; Library lists them all.
    static constexpr int PlaylistLimit = 50;
    // How many of a playlist's first songs its sidebar cover is looked for in.
    static constexpr int CoverSongLimit = 40;

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit SidebarViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] SidebarNavListModel* navItems() { return &m_navItems; }
    [[nodiscard]] SidebarPlaylistListModel* playlists() { return &m_playlists; }
    [[nodiscard]] int activeKind() const { return m_activeKind; }


    Q_INVOKABLE void openView(int kind);
    Q_INVOKABLE void openPlaylist(int row);
    // Liked Songs: the Favourites playlist.
    Q_INVOKABLE void openLiked();
    // "All playlists": Library's Playlists tab.
    Q_INVOKABLE void openAllPlaylists();
    // Reads the playlists again.
    void refreshPlaylists();


signals:
    void activeKindChanged();


private:
    void followNavigator();

    Navigator& m_navigator;
    core::db::LibraryQueries& m_library;
    core::TrackActions& m_actions;
    SidebarNavListModel m_navItems;
    SidebarPlaylistListModel m_playlists;
    int m_activeKind;
};

} // namespace lmp::ui
