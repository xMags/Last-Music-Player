#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "core/model/Track.h"
#include "ui/menus/MenuItemListModel.h"

namespace lmp::core {
class TrackActions;
}

namespace lmp::ui {

// The song menu every page shares (`App.trackMenu`): prepare() fills the menu
// for a song (TrackMenuBuilder, with what TrackActions allows), trigger()
// runs the chosen item. "Add to playlist" fills the playlist menu and asks
// the page to open it; "New playlist" asks for a name.
//
// GUI thread only.
class TrackMenuController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.trackMenu")

    Q_PROPERTY(lmp::ui::MenuItemListModel* menu READ menu CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::MenuItemListModel* playlistMenu READ playlistMenu CONSTANT FINAL)

public:
    // The actions are owned by the composition root and outlive this.
    explicit TrackMenuController(core::TrackActions& actions, QObject* parent = nullptr);

    [[nodiscard]] MenuItemListModel* menu() { return &m_menu; }
    [[nodiscard]] MenuItemListModel* playlistMenu() { return &m_playlistMenu; }

    // The song and where its menu opened: the list it was in (for Play now)
    // and the playlist whose page shows it, if any.
    void prepare(const core::Track& track, const QList<core::Track>& context, const QString& playlistKey = {});
    // Where a song sits on a playlist page that shows the playlist's own order.
    struct PlaylistPlace {
        QString key;
        bool canMoveUp = false;
        bool canMoveDown = false;
    };
    void prepareInPlaylist(const core::Track& track, const QList<core::Track>& context, const PlaylistPlace& place);
    Q_INVOKABLE void trigger(const QString& itemId);
    // Adds the prepared song to a new playlist of this name.
    Q_INVOKABLE void createPlaylist(const QString& name);

signals:
    // Open the playlist menu where the song menu was.
    void playlistMenuRequested();
    // Ask for a new playlist's name.
    void newPlaylistRequested();
    // Move up (-1) or Move down (+1) was chosen; the page moves the song.
    void moveRequested(int step);

private:
    core::TrackActions& m_actions;
    MenuItemListModel m_menu;
    MenuItemListModel m_playlistMenu;
    core::Track m_track;
    QList<core::Track> m_context;
    QString m_playlistKey;
};

} // namespace lmp::ui
