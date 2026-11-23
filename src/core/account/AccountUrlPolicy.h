#pragma once

#include <QString>
#include <QtGlobal>

// Address rules for the account service: which origins may be configured,
// which addresses belong to them, which profile picture addresses may be
// loaded and which media addresses may be handed to the player.
//
// Inputs are the text as received (configuration, JSON). The rules read that
// text directly where it matters (length limits, the trailing slash of an
// origin, the media token in the query) and parse it with QUrl for the rest.
namespace lmp::core::account {

// Whether plain http to this machine (127.0.0.1, ::1, localhost) is accepted
// where https is otherwise required.
enum class LoopbackHttp { Refused, Allowed };

// Debug builds accept loopback http, so the app can talk to a service running
// on the developer's machine. Release builds never do.
[[nodiscard]] constexpr LoopbackHttp loopbackHttpForThisBuild()
{
#ifdef QT_NO_DEBUG
    return LoopbackHttp::Refused;
#else
    return LoopbackHttp::Allowed;
#endif
}

// An origin is "https://host[:port]" with at most one trailing "/" path:
// no user name or password, no query, no fragment and no other path. Http is
// accepted only for a loopback host and only when `loopback` allows it.
[[nodiscard]] bool isTrustedAccountOrigin(const QString& origin,
                                          LoopbackHttp loopback = loopbackHttpForThisBuild());

// The origin as written with every trailing "/" removed, or empty when it is
// not a trusted origin.
[[nodiscard]] QString normalizeTrustedAccountOrigin(const QString& origin,
                                                    LoopbackHttp loopback = loopbackHttpForThisBuild());

// Whether an absolute address has the origin's scheme, host and effective
// port (443 for https and 80 for http when none is written) and carries no
// user name or password. False when the origin itself is not trusted.
[[nodiscard]] bool isUrlFromTrustedAccountOrigin(const QString& url, const QString& origin,
                                                 LoopbackHttp loopback = loopbackHttpForThisBuild());

// Profile pictures may come from any https host, but the address must be at
// most 2048 characters, have a host, carry no user name, password or
// fragment, and have no query field whose name (percent-decoded, A to Z
// folded) is a credential or signed-delivery name (net::QueryFieldKind).
[[nodiscard]] bool isSafeAccountProfileUrl(const QString& url,
                                           LoopbackHttp loopback = loopbackHttpForThisBuild());

// What a media address of the account service is for. Streams live under
// "/v1/stream/<id>" and pictures at "/v1/artwork".
enum class AccountMediaScope { Stream, Artwork };

// Signed media addresses carry media_token=<scope>.<expiry ms>.<signature>,
// or the service's newer form v2.<scope>.<expiry ms>.<signature>.
// True when the address belongs to the scope's path but its token is
// missing, malformed, for another scope, or expires within 30 seconds of
// `nowMsSinceEpoch`; such an address must be fetched again through an
// authenticated request. An empty address, or one without the scope's path,
// does not use these rules and returns false.
[[nodiscard]] bool mediaTokenNeedsRefresh(const QString& mediaUrl, AccountMediaScope scope,
                                          qint64 nowMsSinceEpoch);

// The whole media boundary: a safe ephemeral address (net::isSafeRemoteUrl),
// from the configured media origin, on the scope's exact path, with a fresh
// token for that scope. False when `mediaOrigin` is empty or not trusted.
[[nodiscard]] bool isTrustedAccountMediaUrl(const QString& mediaUrl, const QString& mediaOrigin,
                                            AccountMediaScope scope, qint64 nowMsSinceEpoch,
                                            LoopbackHttp loopback = loopbackHttpForThisBuild());

} // namespace lmp::core::account
