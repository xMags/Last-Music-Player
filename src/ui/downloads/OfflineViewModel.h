#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include "core/model/Track.h"
#include "ui/library/CollectionCardModel.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class DownloadManager;
class TrackActions;
} // namespace lmp::core

namespace lmp::platform {
class IFolderPicker;
} // namespace lmp::platform

namespace lmp::ui {

class TrackMenuController;
class ToastQueue;

// The Offline page and the download controls (`App.offline`; the
// reference's views/offline.ts): the songs downloaded on this PC with Play
// and Shuffle, Albums and Artists cut down to what is downloaded, the strip
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
    // "songs", "albums" or "artists".
    Q_PROPERTY(QString tab READ tab WRITE setTab NOTIFY changed FINAL)
    // An album or artist opened from its card; empty for the tab's cards.
    Q_PROPERTY(QString groupTitle READ groupTitle NOTIFY changed FINAL)
    Q_PROPERTY(bool showsCards READ showsCards NOTIFY changed FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* songs READ songs CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* cards READ cards CONSTANT FINAL)
    Q_PROPERTY(bool hasDownloads READ hasDownloads NOTIFY changed FINAL)
    // "42 songs · 312 MB", empty with nothing downloaded.
    Q_PROPERTY(QString summary READ summary NOTIFY changed FINAL)
    Q_PROPERTY(bool hasCompleted READ hasCompleted NOTIFY changed FINAL)

    // The strip of downloads on their way.
    Q_PROPERTY(bool pendingShown READ pendingShown NOTIFY changed FINAL)
    Q_PROPERTY(QString pendingTitle READ pendingTitle NOTIFY changed FINAL)
    Q_PROPERTY(QString pendingDetail READ pendingDetail NOTIFY changed FINAL)
    Q_PROPERTY(int pendingPercent READ pendingPercent NOTIFY changed FINAL)
    Q_PROPERTY(bool pendingActive READ pendingActive NOTIFY changed FINAL)
    Q_PROPERTY(bool paused READ paused NOTIFY changed FINAL)
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
        TrackMenuController& trackMenu;
        ToastQueue& toasts;
        platform::IFolderPicker& picker;
        // The song playing now, for the player bar's Download.
        std::function<std::optional<core::Track>()> currentTrack;
        // Whether songs can be downloaded now (Capabilities::downloadSongs).
        std::function<bool()> canDownload;
        // The key a remote song is downloaded under (downloadStableKey).
        std::function<QString(const core::Track&)> stableKey;
    };

    // Everything in `parts` outlives this object.
    explicit OfflineViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] QStringList tabs() const;
    [[nodiscard]] QString tab() const { return m_tab; }
    void setTab(const QString& tab);
    [[nodiscard]] QString groupTitle() const { return m_groupTitle; }
    [[nodiscard]] bool showsCards() const { return m_tab != QLatin1StringView("songs") && m_groupTitle.isEmpty(); }
    [[nodiscard]] TrackListModel* songs() { return &m_songs; }
    [[nodiscard]] CollectionCardModel* cards() { return &m_cards; }
    [[nodiscard]] bool hasDownloads() const { return !m_downloaded.isEmpty(); }
    [[nodiscard]] QString summary() const;
    [[nodiscard]] bool hasCompleted() const { return !m_downloaded.isEmpty(); }
    [[nodiscard]] bool pendingShown() const { return !m_pending.isEmpty(); }
    [[nodiscard]] QString pendingTitle() const { return m_pendingTitle; }
    [[nodiscard]] QString pendingDetail() const { return m_pendingDetail; }
    [[nodiscard]] int pendingPercent() const { return m_pendingPercent; }
    [[nodiscard]] bool pendingActive() const { return m_pendingActive; }
    [[nodiscard]] bool paused() const;
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

    Q_INVOKABLE void openCard(int row);
    Q_INVOKABLE void closeGroup();
    Q_INVOKABLE void play();
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void playRow(int row);
    Q_INVOKABLE void prepareSongMenu(int row);
    Q_INVOKABLE void pauseAll();
    Q_INVOKABLE void resumeAll();
    Q_INVOKABLE void cancel(const QString& jobId);
    Q_INVOKABLE void retry(const QString& jobId);
    Q_INVOKABLE void clearCompleted();
    Q_INVOKABLE void chooseFolder();
    // The player bar's Download: the song playing now.
    Q_INVOKABLE void downloadPlaying();
    // A collection's Download (its songs that can be downloaded).
    bool downloadTracks(const QString& title, const QString& subtitle, const QString& artworkUrl,
                        const QList<core::Track>& tracks);

    void setOwnerWindow(quintptr window) { m_ownerWindow = window; }

    // The downloaded songs as playable songs on this PC (for tests).
    [[nodiscard]] const QList<core::Track>& downloaded() const { return m_downloaded; }

signals:
    void changed();

private:
    void reload();
    void showTab();

    core::DownloadManager& m_downloads;
    core::TrackActions& m_actions;
    TrackMenuController& m_trackMenu;
    ToastQueue& m_toasts;
    platform::IFolderPicker& m_picker;
    std::function<std::optional<core::Track>()> m_currentTrack;
    std::function<bool()> m_canDownload;
    std::function<QString(const core::Track&)> m_stableKey;
    quintptr m_ownerWindow = 0;
    QString m_tab = QStringLiteral("songs");
    QString m_groupTitle;
    QList<core::Track> m_downloaded;
    std::uint64_t m_bytes = 0;
    TrackListModel m_songs;
    CollectionCardModel m_cards;
    QVariantList m_pending;
    QString m_pendingTitle;
    QString m_pendingDetail;
    int m_pendingPercent = 0;
    bool m_pendingActive = false;
    std::uint64_t m_revision = 0;
};

} // namespace lmp::ui
