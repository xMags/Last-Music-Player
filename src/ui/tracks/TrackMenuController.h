#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>
#include <QtQml/qqmlregistration.h>

#include "core/model/Track.h"
#include "ui/menus/MenuItemListModel.h"

namespace lmp::core {
class TrackActions;
}

namespace lmp::ui {

class ToastQueue;

// The song menu every page shares (`App.trackMenu`): prepare() fills the menu
// for a song (TrackMenuBuilder, with what TrackActions allows), trigger()
// runs the chosen item. "Add to playlist" lists the playlists the song can
// join (playlist-menu.ts openPlaylistMenu) and asks the page to open its
// popup, where a new playlist can also be named.
//
// GUI thread only.
class TrackMenuController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.trackMenu")

    Q_PROPERTY(lmp::ui::MenuItemListModel* menu READ menu CONSTANT FINAL)
    // The popup's playlists, newest first, at most 8: {key, title, count}.
    Q_PROPERTY(QVariantList playlistChoices READ playlistChoices NOTIFY playlistChoicesChanged FINAL)

public:
    // Both are owned by the composition root and outlive this; without
    // toasts, adding to a playlist says nothing.
    explicit TrackMenuController(core::TrackActions& actions, ToastQueue* toasts = nullptr, QObject* parent = nullptr);

    [[nodiscard]] MenuItemListModel* menu() { return &m_menu; }
    [[nodiscard]] QVariantList playlistChoices() const { return m_playlistChoices; }

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
    // Where the song menu learns and changes a song's download (the
    // Downloads view model); without them it offers none.
    struct DownloadHooks {
        // "", "offer", "done", "pending" or "failed".
        std::function<QString(const core::Track&)> state;
        // "download", "remove-download" or "retry-download".
        std::function<void(const core::Track&, const QString&)> run;
    };
    void setDownloadHooks(DownloadHooks hooks) { m_downloads = std::move(hooks); }

    Q_INVOKABLE void trigger(const QString& itemId);
    // Adds the prepared song to a new playlist of this name (blank names are
    // ignored), or to the playlist with this key; a toast says how it went.
    Q_INVOKABLE void createPlaylist(const QString& name);
    Q_INVOKABLE void addToPlaylist(const QString& playlistKey);

signals:
    // Open the playlist popup where the song menu was.
    void playlistMenuRequested();
    void playlistChoicesChanged();
    // Move up (-1) or Move down (+1) was chosen; the page moves the song.
    void moveRequested(int step);

private:
    core::TrackActions& m_actions;
    ToastQueue* m_toasts = nullptr;
    MenuItemListModel m_menu;
    QVariantList m_playlistChoices;
    DownloadHooks m_downloads;
    core::Track m_track;
    QList<core::Track> m_context;
    QString m_playlistKey;
};

} // namespace lmp::ui
