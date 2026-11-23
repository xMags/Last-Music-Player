#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "core/db/LibraryQueryTypes.h"
#include "core/library/DetailSortPolicy.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class PlayerController;
class SettingsStore;
class TrackActions;
} // namespace lmp::core

namespace lmp::core::db {
class LibraryQueries;
}

namespace lmp::ui {

class Navigator;
class TrackMenuController;

// The page of one local collection (`App.collection`): an album (from song
// tags, or one the user made), a genre, a user playlist or a system playlist
// (Favourites, Most Played, Recently Added). It follows the navigator: a
// Collection route of one of those kinds opens it.
//
// The header (collection-header.ts): the cover, a collage of up to four
// distinct covers, a system playlist's gradient or the genre's drawn tile;
// the eyebrow, title, byline and info line ("12 songs · 48 min"); Play,
// Shuffle and a menu. Then the current app's detail toolbar (kept by D10):
// find in the list, sort (Custom order only where the collection stores
// one), row density, a multi-selection with its bar (add to queue; move to
// another playlist and remove, on a user playlist), and reordering a user
// playlist by dragging while it shows its own order unfiltered.
//
// GUI thread only.
class CollectionViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.collection")

    // "album", "album-collection", "genre", "playlist" or "system".
    Q_PROPERTY(QString kind READ kind NOTIFY opened FINAL)
    Q_PROPERTY(QString title READ title NOTIFY opened FINAL)
    Q_PROPERTY(QString eyebrow READ eyebrow NOTIFY opened FINAL)
    Q_PROPERTY(QString backLabel READ backLabel NOTIFY opened FINAL)
    Q_PROPERTY(QString description READ description NOTIFY opened FINAL)
    Q_PROPERTY(QString byline READ byline NOTIFY contentChanged FINAL)
    // "12 songs · 48 min", for the whole collection (not only loaded rows).
    Q_PROPERTY(QString info READ info NOTIFY contentChanged FINAL)
    // "image" (artworkUrl or the lettered placeholder), "collage", "system" or "genre".
    Q_PROPERTY(QString coverStyle READ coverStyle NOTIFY contentChanged FINAL)
    Q_PROPERTY(QUrl artworkUrl READ artworkUrl NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString artworkId READ artworkId NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString artworkLabel READ artworkLabel NOTIFY contentChanged FINAL)
    // Four tile addresses in slot order (a 2x2 collage).
    Q_PROPERTY(QList<QUrl> collage READ collage NOTIFY contentChanged FINAL)
    Q_PROPERTY(QColor gradientStart READ gradientStart NOTIFY opened FINAL)
    Q_PROPERTY(QColor gradientEnd READ gradientEnd NOTIFY opened FINAL)
    Q_PROPERTY(QString iconName READ iconName NOTIFY opened FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(int songCount READ songCount NOTIFY contentChanged FINAL)
    // What the list says when it has no rows.
    Q_PROPERTY(QString emptyText READ emptyText NOTIFY contentChanged FINAL)

    Q_PROPERTY(QString findText READ findText WRITE setFindText NOTIFY toolbarChanged FINAL)
    Q_PROPERTY(QString findPlaceholder READ findPlaceholder NOTIFY opened FINAL)
    Q_PROPERTY(int sort READ sort WRITE setSort NOTIFY toolbarChanged FINAL)
    Q_PROPERTY(QStringList sorts READ sorts NOTIFY opened FINAL)
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY compactChanged FINAL)
    // A user playlist (move, remove, reorder, Rename and Delete belong to it).
    Q_PROPERTY(bool userPlaylist READ userPlaylist NOTIFY opened FINAL)
    // Rows can be dragged into a new order now.
    Q_PROPERTY(bool canReorder READ canReorder NOTIFY toolbarChanged FINAL)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged FINAL)
    // The user's other playlists, as menu items (id = key, label = title).
    Q_PROPERTY(QVariantList moveTargets READ moveTargets NOTIFY selectionChanged FINAL)

public:
    struct Parts {
        core::db::LibraryQueries& library;
        core::TrackActions& actions;
        core::PlayerController& player;
        core::SettingsStore& settings;
        TrackMenuController& trackMenu;
        Navigator& navigator;
    };

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit CollectionViewModel(Parts parts, QObject* parent = nullptr);

    // Shows the collection; the navigator calls this through its route.
    void open(const QString& kind, const QString& key, const QString& title);

    [[nodiscard]] QString kind() const { return m_kind; }
    [[nodiscard]] QString key() const { return m_key; }
    [[nodiscard]] QString title() const { return m_title; }
    [[nodiscard]] QString eyebrow() const;
    [[nodiscard]] QString backLabel() const;
    [[nodiscard]] QString description() const { return m_description; }
    [[nodiscard]] QString byline() const { return m_byline; }
    [[nodiscard]] QString info() const;
    [[nodiscard]] QString coverStyle() const { return m_coverStyle; }
    [[nodiscard]] QUrl artworkUrl() const { return m_artworkUrl; }
    [[nodiscard]] QString artworkId() const;
    [[nodiscard]] QString artworkLabel() const;
    [[nodiscard]] QList<QUrl> collage() const { return m_collage; }
    [[nodiscard]] QColor gradientStart() const { return m_gradientStart; }
    [[nodiscard]] QColor gradientEnd() const { return m_gradientEnd; }
    [[nodiscard]] QString iconName() const { return m_iconName; }
    [[nodiscard]] TrackListModel* tracks() { return &m_tracks; }
    [[nodiscard]] int songCount() const { return m_tracks.totalCount(); }
    [[nodiscard]] QString emptyText() const;

    [[nodiscard]] QString findText() const { return m_findText; }
    void setFindText(const QString& text);
    [[nodiscard]] QString findPlaceholder() const;
    [[nodiscard]] int sort() const;
    void setSort(int index);
    [[nodiscard]] QStringList sorts() const;
    [[nodiscard]] bool compact() const;
    void setCompact(bool compact);
    [[nodiscard]] bool userPlaylist() const { return m_kind == QLatin1StringView("playlist"); }
    [[nodiscard]] bool canReorder() const;
    [[nodiscard]] int selectedCount() const { return static_cast<int>(m_selection.size()); }
    [[nodiscard]] QVariantList moveTargets() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void playNextAll();
    Q_INVOKABLE void addAllToQueue();
    // A row was clicked: with Ctrl it joins or leaves the selection, with
    // Shift the range from the last one is selected, otherwise it plays.
    Q_INVOKABLE void activate(int row, bool toggle, bool extend);
    Q_INVOKABLE void toggleSelected(int row);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void queueSelection();
    Q_INVOKABLE void removeSelection();
    Q_INVOKABLE void moveSelection(const QString& playlistKey);
    Q_INVOKABLE void like(int row);
    Q_INVOKABLE void prepareMenu(int row);
    // Drops the dragged row `from` at `to` and stores the playlist's new order.
    Q_INVOKABLE bool moveRow(int from, int to);
    // Every page loaded (before a drag starts).
    Q_INVOKABLE void loadAll();

    // The sorts a page of this kind offers, in menu order.
    [[nodiscard]] static QList<core::DetailSort> sortsFor(const QString& kind);

signals:
    void opened();
    void contentChanged();
    void toolbarChanged();
    void compactChanged();
    void selectionChanged();
    // The selection's songs were removed or moved (for a short notice).
    void notice(const QString& text);

private:
    void followNavigator();
    void reload();
    void readCover();
    void followPlayer();
    void setSelection(QList<int> rows);
    [[nodiscard]] QList<core::Track> selectedTracks() const;
    [[nodiscard]] QList<core::Track> allTracks();
    [[nodiscard]] core::db::TrackQuery query() const;

    core::db::LibraryQueries& m_library;
    core::TrackActions& m_actions;
    core::PlayerController& m_player;
    core::SettingsStore& m_settings;
    TrackMenuController& m_trackMenu;
    Navigator& m_navigator;

    QString m_kind;
    QString m_key;
    QString m_title;
    QString m_description;
    QString m_byline;
    QString m_coverStyle = QStringLiteral("image");
    QUrl m_artworkUrl;
    QList<QUrl> m_collage;
    QColor m_gradientStart;
    QColor m_gradientEnd;
    QString m_iconName;
    core::db::TrackQuery m_systemQuery;
    int m_totalCount = 0;
    double m_totalSeconds = 0.0;
    // Set while this page stores a dragged order.
    bool m_reordering = false;

    TrackListModel m_tracks;
    QString m_findText;
    core::DetailSort m_sort = core::DetailSort::DateAdded;
    QList<int> m_selection;
    int m_anchor = -1;
};

} // namespace lmp::ui
