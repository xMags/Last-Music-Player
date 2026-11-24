#include "ui/player/QueueRailViewModel.h"

#include <QSet>

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "core/connect/ConnectSync.h"
#include "core/db/LibraryQueries.h"
#include "core/playback/PlayerController.h"
#include "core/playback/QueueTrackIdentity.h"
#include "ui/toasts/ToastQueue.h"
#include "ui/tracks/TrackMenuBuilder.h"
#include "ui/tracks/TrackMenuController.h"

namespace lmp::ui {

namespace {

constexpr QLatin1StringView kPlay{"play"};
constexpr QLatin1StringView kNext{"next"};
constexpr QLatin1StringView kUp{"up"};
constexpr QLatin1StringView kDown{"down"};
constexpr QLatin1StringView kRemove{"remove"};
constexpr QLatin1StringView kUndoPrefix{"queue-undo-"};
// The song menu's items in a row's menu carry this before their own id.
constexpr QLatin1StringView kSongPrefix{"song:"};

[[nodiscard]] bool inRange(qsizetype index, qsizetype size)
{
    return index >= 0 && index < size;
}

} // namespace

QueueRailViewModel::QueueRailViewModel(core::PlayerController& player, core::db::LibraryQueries& library,
                                       ToastQueue& toasts, core::connect::ConnectSync* link, QObject* parent)
    : QObject(parent)
    , m_player(player)
    , m_connect(link)
    , m_library(library)
    , m_toasts(toasts)
    , m_queue(this)
    , m_recent(this)
    , m_menu(this)
{
    connect(&m_player, &core::PlayerController::queueChanged, this, &QueueRailViewModel::refreshQueue);
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, [this]() {
        if (!m_player.currentTrack()) {
            refreshRecent();
        }
        emit trackChanged();
    });
    connect(&m_player, &core::PlayerController::autoplayChanged, this,
            &QueueRailViewModel::autoplayStatusChanged);
    connect(&m_player, &core::PlayerController::repeatModeChanged, this,
            &QueueRailViewModel::autoplayStatusChanged);
    connect(&m_toasts, &ToastQueue::actionTriggered, this, &QueueRailViewModel::undo);
    if (m_connect != nullptr) {
        connect(m_connect, &core::connect::ConnectSync::remoteChanged, this, [this]() {
            refreshQueue();
            emit trackChanged();
            emit autoplayStatusChanged();
        });
    }
    refreshQueue();
    if (!m_player.currentTrack()) {
        refreshRecent();
    }
}

bool QueueRailViewModel::remote() const
{
    return m_connect != nullptr && m_connect->isRemote();
}

qsizetype QueueRailViewModel::visibleCount() const
{
    return remote() ? m_connect->remote().upcoming.size() : m_player.visibleQueue().size();
}

bool QueueRailViewModel::hasTrack() const
{
    return remote() ? m_connect->remote().track.has_value() : m_player.currentTrack().has_value();
}

QString QueueRailViewModel::autoplayStatus() const
{
    if (remote()) {
        return {};
    }
    return m_player.autoplay() && m_player.repeatMode() == core::RepeatMode::Off ? QString() : tr("Autoplay off");
}

void QueueRailViewModel::playRow(int row, const QString& rowKey)
{
    if (!rowStillMatches(row, rowKey)) {
        return;
    }
    if (remote()) {
        m_connect->remotePlayUpcoming(row);
        return;
    }
    m_player.playVisibleQueueEntry(row);
}

void QueueRailViewModel::moveRow(int from, int to, const QString& rowKey)
{
    if (remote()) {
        return;
    }
    QList<core::QueueEntry> entries = m_player.visibleQueue();
    if (!rowStillMatches(from, rowKey) || !inRange(to, entries.size()) || from == to) {
        return;
    }
    entries.move(from, to);
    QList<core::Track> reordered;
    reordered.reserve(entries.size());
    for (const core::QueueEntry& entry : std::as_const(entries)) {
        reordered.append(entry.track);
    }
    m_player.commitVisibleOrder(reordered);
}

bool QueueRailViewModel::prepareMenu(int row, const QString& rowKey)
{
    if (remote() || !rowStillMatches(row, rowKey)) {
        return false;
    }
    const QList<core::QueueEntry> entries = m_player.visibleQueue();
    m_menuPosition = row;
    m_menuIdentity = core::queueTrackIdentity(entries.at(row).track);
    QList<MenuItemListModel::Item> items{{.id = QString(kPlay), .label = tr("Play now"), .iconName = QStringLiteral("play")}};
    if (row > 0) {
        items.append({.id = QString(kNext), .label = tr("Play next"), .iconName = QStringLiteral("play-next")});
        items.append({.id = QString(kUp), .label = tr("Move up"), .iconName = QStringLiteral("arrow-up")});
    }
    if (row < entries.size() - 1) {
        items.append({.id = QString(kDown), .label = tr("Move down"), .iconName = QStringLiteral("arrow-down")});
    }
    // Then what any song menu offers (track-menu.ts songMenuItems): Add to
    // playlist and Like, Share, then Go to artist and Go to album.
    if (m_trackMenu != nullptr) {
        m_trackMenu->prepare(entries.at(row).track, {});
        const QSet<QString> songItems{QString(TrackMenuBuilder::AddToPlaylist), QString(TrackMenuBuilder::Like),
                                      QString(TrackMenuBuilder::Share), QString(TrackMenuBuilder::Artist),
                                      QString(TrackMenuBuilder::Album)};
        for (const MenuItemListModel::Item& item : m_trackMenu->menu()->items()) {
            if (!songItems.contains(item.id)) {
                continue;
            }
            MenuItemListModel::Item copy = item;
            copy.id = QString(kSongPrefix) + item.id;
            copy.separatorBefore = item.id == TrackMenuBuilder::AddToPlaylist || item.id == TrackMenuBuilder::Artist;
            items.append(copy);
        }
    }
    items.append({.id = QString(kRemove),
                  .label = tr("Remove from queue"),
                  .iconName = QStringLiteral("trash"),
                  .danger = true,
                  .separatorBefore = true});
    m_menu.setItems(std::move(items));
    return true;
}

void QueueRailViewModel::triggerMenu(const QString& itemId)
{
    // The queue can move on while the menu is open (a song ends), so the
    // action follows the song, not the row it was on.
    if (itemId.startsWith(kSongPrefix)) {
        // The song menu was prepared for this row's song when the menu opened.
        if (m_trackMenu != nullptr) {
            m_trackMenu->trigger(itemId.mid(kSongPrefix.size()));
        }
        return;
    }
    const qsizetype now = livePosition(m_menuPosition, m_menuIdentity);
    if (now < 0) {
        return;
    }
    if (itemId == kPlay) {
        m_player.playVisibleQueueEntry(now);
    } else if (itemId == kNext) {
        moveToFront(now);
    } else if (itemId == kUp) {
        m_player.moveVisibleEntry(now, core::QueueDirection::Previous);
    } else if (itemId == kDown) {
        m_player.moveVisibleEntry(now, core::QueueDirection::Next);
    } else if (itemId == kRemove) {
        removeWithUndo(now);
    }
}

void QueueRailViewModel::clearUpcoming()
{
    if (remote()) {
        m_connect->remoteClearUpcoming();
        return;
    }
    m_player.clearUpcoming();
}

void QueueRailViewModel::playRecent(int row)
{
    // The rows are the first songs of the context, in its order.
    if (!inRange(row, m_recent.count()) || !inRange(row, m_recentContext.size())) {
        return;
    }
    const QList<core::Track> context = m_recentContext;
    m_player.playInContext(context.at(row), context);
}

bool QueueRailViewModel::rowStillMatches(int row, const QString& rowKey) const
{
    return !rowKey.isEmpty() && m_queue.keyAt(row) == rowKey && inRange(row, visibleCount());
}

qsizetype QueueRailViewModel::livePosition(qsizetype position, const QString& identity) const
{
    if (identity.isEmpty()) {
        return -1;
    }
    const QList<core::QueueEntry> entries = m_player.visibleQueue();
    qsizetype best = -1;
    for (qsizetype index = 0; index < entries.size(); ++index) {
        if (core::queueTrackIdentity(entries.at(index).track) != identity) {
            continue;
        }
        if (best < 0 || std::abs(index - position) < std::abs(best - position)) {
            best = index;
        }
    }
    return best;
}

void QueueRailViewModel::removeWithUndo(qsizetype position)
{
    const QList<core::QueueEntry> entries = m_player.visibleQueue();
    if (!inRange(position, entries.size())) {
        return;
    }
    const core::QueueEntry entry = entries.at(position);
    m_player.removeVisibleEntry(position);
    // A song still in the queue (listed twice, or the one playing) cannot be
    // put back without a duplicate, so Undo is offered only when it works.
    const core::QueueEngine& remaining = m_player.queue();
    if (remaining.restoreVisibleEntry(position, entry) == remaining) {
        m_toasts.show(tr("Removed from queue"));
        return;
    }
    const QString actionId = kUndoPrefix + QString::number(m_nextUndo++);
    m_undos.append(PendingUndo{actionId, position, entry});
    // No more can still be on screen than the toasts that show at once.
    while (m_undos.size() > ToastQueue::kMaxVisible) {
        m_undos.removeFirst();
    }
    m_toasts.show(tr("Removed from queue"), false, tr("Undo"), actionId);
}

void QueueRailViewModel::moveToFront(qsizetype position)
{
    QList<core::QueueEntry> entries = m_player.visibleQueue();
    if (!inRange(position, entries.size())) {
        return;
    }
    entries.move(position, 0);
    QList<core::Track> reordered;
    reordered.reserve(entries.size());
    for (const core::QueueEntry& entry : std::as_const(entries)) {
        reordered.append(entry.track);
    }
    m_player.commitVisibleOrder(reordered);
}

void QueueRailViewModel::undo(const QString& actionId)
{
    const auto pending = std::find_if(m_undos.cbegin(), m_undos.cend(), [&actionId](const PendingUndo& undo) {
        return undo.actionId == actionId;
    });
    if (pending == m_undos.cend()) {
        return;
    }
    const PendingUndo restore = *pending;
    m_undos.erase(pending);
    m_player.restoreVisibleEntry(restore.position, restore.entry);
}

void QueueRailViewModel::refreshQueue()
{
    if (remote()) {
        QList<core::QueueEntry> entries;
        const QList<core::Track>& upcoming = m_connect->remote().upcoming;
        entries.reserve(upcoming.size());
        for (qsizetype index = 0; index < upcoming.size(); ++index) {
            entries.append({.track = upcoming.at(index), .kind = core::QueueEntryKind::Context, .sourceIndex = index});
        }
        m_queue.setEntries(entries);
        return;
    }
    m_queue.setEntries(m_player.visibleQueue());
}

void QueueRailViewModel::refreshRecent()
{
    // The latest plays, newest first, each song once; songs that cannot
    // play are left out.
    m_recentContext.clear();
    QSet<QString> seen;
    for (const core::Track& track : m_library.loadHistoryTracks()) {
        const QString identity = core::queueTrackIdentity(track);
        if (track.unavailable || identity.isEmpty() || seen.contains(identity)) {
            continue;
        }
        seen.insert(identity);
        m_recentContext.append(track);
    }
    QList<core::QueueEntry> shown;
    for (const core::Track& track : m_recentContext.first(std::min<qsizetype>(RecentLimit, m_recentContext.size()))) {
        shown.append(core::QueueEntry{.track = track, .kind = core::QueueEntryKind::Context, .sourceIndex = 0});
    }
    m_recent.setEntries(shown);
}

} // namespace lmp::ui
