#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "ui/library/CollectionCardModel.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class CollectionActions;
class HomeMixService;
class LibraryScanner;
class PlayerController;
class TrackActions;
} // namespace lmp::core

namespace lmp::core::db {
class GroupQueries;
class LibraryQueries;
} // namespace lmp::core::db

namespace lmp::ui {

class Navigator;
class TrackMenuController;

// The Library page (`App.library`): the reference's tabs (Playlists, History,
// Albums, Artists, Genres) and, kept from the current app, Songs. Playlists
// lists the system playlists, the user's playlists, then the generated mixes. Each tab's
// data is the local library's, read from the legacy queries: the system
// playlists Favourites, Most Played and Recently Added before the user's
// own; albums, artists and genres as the library groups them; History in
// play order with the reference's sorts; Songs in pages with the current
// app's sorts and a filter. A card opens its collection page; a song plays
// in its list.
//
// The tab is kept in the history entry (Back returns to it).
//
// GUI thread only.
class LibraryViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.library")

    // "playlists", "history", "albums", "artists", "genres" or "songs".
    Q_PROPERTY(QString tab READ tab WRITE setTab NOTIFY tabChanged FINAL)
    // One map per tab: value and label.
    Q_PROPERTY(QVariantList tabs READ tabs CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* playlists READ playlists CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* albums READ albums CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* artists READ artists CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* genres READ genres CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* history READ history CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* songs READ songs CONSTANT FINAL)
    // History's sort: "relevance", "title", "artist" or "duration".
    Q_PROPERTY(int historySort READ historySort WRITE setHistorySort NOTIFY historySortChanged FINAL)
    Q_PROPERTY(QStringList historySorts READ historySorts CONSTANT FINAL)
    // Songs' sort, an index into songsSorts.
    Q_PROPERTY(int songsSort READ songsSort WRITE setSongsSort NOTIFY songsChanged FINAL)
    Q_PROPERTY(QStringList songsSorts READ songsSorts CONSTANT FINAL)
    Q_PROPERTY(QString songsFilter READ songsFilter WRITE setSongsFilter NOTIFY songsChanged FINAL)
    // The library has no songs at all (the empty state with a folder button).
    Q_PROPERTY(bool empty READ empty NOTIFY contentChanged FINAL)

public:
    struct Parts {
        core::db::LibraryQueries& library;
        core::db::GroupQueries& groups;
        core::TrackActions& actions;
        core::CollectionActions& collections;
        core::HomeMixService& mixes;
        core::PlayerController& player;
        core::LibraryScanner& scanner;
        TrackMenuController& trackMenu;
        Navigator& navigator;
    };

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit LibraryViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] QString tab() const { return m_tab; }
    void setTab(const QString& tab);
    [[nodiscard]] QVariantList tabs() const;
    [[nodiscard]] CollectionCardModel* playlists() { return &m_playlists; }
    [[nodiscard]] CollectionCardModel* albums() { return &m_albums; }
    [[nodiscard]] CollectionCardModel* artists() { return &m_artists; }
    [[nodiscard]] CollectionCardModel* genres() { return &m_genres; }
    [[nodiscard]] TrackListModel* history() { return &m_history; }
    [[nodiscard]] TrackListModel* songs() { return &m_songs; }
    [[nodiscard]] int historySort() const { return m_historySort; }
    void setHistorySort(int sort);
    [[nodiscard]] QStringList historySorts() const;
    [[nodiscard]] int songsSort() const { return m_songsSort; }
    void setSongsSort(int sort);
    [[nodiscard]] QStringList songsSorts() const;
    [[nodiscard]] QString songsFilter() const { return m_songsFilter; }
    void setSongsFilter(const QString& filter);
    [[nodiscard]] bool empty() const { return m_empty; }

    // Reads every tab again (the page opens, a scan finished).
    Q_INVOKABLE void refresh();
    // A card of the given tab's model was chosen.
    Q_INVOKABLE void openCard(const QString& tab, int row);
    // A song of History or Songs was chosen: it plays in its list.
    Q_INVOKABLE void playSong(const QString& list, int row);
    Q_INVOKABLE void likeSong(const QString& list, int row);
    // Fills App.trackMenu for the song; the page opens the menu.
    Q_INVOKABLE void prepareSongMenu(const QString& list, int row);

    // The system playlists' keys, as the current app named them.
    static constexpr QLatin1StringView FavouritesKey{"smart-liked"};
    static constexpr QLatin1StringView MostPlayedKey{"smart-most"};
    static constexpr QLatin1StringView RecentlyAddedKey{"smart-recent"};

signals:
    void tabChanged();
    void historySortChanged();
    void songsChanged();
    void contentChanged();

private:
    [[nodiscard]] TrackListModel* listNamed(const QString& list);
    void readPlaylists();
    void readGroups();
    void readHistory();
    void readSongs();
    void followPlayer();
    void onLiked(const QString& sourceKey, bool liked);
    void followNavigator();

    core::db::LibraryQueries& m_library;
    core::db::GroupQueries& m_groups;
    core::TrackActions& m_actions;
    core::HomeMixService& m_mixes;
    core::PlayerController& m_player;
    TrackMenuController& m_trackMenu;
    Navigator& m_navigator;

    QString m_tab = QStringLiteral("playlists");
    CollectionCardModel m_playlists;
    CollectionCardModel m_albums;
    CollectionCardModel m_artists;
    CollectionCardModel m_genres;
    TrackListModel m_history;
    TrackListModel m_songs;
    int m_historySort = 0;
    int m_songsSort = 0;
    QString m_songsFilter;
    bool m_empty = false;
};

} // namespace lmp::ui
