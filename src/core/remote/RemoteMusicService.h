#pragma once

#include "core/account/LibraryArtistContract.h"
#include "core/async/Result.h"
#include "core/lyrics/LyricsService.h"
#include "core/model/Track.h"

#include <QByteArray>
#include <QFuture>
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

private:
    // Which way a call goes, decided once when it starts.
    using AccountCall = std::function<QFuture<Result<QByteArray>>(const QString& bearer)>;
    using ProviderCall = std::function<QFuture<Result<QByteArray>>()>;

    [[nodiscard]] QFuture<Result<QByteArray>> route(bool allowed, const AccountCall& account,
                                                    const ProviderCall& provider);

    Deps m_deps;
};

} // namespace lmp::core::remote
