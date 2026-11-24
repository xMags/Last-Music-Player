#include "ui/downloads/OfflineViewModel.h"

#include "core/downloads/DownloadManager.h"
#include "core/downloads/DownloadPolicy.h"
#include "core/library/TrackActions.h"
#include "core/model/DisplayTitle.h"
#include "platform/IFolderPicker.h"
#include "ui/toasts/ToastQueue.h"
#include "ui/tracks/TrackMenuController.h"

#include <QMap>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace lmp::ui {

using core::DownloadItemState;

namespace {

[[nodiscard]] QString songCount(int count)
{
    return count == 1 ? QObject::tr("1 song") : QObject::tr("%1 songs").arg(count);
}

[[nodiscard]] core::Track playable(const core::DownloadItem& item)
{
    core::Track track;
    track.title = item.title;
    track.artist = item.artist;
    track.album = item.album;
    track.artworkUrl = item.artworkUrl;
    track.filePath = item.filePath;
    track.sourceKind = QStringLiteral("local");
    track.sourceUrl = item.sourceUrl;
    track.sourceTag = item.provider;
    return track;
}

} // namespace

OfflineViewModel::OfflineViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_downloads(parts.downloads)
    , m_actions(parts.actions)
    , m_trackMenu(parts.trackMenu)
    , m_toasts(parts.toasts)
    , m_picker(parts.picker)
    , m_currentTrack(std::move(parts.currentTrack))
    , m_canDownload(std::move(parts.canDownload))
    , m_stableKey(std::move(parts.stableKey))
{
    connect(&m_downloads, &core::DownloadManager::changed, this, &OfflineViewModel::reload);
    reload();
}

QStringList OfflineViewModel::tabs() const
{
    return {tr("Songs"), tr("Albums"), tr("Artists")};
}

void OfflineViewModel::setTab(const QString& tab)
{
    const QString chosen = tab == QLatin1StringView("albums") || tab == QLatin1StringView("artists")
        ? tab
        : QStringLiteral("songs");
    if (chosen == m_tab && m_groupTitle.isEmpty()) {
        return;
    }
    m_tab = chosen;
    m_groupTitle.clear();
    showTab();
    emit changed();
}

QString OfflineViewModel::summary() const
{
    if (m_downloaded.isEmpty()) {
        return {};
    }
    // "42 songs · 312 MB" (offlineSummaryText).
    return songCount(static_cast<int>(m_downloaded.size())) + QStringLiteral(" · ") + core::downloadSizeText(m_bytes);
}

bool OfflineViewModel::paused() const
{
    return m_downloads.snapshot().allPaused;
}

void OfflineViewModel::reload()
{
    const core::DownloadSnapshot snapshot = m_downloads.snapshot();
    if (snapshot.revision == m_revision) {
        return;
    }
    m_revision = snapshot.revision;
    m_downloaded.clear();
    m_bytes = snapshot.offlineBytes;
    m_pending.clear();
    int waiting = 0;
    int failed = 0;
    int doneInJobs = 0;
    int itemsInJobs = 0;
    double progress = 0.0;
    QString now;
    for (const core::DownloadJobSummary& summary : snapshot.jobs) {
        bool jobPending = false;
        for (const core::DownloadItem& item : summary.job.items) {
            if (item.state == DownloadItemState::Completed) {
                m_downloaded.append(playable(item));
                continue;
            }
            jobPending = true;
        }
        if (!jobPending) {
            continue;
        }
        int jobPercent = 0;
        for (const core::DownloadItem& item : summary.job.items) {
            ++itemsInJobs;
            if (item.state == DownloadItemState::Completed) {
                ++doneInJobs;
                progress += 1.0;
            } else if (item.state == DownloadItemState::Failed) {
                ++failed;
            } else {
                ++waiting;
                if (item.state == DownloadItemState::Downloading) {
                    if (item.bytesTotal > 0) {
                        progress += static_cast<double>(item.bytesDownloaded) / static_cast<double>(item.bytesTotal);
                        jobPercent = static_cast<int>(core::downloadProgressPercent(item.bytesDownloaded, item.bytesTotal));
                    }
                    if (now.isEmpty()) {
                        now = core::displayTitle(item.title)
                            + (item.artist.isEmpty() ? QString() : QStringLiteral(" · ") + item.artist);
                    }
                }
            }
        }
        const QString state = summary.state == DownloadItemState::Failed      ? tr("Failed")
            : summary.state == DownloadItemState::Downloading                 ? tr("Downloading")
            : summary.state == DownloadItemState::Paused || snapshot.allPaused ? tr("Paused")
                                                                               : tr("Queued");
        m_pending.append(QVariantMap{{QStringLiteral("id"), summary.job.id},
                                     {QStringLiteral("title"), summary.job.title},
                                     {QStringLiteral("subtitle"), summary.job.subtitle},
                                     {QStringLiteral("state"), state},
                                     {QStringLiteral("percent"), jobPercent},
                                     {QStringLiteral("failed"), summary.state == DownloadItemState::Failed},
                                     {QStringLiteral("error"), summary.error}});
    }
    m_pendingActive = waiting > 0;
    const bool allPaused = snapshot.allPaused;
    m_pendingTitle = waiting > 0 ? (allPaused ? tr("Paused: %1 left").arg(songCount(waiting)) : tr("Downloading %1").arg(songCount(waiting)))
        : failed == 1             ? tr("1 download failed")
                                  : tr("%1 downloads failed").arg(failed);
    QStringList detail;
    if (waiting > 0 && !allPaused && !now.isEmpty()) {
        detail.append(tr("Now: %1").arg(now));
    }
    detail.append(tr("%1 of %2 done").arg(doneInJobs).arg(itemsInJobs));
    if (waiting > 0 && failed > 0) {
        detail.append(tr("%1 failed").arg(failed));
    }
    m_pendingDetail = detail.join(QStringLiteral(" · "));
    m_pendingPercent = itemsInJobs > 0 ? std::min(100, static_cast<int>(progress * 100.0 / itemsInJobs + 0.5)) : 0;
    showTab();
    emit changed();
}

void OfflineViewModel::showTab()
{
    if (m_tab == QLatin1StringView("songs")) {
        m_songs.setTracks(m_downloaded);
        return;
    }
    const bool albums = m_tab == QLatin1StringView("albums");
    if (!m_groupTitle.isEmpty()) {
        QList<core::Track> members;
        for (const core::Track& track : std::as_const(m_downloaded)) {
            if ((albums ? track.album : track.artist) == m_groupTitle) {
                members.append(track);
            }
        }
        m_songs.setTracks(members);
        return;
    }
    QMap<QString, QList<core::Track>> groups;
    for (const core::Track& track : std::as_const(m_downloaded)) {
        const QString key = (albums ? track.album : track.artist).trimmed();
        if (!key.isEmpty()) {
            groups[key].append(track);
        }
    }
    QList<CollectionCardModel::Card> cards;
    for (auto group = groups.cbegin(); group != groups.cend(); ++group) {
        const core::Track& first = group.value().constFirst();
        cards.append({.kind = albums ? QStringLiteral("album") : QStringLiteral("artist"),
                      .key = group.key(),
                      .title = group.key(),
                      .subtitle = albums ? first.artist : songCount(static_cast<int>(group.value().size())),
                      .artworkUrl = QUrl(first.artworkUrl)});
    }
    m_cards.setCards(cards);
}

void OfflineViewModel::openCard(int row)
{
    if (row < 0 || row >= m_cards.count()) {
        return;
    }
    m_groupTitle = m_cards.cardAt(row).key;
    showTab();
    emit changed();
}

void OfflineViewModel::closeGroup()
{
    if (m_groupTitle.isEmpty()) {
        return;
    }
    m_groupTitle.clear();
    showTab();
    emit changed();
}

void OfflineViewModel::play()
{
    if (m_songs.count() > 0) {
        m_actions.play(m_songs.trackAt(0), m_songs.tracks());
    }
}

void OfflineViewModel::shuffle()
{
    m_actions.shufflePlay(m_songs.tracks());
}

void OfflineViewModel::playRow(int row)
{
    if (row >= 0 && row < m_songs.count()) {
        m_actions.play(m_songs.trackAt(row), m_songs.tracks());
    }
}

void OfflineViewModel::prepareSongMenu(int row)
{
    if (row >= 0 && row < m_songs.count()) {
        m_trackMenu.prepare(m_songs.trackAt(row), m_songs.tracks());
    }
}

void OfflineViewModel::pauseAll()
{
    m_downloads.pauseAll();
}

void OfflineViewModel::resumeAll()
{
    m_downloads.resumeAll();
}

void OfflineViewModel::cancel(const QString& jobId)
{
    m_downloads.cancelJob(jobId);
}

void OfflineViewModel::retry(const QString& jobId)
{
    m_downloads.retryJob(jobId);
}

void OfflineViewModel::clearCompleted()
{
    m_downloads.clearCompletedHistory();
}

QString OfflineViewModel::storedSummary() const
{
    const core::DownloadSnapshot snapshot = m_downloads.snapshot();
    if (snapshot.offlineTracks == 0) {
        return tr("None yet · use Download on any song, album or playlist");
    }
    return tr("%1 on this PC · %2").arg(songCount(snapshot.offlineTracks), core::downloadSizeText(snapshot.offlineBytes));
}

bool OfflineViewModel::hasAnyDownloads() const
{
    const core::DownloadSnapshot snapshot = m_downloads.snapshot();
    return snapshot.offlineTracks > 0 || !snapshot.jobs.isEmpty();
}

void OfflineViewModel::removeAll()
{
    if (m_downloads.removeAllDownloads()) {
        (void)m_toasts.show(tr("Removed all downloads from this PC"));
        return;
    }
    (void)m_toasts.show(tr("The downloads could not be removed."), true);
}

bool OfflineViewModel::avoidMetered() const
{
    return m_downloads.snapshot().avoidMeteredNetworks;
}

void OfflineViewModel::setAvoidMetered(bool value)
{
    m_downloads.setAvoidMeteredNetworks(value);
}

bool OfflineViewModel::autoLiked() const
{
    return m_downloads.snapshot().autoDownloadLiked;
}

void OfflineViewModel::setAutoLiked(bool value)
{
    m_downloads.setAutoDownloadLiked(value);
}

bool OfflineViewModel::onBattery() const
{
    return m_downloads.snapshot().downloadOnBattery;
}

void OfflineViewModel::setOnBattery(bool value)
{
    m_downloads.setDownloadOnBattery(value);
}

bool OfflineViewModel::keepRecent() const
{
    return m_downloads.snapshot().keepRecentOffline;
}

void OfflineViewModel::setKeepRecent(bool value)
{
    m_downloads.setKeepRecentOffline(value);
}

QString OfflineViewModel::folder() const
{
    return m_downloads.snapshot().rootFolder;
}

void OfflineViewModel::chooseFolder()
{
    const std::optional<QString> chosen = m_picker.pickFolder(m_ownerWindow, folder());
    if (!chosen) {
        return;
    }
    if (!m_downloads.setRootFolder(*chosen)) {
        m_toasts.show(tr("The download folder can be changed once downloads finish."), true);
    }
}

bool OfflineViewModel::downloadTracks(const QString& title, const QString& subtitle, const QString& artworkUrl,
                                      const QList<core::Track>& tracks)
{
    if (!m_canDownload || !m_canDownload() || !m_stableKey) {
        m_toasts.show(tr("Downloads need a music source."), true);
        return false;
    }
    QList<core::DownloadTrackRequest> requests;
    for (const core::Track& track : tracks) {
        if (!track.isRemote() || !track.sourceUrl.startsWith(QLatin1StringView("http"))) {
            continue;
        }
        requests.append({m_stableKey(track), track.sourceUrl, track.sourceTag, track.title, track.artist, track.album,
                         track.artworkUrl});
    }
    if (requests.isEmpty()) {
        m_toasts.show(tr("This has no songs that can be downloaded."));
        return false;
    }
    if (!m_downloads.enqueue(title, subtitle, artworkUrl, requests)) {
        m_toasts.show(tr("These songs are already downloaded or on their way."));
        return false;
    }
    m_toasts.show(requests.size() == 1 ? tr("Downloading 1 song")
                                       : tr("Downloading %1 songs").arg(requests.size()));
    return true;
}

void OfflineViewModel::downloadPlaying()
{
    if (const std::optional<core::Track> track = m_currentTrack ? m_currentTrack() : std::nullopt) {
        (void)downloadTracks(track->title, tr("Single track"), track->artworkUrl, {*track});
    }
}

} // namespace lmp::ui
