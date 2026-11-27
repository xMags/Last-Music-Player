#include "core/account/AccountLibrarySync.h"

#include <limits>
#include "core/net/Json.h"
#include "core/net/ArtworkUrl.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QCryptographicHash>
#include <QHash>
#include <QLatin1StringView>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <utility>

namespace lmp::core::account {

namespace {

[[nodiscard]] QString formatDuration(double seconds)
{
    if (seconds <= 0.0) {
        return {};
    }
    const qint64 whole = json::wholeNumber(seconds, 0, std::numeric_limits<qint64>::max());
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

namespace {

// Writes values into a digest so that no two different sequences read the
// same: every string carries its length.
class Digest {
public:
    void add(const QString& value)
    {
        add(static_cast<std::int64_t>(value.size()));
        m_hash.addData(QByteArrayView(reinterpret_cast<const char*>(value.utf16()),
                                      value.size() * static_cast<qsizetype>(sizeof(char16_t))));
    }
    void add(const QStringList& values)
    {
        add(static_cast<std::int64_t>(values.size()));
        for (const QString& value : values) {
            add(value);
        }
    }
    void add(std::int64_t value) { m_hash.addData(QByteArrayView(reinterpret_cast<const char*>(&value), sizeof(value))); }
    void add(int value) { add(static_cast<std::int64_t>(value)); }
    void add(bool value) { add(static_cast<std::int64_t>(value ? 1 : 0)); }
    void add(double value) { add(std::bit_cast<std::int64_t>(value)); }
    [[nodiscard]] QByteArray result() const { return m_hash.result(); }

private:
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
};

// Every field but rowId (see accountLibraryFingerprint). A new Track field
// changes the size and stops the build here until it is added below (the
// size is that of this compiler and platform).
#if defined(_MSC_VER) && defined(_WIN64)
static_assert(sizeof(Track) == 712, "Track changed: add the new field to addTrack, then update this size");
#endif

void addTrack(Digest& digest, const Track& track)
{
    for (const QString* text : {&track.title, &track.artist, &track.album, &track.genre, &track.durationText,
                                &track.filePath, &track.artworkUrl, &track.sourceKind, &track.origin, &track.sourceTag,
                                &track.sourceUrl, &track.sourceReference, &track.streamUrl, &track.sourceLabel,
                                &track.quality, &track.remoteId, &track.catalogId, &track.albumCatalogId,
                                &track.artistCatalogId, &track.primaryArtistName, &track.primaryArtistCatalogId,
                                &track.dateAdded, &track.likedAt, &track.addedAt, &track.firstPlayedAt,
                                &track.lastPlayedAt, &track.unavailableReason}) {
        digest.add(*text);
    }
    digest.add(track.year);
    digest.add(track.durationSeconds);
    digest.add(track.dateAddedSortKey);
    digest.add(track.playCount);
    digest.add(track.resumePositionSeconds);
    digest.add(track.liked);
    digest.add(track.offline);
    digest.add(track.inLibrary);
    digest.add(track.followed);
    digest.add(track.unavailable);
    digest.add(track.index);
    digest.add(track.trackCount);
}

} // namespace

QByteArray accountLibraryFingerprint(const db::AccountLibrarySnapshot& library)
{
    Digest digest;
    const db::AccountProfileSnapshot& profile = library.profile;
    for (const QString* text :
         {&profile.accountId, &profile.displayName, &profile.username, &profile.avatarUrl, &profile.planLabel}) {
        digest.add(*text);
    }
    digest.add(static_cast<std::int64_t>(library.tracks.size()));
    for (const db::AccountTrackSnapshot& entry : library.tracks) {
        addTrack(digest, entry.track);
        digest.add(static_cast<std::int64_t>(entry.playCount));
        digest.add(entry.firstPlayedAtUtc);
        digest.add(entry.lastPlayedAtUtc);
        digest.add(entry.lastPositionSeconds);
    }
    digest.add(static_cast<std::int64_t>(library.playlists.size()));
    for (const db::AccountPlaylistSnapshot& playlist : library.playlists) {
        for (const QString* text :
             {&playlist.playlistId, &playlist.name, &playlist.description, &playlist.sourceUrl, &playlist.updatedAtUtc}) {
            digest.add(*text);
        }
        digest.add(playlist.remoteTrackIds);
    }
    return digest.result();
}

} // namespace lmp::core::account
