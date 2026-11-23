#pragma once

#include "core/mode/SourceMode.h"
#include "core/model/Track.h"

#include <QString>
#include <QtGlobal>

// The cache and download keys built from a song, exactly as earlier versions
// built them, so entries they kept are found again. Pure functions, no I/O.
//
// The other song identities live beside the code that first needed them:
// catalogSourceKey (core/model/CatalogSourceKey.h) is the Tracks SourceKey;
// homeHistoryKey, the queue de-duplication key, canonicalQueueText and
// legacyCatalogSourceKey are in core/playback/LastTrackIdentity.h.
namespace lmp::core::track_keys {

// Everything a remote cache key depends on: the mode, its generation, the
// account generation (Account mode only) and the cache partition of the API
// key server (ApiKey mode only).
struct RemoteScopeKeyParts {
    ::lmp::core::mode::SourceMode mode = ::lmp::core::mode::SourceMode::LocalOnly;
    quint64 generation = 0;
    quint64 accountGeneration = 0;
    QString cachePartition;
};

// "<mode name>[:<partition> in ApiKey]:<generation>[:<account generation> in
// Account]", for example "ApiKey:p1:3" or "Account:2:7".
[[nodiscard]] QString remoteScopeCacheKey(const RemoteScopeKeyParts& scope);

// The key a stream address is cached under in ApiKey mode: the scope key, a
// line break, then the source address. Empty outside ApiKey mode or when the
// song has no source address.
[[nodiscard]] QString streamCacheKey(const RemoteScopeKeyParts& scope, const Track& track);

// The key a download is kept under. A song from this PC uses its
// legacyCatalogSourceKey as is. A remote song uses "download|" + the scope
// key + a line break + that key, and has none in LocalOnly mode. Empty when
// the song has no key at all.
[[nodiscard]] QString downloadStableKey(const RemoteScopeKeyParts& scope, const Track& track);

} // namespace lmp::core::track_keys
