#pragma once

#include "core/async/Result.h"
#include "core/net/HttpMessage.h"
#include "core/net/IHttpTransport.h"

#include <QByteArray>
#include <QFuture>
#include <QStringList>

#include "core/account/LibraryArtistContract.h"
#include <QLatin1StringView>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <chrono>

namespace lmp::core::provider {

// The address used when none is saved: a provider on this machine.
inline constexpr QLatin1StringView DefaultProviderBaseUrl{"http://127.0.0.1:4527"};

// Capabilities a provider can list in its status answer (docs/PROVIDER_API.md).
inline constexpr QLatin1StringView ArtistDetailCapability{"artist-detail-v1"};
inline constexpr QLatin1StringView AlbumDetailCapability{"album-detail-v1"};

// The saved base address in the form requests are built from: the default
// when nothing is saved, and without trailing "/" or "\" characters. Nothing
// else is changed; the address is not checked here.
[[nodiscard]] QString normalizeProviderBaseUrl(const QString& savedBase);

// Whether a normalised base address may be sent requests: a valid absolute
// http or https address with a host, and no user name, password, query or
// fragment (request paths are appended to it as text). Any host is allowed,
// over http as well: providers are commonly run on the local network.
[[nodiscard]] bool isUsableProviderBaseUrl(const QString& normalizedBase);

// The capability names in a status answer: the strings of the top-level
// "capabilities" array, then those of each object in the "providers" array,
// each name once, in that order. As the provider contract has always been
// read: a body over 1,000,000 bytes, a body that is not a JSON object, or a
// top-level "capabilities" that is not an array of at most 128 entries means
// no capabilities at all; a "providers" that is not an array of at most 64
// entries adds nothing; an entry that is not an object, or whose own
// "capabilities" is not an array of at most 128 entries, is skipped; other
// values in the arrays are ignored. Names are compared exactly.
[[nodiscard]] QStringList parseProviderCapabilities(const QByteArray& body);

// Where and as whom a request is sent: the user's base address (normalised
// by the client) and the API key the provider issued.
struct ProviderConnection {
    QString baseUrl;
    QString apiKey;
};

// One request to the provider.
struct ProviderCall {
    net::HttpMethod method = net::HttpMethod::Get;
    // Must start with "/v1/"; appended to the base address as is, so the
    // caller percent-encodes every value it puts in the query.
    QString pathAndQuery;
    // Sent as UTF-8 JSON when not empty.
    QByteArray jsonBody;
    std::chrono::milliseconds timeout{std::chrono::seconds(15)};
    qint64 maxResponseBytes = 32 * 1024 * 1024;
};

// What a lyrics request names (GET /v1/lyrics). Empty or zero fields are
// left out of the query.
struct ProviderLyricsQuery {
    QString artist;
    QString title;
    QString album;
    qint64 durationMs = 0;
    QString sourceUrl;
};

// What the status call reports about a reachable provider.
struct ProviderStatus {
    QStringList capabilities;
};

// The HTTP API of the user's provider (API key mode).
//
// Every request carries "Authorization: Bearer <key>" and
// "Accept: application/json" and is never redirected: a 3xx answer is a
// failure, so the key cannot follow a redirect to another host. Failures are
// the transport's (net::HttpClient), with the HTTP status on the Failure. A
// base address that is not usable or an empty key is refused with
// NotAvailable, and a key with control characters or a path outside "/v1/"
// with Unknown, without sending anything. Failure details never quote the
// key or the address.
//
// Holds no state between calls. The transport must outlive the client;
// futures finish on the transport's thread.
class ProviderClient {
public:
    explicit ProviderClient(net::IHttpTransport& transport);

    // The request core every call goes through.
    [[nodiscard]] QFuture<Result<net::HttpResponse>> send(const ProviderConnection& connection,
                                                          const ProviderCall& call);

    // GET /v1/providers: the provider is healthy when it answers with any
    // 2xx status; the body only adds the capability list.
    [[nodiscard]] QFuture<Result<ProviderStatus>> fetchStatus(const ProviderConnection& connection);

    // The calls below answer with the body of a 2xx answer, unread; their
    // readers live with the features that use them.

    // GET /v1/search?q=: songs ({"results": [...]}).
    [[nodiscard]] QFuture<Result<QByteArray>> search(const ProviderConnection& connection, const QString& query);
    // GET /v1/artists/detail and /v1/albums/detail, by one identity (a
    // catalogue id, else a link, else a name). Asked only when the
    // provider's capability list names the call (ArtistDetailCapability,
    // AlbumDetailCapability); refused with NotAvailable otherwise, and with
    // Unknown for an identity that cannot be sent. Limited to 5 s.
    [[nodiscard]] QFuture<Result<QByteArray>> artistDetail(const ProviderConnection& connection,
                                                           const QStringList& capabilities,
                                                           const account::LibraryArtistIdentity& identity);
    [[nodiscard]] QFuture<Result<QByteArray>> albumDetail(const ProviderConnection& connection,
                                                          const QStringList& capabilities,
                                                          const account::LibraryArtistIdentity& identity);
    // GET /v1/related?sourceUrl=: songs that follow `sourceUrl` (autoplay).
    [[nodiscard]] QFuture<Result<QByteArray>> related(const ProviderConnection& connection, const QString& sourceUrl);
    // POST /v1/resolve {"url"}: a song link to a playable song.
    [[nodiscard]] QFuture<Result<QByteArray>> resolve(const ProviderConnection& connection, const QString& url);
    // GET /v1/lyrics.
    [[nodiscard]] QFuture<Result<QByteArray>> lyrics(const ProviderConnection& connection,
                                                     const ProviderLyricsQuery& query);
    // POST /v1/discord/artwork-url {"title", "artist"}: a picture address
    // Discord can show ({"url"}).
    [[nodiscard]] QFuture<Result<QByteArray>> presenceArtwork(const ProviderConnection& connection, const QString& title,
                                                              const QString& artist);
    // POST /v1/import-album {"url"}: an album or playlist link to its songs.
    [[nodiscard]] QFuture<Result<QByteArray>> importAlbum(const ProviderConnection& connection, const QString& url);

    // The limit for the detail calls.
    static constexpr std::chrono::milliseconds DetailTimeout{std::chrono::seconds(5)};

private:
    // The body of a 2xx answer to `call`.
    [[nodiscard]] QFuture<Result<QByteArray>> body(const ProviderConnection& connection, const ProviderCall& call);
    [[nodiscard]] QFuture<Result<QByteArray>> detail(const ProviderConnection& connection,
                                                     const QStringList& capabilities, QLatin1StringView capability,
                                                     const QString& path,
                                                     const account::LibraryArtistIdentity& identity);

    net::IHttpTransport& m_transport;
};

} // namespace lmp::core::provider
