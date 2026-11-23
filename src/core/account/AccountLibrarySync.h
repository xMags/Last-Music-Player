#pragma once

#include "core/account/AccountMusicContract.h"
#include "core/account/AccountSessionProtocol.h"
#include "core/db/AccountOutboxRecords.h"
#include "core/db/AccountRecords.h"
#include "core/model/Track.h"

#include <QList>
#include <QString>

#include <optional>

// The pure parts of library sync: turning service songs into stored ones and
// back, and building the library copy that replaces the cached one. Built as
// earlier versions built them.
namespace lmp::core::account {

// A song from the account service as the app keeps it: origin "account",
// label "Account", no file, the length as shown ("3:05", empty for 0).
[[nodiscard]] Track trackFromPortable(const PortableAccountTrack& value);

// The service form of a stored song: the remote id, kind "remote" when
// empty, the source tag (else the origin unless it is "account"), the source
// address only when it may be kept, the artwork made durable, the length
// never below 0. Not necessarily valid (isValidPortableTrack).
[[nodiscard]] PortableAccountTrack portableFromTrack(const Track& track);

// The song a queued change carries: its stored JSON when that parses as a
// valid song, else the song read from it, when valid.
[[nodiscard]] std::optional<PortableAccountTrack> portableFromQueued(const QString& trackJson,
                                                                     const std::optional<Track>& track);

struct AccountLibraryInputs {
    QString ownerId;
    AccountProfile profile;
    // ISO 8601 UTC, "2026-08-24T10:00:00Z".
    QString nowUtc;
    // The cached library before this sync.
    std::optional<db::AccountLibrarySnapshot> existing;
    // Every queued like and play of the account, sent or not.
    QList<db::PendingLikeRecord> pendingLikes;
    QList<db::PlaybackEventRecord> pendingEvents;
};

// The library to store: the service's songs (liked, date added and play
// statistics as sent; songs without a date added get -1, -2, ... in order
// so they keep their place), its playlists, then the changes not yet sent
// laid over them: a queued like sets the song's liked state, a queued play
// raises its play statistics (keeping the cached ones when they were newer).
// A queued song the service does not know yet is added.
[[nodiscard]] db::AccountLibrarySnapshot buildAccountLibrarySnapshot(const ParsedAccountLibrary& parsed,
                                                                     const AccountLibraryInputs& inputs);

} // namespace lmp::core::account
