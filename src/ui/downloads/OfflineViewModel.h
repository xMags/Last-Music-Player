#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include "core/downloads/OfflineCollections.h"
#include "core/model/Track.h"
#include "ui/library/CollectionCardModel.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class DownloadManager;
class SettingsStore;
class TrackActions;
namespace db {
class LibraryQueries;
}
} // namespace lmp::core

namespace lmp::platform {
class IFolderPicker;
} // namespace lmp::platform

namespace lmp::ui {

class OfflineArtistPhotos;
class TrackMenuController;
class ToastQueue;

// The Offline page and the download controls (`App.offline`; the
// reference's views/offline.ts): the songs downloaded on this PC with Play
// and Shuffle; Library's Playlists, History, Albums, Artists and Genres cut
// down to what is downloaded, a card opening as a page of its own; the strip
// of downloads on their way (pause or resume all, cancel, retry), the
// storage summary and Clear completed; the Download buttons of the player
// bar and collection headers; and the Settings "Downloads" card (the four
// rules and the folder).
//
// GUI thread only.
class OfflineViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.offline")

    Q_PROPERTY(QStringList tabs READ tabs CONSTANT FINAL)
    // The tabs' keys, in the order of `tabs`.
    Q_PROPERTY(QStringList tabKeys READ tabKeys CONSTANT FINAL)
    // "songs", "albums" or "artists".
    Q_PROPERTY(QString tab READ tab WRITE setTab NOTIFY changed FINAL)
    // An album or artist opened from its card; empty for the tab's cards.
    Q_PROPERTY(QString groupTitle READ groupTitle NOTIFY changed FINAL)
    // An opened card's header: "DOWNLOADED ALBUM", "All albums", an album's
    // artist, and "12 songs · 48 min".
    Q_PROPERTY(QString groupEyebrow READ groupEyebrow NOTIFY changed FINAL)
    Q_PROPERTY(QString groupBackLabel READ groupBackLabel NOTIFY changed FINAL)
    Q_PROPERTY(QString groupByline READ groupByline NOTIFY changed FINAL)
    Q_PROPERTY(QString groupInfo READ groupInfo NOTIFY changed FINAL)
    Q_PROPERTY(QString groupArtworkUrl READ groupArtworkUrl NOTIFY changed FINAL)
    Q_PROPERTY(bool groupRound READ groupRound NOTIFY changed FINAL)
    // The opened card's cover: {style: "image" | "system" | "genre",
    // gradientStart, gradientEnd, iconName} (a system playlist keeps its own).
    Q_PROPERTY(QVariantMap groupCover READ groupCover NOTIFY changed FINAL)
    // How many songs are downloaded, for "Remove all 12 songs from this PC".
    Q_PROPERTY(int downloadedCount READ downloadedCount NOTIFY changed FINAL)
    // A collection tab with nothing to show says why ("" otherwise).
    Q_PROPERTY(QString emptyTabText READ emptyTabText NOTIFY changed FINAL)
    Q_PROPERTY(bool showsCards READ showsCards NOTIFY changed FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* songs READ songs CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* cards READ cards CONSTANT FINAL)
    Q_PROPERTY(bool hasDownloads READ hasDownloads NOTIFY changed FINAL)
    // Settings' Downloaded songs row: "<n> songs on this PC · <size>", or how
    // to start; and whether anything is on the list at all (finished, queued,
    // running or failed), which shows Remove all downloads.
    Q_PROPERTY(QString storedSummary READ storedSummary NOTIFY changed FINAL)
    Q_PROPERTY(bool hasAnyDownloads READ hasAnyDownloads NOTIFY changed FINAL)
    // "42 songs · 312 MB", empty with nothing downloaded.
    Q_PROPERTY(QString summary READ summary NOTIFY changed FINAL)
    Q_PROPERTY(bool hasCompleted READ hasCompleted NOTIFY changed FINAL)

    // The strip of downloads on their way.
    Q_PROPERTY(bool pendingShown READ pendingShown NOTIFY changed FINAL)
    Q_PROPERTY(QString pendingTitle READ pendingTitle NOTIFY changed FINAL)
    Q_PROPERTY(QString pendingDetail READ pendingDetail NOTIFY changed FINAL)
    Q_PROPERTY(int pendingPercent READ pendingPercent NOTIFY changed FINAL)
    Q_PROPERTY(bool pendingActive READ pendingActive NOTIFY changed FINAL)
    // How many songs of the batch failed, and how fast the rest come in
    // ("1.4 MB/s"; empty while paused or with nothing coming).
    Q_PROPERTY(int pendingFailed READ pendingFailed NOTIFY changed FINAL)
    Q_PROPERTY(QString pendingSpeed READ pendingSpeed NOTIFY changed FINAL)
    Q_PROPERTY(bool paused READ paused NOTIFY changed FINAL)
    // Songs still on their way (not failed), for the sidebar's count.
    Q_PROPERTY(int waitingCount READ waitingCount NOTIFY changed FINAL)
    // Goes up whenever any song's download changes, so a binding that
    // reads markOf() is read again.
    Q_PROPERTY(int markRevision READ markRevision NOTIFY changed FINAL)
    // One map per job not finished: id, title, subtitle, state ("Queued",
    // "Paused", "Failed", "Downloading"), percent, failed, error.
    Q_PROPERTY(QVariantList pending READ pending NOTIFY changed FINAL)

    // Settings "Downloads".
    Q_PROPERTY(bool avoidMetered READ avoidMetered WRITE setAvoidMetered NOTIFY changed FINAL)
    Q_PROPERTY(bool autoLiked READ autoLiked WRITE setAutoLiked NOTIFY changed FINAL)
    Q_PROPERTY(bool onBattery READ onBattery WRITE setOnBattery NOTIFY changed FINAL)
    Q_PROPERTY(bool keepRecent READ keepRecent WRITE setKeepRecent NOTIFY changed FINAL)
    Q_PROPERTY(QString folder READ folder NOTIFY changed FINAL)

public:
    struct Parts {
        core::DownloadManager& downloads;
        core::TrackActions& actions;
        // The library the collection tabs are cut down from.
        core::db::LibraryQueries& library;
        TrackMenuController& trackMenu;
        ToastQueue& toasts;
        platform::IFolderPicker& picker;
        // The song playing now, for the player bar's Download.
        std::function<std::optional<core::Track>()> currentTrack;
        // Whether songs can be downloaded now (Capabilities::downloadSongs).
        std::function<bool()> canDownload;
        // The key a remote song is downloaded under (downloadStableKey).
        std::function<QString(const core::Track&)> stableKey;
        // Keeps the tab between runs; may be missing.
        core::SettingsStore* settings = nullptr;
        // The Artists tab's photos, looked up by name; may be missing (the
        // cards then show a song's cover).
        OfflineArtistPhotos* artistPhotos = nullptr;
    };

    // Everything in `parts` outlives this object.
    explicit OfflineViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] QStringList tabs() const;
    [[nodiscard]] QStringList tabKeys() const;
    [[nodiscard]] QString tab() const { return m_tab; }
    void setTab(const QString& tab);
    [[nodiscard]] QString groupTitle() const;
    [[nodiscard]] QString groupEyebrow() const;
    [[nodiscard]] QString groupBackLabel() const;
    [[nodiscard]] QString groupByline() const;
    [[nodiscard]] QString groupInfo() const;
    [[nodiscard]] QString groupArtworkUrl() const;
    [[nodiscard]] bool groupRound() const;
    [[nodiscard]] QVariantMap groupCover() const;
    [[nodiscard]] int downloadedCount() const { return static_cast<int>(m_downloaded.size()); }
    [[nodiscard]] QString emptyTabText() const;
    [[nodiscard]] bool showsCards() const;
    [[nodiscard]] TrackListModel* songs() { return &m_songs; }
    [[nodiscard]] CollectionCardModel* cards() { return &m_cards; }
    [[nodiscard]] bool hasDownloads() const { return !m_downloaded.isEmpty(); }
    [[nodiscard]] QString storedSummary() const;
    [[nodiscard]] bool hasAnyDownloads() const;
    [[nodiscard]] QString summary() const;
    [[nodiscard]] bool hasCompleted() const { return !m_downloaded.isEmpty(); }
    [[nodiscard]] bool pendingShown() const { return !m_pending.isEmpty(); }
    [[nodiscard]] QString pendingTitle() const { return m_pendingTitle; }
    [[nodiscard]] QString pendingDetail() const { return m_pendingDetail; }
    [[nodiscard]] int pendingPercent() const { return m_pendingPercent; }
    [[nodiscard]] bool pendingActive() const { return m_pendingActive; }
    [[nodiscard]] int pendingFailed() const { return m_pendingFailed; }
    [[nodiscard]] QString pendingSpeed() const { return m_pendingSpeed; }
    [[nodiscard]] bool paused() const;
    [[nodiscard]] int waitingCount() const { return m_waitingCount; }
    [[nodiscard]] int markRevision() const { return m_markRevision; }
    // A song's download mark (download-badge.ts downloadBadgeContent), by
    // its address: {state: "done" | "downloading" | "queued" | "failed" | "",
    // percent (-1 unknown), label}.
    Q_INVOKABLE QVariantMap markOf(const QString& remoteUrl) const;
    // The song menu's download item for `track`: "" (songs cannot be kept
    // here, or this one is not from the service), "offer", "done",
    // "pending" or "failed".
    [[nodiscard]] QString songDownloadState(const core::Track& track) const;
    // Runs the song menu's "download", "remove-download" or
    // "retry-download"; a toast says how it went.
    void runSongDownload(const core::Track& track, const QString& action);
    [[nodiscard]] QVariantList pending() const { return m_pending; }
    [[nodiscard]] bool avoidMetered() const;
    void setAvoidMetered(bool value);
    [[nodiscard]] bool autoLiked() const;
    void setAutoLiked(bool value);
    [[nodiscard]] bool onBattery() const;
    void setOnBattery(bool value);
    [[nodiscard]] bool keepRecent() const;
    void setKeepRecent(bool value);
    [[nodiscard]] QString folder() const;

    // Reads the library again (the page opening): playlists and recent
    // plays change without the downloads changing.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openCard(int row);
    Q_INVOKABLE void closeGroup();
    Q_INVOKABLE void play();
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void playRow(int row);
    // The heart on a song's row.
    Q_INVOKABLE void likeRow(int row);
    Q_INVOKABLE void prepareSongMenu(int row);
    Q_INVOKABLE void pauseAll();
    Q_INVOKABLE void resumeAll();
    // One song of the strip.
    Q_INVOKABLE void cancel(const QString& jobId, const QString& itemId);
    Q_INVOKABLE void retry(const QString& jobId, const QString& itemId);
    Q_INVOKABLE void retryAll();
    // The strip's close button: the songs still coming, or, with none, the
    // failed ones. Finished songs stay.
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE void clearCompleted();
    // Removes every download from this PC and says how it went in a toast.
    Q_INVOKABLE void removeAll();
    Q_INVOKABLE void chooseFolder();
    // The player bar's Download: the song playing now.
    Q_INVOKABLE void downloadPlaying();
    // A collection's Download (its songs that can be downloaded).
    bool downloadTracks(const QString& title, const QString& subtitle, const QString& artworkUrl,
                        const QList<core::Track>& tracks);

    void setOwnerWindow(quintptr window) { m_ownerWindow = window; }

    // The downloaded songs as playable songs on this PC (for tests).
    [[nodiscard]] const QList<core::Track>& downloaded() const { return m_downloaded; }
    // The photos known for the downloaded songs' artists (picture
    // addresses), so they are kept on disk with the covers.
    [[nodiscard]] QStringList artistPhotoUrls() const;

signals:
    void changed();

private:
    void reload();
    void showTab();
    // The downloaded songs with what the library knows about them (genre,
    // length, main artist), and the collection tabs' groups, rebuilt only
    // when the set of downloaded songs changes.
    // True when the lists were made again (the downloaded songs changed).
    bool refreshGroups();
    void markLiked();
    [[nodiscard]] const core::offline::OfflineGroup* openedGroup() const;
    [[nodiscard]] QList<core::Track> historySongs();

    core::DownloadManager& m_downloads;
    core::TrackActions& m_actions;
    core::db::LibraryQueries& m_library;
    TrackMenuController& m_trackMenu;
    ToastQueue& m_toasts;
    platform::IFolderPicker& m_picker;
    std::function<std::optional<core::Track>()> m_currentTrack;
    std::function<bool()> m_canDownload;
    std::function<QString(const core::Track&)> m_stableKey;
    core::SettingsStore* m_settings = nullptr;
    OfflineArtistPhotos* m_artistPhotos = nullptr;
    quintptr m_ownerWindow = 0;
    QString m_tab = QStringLiteral("songs");
    // The opened card's key on a collection tab; empty for the tab's cards.
    QString m_groupKey;
    QList<core::Track> m_downloaded;
    // m_downloadedKeys[i] is the download key of m_downloaded[i].
    QStringList m_downloadedKeys;
    // Whether each download is liked, by download key (markLiked).
    QHash<QString, bool> m_likedByKey;
    QString m_groupsSignature;
    QList<core::Track> m_detailed;
    QList<core::offline::OfflineGroup> m_playlistGroups;
    QList<core::offline::OfflineGroup> m_albumGroups;
    QList<core::offline::OfflineGroup> m_artistGroups;
    QList<core::offline::OfflineGroup> m_genreGroups;
    QList<core::Track> m_history;
    std::uint64_t m_bytes = 0;
    TrackListModel m_songs;
    CollectionCardModel m_cards;
    QVariantList m_pending;
    QString m_pendingTitle;
    QString m_pendingDetail;
    int m_pendingPercent = 0;
    bool m_pendingActive = false;
    int m_waitingCount = 0;
    int m_markRevision = 0;
    struct Mark {
        QString state;
        int percent = -1;
        QString error;
    };
    QHash<QString, Mark> m_marks;
    // The same marks by download key.
    QHash<QString, Mark> m_marksByKey;
    int m_pendingFailed = 0;
    QString m_pendingSpeed;
    std::uint64_t m_revision = 0;
};

} // namespace lmp::ui
