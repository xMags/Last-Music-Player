#pragma once

#include "core/account/LibraryArtistContract.h"
#include "core/async/Result.h"
#include "core/lyrics/LyricsService.h"
#include "core/model/Track.h"
#include "core/model/TrackKeys.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QString>

#include <functional>

namespace lmp::core {
namespace account {
class AccountClient;
class AccountSessionService;
} // namespace account
namespace catalog {
class CatalogClient;
} // namespace catalog
namespace mode {
class ModeService;
} // namespace mode
namespace provider {
class ProviderClient;
} // namespace provider
} // namespace lmp::core

namespace lmp::core::remote {

// The one way to the remote music source: search, related songs, resolving a
// link, lyrics, artist and album pages, and importing a link. Each call goes
// to the account service in Account mode and to the user's provider in API
// key mode, with the same answer shape the callers read; Local mode, and a
// call the current Capabilities do not allow, are refused with NotAvailable
// before anything is sent.
//
// Every call is stamped with the mode scope it started in (and, in Account
// mode, the account session): an answer that arrives after the mode, the
// provider, the owner or the session changed is turned into Cancelled, and a
// 401 from the account service signs that session out (when it is still the
// current one).
//
// GUI thread only; the dependencies must outlive the service.
class RemoteMusicService {
public:
    struct Deps {
        mode::ModeService& mode;
        account::AccountSessionService& session;
        account::AccountClient& account;
        catalog::CatalogClient& catalog;
        provider::ProviderClient& provider;
    };

    explicit RemoteMusicService(Deps deps);

    // Songs for a query: {"results": [...]} or the account's own shape.
    [[nodiscard]] QFuture<Result<QByteArray>> search(const QString& query);
    // Songs that follow `seed` (autoplay, mixes), by its link (its server
    // reference in Account mode when it has one).
    [[nodiscard]] QFuture<Result<QByteArray>> related(const Track& seed);
    // A song link to a playable song; `lowQuality` asks the account service
    // for the smaller stream.
    [[nodiscard]] QFuture<Result<QByteArray>> resolve(const QString& sourceUrl, bool lowQuality = false);
    [[nodiscard]] QFuture<Result<QByteArray>> lyrics(const lyrics::LyricsQuery& query);
    [[nodiscard]] QFuture<Result<QByteArray>> artistDetail(const account::LibraryArtistIdentity& identity);
    [[nodiscard]] QFuture<Result<QByteArray>> albumDetail(const account::LibraryArtistIdentity& identity,
                                                          const QString& artist = {});
    // An album or playlist link to its songs.
    [[nodiscard]] QFuture<Result<QByteArray>> importCollection(const QString& url);

    // A key naming the current scope ("<mode>:<generation>"), for caches
    // that must not mix answers from different sources; empty in Local mode.
    [[nodiscard]] QString scopeKey() const;
    // The parts of the current scope that song keys are built from
    // (track_keys): the mode, its generation, the account session's
    // generation, and in API key mode the provider's cache partition.
    [[nodiscard]] track_keys::RemoteScopeKeyParts scopeKeyParts() const;
    // In LocalOnly mode: the scopes of the signed-in account and the saved
    // provider, whose downloads still play (track_keys::localOnlyDownloadScopes).
    [[nodiscard]] QList<track_keys::RemoteScopeKeyParts> localOnlyDownloadScopes() const;

private:
    // Which way a call goes, decided once when it starts.
    using AccountCall = std::function<QFuture<Result<QByteArray>>(const QString& bearer)>;
    using ProviderCall = std::function<QFuture<Result<QByteArray>>()>;

    [[nodiscard]] QFuture<Result<QByteArray>> route(bool allowed, const AccountCall& account,
                                                    const ProviderCall& provider);

    Deps m_deps;
};

// The earlier versions' cache partition of an API key provider: SHA-256 of
// the normalised base address, a line break and the key, taken over their
// UTF-16 text (little-endian), in lower-case hex. Empty when either is empty.
[[nodiscard]] QString apiKeyCachePartition(const QString& baseUrl, const QString& apiKey);

} // namespace lmp::core::remote
