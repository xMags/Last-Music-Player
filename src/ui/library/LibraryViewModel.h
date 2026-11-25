#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include "ui/library/CollectionCardModel.h"
#include "ui/menus/MenuItemListModel.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class CollectionActions;
class HomeMixService;
class LibraryScanner;
class PlayerController;
class SettingsStore;
class TrackActions;
} // namespace lmp::core

namespace lmp::core::mode {
class ModeService;
}

namespace lmp::core::db {
class GroupQueries;
class LibraryQueries;
} // namespace lmp::core::db

namespace lmp::ui {

class Navigator;
class ShareController;
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
    // The Playlists tab's filter: "manual" (Liked Songs, the smart playlists
    // and the user's own) or "auto" (the auto mixes).
    Q_PROPERTY(QString playlistFilter READ playlistFilter WRITE setPlaylistFilter NOTIFY playlistFilterChanged FINAL)
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
    // A playlist, mix, album or genre card's menu (library.ts
    // openCardActionsMenu): Play now, Play next, Add to queue, Shuffle,
    // Download, then Share.
    Q_PROPERTY(lmp::ui::MenuItemListModel* cardMenu READ cardMenu CONSTANT FINAL)

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
        // A card menu's Download (where songs can be kept) and Share; any
        // may be missing, and its item is left out then.
        const core::mode::ModeService* mode = nullptr;
        ShareController* share = nullptr;
        std::function<bool(const QString&, const QString&, const QString&, const QList<core::Track>&)> download = {};
        // Keeps the tab, the playlists filter and the sorts between runs;
        // may be missing.
        core::SettingsStore* settings = nullptr;
    };

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit LibraryViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] QString tab() const { return m_tab; }
    void setTab(const QString& tab);
    [[nodiscard]] QString playlistFilter() const { return m_playlistFilter; }
    void setPlaylistFilter(const QString& filter);
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
    [[nodiscard]] MenuItemListModel* cardMenu() { return &m_cardMenu; }
    // Fills cardMenu for the card at `row` of `tab`; false when it has none.
    Q_INVOKABLE bool prepareCardMenu(const QString& tab, int row);
    // An account artist's card menu (openArtistActionsMenu): the saved
    // songs' Play, Play next, Add to queue, Shuffle and Download, then Follow
    // or Unfollow and View artist, which the page runs itself ("follow",
    // "details"). False for no name.
    Q_INVOKABLE bool prepareArtistMenu(const QString& name, bool followed);
    Q_INVOKABLE void triggerCardMenu(const QString& itemId);
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
    void playlistFilterChanged();
    void historySortChanged();
    void songsChanged();
    void contentChanged();

private:
    // Every song of a card, as its page lists them.
    [[nodiscard]] QList<core::Track> tracksOf(const QString& kind, const QString& key);
    [[nodiscard]] TrackListModel* listNamed(const QString& list);
    void readPlaylists();
    void readGroups();
    void readHistory();
    void readSongs();
    void followPlayer();
    void onLiked(const QString& sourceKey, bool liked);
    void followNavigator();
    // The tab the user last chose (LibraryTab), or Playlists.
    [[nodiscard]] QString savedTab() const;

    core::db::LibraryQueries& m_library;
    core::db::GroupQueries& m_groups;
    core::TrackActions& m_actions;
    core::HomeMixService& m_mixes;
    core::PlayerController& m_player;
    TrackMenuController& m_trackMenu;
    Navigator& m_navigator;

    QString m_tab = QStringLiteral("playlists");
    QString m_playlistFilter = QStringLiteral("manual");
    CollectionCardModel m_playlists;
    MenuItemListModel m_cardMenu;
    QString m_menuKind;
    QString m_menuKey;
    QString m_menuTitle;
    const core::mode::ModeService* m_mode = nullptr;
    ShareController* m_share = nullptr;
    std::function<bool(const QString&, const QString&, const QString&, const QList<core::Track>&)> m_download;
    core::SettingsStore* m_settings = nullptr;
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
