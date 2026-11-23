#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

#include "core/home/HomeModel.h"

#include <optional>

#include "core/model/Track.h"
#include "ui/library/CollectionCardModel.h"
#include "ui/search/SearchSource.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class PlayerController;
class SettingsStore;
class TrackActions;
} // namespace lmp::core

namespace lmp::core::db {
class GroupQueries;
class LibraryQueries;
} // namespace lmp::core::db

namespace lmp::ui {

class Navigator;
class TrackMenuController;

// Browse and search (`App.search`; views/search.ts with the current app's
// recent searches). With no query: the recent searches and the category
// tiles (the library's genres, then built-in shortcuts). Typing searches
// after 350 ms; Enter searches at once. A query that stays the same for two
// seconds, or is searched with Enter, joins the recent searches (legacy
// RecentSearches setting, six at most).
//
// Results as the reference groups them (BrowseGroupingPolicy): the top
// result and the first four songs side by side, then artists, albums and
// playlists as card rows, then every song with List / Grid and a sort.
//
// Every search carries a generation number; an answer for an older one is
// dropped, so a slow answer never replaces a newer one.
//
// GUI thread only.
class SearchViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.search")

    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    // "landing", "searching" (no answer yet), "results" or "empty".
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList recentSearches READ recentSearches NOTIFY recentSearchesChanged FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* categories READ categories CONSTANT FINAL)
    // The top result: "song", "artist", "album" or "" (none).
    Q_PROPERTY(QString topKind READ topKind NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString topTitle READ topTitle NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString topDetail READ topDetail NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QUrl topArtworkUrl READ topArtworkUrl NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString topArtworkId READ topArtworkId NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString topArtworkLabel READ topArtworkLabel NOTIFY resultsChanged FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* topSongs READ topSongs CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* songs READ songs CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* artists READ artists CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* albums READ albums CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* playlists READ playlists CONSTANT FINAL)
    // All songs' sort: an index into songSorts (Relevance, Title, Artist, Duration).
    Q_PROPERTY(int songSort READ songSort WRITE setSongSort NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QStringList songSorts READ songSorts CONSTANT FINAL)

public:
    struct Parts {
        SearchSource& source;
        core::db::LibraryQueries& library;
        core::db::GroupQueries& groups;
        core::TrackActions& actions;
        core::PlayerController& player;
        core::SettingsStore& settings;
        TrackMenuController& trackMenu;
        Navigator& navigator;
        // The account library in Account mode, else nothing: Browse's tiles
        // then come from it as the reference client ranks them.
        std::function<std::optional<core::home::HomeLibrary>()> accountLibrary = {};
    };

    // The pause after typing (views/search.ts and the current app: 350 ms)
    // and how long a query must stay before it joins the recent searches.
    static constexpr int TypingDelayMs = 350;
    static constexpr int RecentDwellMs = 2000;
    static constexpr int TopSongCount = 4;
    static constexpr int CategoryTileCount = 8;

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit SearchViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] QString query() const { return m_query; }
    void setQuery(const QString& query);
    [[nodiscard]] QString state() const { return m_state; }
    [[nodiscard]] QStringList recentSearches() const { return m_recent; }
    [[nodiscard]] CollectionCardModel* categories() { return &m_categories; }
    [[nodiscard]] QString topKind() const { return m_topKind; }
    [[nodiscard]] QString topTitle() const { return m_topTitle; }
    [[nodiscard]] QString topDetail() const { return m_topDetail; }
    [[nodiscard]] QUrl topArtworkUrl() const { return m_topArtworkUrl; }
    [[nodiscard]] QString topArtworkId() const { return m_topArtworkId; }
    [[nodiscard]] QString topArtworkLabel() const { return m_topArtworkLabel; }
    [[nodiscard]] TrackListModel* topSongs() { return &m_topSongs; }
    [[nodiscard]] TrackListModel* songs() { return &m_songs; }
    [[nodiscard]] CollectionCardModel* artists() { return &m_artists; }
    [[nodiscard]] CollectionCardModel* albums() { return &m_albums; }
    [[nodiscard]] CollectionCardModel* playlists() { return &m_playlists; }
    [[nodiscard]] int songSort() const { return m_songSort; }
    void setSongSort(int sort);
    [[nodiscard]] QStringList songSorts() const;
    // The generation of the latest search (for tests).
    [[nodiscard]] quint64 generation() const { return m_generation; }

    // Enter: search now, and remember the query.
    Q_INVOKABLE void searchNow();
    // Ctrl+K: the page puts the caret in its search field.
    Q_INVOKABLE void requestFocus() { emit focusRequested(); }
    Q_INVOKABLE void useRecent(int row);
    Q_INVOKABLE void removeRecent(int row);
    Q_INVOKABLE void clearRecents();
    // Reads the category tiles again (the page opens).
    Q_INVOKABLE void refreshLanding();
    // A category tile: a genre of the library opens its page; a shortcut searches.
    Q_INVOKABLE void openCategory(int row);
    Q_INVOKABLE void openTop();
    // A card of "artists", "albums" or "playlists".
    Q_INVOKABLE void openCard(const QString& group, int row);
    // A song of "top" (the first four) or "all".
    Q_INVOKABLE void playSong(const QString& list, int row);
    Q_INVOKABLE void likeSong(const QString& list, int row);
    Q_INVOKABLE void prepareSongMenu(const QString& list, int row);

signals:
    void queryChanged();
    void stateChanged();
    void recentSearchesChanged();
    void resultsChanged();
    void focusRequested();

private:
    void refreshAccountLanding(const core::home::HomeLibrary& library);
    void run();
    void apply(quint64 generation, const QString& query, SearchHits hits);
    void showSongs();
    void setState(const QString& state);
    void record(const QString& query);
    void followPlayer();
    [[nodiscard]] TrackListModel* listNamed(const QString& list);

    SearchSource& m_source;
    core::db::LibraryQueries& m_library;
    core::db::GroupQueries& m_groups;
    core::TrackActions& m_actions;
    core::PlayerController& m_player;
    core::SettingsStore& m_settings;
    TrackMenuController& m_trackMenu;
    Navigator& m_navigator;

    QTimer m_typing;
    QTimer m_dwell;
    QString m_query;
    QString m_state = QStringLiteral("landing");
    quint64 m_generation = 0;
    QString m_shownQuery;
    QStringList m_recent;
    CollectionCardModel m_categories;
    std::function<std::optional<core::home::HomeLibrary>()> m_accountLibrary;
    QList<core::Track> m_hits;
    QString m_topKind;
    QString m_topTitle;
    QString m_topDetail;
    QUrl m_topArtworkUrl;
    QString m_topArtworkId;
    QString m_topArtworkLabel;
    std::optional<core::Track> m_topSong;
    QString m_topKey;
    TrackListModel m_topSongs;
    TrackListModel m_songs;
    CollectionCardModel m_artists;
    CollectionCardModel m_albums;
    CollectionCardModel m_playlists;
    int m_songSort = 0;
};

} // namespace lmp::ui
