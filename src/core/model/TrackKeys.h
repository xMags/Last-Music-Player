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
    // The signed-in account in Account mode (empty while none is known).
    QString ownerId = {};
};

// "<mode name>[:<partition> in ApiKey]:<generation>[:<account generation> in
// Account]", for example "ApiKey:p1:3" or "Account:2:7".
[[nodiscard]] QString remoteScopeCacheKey(const RemoteScopeKeyParts& scope);

// The key a stream address is cached under in ApiKey mode: the scope key, a
// line break, then the source address. Empty outside ApiKey mode or when the
// song has no source address.
[[nodiscard]] QString streamCacheKey(const RemoteScopeKeyParts& scope, const Track& track);

// The lasting part of a scope, for keys kept between runs: the mode name,
// then ":<partition>" in ApiKey mode or ":<owner id>" in Account mode. No
// run counters (they start over every launch and move whenever the session
// is checked). Empty in Account mode while no account is known.
[[nodiscard]] QString durableScopeKey(const RemoteScopeKeyParts& scope);

// The key a download is kept under. A song from this PC uses its
// legacyCatalogSourceKey as is. A remote song uses "download|" + the
// durable scope key + a line break + that key, and has none in LocalOnly
// mode or while no account is known. Empty when the song has no key at all.
//
// Earlier versions wrote the run counters into the scope ("Account:3:7");
// those keys stay in Downloads.json as they were and are compared through
// downloadMatchKey and downloadInScope.
[[nodiscard]] QString downloadStableKey(const RemoteScopeKeyParts& scope, const Track& track);

// What download keys are compared by: "download|<mode>[:<partition> in
// ApiKey]" + a line break + the song's key, so an earlier key and today's
// key of the same song match. A key of a song on this PC is itself.
[[nodiscard]] QString downloadMatchKey(const QString& stableKey);

// Whether a stored download key may run in the scope `scopePrefix` names
// ("download|" + durableScopeKey + a line break): a current key by its
// prefix (so another account's is refused); an earlier one, which says
// nothing about the account, by its mode (and partition in ApiKey mode).
[[nodiscard]] bool downloadInScope(const QString& stableKey, const QString& scopePrefix);

} // namespace lmp::core::track_keys
