#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>
#include <QtGlobal>

// The stream and artwork address rules Account mode needs, as earlier
// versions applied them. Pure functions, no I/O. (The API key server's
// builders follow with API key mode.)
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

} // namespace lmp::core::remote
