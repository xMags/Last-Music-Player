#pragma once

#include "core/db/LibraryQueryTypes.h"
#include "core/model/Track.h"

#include <QList>
#include <QString>

namespace lmp::core::db {

// Where the songs of an account playlist come from. The account store
// implements it; LibraryQueries only reads through it.
class AccountPlaylistTrackSource {
public:
    virtual ~AccountPlaylistTrackSource() = default;

    // In playlist order. Empty when the playlist or the account is unknown.
    [[nodiscard]] virtual QList<Track> loadAccountPlaylistTracks(const QString& accountId,
                                                                 const QString& playlistId) const = 0;

protected:
    AccountPlaylistTrackSource() = default;
    AccountPlaylistTrackSource(const AccountPlaylistTrackSource&) = default;
    AccountPlaylistTrackSource& operator=(const AccountPlaylistTrackSource&) = default;
};

// Applies the query's find text, sort, offset and limit to an account
// playlist held in memory, the same way the SQL branches do for the other
// collections: find matches title, artist or album without regard to case;
// "title", "artist", "duration" (longest first) and "dateadded" (newest
// first) sort stably; any other sort keeps the playlist order. Indexes count
// from offset + 1.
[[nodiscard]] TrackPage pageAccountPlaylistTracks(QList<Track> tracks, const TrackQuery& query);

} // namespace lmp::core::db
