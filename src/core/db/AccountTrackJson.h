#pragma once

#include "core/model/Track.h"

#include <QString>

#include <optional>

// The song a queued account change carries (the TrackJson column of
// PlaybackEvents and PendingLikes). The keys and value types are those
// earlier versions wrote, so rows queued by them are read back unchanged.
namespace lmp::core::db::account_track_json {

// A compact JSON object with the string fields remoteId, id (both the remote
// id), sourceKind, provider (the source tag, else the origin unless that is
// "account"), sourceUrl and artworkUrl (both cleaned the way stored account
// addresses are), title, artist, album, genre, catalogId, albumCatalogId,
// artistCatalogId, dateAdded and duration (the length as shown); the numbers
// durationSeconds, durationSec and dateAddedSortKey; and the bool isLiked.
[[nodiscard]] QString serialize(const Track& track);

// Empty when the text is not a JSON object. Missing (or mistyped) fields take
// their defaults: the id comes from "id", else "remoteId"; the length from
// "durationSec", else "durationSeconds"; sourceKind defaults to "remote". The
// song is always an account song: origin "account", label "Account", no file.
[[nodiscard]] std::optional<Track> deserialize(const QString& payload);

} // namespace lmp::core::db::account_track_json
