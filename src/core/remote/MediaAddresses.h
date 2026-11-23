#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>
#include <QtGlobal>

// The stream and artwork address rules of both server modes, as earlier
// versions applied them. Pure functions, no I/O.
namespace lmp::core::remote {

// Whether `url`, without its fragment, starts with the base address (as
// provider::normalizeProviderBaseUrl gives it) followed by `path`, compared
// without regard to case.
[[nodiscard]] bool isCurrentProviderPath(const QString& url, const QString& baseUrl, QStringView path);

// A stream address under "<media origin>/v1/stream/", without its fragment
// and without the obsolete access_token field; empty for any other address.
[[nodiscard]] QString accountStreamUrl(const QString& filePath, const QString& mediaOrigin);

// An artwork address: anything that is not http(s), or has no "/v1/artwork"
// in it, is returned as is; a relay address under "<media origin>
// /v1/artwork" without its fragment and obsolete credential; empty for a
// relay address on another server.
[[nodiscard]] QString accountArtworkUrl(const QString& artworkUrl, const QString& mediaOrigin);

// The song's saved address when it is a stream address of the media origin
// that may be played as it is (account::isTrustedAccountMediaUrl, for the
// stream scope, at `nowMsSinceEpoch`); empty otherwise, and then the song
// must be resolved again.
[[nodiscard]] QString trustedAccountStreamUrl(const QString& filePath, const QString& mediaOrigin,
                                              qint64 nowMsSinceEpoch);

// What a resolve answer gives: {"result": {"streamUrl", "artworkUrl"}}.
// Both empty when the body is not that shape; fields of the wrong type are
// empty.
struct ResolvedStream {
    QString streamUrl;
    QString artworkUrl;
};
[[nodiscard]] ResolvedStream parseResolvedStream(const QByteArray& body);

// API key mode: the address a song streams from on the user's provider.
// A saved provider stream address ("/v1/stream/" anywhere in an http(s)
// address) is moved to `baseUrl`, keeping its path and query, without the
// internal "#lmp=" fragment and old media_token or access_token fields;
// else a web source link gets "<base>/v1/stream/<kind>:<hash>?url=<link>",
// the kind being "direct" for an empty or "direct" source tag and "remote"
// for any other, the hash the earlier versions' 64-bit FNV-1a of the link's
// UTF-16 text (stableStreamId); else an old stream address in `filePath` is cleaned as above
// without moving it. The API key goes in an access_token field when there
// is one. Empty when nothing applies (a song on this PC, another scheme).
[[nodiscard]] QString providerStreamUrl(const QString& filePath, const QString& sourceUrl, const QString& sourceTag,
                                        const QString& baseUrl, const QString& apiKey);

// API key mode: a provider artwork address ("/v1/artwork" in an http(s)
// address) moved to `baseUrl` with the API key as above; any other address
// is returned as is.
[[nodiscard]] QString providerArtworkUrl(const QString& artworkUrl, const QString& baseUrl, const QString& apiKey);

// The stream id the earlier versions gave a source link: "<kind>:" and 16
// lower-case hex digits.
[[nodiscard]] QString stableStreamId(const QString& kind, const QString& sourceUrl);

} // namespace lmp::core::remote
