#pragma once

#include "core/async/Result.h"
#include "core/net/HttpMessage.h"
#include "core/net/IHttpTransport.h"

#include <QByteArray>
#include <QFuture>
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

private:
    net::IHttpTransport& m_transport;
};

} // namespace lmp::core::provider
