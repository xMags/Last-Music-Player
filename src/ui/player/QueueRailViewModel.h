#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "core/model/Track.h"
#include "core/playback/QueueTypes.h"
#include "ui/menus/MenuItemListModel.h"
#include "ui/player/QueueListModel.h"

namespace lmp::core {
class PlayerController;
namespace db {
class LibraryQueries;
}
} // namespace lmp::core

namespace lmp::core::connect {
class ConnectSync;
} // namespace lmp::core::connect

namespace lmp::ui {

class ToastQueue;
class TrackMenuController;

// The Up Next list (`App.queueRail`), shown in the queue rail and in the
// full-screen player's side panel: the songs to come (`queue`), their row
// menu, remove with Undo, reorder, Clear, and, while nothing plays, the
// latest plays (`recent`). The song shown above the list comes from
// App.playerBar. Every edit goes through the PlayerController, which saves
// the queue.
//
// Commands name a row by its index and its key (QueueListModel), so a row
// that changed under the pointer (a song ended meanwhile) is left alone.
//
// Lives on the GUI thread. The recent plays are read from the library on
// this thread, the database's owner, and only while nothing plays.
class QueueRailViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.queueRail")

    Q_PROPERTY(lmp::ui::QueueListModel* queue READ queue CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::QueueListModel* recent READ recent CONSTANT FINAL)
    // The items of the row menu last prepared with prepareMenu().
    Q_PROPERTY(lmp::ui::MenuItemListModel* menu READ menu CONSTANT FINAL)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged FINAL)
    // "Autoplay off" while related songs will not top the queue up, else empty.
    Q_PROPERTY(QString autoplayStatus READ autoplayStatus NOTIFY autoplayStatusChanged FINAL)

public:
    // How many recent plays the idle panel lists.
    static constexpr int RecentLimit = 6;

    // Every service must outlive the view model.
    QueueRailViewModel(core::PlayerController& player, core::db::LibraryQueries& library,
                       ToastQueue& toasts, core::connect::ConnectSync* link = nullptr,
                       QObject* parent = nullptr);

    [[nodiscard]] QueueListModel* queue() { return &m_queue; }
    [[nodiscard]] QueueListModel* recent() { return &m_recent; }
    [[nodiscard]] MenuItemListModel* menu() { return &m_menu; }
    // The song menu whose items a row's menu also offers (Add to playlist,
    // Like, Share, Go to artist, Go to album), as the reference's queue menu
    // does. It outlives the view model; without it the row menu keeps only
    // the queue's own actions.
    void setTrackMenu(TrackMenuController* trackMenu) { m_trackMenu = trackMenu; }
    [[nodiscard]] bool hasTrack() const;
    [[nodiscard]] QString autoplayStatus() const;

    Q_INVOKABLE void playRow(int row, const QString& rowKey);
    Q_INVOKABLE void moveRow(int from, int to, const QString& rowKey);
    // Fills `menu` for the row; false when the row is no longer there.
    Q_INVOKABLE bool prepareMenu(int row, const QString& rowKey);
    // Runs a menu item on the row the menu was prepared for, wherever that
    // song stands now.
    Q_INVOKABLE void triggerMenu(const QString& itemId);
    // Empties the list; the playing song goes on. The view asks first.
    Q_INVOKABLE void clearUpcoming();
    // Plays a recent song with the whole history as what follows.
    Q_INVOKABLE void playRecent(int row);

signals:
    void trackChanged();
    void autoplayStatusChanged();

private:
    [[nodiscard]] bool remote() const;
    [[nodiscard]] qsizetype visibleCount() const;
    struct PendingUndo {
        QString actionId;
        qsizetype position = 0;
        core::QueueEntry entry;
    };

    // The live row for a command: `row` when it still holds `rowKey`.
    [[nodiscard]] bool rowStillMatches(int row, const QString& rowKey) const;
    // Where the song last seen at `position` stands now (the copy nearest to
    // it when listed twice), or -1 once it has left.
    [[nodiscard]] qsizetype livePosition(qsizetype position, const QString& identity) const;
    void removeWithUndo(qsizetype position);
    void moveToFront(qsizetype position);
    void undo(const QString& actionId);
    void refreshQueue();
    void refreshRecent();

    core::PlayerController& m_player;
    // While another device plays, Up Next is its list: a row plays there,
    // Clear clears it there, and rows cannot be moved or removed from here.
    core::connect::ConnectSync* m_connect = nullptr;
    core::db::LibraryQueries& m_library;
    ToastQueue& m_toasts;
    QueueListModel m_queue;
    QueueListModel m_recent;
    MenuItemListModel m_menu;
    TrackMenuController* m_trackMenu = nullptr;
    // The history, newest first, each song once: what a recent play goes on with.
    QList<core::Track> m_recentContext;
    // The row the menu was prepared for.
    qsizetype m_menuPosition = -1;
    QString m_menuIdentity;
    // The removals whose toast still offers Undo, oldest first.
    QList<PendingUndo> m_undos;
    int m_nextUndo = 1;
};

} // namespace lmp::ui
