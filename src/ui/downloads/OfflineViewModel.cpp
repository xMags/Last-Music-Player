#include "ui/downloads/OfflineViewModel.h"

#include "core/downloads/DownloadManager.h"
#include "core/downloads/DownloadPolicy.h"
#include "core/db/DatabaseReader.h"
#include "core/library/TrackActions.h"
#include "core/model/CatalogSourceKey.h"
#include "core/model/DisplayTitle.h"
#include "core/model/TrackKeys.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/IFolderPicker.h"
#include "ui/downloads/OfflineArtistPhotos.h"
#include "ui/library/SystemPlaylists.h"
#include "ui/player/CoverSource.h"
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

// formatSpeed: "1.4 MB/s" from a megabyte a second, else "320 KB/s"
// (at least 1); empty for nothing.
[[nodiscard]] QString speedText(std::uint64_t bytesPerSecond)
{
    constexpr double Kilobyte = 1024.0;
    constexpr double Megabyte = 1024.0 * 1024.0;
    if (bytesPerSecond == 0) {
        return {};
    }
    const double rate = static_cast<double>(bytesPerSecond);
    if (rate >= Megabyte) {
        return QObject::tr("%1 MB/s").arg(rate / Megabyte, 0, 'f', 1);
    }
    return QObject::tr("%1 KB/s").arg(std::max(1, static_cast<int>(rate / Kilobyte + 0.5)));
}

[[nodiscard]] QString songCount(int count)
{
    return count == 1 ? QObject::tr("1 song") : QObject::tr("%1 songs").arg(count);
}

// The file a download is, as a song of this PC. A download of an account
// song becomes that account song where the account library has it
// (refreshGroups): a download keeps no account id, and a remote song without
// one has no identity in the queue.
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
    , m_reader(parts.reader)
    , m_trackMenu(parts.trackMenu)
    , m_toasts(parts.toasts)
    , m_picker(parts.picker)
    , m_currentTrack(std::move(parts.currentTrack))
    , m_canDownload(std::move(parts.canDownload))
    , m_stableKey(std::move(parts.stableKey))
    , m_settings(parts.settings)
    , m_artistPhotos(parts.artistPhotos)
{
    // A photo found for an artist shows on its card.
    if (m_artistPhotos != nullptr) {
        connect(m_artistPhotos, &OfflineArtistPhotos::photoFound, this, [this]() {
            if (m_tab == QLatin1StringView("artists") && m_groupKey.isEmpty()) {
                showTab();
            }
            emit changed();
        });
    }
    // The tab the user last chose.
    if (m_settings != nullptr) {
        const QString tab = m_settings->string(core::settings::keys::OfflineTab);
        if (tabKeys().contains(tab)) {
            m_tab = tab;
        }
    }
    connect(&m_downloads, &core::DownloadManager::changed, this, &OfflineViewModel::reload);
    // A like or unlike anywhere: every heart is read again.
    // A like or unlike anywhere: the hearts are read again and changed in
    // place (a reset of the list would lose its scroll place).
    connect(&m_actions, &core::TrackActions::likedChanged, this, &OfflineViewModel::rereadHearts);
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
    if (m_settings != nullptr) {
        m_settings->setString(core::settings::keys::OfflineTab, chosen);
    }
    refresh();
}

void OfflineViewModel::refresh()
{
    m_groupsSignature.clear();
    refreshGroups(true);
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

QVariantMap OfflineViewModel::markOf(const QString& remoteUrl) const
{
    const auto found = remoteUrl.isEmpty() ? m_marks.constEnd() : m_marks.constFind(remoteUrl);
    if (found == m_marks.constEnd()) {
        return {{QStringLiteral("state"), QString()}, {QStringLiteral("percent"), -1}, {QStringLiteral("label"), QString()}};
    }
    QString label;
    if (found->state == QLatin1StringView("done")) {
        label = tr("Downloaded");
    } else if (found->state == QLatin1StringView("downloading")) {
        label = found->percent < 0 ? tr("Downloading") : tr("Downloading, %1%").arg(found->percent);
    } else if (found->state == QLatin1StringView("queued")) {
        label = tr("Waiting to download");
    } else {
        label = found->error.isEmpty() ? tr("Download failed") : found->error;
    }
    return {{QStringLiteral("state"), found->state}, {QStringLiteral("percent"), found->percent},
            {QStringLiteral("label"), label}};
}

QString OfflineViewModel::songDownloadState(const core::Track& track) const
{
    if (!m_canDownload || !m_canDownload() || !m_stableKey || !track.isRemote() || track.unavailable
        || !track.sourceUrl.startsWith(QLatin1StringView("http"))) {
        return {};
    }
    const QString key = core::track_keys::downloadMatchKey(m_stableKey(track));
    const auto found = key.isEmpty() ? m_marksByKey.constEnd() : m_marksByKey.constFind(key);
    if (found == m_marksByKey.constEnd()) {
        return QStringLiteral("offer");
    }
    if (found->state == QLatin1StringView("done")) {
        return QStringLiteral("done");
    }
    return found->state == QLatin1StringView("failed") ? QStringLiteral("failed") : QStringLiteral("pending");
}

void OfflineViewModel::runSongDownload(const core::Track& track, const QString& action)
{
    if (action == QLatin1StringView("download")) {
        // downloadTracks says how it went.
        (void)downloadTracks(track.title, tr("Single track"), track.artworkUrl, {track});
        return;
    }
    if (!m_stableKey) {
        return;
    }
    const QString key = m_stableKey(track);
    if (action == QLatin1StringView("retry-download")) {
        m_downloads.retrySong(key);
        return;
    }
    if (action != QLatin1StringView("remove-download")) {
        return;
    }
    const bool wasDone = songDownloadState(track) == QLatin1StringView("done");
    if (!m_downloads.removeSong(key)) {
        (void)m_toasts.show(tr("That didn't work. Try again."), true);
        return;
    }
    (void)m_toasts.show(wasDone ? tr("Removed from this PC") : tr("Download cancelled"));
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
    // Kept to tell whether any mark changed: a progress tick of one song
    // must not make every row read its mark again.
    QHash<QString, Mark> previousMarks;
    previousMarks.swap(m_marks);
    m_marksByKey.clear();
    for (const core::DownloadJobSummary& summary : snapshot.jobs) {
        for (const core::DownloadItem& item : summary.job.items) {
            if (item.sourceUrl.isEmpty()) {
                continue;
            }
            Mark mark;
            switch (item.state) {
            case DownloadItemState::Completed:
                mark.state = QStringLiteral("done");
                break;
            case DownloadItemState::Downloading:
                mark.state = QStringLiteral("downloading");
                mark.percent = item.bytesTotal > 0
                    ? static_cast<int>(core::downloadProgressPercent(item.bytesDownloaded, item.bytesTotal))
                    : -1;
                break;
            case DownloadItemState::Failed:
                mark.state = QStringLiteral("failed");
                mark.error = item.error;
                break;
            default:
                mark.state = QStringLiteral("queued");
                break;
            }
            // A song in two jobs shows its furthest state.
            const auto known = m_marks.constFind(item.sourceUrl);
            if (known == m_marks.constEnd() || known->state != QLatin1StringView("done")) {
                m_marks.insert(item.sourceUrl, mark);
            }
            const QString match = core::track_keys::downloadMatchKey(item.stableKey);
            const auto keyed = m_marksByKey.constFind(match);
            if (!item.stableKey.isEmpty()
                && (keyed == m_marksByKey.constEnd() || keyed->state != QLatin1StringView("done"))) {
                m_marksByKey.insert(match, mark);
            }
        }
    }
    const bool marksDiffer = m_marks != previousMarks;
    if (marksDiffer) {
        ++m_markRevision;
    }
    m_bytes = snapshot.offlineBytes;
    m_pending.clear();
    // pendingStripMarkup: the songs of the jobs still on their way (the
    // current batch), on their way first, then waiting, then failed.
    struct Row {
        int order = 0;
        QVariantMap fields;
    };
    QList<Row> rows;
    int waiting = 0;
    int failed = 0;
    int doneInJobs = 0;
    int itemsInJobs = 0;
    double progress = 0.0;
    std::uint64_t speed = 0;
    QString now;
    QString next;
    const bool allPaused = snapshot.allPaused;
    for (const core::DownloadJobSummary& summary : snapshot.jobs) {
        bool jobPending = false;
        for (const core::DownloadItem& item : summary.job.items) {
            if (item.state == DownloadItemState::Completed) {
                m_downloaded.append(playable(item));
                m_downloadedKeys.append(core::track_keys::downloadMatchKey(item.stableKey));
                continue;
            }
            jobPending = true;
        }
        if (!jobPending) {
            continue;
        }
        for (const core::DownloadItem& item : summary.job.items) {
            ++itemsInJobs;
            if (item.state == DownloadItemState::Completed) {
                ++doneInJobs;
                progress += 1.0;
                continue;
            }
            const bool itemFailed = item.state == DownloadItemState::Failed;
            const bool downloading = item.state == DownloadItemState::Downloading;
            int percent = -1;
            if (itemFailed) {
                ++failed;
            } else {
                ++waiting;
                if (downloading) {
                    speed += item.bytesPerSecond;
                    if (item.bytesTotal > 0) {
                        percent = static_cast<int>(core::downloadProgressPercent(item.bytesDownloaded, item.bytesTotal));
                        progress += static_cast<double>(percent) / 100.0;
                    }
                }
                const QString line = core::displayTitle(item.title)
                    + (item.artist.isEmpty() ? QString() : QStringLiteral(" · ") + item.artist);
                if (downloading && now.isEmpty()) {
                    now = line;
                } else if (!downloading && next.isEmpty()) {
                    next = line;
                }
            }
            // A song whose size is not announced still shows it is coming in.
            const QString state = itemFailed ? QStringLiteral("failed")
                : downloading && percent >= 0 ? QStringLiteral("progress")
                : downloading && item.bytesDownloaded > 0 ? QStringLiteral("receiving")
                : downloading ? QStringLiteral("starting")
                : allPaused || item.state == DownloadItemState::Paused ? QStringLiteral("paused")
                                                                       : QStringLiteral("queued");
            rows.append({.order = downloading ? 0 : itemFailed ? 2 : 1,
                         .fields = {{QStringLiteral("jobId"), summary.job.id},
                                    {QStringLiteral("itemId"), item.id},
                                    {QStringLiteral("title"), core::displayTitle(item.title)},
                                    {QStringLiteral("artist"), item.artist},
                                    {QStringLiteral("artworkUrl"), item.artworkUrl},
                                    {QStringLiteral("state"), state},
                                    {QStringLiteral("percent"), std::max(0, percent)},
                                    {QStringLiteral("received"), core::downloadSizeText(item.bytesDownloaded)},
                                    {QStringLiteral("error"), item.error}}});
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) { return left.order < right.order; });
    for (const Row& row : std::as_const(rows)) {
        m_pending.append(row.fields);
    }
    m_pendingActive = waiting > 0;
    m_waitingCount = waiting;
    m_pendingFailed = failed;
    m_pendingSpeed = waiting > 0 && !allPaused ? speedText(speed) : QString();
    m_pendingTitle = waiting > 0 ? (allPaused ? tr("Paused: %1 left").arg(songCount(waiting)) : tr("Downloading %1").arg(songCount(waiting)))
        : failed == 1             ? tr("1 download failed")
                                  : tr("%1 downloads failed").arg(failed);
    QStringList detail;
    const QString current = now.isEmpty() ? next : now;
    if (waiting > 0 && !allPaused && !current.isEmpty()) {
        detail.append(tr("Now: %1").arg(current));
    }
    detail.append(tr("%1 of %2 done").arg(doneInJobs).arg(itemsInJobs));
    if (waiting > 0 && failed > 0) {
        detail.append(tr("%1 failed").arg(failed));
    }
    m_pendingDetail = detail.join(QStringLiteral(" · "));
    m_pendingPercent = itemsInJobs > 0 ? std::min(100, static_cast<int>(progress * 100.0 / itemsInJobs + 0.5)) : 0;
    markLiked();
    // A progress tick changes only the strip above and the rows' marks; the
    // lists are set again (a reset, which loses the scroll place, hover and
    // focus) only when what they show changed.
    refreshGroups(false);
    emit changed();
    if (marksDiffer) {
        emit marksChanged();
    }
}

void OfflineViewModel::refreshGroups(bool invalidate)
{
    // Progress ticks change the snapshot but not which songs are here; the
    // library is read again only when that set changes.
    const QString signature = m_downloadedKeys.join(QLatin1Char('\n'));
    if (signature == m_groupsSignature && m_detailed.size() == m_downloaded.size()) {
        return;
    }
    m_groupsSignature = signature;
    if (m_downloaded.isEmpty()) {
        // Nothing to match: no read, and any read under way is out of date.
        ++m_groupsGeneration;
        m_groupsDirty = false;
        applyGroups({});
        showTab();
        return;
    }
    if (m_groupsReading) {
        m_groupsDirty = true;
        if (invalidate) {
            ++m_groupsGeneration;
        }
        return;
    }
    startGroupsRead();
}

void OfflineViewModel::startGroupsRead()
{
    const std::uint64_t generation = ++m_groupsGeneration;
    m_groupsReading = true;
    QList<core::db::TrackQuery> systemQueries;
    for (const SystemPlaylist& system : systemPlaylists()) {
        systemQueries.append(system.query);
    }
    const bool accountPlaylists = m_actions.accountPlaylistsActive();
    m_reader
        .read<OfflineLibraryRead>([systemQueries, accountPlaylists](core::db::Database& database) {
            return readOfflineLibrary(database, systemQueries, accountPlaylists);
        })
        .then(this, [this, generation](const OfflineLibraryRead& read) {
            m_groupsReading = false;
            if (generation == m_groupsGeneration) {
                applyGroups(read);
                showTab();
                emit changed();
            }
            if (m_groupsDirty) {
                m_groupsDirty = false;
                startGroupsRead();
            }
        });
}

void OfflineViewModel::applyGroups(const OfflineLibraryRead& read)
{
    // withLibraryDetails: a download is stored with only what it needs to
    // play, so its genre, length and main artist come from the library.
    QHash<QString, core::Track> known;
    if (m_stableKey && !m_downloaded.isEmpty()) {
        for (const core::Track& track : read.remoteSongs) {
            const QString key = core::track_keys::downloadMatchKey(m_stableKey(track));
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
        // A song downloaded through the account is still that account song:
        // played, liked, shown and sent to other devices as it, and counted
        // in the account's history. Its file only says where the audio is
        // (the player finds it through the downloads). Songs from a provider
        // key, and files of this PC, keep the shape of a file here.
        if (found->origin == QLatin1StringView("account") && found->isRemote()) {
            const bool liked = song.liked;
            const QString file = song.filePath;
            song = *found;
            song.liked = liked;
            song.filePath = file;
            continue;
        }
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
            keys.append(track.isRemote() && m_stableKey ? core::track_keys::downloadMatchKey(m_stableKey(track))
                                                         : QString());
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
        const QList<SystemPlaylist>& systems = systemPlaylists();
        for (qsizetype index = 0; index < systems.size() && index < read.systemSongs.size(); ++index) {
            addPlaylist(systems.at(index).key, systems.at(index).title, read.systemSongs.at(index));
        }
        for (qsizetype index = 0; index < read.playlists.size() && index < read.playlistSongs.size(); ++index) {
            const core::Track& playlist = read.playlists.at(index);
            playlistNames.append(playlist.title);
            addPlaylist(playlist.sourceUrl, playlist.title, read.playlistSongs.at(index));
        }
    }
    m_albumGroups = core::offline::albumGroups(m_detailed, playlistNames);
    m_artistGroups = core::offline::artistGroups(m_detailed);
    if (m_artistPhotos != nullptr) {
        QStringList names;
        names.reserve(m_artistGroups.size());
        for (const core::offline::OfflineGroup& group : m_artistGroups) {
            names.append(group.name);
        }
        m_artistPhotos->lookUp(names);
    }
    m_genreGroups = core::offline::genreGroups(m_detailed);
    m_history = historySongs(read.history);
    // An opened card whose songs are all gone closes.
    if (!m_groupKey.isEmpty() && openedGroup() == nullptr) {
        m_groupKey.clear();
    }
}

QList<core::Track> OfflineViewModel::historySongs(const QList<core::Track>& history) const
{
    if (m_downloaded.isEmpty() || !m_stableKey) {
        return {};
    }
    QStringList keys;
    for (const core::Track& track : history) {
        keys.append(track.isRemote() ? core::track_keys::downloadMatchKey(m_stableKey(track)) : QString());
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
        // A song's cover, loaded as every cover is (an account cover link
        // goes through the account; a kept one comes from disk).
        QUrl artwork;
        for (const core::Track& track : group.tracks) {
            artwork = CoverSource::forLibraryTrack(track).displayUrl();
            if (!artwork.isEmpty()) {
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
            // The artist's own photo once it is known.
            if (m_artistPhotos != nullptr) {
                if (const QString photo = m_artistPhotos->known(group.name); !photo.isEmpty()) {
                    core::Track picture;
                    picture.artworkUrl = photo;
                    picture.sourceKind = QStringLiteral("remote");
                    if (const QUrl shown = CoverSource::forTrack(picture).displayUrl(); !shown.isEmpty()) {
                        card.artworkUrl = shown;
                    }
                }
            }
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

QStringList OfflineViewModel::artistPhotoUrls() const
{
    QStringList urls;
    if (m_artistPhotos == nullptr) {
        return urls;
    }
    for (const core::offline::OfflineGroup& group : m_artistGroups) {
        if (const QString photo = m_artistPhotos->known(group.name); !photo.isEmpty()) {
            urls.append(photo);
        }
    }
    return urls;
}

void OfflineViewModel::markLiked()
{
    // The hearts: each download's answer is kept, since this runs on every
    // progress tick, and the likes are read only for songs not seen yet.
    std::function<bool(const core::Track&)> lookup;
    for (qsizetype i = 0; i < m_downloaded.size(); ++i) {
        core::Track& track = m_downloaded[i];
        const QString key = m_downloadedKeys.value(i).isEmpty() ? track.sourceUrl : m_downloadedKeys.value(i);
        if (const auto known = m_likedByKey.constFind(key); known != m_likedByKey.constEnd()) {
            track.liked = known.value();
            continue;
        }
        if (!lookup) {
            lookup = m_actions.likedLookup();
        }
        track.liked = lookup(track);
        m_likedByKey.insert(key, track.liked);
    }
}

void OfflineViewModel::rereadHearts()
{
    m_likedByKey.clear();
    const std::function<bool(const core::Track&)> liked = m_actions.likedLookup();
    // Every copy the page keeps, so the next tab or group shows them too.
    const auto reread = [&liked](QList<core::Track>& tracks) {
        for (core::Track& track : tracks) {
            track.liked = liked(track);
        }
    };
    reread(m_downloaded);
    reread(m_detailed);
    for (QList<core::offline::OfflineGroup>* groups : {&m_playlistGroups, &m_albumGroups, &m_artistGroups, &m_genreGroups}) {
        for (core::offline::OfflineGroup& group : *groups) {
            reread(group.tracks);
        }
    }
    markLiked();
    // The list on show: row by row, its liked mark only.
    const QList<core::Track> shown = m_songs.tracks();
    for (const core::Track& track : shown) {
        m_songs.setLiked(core::catalogSourceKey(track), liked(track));
    }
}

void OfflineViewModel::likeRow(int row)
{
    if (row >= 0 && row < m_songs.count()) {
        [[maybe_unused]] const std::optional<bool> liked = m_actions.toggleLike(m_songs.trackAt(row));
    }
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

void OfflineViewModel::cancel(const QString& jobId, const QString& itemId)
{
    m_downloads.cancelItem(jobId, itemId);
}

void OfflineViewModel::retry(const QString& jobId, const QString& itemId)
{
    m_downloads.retryItem(jobId, itemId);
}

void OfflineViewModel::retryAll()
{
    QStringList jobs;
    for (const QVariant& row : std::as_const(m_pending)) {
        const QVariantMap fields = row.toMap();
        const QString jobId = fields.value(QStringLiteral("jobId")).toString();
        if (fields.value(QStringLiteral("state")).toString() == QLatin1StringView("failed") && !jobs.contains(jobId)) {
            jobs.append(jobId);
        }
    }
    for (const QString& jobId : std::as_const(jobs)) {
        m_downloads.retryJob(jobId);
    }
}

void OfflineViewModel::cancelAll()
{
    // Song by song, so the finished songs of the same jobs stay downloaded.
    // With songs still coming, only those go; otherwise the failed ones.
    const QVariantList rows = m_pending;
    const bool active = m_pendingActive;
    for (const QVariant& row : rows) {
        const QVariantMap fields = row.toMap();
        const bool rowFailed = fields.value(QStringLiteral("state")).toString() == QLatin1StringView("failed");
        if (active && rowFailed) {
            continue;
        }
        m_downloads.cancelItem(fields.value(QStringLiteral("jobId")).toString(),
                               fields.value(QStringLiteral("itemId")).toString());
    }
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
