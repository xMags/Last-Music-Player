#include "ui/library/CollectionViewModel.h"

#include <QSet>
#include <QVariantMap>

#include <algorithm>
#include <memory>
#include <utility>

#include "core/db/LibraryQueries.h"
#include "core/library/TrackActions.h"
#include "core/model/ArtworkPlaceholder.h"
#include "core/model/CatalogSourceKey.h"
#include "core/playback/PlayerController.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "ui/library/SystemPlaylists.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/player/CoverSource.h"
#include "ui/tracks/QueryTrackSource.h"
#include "ui/tracks/TrackMenuController.h"

namespace lmp::ui {

namespace keys = core::settings::keys;

namespace {

const QStringList kKinds{QStringLiteral("album"), QStringLiteral("album-collection"), QStringLiteral("genre"),
                         QStringLiteral("playlist"), QStringLiteral("system")};

QString countText(int count)
{
    return count == 1 ? CollectionViewModel::tr("1 song") : CollectionViewModel::tr("%1 songs").arg(count);
}

// "48 min" or "1 hr 12 min" (collection-header.ts totalDurationLabel); empty
// when no song knows its length.
QString durationText(double seconds)
{
    if (seconds <= 0.0) {
        return {};
    }
    const int minutes = std::max(1, static_cast<int>(seconds / 60.0 + 0.5));
    if (minutes < 60) {
        return CollectionViewModel::tr("%1 min").arg(minutes);
    }
    const int hours = minutes / 60;
    const int rest = minutes % 60;
    return rest == 0 ? CollectionViewModel::tr("%1 hr").arg(hours)
                     : CollectionViewModel::tr("%1 hr %2 min").arg(hours).arg(rest);
}

QString sortLabel(core::DetailSort sort)
{
    switch (sort) {
    case core::DetailSort::Curated:
        return CollectionViewModel::tr("Custom order");
    case core::DetailSort::DateAdded:
        return CollectionViewModel::tr("Recently added");
    case core::DetailSort::Title:
        return CollectionViewModel::tr("Title");
    case core::DetailSort::Artist:
        return CollectionViewModel::tr("Artist");
    case core::DetailSort::Duration:
        return CollectionViewModel::tr("Duration");
    }
    return {};
}

} // namespace

CollectionViewModel::CollectionViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_library(parts.library)
    , m_actions(parts.actions)
    , m_player(parts.player)
    , m_settings(parts.settings)
    , m_trackMenu(parts.trackMenu)
    , m_navigator(parts.navigator)
{
    connect(&m_navigator, &Navigator::currentChanged, this, &CollectionViewModel::followNavigator);
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, &CollectionViewModel::followPlayer);
    connect(&m_actions, &core::TrackActions::likedChanged, this, [this](const QString& sourceKey, bool liked) {
        m_tracks.setLiked(sourceKey, liked);
        // Favourites changes with every like.
        if (m_kind == QLatin1StringView("system")) {
            reload();
        }
    });
    connect(&m_actions, &core::TrackActions::playlistChanged, this, [this](const QString& playlistKey) {
        if (userPlaylist() && playlistKey == m_key && !m_reordering) {
            reload();
        }
    });
}

void CollectionViewModel::followNavigator()
{
    const Route route = m_navigator.current();
    if (route.kind() != Route::Kind::Collection) {
        return;
    }
    const QString kind = route.param(QStringLiteral("kind"));
    if (!kKinds.contains(kind)) {
        return;
    }
    open(kind, route.param(QStringLiteral("key")), route.param(QStringLiteral("title")));
}

void CollectionViewModel::open(const QString& kind, const QString& key, const QString& title)
{
    m_kind = kind;
    m_key = key;
    m_title = title;
    m_description.clear();
    m_iconName.clear();
    m_gradientStart = QColor();
    m_gradientEnd = QColor();
    m_systemQuery = {};
    if (kind == QLatin1StringView("system")) {
        if (const std::optional<SystemPlaylist> system = systemPlaylist(key)) {
            m_title = system->title;
            m_description = system->description;
            m_iconName = system->iconName;
            m_gradientStart = system->gradientStart;
            m_gradientEnd = system->gradientEnd;
            m_systemQuery = system->query;
        }
    }
    m_findText.clear();
    m_sort = sortsFor(kind).constFirst();
    m_selection.clear();
    m_anchor = -1;
    emit opened();
    emit toolbarChanged();
    emit selectionChanged();
    reload();
}

QList<core::DetailSort> CollectionViewModel::sortsFor(const QString& kind)
{
    using core::DetailSort;
    // A system playlist's own order is its query's (newest like, most
    // played, newest added); albums from tags and genres have no stored one.
    if (core::kindSupportsCuratedOrder(kind) || kind == QLatin1StringView("system")) {
        return {DetailSort::Curated, DetailSort::DateAdded, DetailSort::Title, DetailSort::Artist,
                DetailSort::Duration};
    }
    return {DetailSort::DateAdded, DetailSort::Title, DetailSort::Artist, DetailSort::Duration};
}

core::db::TrackQuery CollectionViewModel::query() const
{
    core::db::TrackQuery query;
    if (m_kind == QLatin1StringView("system")) {
        query = m_systemQuery;
        if (m_sort != core::DetailSort::Curated) {
            query.sort = core::detailSortQueryValue(m_sort);
        }
    } else {
        query.groupKind = m_kind;
        query.groupKey = m_key;
        query.sort = core::detailSortQueryValue(m_sort);
    }
    query.searchText = m_findText.trimmed();
    return query;
}

void CollectionViewModel::reload()
{
    if (m_kind.isEmpty() || m_key.isEmpty()) {
        return;
    }
    setSelection({});
    const core::db::TrackQuery listQuery = query();
    // The whole collection's count and length, for the header.
    core::db::TrackQuery whole = listQuery;
    whole.searchText.clear();
    whole.limit = 1;
    const core::db::TrackPage wholePage = m_library.loadTrackPage(whole);
    m_totalCount = wholePage.totalCount;
    m_totalSeconds = wholePage.totalSeconds;
    m_tracks.setSource(std::make_shared<QueryTrackSource>(m_library, listQuery));
    m_byline.clear();
    if (m_kind == QLatin1StringView("album") || m_kind == QLatin1StringView("album-collection")) {
        // The album's artist when every loaded song shares one.
        QSet<QString> artists;
        for (const core::Track& track : m_tracks.tracks()) {
            artists.insert(track.artist);
        }
        if (artists.size() == 1) {
            m_byline = *artists.cbegin();
        }
    }
    readCover();
    followPlayer();
    emit contentChanged();
}

void CollectionViewModel::readCover()
{
    m_artworkUrl.clear();
    m_collage.clear();
    if (m_kind == QLatin1StringView("system")) {
        m_coverStyle = QStringLiteral("system");
        return;
    }
    if (m_kind == QLatin1StringView("genre")) {
        m_coverStyle = QStringLiteral("genre");
        return;
    }
    // Up to four distinct covers in list order (playlist-art.ts).
    QList<QUrl> covers;
    for (const core::Track& track : m_tracks.tracks()) {
        const QUrl url = CoverSource::forLibraryTrack(track).displayUrl();
        if (url.isEmpty() || covers.contains(url)) {
            continue;
        }
        covers.append(url);
        if (covers.size() == 4) {
            break;
        }
    }
    // An album shows its cover; a playlist with two or more covers a
    // collage: two as a checkerboard, three or four in order.
    const bool playlist = m_kind == QLatin1StringView("playlist");
    if (playlist && covers.size() >= 2) {
        m_coverStyle = QStringLiteral("collage");
        if (covers.size() == 2) {
            m_collage = {covers.at(0), covers.at(1), covers.at(1), covers.at(0)};
        } else {
            for (int slot = 0; slot < 4; ++slot) {
                m_collage.append(covers.at(slot % covers.size()));
            }
        }
        return;
    }
    m_coverStyle = QStringLiteral("image");
    if (!covers.isEmpty()) {
        m_artworkUrl = covers.constFirst();
    }
}

QString CollectionViewModel::eyebrow() const
{
    if (m_kind == QLatin1StringView("genre")) {
        return tr("GENRE");
    }
    if (m_kind == QLatin1StringView("album") || m_kind == QLatin1StringView("album-collection")) {
        return tr("ALBUM");
    }
    return tr("PLAYLIST");
}

QString CollectionViewModel::backLabel() const
{
    if (m_kind == QLatin1StringView("genre")) {
        return tr("All genres");
    }
    if (m_kind == QLatin1StringView("album") || m_kind == QLatin1StringView("album-collection")) {
        return tr("All albums");
    }
    return tr("All playlists");
}

QString CollectionViewModel::info() const
{
    // The whole collection, also while a find narrows the list.
    QStringList parts{countText(m_totalCount)};
    const QString duration = durationText(m_totalSeconds);
    if (!duration.isEmpty()) {
        parts.append(duration);
    }
    return parts.join(QStringLiteral(" · "));
}

QString CollectionViewModel::artworkId() const
{
    return m_kind + QLatin1Char(':') + m_key;
}

QString CollectionViewModel::artworkLabel() const
{
    return core::ArtworkPlaceholder::tileLabelFor(m_title);
}

QString CollectionViewModel::emptyText() const
{
    if (!m_findText.trimmed().isEmpty()) {
        return tr("No songs match “%1”").arg(m_findText.trimmed());
    }
    if (m_kind == QLatin1StringView("system")) {
        if (m_key == QLatin1StringView("smart-liked")) {
            return tr("Tap the heart on a song to add it to Favourites.");
        }
        if (m_key == QLatin1StringView("smart-recent")) {
            return tr("New library additions will appear here.");
        }
        return tr("Play some music and your most-played tracks will appear here.");
    }
    if (userPlaylist()) {
        return tr("This playlist is empty. Add tracks from a song menu.");
    }
    return tr("Nothing here yet.");
}

void CollectionViewModel::setFindText(const QString& text)
{
    if (text == m_findText) {
        return;
    }
    m_findText = text;
    emit toolbarChanged();
    reload();
}

QString CollectionViewModel::findPlaceholder() const
{
    return userPlaylist() ? tr("Find in playlist") : tr("Find in this list");
}

int CollectionViewModel::sort() const
{
    return static_cast<int>(sortsFor(m_kind).indexOf(m_sort));
}

void CollectionViewModel::setSort(int index)
{
    const QList<core::DetailSort> sorts = sortsFor(m_kind);
    if (index < 0 || index >= sorts.size() || sorts.at(index) == m_sort) {
        return;
    }
    m_sort = sorts.at(index);
    emit toolbarChanged();
    reload();
}

QStringList CollectionViewModel::sorts() const
{
    QStringList labels;
    for (const core::DetailSort sort : sortsFor(m_kind)) {
        labels.append(sortLabel(sort));
    }
    return labels;
}

bool CollectionViewModel::compact() const
{
    return m_settings.string(keys::LibraryDetailDensity, QStringLiteral("Comfortable")) == QLatin1StringView("Compact");
}

void CollectionViewModel::setCompact(bool compact)
{
    if (compact == this->compact()) {
        return;
    }
    m_settings.setString(keys::LibraryDetailDensity,
                         compact ? QStringLiteral("Compact") : QStringLiteral("Comfortable"));
    emit compactChanged();
}

bool CollectionViewModel::canReorder() const
{
    return userPlaylist() && m_sort == core::DetailSort::Curated && m_findText.trimmed().isEmpty();
}

QVariantList CollectionViewModel::moveTargets() const
{
    QVariantList items;
    if (!userPlaylist()) {
        return items;
    }
    for (const core::Track& playlist : m_actions.playlists()) {
        if (playlist.sourceUrl == m_key) {
            continue;
        }
        items.append(QVariantMap{{QStringLiteral("id"), playlist.sourceUrl},
                                 {QStringLiteral("label"), playlist.title}});
    }
    return items;
}

QList<core::Track> CollectionViewModel::allTracks()
{
    m_tracks.fetchAll();
    return m_tracks.tracks();
}

void CollectionViewModel::play()
{
    const QList<core::Track> tracks = allTracks();
    for (const core::Track& track : tracks) {
        if (m_actions.canPlay(track)) {
            m_actions.play(track, tracks);
            return;
        }
    }
}

void CollectionViewModel::shuffle()
{
    m_actions.shufflePlay(allTracks());
}

void CollectionViewModel::playNextAll()
{
    // Play next puts each song after the current one, so the list goes in
    // backwards to keep its order.
    QList<core::Track> tracks = allTracks();
    std::reverse(tracks.begin(), tracks.end());
    for (const core::Track& track : tracks) {
        m_actions.playNext(track);
    }
}

void CollectionViewModel::addAllToQueue()
{
    m_actions.addAllToQueue(allTracks());
}

void CollectionViewModel::activate(int row, bool toggle, bool extend)
{
    if (row < 0 || row >= m_tracks.count()) {
        return;
    }
    if (extend && m_anchor >= 0) {
        QList<int> rows;
        for (int index = std::min(m_anchor, row); index <= std::max(m_anchor, row); ++index) {
            rows.append(index);
        }
        setSelection(rows);
        return;
    }
    if (toggle) {
        toggleSelected(row);
        return;
    }
    clearSelection();
    m_actions.play(m_tracks.trackAt(row), allTracks());
}

void CollectionViewModel::toggleSelected(int row)
{
    if (row < 0 || row >= m_tracks.count()) {
        return;
    }
    QList<int> rows = m_selection;
    if (rows.contains(row)) {
        rows.removeAll(row);
    } else {
        rows.append(row);
    }
    m_anchor = row;
    setSelection(rows);
}

void CollectionViewModel::clearSelection()
{
    m_anchor = -1;
    setSelection({});
}

void CollectionViewModel::setSelection(QList<int> rows)
{
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows == m_selection) {
        return;
    }
    m_selection = std::move(rows);
    m_tracks.setSelectedRows(m_selection);
    emit selectionChanged();
}

QList<core::Track> CollectionViewModel::selectedTracks() const
{
    QList<core::Track> tracks;
    for (const int row : m_selection) {
        tracks.append(m_tracks.trackAt(row));
    }
    return tracks;
}

void CollectionViewModel::queueSelection()
{
    if (m_actions.addAllToQueue(selectedTracks())) {
        clearSelection();
    }
}

void CollectionViewModel::removeSelection()
{
    if (!userPlaylist() || m_selection.isEmpty()) {
        return;
    }
    const int count = selectedCount();
    if (m_actions.removeAllFromPlaylist(selectedTracks(), m_key)) {
        emit notice(count == 1 ? tr("Removed 1 song") : tr("Removed %1 songs").arg(count));
    } else {
        emit notice(tr("The songs could not be removed."));
    }
}

void CollectionViewModel::moveSelection(const QString& playlistKey)
{
    if (!userPlaylist() || m_selection.isEmpty()) {
        return;
    }
    QString target;
    for (const QVariant& item : moveTargets()) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("id")).toString() == playlistKey) {
            target = map.value(QStringLiteral("label")).toString();
        }
    }
    if (target.isEmpty()) {
        return;
    }
    const int count = selectedCount();
    if (m_actions.moveToPlaylist(selectedTracks(), m_key, playlistKey)) {
        emit notice(count == 1 ? tr("Moved 1 song to %1").arg(target) : tr("Moved %1 songs to %2").arg(count).arg(target));
    } else {
        emit notice(tr("The songs could not be moved."));
    }
}

void CollectionViewModel::like(int row)
{
    if (row >= 0 && row < m_tracks.count()) {
        [[maybe_unused]] const std::optional<bool> liked = m_actions.toggleLike(m_tracks.trackAt(row));
    }
}

void CollectionViewModel::prepareMenu(int row)
{
    if (row < 0 || row >= m_tracks.count()) {
        return;
    }
    m_trackMenu.prepare(m_tracks.trackAt(row), m_tracks.tracks(), userPlaylist() ? m_key : QString());
}

void CollectionViewModel::loadAll()
{
    m_tracks.fetchAll();
}

bool CollectionViewModel::moveRow(int from, int to)
{
    if (!canReorder()) {
        return false;
    }
    m_tracks.fetchAll();
    if (!m_tracks.moveRow(from, to)) {
        return false;
    }
    m_selection.clear();
    emit selectionChanged();
    // Stored as the playlist's whole order. The list already shows it, so
    // the playlist's change signal is not followed by a reload (which would
    // reset the list and its scroll); on failure the list is read again so
    // it shows what is stored.
    m_reordering = true;
    const bool stored = static_cast<bool>(m_actions.reorderPlaylist(m_key, m_tracks.tracks()));
    m_reordering = false;
    if (!stored) {
        emit notice(tr("The new order could not be saved."));
        reload();
        return false;
    }
    return true;
}

void CollectionViewModel::followPlayer()
{
    const std::optional<core::Track>& current = m_player.currentTrack();
    m_tracks.setPlayingKey(current ? core::catalogSourceKey(*current) : QString());
}

} // namespace lmp::ui
