#pragma once

#include "core/account/AccountMusicContract.h"
#include "core/account/AccountSessionProtocol.h"
#include "core/account/AccountSyncTransport.h"
#include "core/account/LibraryArtistContract.h"
#include "core/account/AccountUrlPolicy.h"
#include "core/account/Pkce.h"
#include "core/async/Result.h"
#include "core/config/PrivateConfig.h"
#include "core/net/HttpMessage.h"
#include "core/net/IHttpTransport.h"

#include <QByteArray>
#include <QFuture>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>

#include <chrono>

namespace lmp::core::account {

// One request to the account service.
struct AccountCall {
    net::HttpMethod method = net::HttpMethod::Get;
    // Must start with "/v1/"; it is appended to the configured origin as is,
    // so the caller percent-encodes every value it puts in the query.
    QString pathAndQuery;
    // Sent as "Authorization: Bearer <session>" when not empty.
    QString bearerSession;
    // Sent as UTF-8 JSON when not empty.
    QByteArray jsonBody;
    std::chrono::milliseconds timeout{std::chrono::seconds(20)};
    qint64 maxResponseBytes = 32 * 1024 * 1024;
    // Finish on the status and headers; the body is never read.
    bool headersOnly = false;
};

// The account service's HTTP API.
//
// Every request goes to the configured API origin only (never another host),
// carries "X-Requested-With" and "Accept: application/json", and is never
// redirected: a 3xx answer is a failure, so a bearer session cannot follow a
// redirect to another host. Failures are the transport's (net::HttpClient):
// the HTTP status and any Retry-After delay are on the Failure. A client
// without a trusted origin refuses every call with NotAvailable, and a path
// outside "/v1/" with Unknown, without sending anything.
//
// Holds no state between calls. The transport must outlive the client;
// futures finish on the transport's thread.
//
// Music calls send the caller's JSON as given (build it with
// AccountMusicContract and remote::track_json) and put every id they place
// in a path or query through percent-encoding.
class AccountClient final : public AccountSyncTransport {
public:
    // The largest artwork the relay may return (12 MiB).
    static constexpr qint64 MaxArtworkBytes = 12LL * 1024 * 1024;
    // How many plays the library answer carries, so History and Most Played
    // are not held to the service's smaller default.
    static constexpr int LibraryHistoryLimit = 300;

    AccountClient(net::IHttpTransport& transport, const PrivateConfig& config,
                  LoopbackHttp loopback = loopbackHttpForThisBuild());

    // True when the API origin is a trusted origin.
    [[nodiscard]] bool isConfigured() const { return !m_baseOrigin.isEmpty(); }
    // The API origin without a trailing slash, or empty.
    [[nodiscard]] QString baseOrigin() const { return m_baseOrigin; }

    // "<frontend origin>/account", the page where the user manages the
    // account; empty when the build has no trusted frontend origin.
    [[nodiscard]] QUrl managementUrl() const;

    // The browser address that starts a sign-in (buildAccountAuthorizeUrl),
    // or empty when the client is not configured.
    [[nodiscard]] QString authorizeUrl(const QString& redirectUri, const PkceTransaction& transaction) const;

    // The request core every call goes through.
    [[nodiscard]] QFuture<Result<net::HttpResponse>> send(const AccountCall& call);

    // POST /v1/sso/token: trades the code from the browser for a bearer
    // session. InvalidResponse when the answer holds no valid session.
    [[nodiscard]] QFuture<Result<QString>> exchangeCode(const QString& code, const QString& redirectUri,
                                                        const QString& verifier);

    // GET /v1/auth/me. InvalidResponse when the answer has no valid owner id.
    [[nodiscard]] QFuture<Result<AccountProfile>> fetchProfile(const QString& bearerSession);

    // POST /v1/auth/logout: ends the session on the service.
    [[nodiscard]] QFuture<Result<void>> logout(const QString& bearerSession);

    // Library sync (AccountSyncTransport). The library is asked for with
    // "?historyLimit=" LibraryHistoryLimit. Liking needs a valid track object
    // and unliking a remote id; otherwise nothing is sent (Unknown).
    [[nodiscard]] QFuture<Result<ParsedAccountLibrary>> fetchLibrary(const QString& bearerSession) override;
    [[nodiscard]] QFuture<Result<void>> setLiked(const QString& bearerSession, const QByteArray& trackJson,
                                                 bool liked, const QString& remoteId) override;
    [[nodiscard]] QFuture<Result<QStringList>> postHistoryBatch(const QString& bearerSession,
                                                                const QByteArray& batchJson,
                                                                const QStringList& submittedEventIds) override;
    [[nodiscard]] QFuture<Result<PortablePreferences>> fetchPreferences(const QString& bearerSession) override;
    [[nodiscard]] QFuture<Result<PortablePreferences>> putPreferences(const QString& bearerSession,
                                                                      const QByteArray& preferencesJson) override;

    // POST /v1/music/playlists. The new playlist; InvalidResponse when the
    // answer names none.
    [[nodiscard]] QFuture<Result<AccountPlaylist>> createPlaylist(const QString& bearerSession,
                                                                  const QByteArray& playlistJson);
    // PATCH /v1/music/playlists/<id>: a rename ({"name"}) or a replacement
    // (account::serializePlaylistReplacement).
    [[nodiscard]] QFuture<Result<void>> updatePlaylist(const QString& bearerSession, const QString& playlistId,
                                                       const QByteArray& playlistJson);
    // DELETE /v1/music/playlists/<id>.
    [[nodiscard]] QFuture<Result<void>> deletePlaylist(const QString& bearerSession, const QString& playlistId);
    // POST /v1/music/playlists/<id>/tracks {"track": ...}.
    [[nodiscard]] QFuture<Result<void>> addPlaylistTrack(const QString& bearerSession, const QString& playlistId,
                                                         const QByteArray& trackJson);
    // DELETE /v1/music/playlists/<id>/tracks/<track id>: only that song goes,
    // so songs another device added meanwhile stay.
    [[nodiscard]] QFuture<Result<void>> removePlaylistTrack(const QString& bearerSession, const QString& playlistId,
                                                            const QString& trackId);

    // POST /v1/music/resolve {"url"[, "quality": "low"]}: the answer as sent,
    // for the caller to read.
    [[nodiscard]] QFuture<Result<QByteArray>> resolve(const QString& bearerSession, const QString& sourceUrl,
                                                      bool lowQuality = false);
    // GET /v1/music/related?sourceUrl=<address>: the answer as sent.
    [[nodiscard]] QFuture<Result<QByteArray>> related(const QString& bearerSession, const QString& sourceUrl);
    // POST /v1/music/import {"url"}: the answer as sent.
    [[nodiscard]] QFuture<Result<QByteArray>> importCollection(const QString& bearerSession,
                                                               const QString& sourceUrl);
    // GET /v1/music/artwork?url=<address>: the image bytes, at most
    // MaxArtworkBytes (InvalidResponse beyond).
    [[nodiscard]] QFuture<Result<QByteArray>> artwork(const QString& bearerSession, const QString& artworkUrl);
    // GET /v1/music/library/artists?... (libraryArtistListPath), 5 s. The
    // page as read (`valid` false when it was not accepted); a request the
    // path refuses sends nothing (Unknown).
    [[nodiscard]] QFuture<Result<LibraryArtistPage>> libraryArtists(const QString& bearerSession,
                                                                     const LibraryArtistListRequest& request);
    // GET /v1/music/library/artists/<public id>/tracks, 5 s. InvalidResponse
    // when the answer is not a song list.
    [[nodiscard]] QFuture<Result<QList<AccountLibraryTrack>>> libraryArtistTracks(const QString& bearerSession,
                                                                                const QString& publicArtistId);
    // PUT (follow) or DELETE (unfollow) /v1/music/library/artists/<public
    // id>/follow, 5 s.
    [[nodiscard]] QFuture<Result<void>> setArtistFollowed(const QString& bearerSession, const QString& publicArtistId,
                                                          bool followed);

    // GET <cover link path> (net::accountCoverLinkPath): the image bytes, at
    // most MaxArtworkBytes. Anything that is not a cover link sends nothing
    // (Unknown).
    [[nodiscard]] QFuture<Result<QByteArray>> coverArt(const QString& bearerSession, const QString& coverLinkPath);
    // GET /v1/music/stream?url=<address>, headers only: the status of a
    // stream that answers 2xx; the audio is never downloaded.
    [[nodiscard]] QFuture<Result<int>> probeStream(const QString& bearerSession, const QString& streamUrl);

private:
    net::IHttpTransport& m_transport;
    QString m_baseOrigin;
    QString m_frontendOrigin;
    LoopbackHttp m_loopback;
};

} // namespace lmp::core::account
