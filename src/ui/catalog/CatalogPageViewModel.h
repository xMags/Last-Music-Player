#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <optional>

#include "core/catalog/CatalogModels.h"
#include "core/catalog/CatalogPaging.h"
#include "ui/catalog/CatalogCardModel.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class TrackActions;
namespace account {
class AccountLibraryEditor;
}
namespace catalog {
class CatalogService;
}
} // namespace lmp::core

namespace lmp::ui {

class ShareController;

class Navigator;
class Route;
class TrackMenuController;

// The catalogue's album, playlist, chart and shelf pages (App.catalogPage;
// views/catalog.ts): a header, then the songs (or the cards of a list of
// albums, playlists or artists), with Play, Shuffle and Save to library on
// albums and playlists, and endless paging on charts and the long shelves.
//
// It follows the navigator: CatalogDetail routes of albums and playlists,
// SourceAlbum, ExternalPlaylist, Chart and Shelf routes load here (see
// catalog_routes for their parameters). A SourceAlbum route with
// "finding" = "1" first asks the service which album the song is on and then
// replaces itself with that album's route, so the pending page never stays
// in the history.
//
// GUI thread only.
class CatalogPageViewModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.catalogPage")

    // "album", "playlist", "chart" or "shelf".
    Q_PROPERTY(QString kind READ kind NOTIFY changed FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY changed FINAL)
    // Looking for a song's album.
    Q_PROPERTY(bool finding READ finding NOTIFY changed FINAL)
    Q_PROPERTY(QString error READ error NOTIFY changed FINAL)
    Q_PROPERTY(QString eyebrow READ eyebrow NOTIFY changed FINAL)
    Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
    Q_PROPERTY(QString byline READ byline NOTIFY changed FINAL)
    Q_PROPERTY(QString info READ info NOTIFY changed FINAL)
    Q_PROPERTY(QString description READ description NOTIFY changed FINAL)
    Q_PROPERTY(QUrl artworkUrl READ artworkUrl NOTIFY changed FINAL)
    Q_PROPERTY(QString artworkId READ artworkId NOTIFY changed FINAL)
    // Cards instead of songs (a list of albums, playlists or artists).
    Q_PROPERTY(bool showsCards READ showsCards NOTIFY changed FINAL)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed FINAL)
    // The songs on screen can be shared as a list.
    Q_PROPERTY(bool canShare READ canShare NOTIFY changed FINAL)
    Q_PROPERTY(bool saved READ saved NOTIFY changed FINAL)
    Q_PROPERTY(bool saving READ saving NOTIFY changed FINAL)
    // "idle", "loading", "failed" or "done".
    Q_PROPERTY(QString pagingState READ pagingState NOTIFY changed FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CatalogCardModel* cards READ cards CONSTANT FINAL)

public:
    struct Parts {
        core::catalog::CatalogService& catalog;
        core::account::AccountLibraryEditor& editor;
        core::TrackActions& actions;
        TrackMenuController& trackMenu;
        Navigator& navigator;
        // Makes share links; none in tests.
        ShareController* share = nullptr;
    };

    // Everything in `parts` outlives this object.
    explicit CatalogPageViewModel(Parts parts, QObject* parent = nullptr);

    // A route this page shows.
    [[nodiscard]] static bool handles(const Route& route);

    [[nodiscard]] QString kind() const { return m_kind; }
    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] bool finding() const { return m_finding; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString eyebrow() const { return m_eyebrow; }
    [[nodiscard]] QString title() const { return m_title; }
    [[nodiscard]] QString byline() const { return m_byline; }
    [[nodiscard]] QString info() const;
    [[nodiscard]] QString description() const { return m_description; }
    [[nodiscard]] QUrl artworkUrl() const { return m_artworkUrl; }
    [[nodiscard]] QString artworkId() const { return m_title; }
    [[nodiscard]] bool showsCards() const { return m_showsCards; }
    [[nodiscard]] bool canSave() const;
    [[nodiscard]] bool canShare() const;
    [[nodiscard]] bool saved() const;
    [[nodiscard]] bool saving() const { return m_saving; }
    [[nodiscard]] QString pagingState() const;
    [[nodiscard]] TrackListModel* tracks() { return &m_tracks; }
    [[nodiscard]] CatalogCardModel* cards() { return &m_cards; }

    Q_INVOKABLE void playRow(int row);
    Q_INVOKABLE void play();
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void prepareSongMenu(int row);
    Q_INVOKABLE void openCard(int row);
    Q_INVOKABLE void toggleSave();
    Q_INVOKABLE void share();
    // The next page of a chart or a long shelf, when there is one.
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void retry();

    // A song's album: opens the finding page (Account mode songs).
    void openAlbumOf(const core::Track& track);

signals:
    void changed();

private:
    void followNavigator();
    void load(const Route& route, bool force);
    void reset(const QString& kind);
    void showDetail(const core::catalog::Detail& detail, const QString& kind);
    void showItems(QList<core::catalog::CatalogItem> items);
    void fail(const QString& message);
    void startPager(QList<core::catalog::CatalogItem> items, std::optional<int> nextOffset, bool seed);
    void applyPagerItems();

    core::catalog::CatalogService& m_catalog;
    core::account::AccountLibraryEditor& m_editor;
    core::TrackActions& m_actions;
    TrackMenuController& m_trackMenu;
    Navigator& m_navigator;

    std::uint64_t m_request = 0;
    QString m_kind;
    bool m_loading = false;
    bool m_finding = false;
    QString m_error;
    QString m_eyebrow;
    QString m_title;
    QString m_byline;
    QString m_description;
    QUrl m_artworkUrl;
    // The picture's own link, for a share link.
    QString m_shareArtwork;
    ShareController* m_share = nullptr;
    QString m_sourceUrl;
    bool m_showsCards = false;
    bool m_saving = false;
    TrackListModel m_tracks;
    CatalogCardModel m_cards;
    // A chart's or a long shelf's paging: which page loads next.
    std::optional<core::catalog::CatalogPager<core::catalog::CatalogItem>> m_pager;
    std::optional<core::catalog::ChartRef> m_chart;
    QString m_shelfKey;
    // A catalogue artist's section in full, which pages.
    QString m_artistId;
    std::optional<core::catalog::ArtistSection> m_artistSection;
};

} // namespace lmp::ui
