#pragma once

#include <QFuture>
#include <functional>

#include "core/async/Result.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <optional>

#include "core/catalog/ArtistPageLoader.h"
#include "core/catalog/CatalogModels.h"
#include "ui/catalog/ArtistSectionListModel.h"
#include "ui/catalog/CatalogCardModel.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class TrackActions;
namespace account {
class AccountClient;
class AccountSessionService;
} // namespace account
namespace catalog {
class CatalogClient;
class CatalogService;
} // namespace catalog
} // namespace lmp::core

namespace lmp::core {
class ArtistFollowService;
}

namespace lmp::ui {

class ShareController;

class Navigator;
class Route;
class TrackMenuController;

// An artist page from the catalogue (App.catalogArtist; views/catalog.ts
// artist part): the header with the artist's picture, Play, Shuffle and
// Follow; the top songs (five, then all); and the page's other sections as
// shelves (latest release, albums, singles and EPs, similar artists), each
// with "See all". The same ArtistPage component shows it and the library's
// artist (App.localArtist): the properties it reads are named alike.
//
// It follows the navigator: SourceArtist routes and CatalogDetail routes of
// type "artist" load here, through core::catalog::ArtistPageLoader.
//
// GUI thread only.
class CatalogArtistViewModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.catalogArtist")

    Q_PROPERTY(bool loading READ loading NOTIFY changed FINAL)
    Q_PROPERTY(QString error READ error NOTIFY changed FINAL)
    Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
    // The hero's details (renderDetail's metadata): year, first genre,
    // label and rating, as the service gives them.
    Q_PROPERTY(QString info READ info NOTIFY changed FINAL)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY changed FINAL)
    Q_PROPERTY(QString description READ description NOTIFY changed FINAL)
    Q_PROPERTY(QUrl artworkUrl READ artworkUrl NOTIFY changed FINAL)
    Q_PROPERTY(QString artworkId READ artworkId NOTIFY changed FINAL)
    Q_PROPERTY(QString artworkLabel READ artworkLabel NOTIFY changed FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* topSongs READ topSongs CONSTANT FINAL)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed FINAL)
    Q_PROPERTY(bool showingAll READ showingAll WRITE setShowingAll NOTIFY changed FINAL)
    Q_PROPERTY(bool canFollow READ canFollow NOTIFY changed FINAL)
    // The artist can be shared by its identity.
    Q_PROPERTY(bool canShare READ canShare NOTIFY changed FINAL)
    Q_PROPERTY(bool followed READ followed NOTIFY changed FINAL)
    Q_PROPERTY(bool followBusy READ followBusy NOTIFY changed FINAL)
    // One row per shelf below the top songs: key, title, pill, link. Its own
    // model, updated by key, so `changed` rebuilds no shelf.
    Q_PROPERTY(lmp::ui::ArtistSectionListModel* sections READ sections CONSTANT FINAL)

public:
    static constexpr int TopSongsShown = 5;

    struct Parts {
        core::catalog::CatalogService& catalog;
        core::catalog::CatalogClient& client;
        core::account::AccountClient& accountClient;
        core::account::AccountSessionService& session;
        core::TrackActions& actions;
        TrackMenuController& trackMenu;
        Navigator& navigator;
        ShareController* share = nullptr;
        // API key mode: while `providerPages` says so, the page comes from
        // the provider's artist detail (no catalogue lookup) and Follow uses
        // the list on this PC (`follows`).
        std::function<bool()> providerPages = {};
        std::function<QFuture<core::Result<core::catalog::Detail>>(const core::account::LibraryArtistIdentity&)>
            providerArtist = {};
        core::ArtistFollowService* follows = nullptr;
    };

    // Everything in `parts` outlives this object.
    explicit CatalogArtistViewModel(Parts parts, QObject* parent = nullptr);

    // A route this page shows.
    [[nodiscard]] static bool handles(const Route& route);

    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString name() const { return m_detail ? m_detail->item.title : m_pendingName; }
    [[nodiscard]] QString subtitle() const { return m_detail ? m_detail->item.subtitle : QString(); }
    [[nodiscard]] QString info() const;
    [[nodiscard]] QString description() const { return m_detail ? m_detail->item.description : QString(); }
    [[nodiscard]] QUrl artworkUrl() const;
    [[nodiscard]] QString artworkId() const { return QStringLiteral("artist:") + name(); }
    [[nodiscard]] QString artworkLabel() const;
    [[nodiscard]] TrackListModel* topSongs() { return &m_topSongs; }
    [[nodiscard]] bool hasMore() const { return m_allTopSongs.size() > TopSongsShown; }
    [[nodiscard]] bool showingAll() const { return m_showingAll; }
    void setShowingAll(bool showingAll);
    [[nodiscard]] bool canFollow() const;
    [[nodiscard]] bool canShare() const { return canFollow(); }
    [[nodiscard]] bool followed() const;
    [[nodiscard]] bool followBusy() const { return m_followBusy; }
    [[nodiscard]] ArtistSectionListModel* sections() { return &m_sectionList; }

    Q_INVOKABLE lmp::ui::CatalogCardModel* sectionCards(const QString& key);
    Q_INVOKABLE void openSectionCard(const QString& key, int row);
    Q_INVOKABLE void seeAllSection(const QString& key);
    Q_INVOKABLE void play();
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void playSong(int row);
    Q_INVOKABLE void likeSong(int row);
    Q_INVOKABLE void prepareSongMenu(int row);
    Q_INVOKABLE void toggleFollow();
    Q_INVOKABLE void share();
    Q_INVOKABLE void retry();
    // The library page's call; the catalogue page has no album row of its own.
    Q_INVOKABLE void openAlbum(int row) { Q_UNUSED(row); }

    // A song's artist: its identity as the song names it.
    void openArtistOf(const core::Track& track);

signals:
    void changed();

private:
    void followNavigator();
    void load(const Route& route, bool force);
    void show(const core::catalog::ArtistPageResult& result);
    void readFollow();
    void showTopSongs();
    // The section rows from m_detail (none without one).
    void readSections();

    core::catalog::CatalogService& m_catalog;
    core::catalog::CatalogClient& m_client;
    core::account::AccountClient& m_accountClient;
    core::account::AccountSessionService& m_session;
    core::TrackActions& m_actions;
    TrackMenuController& m_trackMenu;
    Navigator& m_navigator;
    std::function<bool()> m_providerPages;
    std::function<QFuture<core::Result<core::catalog::Detail>>(const core::account::LibraryArtistIdentity&)>
        m_providerArtist;
    core::ArtistFollowService* m_follows = nullptr;
    // The page on screen came from the provider.
    bool m_fromProvider = false;
    ShareController* m_share = nullptr;
    core::catalog::ArtistPageLoader m_loader;

    std::uint64_t m_request = 0;
    bool m_loading = false;
    QString m_error;
    QString m_pendingName;
    std::optional<core::catalog::Detail> m_detail;
    core::catalog::ArtistPageTarget m_target;
    QList<core::Track> m_allTopSongs;
    TrackListModel m_topSongs;
    bool m_showingAll = false;
    QHash<QString, CatalogCardModel*> m_sectionModels;
    ArtistSectionListModel m_sectionList;
    // The library's public id while the artist is followed.
    QString m_followedId;
    bool m_followBusy = false;
};

} // namespace lmp::ui
