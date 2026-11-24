#include "ui/downloads/OfflineViewModel.h"

#include "core/downloads/DownloadManager.h"
#include "core/downloads/DownloadPolicy.h"
#include "core/db/LibraryQueries.h"
#include "core/library/TrackActions.h"
#include "core/model/DisplayTitle.h"
#include "platform/IFolderPicker.h"
#include "ui/library/SystemPlaylists.h"
#include "ui/toasts/ToastQueue.h"
#include "ui/tracks/TrackMenuController.h"

#include <QColor>
#include <QHash>
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
    , m_library(parts.library)
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
    return {tr("Songs"), tr("Playlists"), tr("History"), tr("Albums"), tr("Artists"), tr("Genres")};
}

QStringList OfflineViewModel::tabKeys() const
{
    return {QStringLiteral("songs"),  QStringLiteral("playlists"), QStringLiteral("history"),
            QStringLiteral("albums"), QStringLiteral("artists"),   QStringLiteral("genres")};
}

void OfflineViewModel::setTab(const QString& tab)
{
    const QString chosen = tabKeys().contains(tab) ? tab : QStringLiteral("songs");
    if (chosen == m_tab && m_groupKey.isEmpty()) {
        return;
    }
    m_tab = chosen;
    m_groupKey.clear();
    refresh();
}

void OfflineViewModel::refresh()
{
    m_groupsSignature.clear();
    refreshGroups();
    showTab();
    emit changed();
}

bool OfflineViewModel::showsCards() const
{
    return m_tab != QLatin1StringView("songs") && m_tab != QLatin1StringView("history") && m_groupKey.isEmpty();
}

const core::offline::OfflineGroup* OfflineViewModel::openedGroup() const
{
    if (m_groupKey.isEmpty()) {
        return nullptr;
    }
    const QList<core::offline::OfflineGroup>* groups = m_tab == QLatin1StringView("playlists") ? &m_playlistGroups
        : m_tab == QLatin1StringView("albums")                                                  ? &m_albumGroups
        : m_tab == QLatin1StringView("artists")                                                 ? &m_artistGroups
        : m_tab == QLatin1StringView("genres")                                                  ? &m_genreGroups
                                                                                                : nullptr;
    if (groups == nullptr) {
        return nullptr;
    }
    for (const core::offline::OfflineGroup& group : *groups) {
        if (group.key == m_groupKey) {
            return &group;
        }
    }
    return nullptr;
}

QString OfflineViewModel::groupTitle() const
{
    const core::offline::OfflineGroup* group = openedGroup();
    return group != nullptr ? group->name : QString();
}

QString OfflineViewModel::groupEyebrow() const
{
    const core::offline::OfflineGroup* group = openedGroup();
    if (group == nullptr) {
        return {};
    }
    switch (group->kind) {
    case core::offline::GroupKind::Playlist:
        return tr("DOWNLOADED PLAYLIST");
    case core::offline::GroupKind::Album:
        return tr("DOWNLOADED ALBUM");
    case core::offline::GroupKind::Artist:
        return tr("DOWNLOADED ARTIST");
    case core::offline::GroupKind::Genre:
        return tr("DOWNLOADED GENRE");
    }
    return {};
}

QString OfflineViewModel::groupBackLabel() const
{
    const qsizetype index = tabKeys().indexOf(m_tab);
    return index < 0 ? QString() : tr("All %1").arg(tabs().at(index).toLower());
}

QString OfflineViewModel::groupByline() const
{
    const core::offline::OfflineGroup* group = openedGroup();
    return group != nullptr && group->kind == core::offline::GroupKind::Album ? group->subtitle : QString();
}

QString OfflineViewModel::groupInfo() const
{
    const core::offline::OfflineGroup* group = openedGroup();
    if (group == nullptr) {
        return {};
    }
    // An album's line names its artist above it; the rest say how much of
    // them is downloaded.
    QStringList parts{group->kind == core::offline::GroupKind::Album
                          ? core::offline::songCountText(static_cast<int>(group->tracks.size()))
                          : group->subtitle};
    double seconds = 0.0;
    for (const core::Track& track : group->tracks) {
        seconds += std::max(0.0, track.durationSeconds);
    }
    if (seconds > 0.0) {
        // totalDurationLabel: at least a minute; "N hr" with no minutes left.
        const int minutes = std::max(1, static_cast<int>(seconds / 60.0 + 0.5));
        const int rest = minutes % 60;
        parts.append(minutes < 60 ? tr("%1 min").arg(minutes)
                     : rest > 0   ? tr("%1 hr %2 min").arg(minutes / 60).arg(rest)
                                  : tr("%1 hr").arg(minutes / 60));
    }
    return parts.join(QStringLiteral(" · "));
}

QString OfflineViewModel::groupArtworkUrl() const
{
    const core::offline::OfflineGroup* group = openedGroup();
    if (group == nullptr) {
        return {};
    }
    for (const core::Track& track : group->tracks) {
        if (!track.artworkUrl.isEmpty()) {
            return track.artworkUrl;
        }
    }
    return {};
}

bool OfflineViewModel::groupRound() const
{
    const core::offline::OfflineGroup* group = openedGroup();
    return group != nullptr && group->kind == core::offline::GroupKind::Artist;
}

QVariantMap OfflineViewModel::groupCover() const
{
    const core::offline::OfflineGroup* group = openedGroup();
    if (group != nullptr && group->kind == core::offline::GroupKind::Playlist) {
        if (const std::optional<SystemPlaylist> system = systemPlaylist(group->key)) {
            return {{QStringLiteral("style"), QStringLiteral("system")},
                    {QStringLiteral("gradientStart"), system->gradientStart},
                    {QStringLiteral("gradientEnd"), system->gradientEnd},
                    {QStringLiteral("iconName"), system->iconName}};
        }
    }
    const bool genre = group != nullptr && group->kind == core::offline::GroupKind::Genre;
    return {{QStringLiteral("style"), genre ? QStringLiteral("genre") : QStringLiteral("image")},
            {QStringLiteral("gradientStart"), QColor()},
            {QStringLiteral("gradientEnd"), QColor()},
            {QStringLiteral("iconName"), QString()}};
}

QString OfflineViewModel::emptyTabText() const
{
    if (m_downloaded.isEmpty() || !m_groupKey.isEmpty()) {
        return {};
    }
    if (m_tab == QLatin1StringView("playlists") && m_playlistGroups.isEmpty()) {
        return tr("None of your playlists has a downloaded song yet.");
    }
    if (m_tab == QLatin1StringView("history") && m_history.isEmpty()) {
        return tr("None of the songs you played lately is downloaded.");
    }
    if (m_tab == QLatin1StringView("albums") && m_albumGroups.isEmpty()) {
        return tr("Download two or more songs from an album and it shows here.");
    }
    if (m_tab == QLatin1StringView("artists") && m_artistGroups.isEmpty()) {
        return tr("The artists of the songs you download show here.");
    }
    if (m_tab == QLatin1StringView("genres") && m_genreGroups.isEmpty()) {
        return tr("The genres of the songs you download show here.");
    }
    return {};
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
    m_downloadedKeys.clear();
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
                m_downloadedKeys.append(item.stableKey);
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
    refreshGroups();
    showTab();
    emit changed();
}

void OfflineViewModel::refreshGroups()
{
    // Progress ticks change the snapshot but not which songs are here; the
    // library is read again only when that set changes.
    const QString signature = m_downloadedKeys.join(QLatin1Char('\n'));
    if (signature == m_groupsSignature && m_detailed.size() == m_downloaded.size()) {
        return;
    }
    m_groupsSignature = signature;

    // withLibraryDetails: a download is stored with only what it needs to
    // play, so its genre, length and main artist come from the library.
    QHash<QString, core::Track> known;
    if (m_stableKey && !m_downloaded.isEmpty()) {
        for (const core::Track& track : m_library.loadTracks()) {
            if (!track.isRemote()) {
                continue;
            }
            const QString key = m_stableKey(track);
            if (!key.isEmpty() && !known.contains(key)) {
                known.insert(key, track);
            }
        }
    }
    m_detailed = m_downloaded;
    for (qsizetype index = 0; index < m_detailed.size(); ++index) {
        const auto found = known.constFind(m_downloadedKeys.value(index));
        if (found == known.constEnd()) {
            continue;
        }
        core::Track& song = m_detailed[index];
        if (song.genre.isEmpty()) {
            song.genre = found->genre;
        }
        if (song.album.isEmpty()) {
            song.album = found->album;
        }
        if (song.durationSeconds <= 0.0) {
            song.durationSeconds = found->durationSeconds;
            song.durationText = found->durationText;
        }
        if (song.primaryArtistName.isEmpty()) {
            song.primaryArtistName = found->primaryArtistName;
        }
    }

    // A collection's songs as download keys (empty for songs on this PC).
    const auto keysOf = [this](const QList<core::Track>& tracks) {
        QStringList keys;
        keys.reserve(tracks.size());
        for (const core::Track& track : tracks) {
            keys.append(track.isRemote() && m_stableKey ? m_stableKey(track) : QString());
        }
        return keys;
    };
    m_playlistGroups.clear();
    const auto addPlaylist = [this, &keysOf](const QString& key, const QString& name, const QList<core::Track>& tracks) {
        const QStringList keys = keysOf(tracks);
        const QList<core::Track> part = core::offline::downloadedPart(keys, m_detailed, m_downloadedKeys);
        if (part.isEmpty()) {
            return;
        }
        QStringList distinct = keys;
        distinct.removeDuplicates();
        m_playlistGroups.append({.kind = core::offline::GroupKind::Playlist,
                                 .key = key,
                                 .name = name,
                                 .subtitle = core::offline::playlistSubtitle(static_cast<int>(part.size()),
                                                                             static_cast<int>(distinct.size())),
                                 .tracks = part});
    };
    QStringList playlistNames;
    if (!m_downloaded.isEmpty()) {
        for (const SystemPlaylist& system : systemPlaylists()) {
            addPlaylist(system.key, system.title, m_library.loadTracksForQuery(system.query));
        }
        for (const core::Track& playlist : m_actions.playlists()) {
            playlistNames.append(playlist.title);
            core::db::TrackQuery query;
            query.groupKind = QStringLiteral("playlist");
            query.groupKey = playlist.sourceUrl;
            addPlaylist(playlist.sourceUrl, playlist.title, m_library.loadTracksForQuery(query));
        }
    }
    m_albumGroups = core::offline::albumGroups(m_detailed, playlistNames);
    m_artistGroups = core::offline::artistGroups(m_detailed);
    m_genreGroups = core::offline::genreGroups(m_detailed);
    m_history = historySongs();
    // An opened card whose songs are all gone closes.
    if (!m_groupKey.isEmpty() && openedGroup() == nullptr) {
        m_groupKey.clear();
    }
}

QList<core::Track> OfflineViewModel::historySongs()
{
    if (m_downloaded.isEmpty() || !m_stableKey) {
        return {};
    }
    QStringList keys;
    for (const core::Track& track : m_library.loadHistoryTracks()) {
        keys.append(track.isRemote() ? m_stableKey(track) : QString());
    }
    return core::offline::downloadedPart(keys, m_detailed, m_downloadedKeys);
}

void OfflineViewModel::showTab()
{
    if (m_tab == QLatin1StringView("songs")) {
        m_songs.setTracks(m_detailed);
        return;
    }
    if (m_tab == QLatin1StringView("history")) {
        m_songs.setTracks(m_history);
        return;
    }
    if (const core::offline::OfflineGroup* group = openedGroup()) {
        m_songs.setTracks(group->tracks);
        return;
    }
    const QList<core::offline::OfflineGroup>& groups = m_tab == QLatin1StringView("playlists") ? m_playlistGroups
        : m_tab == QLatin1StringView("albums")                                                  ? m_albumGroups
        : m_tab == QLatin1StringView("artists")                                                 ? m_artistGroups
                                                                                                : m_genreGroups;
    QList<CollectionCardModel::Card> cards;
    cards.reserve(groups.size());
    for (const core::offline::OfflineGroup& group : groups) {
        QUrl artwork;
        for (const core::Track& track : group.tracks) {
            if (!track.artworkUrl.isEmpty()) {
                artwork = QUrl(track.artworkUrl);
                break;
            }
        }
        CollectionCardModel::Card card{.kind = QStringLiteral("playlist"),
                                       .key = group.key,
                                       .title = group.name,
                                       .subtitle = group.subtitle,
                                       .artworkUrl = artwork};
        switch (group.kind) {
        case core::offline::GroupKind::Playlist:
            // Liked Songs and the other system playlists keep their covers.
            if (const std::optional<SystemPlaylist> system = systemPlaylist(group.key)) {
                card.kind = QStringLiteral("system");
                card.artworkUrl = QUrl();
                card.gradientStart = system->gradientStart;
                card.gradientEnd = system->gradientEnd;
                card.iconName = system->iconName;
            }
            break;
        case core::offline::GroupKind::Album:
            card.kind = QStringLiteral("album");
            break;
        case core::offline::GroupKind::Artist:
            card.kind = QStringLiteral("artist");
            break;
        case core::offline::GroupKind::Genre:
            card.kind = QStringLiteral("genre");
            card.artworkUrl = QUrl();
            break;
        }
        cards.append(std::move(card));
    }
    m_cards.setCards(std::move(cards));
}

void OfflineViewModel::openCard(int row)
{
    if (row < 0 || row >= m_cards.count()) {
        return;
    }
    m_groupKey = m_cards.cardAt(row).key;
    showTab();
    emit changed();
}

void OfflineViewModel::closeGroup()
{
    if (m_groupKey.isEmpty()) {
        return;
    }
    m_groupKey.clear();
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
