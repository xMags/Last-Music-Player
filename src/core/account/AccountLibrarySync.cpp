#include "core/account/AccountLibrarySync.h"

#include "core/net/ArtworkUrl.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QHash>
#include <QLatin1StringView>

#include <algorithm>
#include <utility>

namespace lmp::core::account {

namespace {

[[nodiscard]] QString formatDuration(double seconds)
{
    if (seconds <= 0.0) {
        return {};
    }
    const auto whole = static_cast<qint64>(seconds);
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

Track trackFromPortable(const PortableAccountTrack& value)
{
    Track track;
    track.remoteId = value.id;
    track.title = value.title;
    track.artist = value.artist;
    track.album = value.album;
    track.genre = value.genre;
    track.artworkUrl = value.artworkUrl;
    track.sourceKind = value.sourceKind;
    track.origin = QStringLiteral("account");
    track.sourceTag = value.provider;
    track.sourceUrl = value.sourceUrl;
    track.catalogId = value.catalogId;
    track.albumCatalogId = value.albumCatalogId;
    track.artistCatalogId = value.artistCatalogId;
    track.durationSeconds = value.durationSec;
    track.durationText = formatDuration(value.durationSec);
    track.sourceLabel = QStringLiteral("Account");
    return track;
}

PortableAccountTrack portableFromTrack(const Track& track)
{
    PortableAccountTrack result;
    result.id = track.remoteId;
    result.title = track.title;
    result.artist = track.artist;
    result.album = track.album;
    result.genre = track.genre;
    result.artworkUrl = net::durableAccountArtworkUrl(track.artworkUrl);
    result.sourceKind = track.sourceKind.isEmpty() ? QStringLiteral("remote") : track.sourceKind;
    if (!track.sourceTag.isEmpty()) {
        result.provider = track.sourceTag;
    } else if (track.origin != QLatin1StringView("account")) {
        result.provider = track.origin;
    }
    result.sourceUrl = net::isSafeRemoteUrl(track.sourceUrl, net::RemoteUrlUse::Durable) ? track.sourceUrl : QString();
    result.catalogId = track.catalogId;
    result.albumCatalogId = track.albumCatalogId;
    result.artistCatalogId = track.artistCatalogId;
    result.durationSec = std::max(0.0, track.durationSeconds);
    return result;
}

std::optional<PortableAccountTrack> portableFromQueued(const QString& trackJson, const std::optional<Track>& track)
{
    if (std::optional<PortableAccountTrack> parsed = parsePortableTrack(trackJson.toUtf8())) {
        return parsed;
    }
    if (!track) {
        return std::nullopt;
    }
    PortableAccountTrack fallback = portableFromTrack(*track);
    if (!isValidPortableTrack(fallback)) {
        return std::nullopt;
    }
    return fallback;
}

db::AccountLibrarySnapshot buildAccountLibrarySnapshot(const ParsedAccountLibrary& parsed,
                                                       const AccountLibraryInputs& inputs)
{
    db::AccountLibrarySnapshot snapshot;
    snapshot.profile.accountId = inputs.ownerId;
    snapshot.profile.displayName = inputs.profile.displayName;
    snapshot.profile.username = inputs.profile.username;
    snapshot.profile.avatarUrl = inputs.profile.avatarUrl;
    snapshot.profile.planLabel = inputs.profile.planLabel;
    snapshot.profile.updatedAtUtc = inputs.nowUtc;
    snapshot.synchronizedAtUtc = inputs.nowUtc;

    QHash<QString, qsizetype> indexes;
    double sequence = 1;
    for (const AccountLibraryTrack& remote : parsed.tracks) {
        db::AccountTrackSnapshot entry;
        entry.track = trackFromPortable(remote.track);
        entry.track.liked = remote.liked;
        entry.track.dateAdded = remote.addedAt;
        if (remote.legacyAddedSortKey > 0.0) {
            entry.track.dateAddedSortKey = remote.legacyAddedSortKey;
        } else if (remote.addedAt.isEmpty()) {
            entry.track.dateAddedSortKey = -sequence;
            sequence += 1;
        } else {
            entry.track.dateAddedSortKey = 0.0;
        }
        entry.playCount = remote.playCount;
        entry.firstPlayedAtUtc = remote.firstPlayedAt;
        entry.lastPlayedAtUtc = remote.lastPlayedAt;
        entry.lastPositionSeconds = remote.lastPositionSec;
        indexes.insert(remote.track.id, snapshot.tracks.size());
        snapshot.tracks.append(std::move(entry));
    }

    QHash<QString, db::AccountTrackSnapshot> existingTracks;
    if (inputs.existing) {
        for (const db::AccountTrackSnapshot& track : inputs.existing->tracks) {
            existingTracks.insert(track.track.remoteId, track);
        }
    }

    // The stored song for `remoteId`, added from `fallback` when the service
    // does not have it yet; nothing without either.
    const auto ensureTrack = [&snapshot, &indexes](const QString& remoteId,
                                                   const std::optional<Track>& fallback) -> db::AccountTrackSnapshot* {
        if (const auto found = indexes.constFind(remoteId); found != indexes.constEnd()) {
            return &snapshot.tracks[found.value()];
        }
        if (!fallback) {
            return nullptr;
        }
        db::AccountTrackSnapshot entry;
        entry.track = *fallback;
        entry.track.rowId = 0;
        indexes.insert(remoteId, snapshot.tracks.size());
        snapshot.tracks.append(std::move(entry));
        return &snapshot.tracks.last();
    };

    for (const db::PendingLikeRecord& like : inputs.pendingLikes) {
        if (db::AccountTrackSnapshot* entry = ensureTrack(like.remoteTrackId, like.track)) {
            entry->track.liked = like.desiredState;
        }
    }
    for (const db::PlaybackEventRecord& event : inputs.pendingEvents) {
        db::AccountTrackSnapshot* entry = ensureTrack(event.remoteTrackId, event.track);
        if (entry == nullptr) {
            continue;
        }
        if (const auto found = existingTracks.constFind(event.remoteTrackId); found != existingTracks.constEnd()) {
            entry->playCount = std::max(entry->playCount, found->playCount);
            if (found->lastPlayedAtUtc > entry->lastPlayedAtUtc) {
                entry->lastPlayedAtUtc = found->lastPlayedAtUtc;
                entry->lastPositionSeconds = found->lastPositionSeconds;
            }
        }
        if (event.playedAtUtc > entry->lastPlayedAtUtc) {
            entry->lastPlayedAtUtc = event.playedAtUtc;
            entry->lastPositionSeconds = event.positionSeconds;
        }
    }

    for (const AccountPlaylist& remote : parsed.playlists) {
        db::AccountPlaylistSnapshot playlist;
        playlist.playlistId = remote.id;
        playlist.name = remote.name;
        playlist.description = remote.description;
        playlist.sourceUrl = remote.sourceUrl;
        playlist.updatedAtUtc = remote.updatedAt;
        playlist.remoteTrackIds = remote.trackIds;
        snapshot.playlists.append(std::move(playlist));
    }
    return snapshot;
}

} // namespace lmp::core::account
