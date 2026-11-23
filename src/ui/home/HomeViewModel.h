#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

#include "core/model/Track.h"
#include "ui/catalog/CatalogCardModel.h"
#include "ui/home/HomeCatalogFeed.h"
#include "ui/home/HomeListModels.h"
#include "ui/library/CollectionCardModel.h"
#include "ui/menus/MenuItemListModel.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class CollectionActions;
class HomeMixService;
class LibraryScanner;
class PlayerController;
class TrackActions;
} // namespace lmp::core

namespace lmp::core::db {
class LibraryQueries;
}

namespace lmp::core::catalog {
class CatalogService;
}

namespace lmp::core::mode {
class ModeService;
}

namespace lmp::core::account {
class AccountSessionService;
class MusicSyncService;
} // namespace lmp::core::account

namespace lmp::ui {

class Navigator;
class TrackMenuController;

// Home in its offline form (`App.home`; views/home.ts with the built-in
// layout): the greeting with its time of day, the "Jump back in" card for
// the last song played with shortcuts to the user's own playlists, then the
// For you tier (the generated mixes, Listen Again) and the From your library
// tier (Most Played from three songs, Liked Songs, Recently Added). Sections
// with nothing to show are left out. An empty library shows an empty state
// that offers to choose a music folder.
//
// GUI thread only.
class HomeViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.home")

    // "sunrise", "sun", "sunset" or "moon", and its salutation.
    Q_PROPERTY(QString greetingTime READ greetingTime NOTIFY greetingChanged FINAL)
    Q_PROPERTY(QString salutation READ salutation NOTIFY greetingChanged FINAL)
    Q_PROPERTY(bool hasHero READ hasHero NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString heroTitle READ heroTitle NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString heroArtist READ heroArtist NOTIFY contentChanged FINAL)
    Q_PROPERTY(QUrl heroArtworkUrl READ heroArtworkUrl NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString heroArtworkId READ heroArtworkId NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString heroArtworkLabel READ heroArtworkLabel NOTIFY contentChanged FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* quickTiles READ quickTiles CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::HomeMixListModel* mixes READ mixes CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* listenAgain READ listenAgain CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* mostPlayed READ mostPlayed CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* liked READ liked CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* recentlyAdded READ recentlyAdded CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::HomeSectionListModel* sections READ sections CONSTANT FINAL)
    // The menu of a mix card or a shortcut tile (Home's own items).
    Q_PROPERTY(lmp::ui::MenuItemListModel* cardMenu READ cardMenu CONSTANT FINAL)
    Q_PROPERTY(bool empty READ empty NOTIFY contentChanged FINAL)
    // The catalogue tiers (Account mode): loading, a failure with nothing
    // shown, the skeleton while nothing is shown, an older copy shown after a
    // failed refresh, and the region's name.
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY catalogChanged FINAL)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY catalogChanged FINAL)
    Q_PROPERTY(bool catalogSkeleton READ catalogSkeleton NOTIFY catalogChanged FINAL)
    Q_PROPERTY(bool catalogCached READ catalogCached NOTIFY catalogChanged FINAL)
    Q_PROPERTY(QString regionName READ regionName NOTIFY catalogChanged FINAL)

public:
    using Clock = std::function<QDateTime()>;

    struct Parts {
        core::db::LibraryQueries& library;
        core::TrackActions& actions;
        core::CollectionActions& collections;
        core::PlayerController& player;
        core::LibraryScanner& scanner;
        core::HomeMixService& mixes;
        TrackMenuController& trackMenu;
        Navigator& navigator;
        Clock now;
        // Account mode's catalogue tiers and the service's Home layout; all
        // or none (then Home has its local sections only).
        core::catalog::CatalogService* catalog = nullptr;
        core::mode::ModeService* mode = nullptr;
        core::account::MusicSyncService* sync = nullptr;
        core::account::AccountSessionService* session = nullptr;
    };

    // Shelf lengths (home.ts and the current app: twelve songs a row).
    static constexpr int ShelfLength = 12;
    static constexpr int QuickTileLimit = 6;
    static constexpr int QuickTileColumns = 3;
    // Most Played shows from this many songs.
    static constexpr int MostPlayedMinimum = 3;

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit HomeViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] QString greetingTime() const { return m_greetingTime; }
    [[nodiscard]] QString salutation() const;
    [[nodiscard]] bool hasHero() const { return m_hero.has_value(); }
    [[nodiscard]] QString heroTitle() const;
    [[nodiscard]] QString heroArtist() const;
    [[nodiscard]] QUrl heroArtworkUrl() const;
    [[nodiscard]] QString heroArtworkId() const;
    [[nodiscard]] QString heroArtworkLabel() const;
    [[nodiscard]] CollectionCardModel* quickTiles() { return &m_quickTiles; }
    [[nodiscard]] HomeMixListModel* mixes() { return &m_mixCards; }
    [[nodiscard]] TrackListModel* listenAgain() { return &m_listenAgain; }
    [[nodiscard]] TrackListModel* mostPlayed() { return &m_mostPlayed; }
    [[nodiscard]] TrackListModel* liked() { return &m_liked; }
    [[nodiscard]] TrackListModel* recentlyAdded() { return &m_recentlyAdded; }
    [[nodiscard]] HomeSectionListModel* sections() { return &m_sections; }
    [[nodiscard]] MenuItemListModel* cardMenu() { return &m_cardMenu; }
    [[nodiscard]] bool empty() const { return m_empty; }
    [[nodiscard]] bool catalogLoading() const;
    [[nodiscard]] QString catalogError() const;
    [[nodiscard]] bool catalogSkeleton() const;
    [[nodiscard]] bool catalogCached() const;
    [[nodiscard]] QString regionName() const;

    // Reads every section again (the page opens, the library changed).
    Q_INVOKABLE void refresh();
    // The part of the day, from the clock (the page checks it now and then).
    Q_INVOKABLE void refreshGreeting();

    Q_INVOKABLE void playHero();
    Q_INVOKABLE void prepareHeroMenu();
    // A song of a shelf ("listenAgain", "mostPlayed", "liked", "recentlyAdded").
    Q_INVOKABLE void playSong(const QString& shelf, int row);
    Q_INVOKABLE void prepareSongMenu(const QString& shelf, int row);
    // A section's "See all".
    Q_INVOKABLE void seeAll(const QString& shelf);
    Q_INVOKABLE void playMix(int row);
    Q_INVOKABLE void prepareMixMenu(int row);
    Q_INVOKABLE void playQuickTile(int row);
    Q_INVOKABLE void prepareQuickTileMenu(int row);
    // An item of cardMenu was chosen.
    Q_INVOKABLE void triggerCardMenu(const QString& itemId);

    // A catalogue section's cards ("catalog" and "charts" sections).
    Q_INVOKABLE lmp::ui::CatalogCardModel* catalogShelf(const QString& key);
    Q_INVOKABLE lmp::ui::ChartCardModel* chartRow(const QString& key);
    // A catalogue card: a song plays with the shelf's other songs.
    Q_INVOKABLE void playCatalogCard(const QString& key, int row);
    Q_INVOKABLE void retryCatalog();
    // Home is no longer shown: a pending catalogue retry waits.
    Q_INVOKABLE void leave();

    // The part of the day for an hour (home.ts greetingTime).
    [[nodiscard]] static QString greetingTimeFor(int hour);

signals:
    void greetingChanged();
    void contentChanged();
    void catalogChanged();

private:
    [[nodiscard]] TrackListModel* shelfNamed(const QString& shelf);
    void readMixes();
    void readQuickTiles();
    void readSections();
    void readAccountSections();
    [[nodiscard]] bool accountHome() const;
    void followPlayer();
    [[nodiscard]] QList<core::Track> tracksOf(const QString& kind, const QString& key);
    void openCollection(const QString& kind, const QString& key, const QString& title);

    core::db::LibraryQueries& m_library;
    core::TrackActions& m_actions;
    core::PlayerController& m_player;
    core::HomeMixService& m_mixService;
    TrackMenuController& m_trackMenu;
    Navigator& m_navigator;
    Clock m_now;
    QTimer m_greetingTimer;
    core::mode::ModeService* m_mode = nullptr;
    core::account::MusicSyncService* m_sync = nullptr;
    core::account::AccountSessionService* m_session = nullptr;
    // Account mode's catalogue; null without one.
    HomeCatalogFeed* m_feed = nullptr;

    QString m_greetingTime;
    std::optional<core::Track> m_hero;
    QList<core::Track> m_history;
    CollectionCardModel m_quickTiles;
    HomeMixListModel m_mixCards;
    TrackListModel m_listenAgain;
    TrackListModel m_mostPlayed;
    TrackListModel m_liked;
    TrackListModel m_recentlyAdded;
    HomeSectionListModel m_sections;
    MenuItemListModel m_cardMenu;
    bool m_empty = false;
    // What cardMenu acts on: a mix id, or a tile's collection.
    QString m_menuKind;
    QString m_menuKey;
    QString m_menuTitle;
};

} // namespace lmp::ui
