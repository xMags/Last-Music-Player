#pragma once

#include "core/account/AccountUrlPolicy.h"
#include "core/account/Pkce.h"

#include <QByteArray>
#include <QString>

// The parts of the sign-in protocol that are plain data: the browser address
// that starts a sign-in, and reading the token and profile answers. Nothing
// here talks to the network.
namespace lmp::core::account {

// The signed-in user as the account service describes them. Every field has
// passed the checks of parseAccountProfile.
struct AccountProfile {
    // The owner id; never empty for a profile that was accepted.
    QString id;
    QString displayName;
    QString username;
    // An https address or an inline image; empty when there is none.
    QString avatarUrl;
    QString planLabel;

    friend bool operator==(const AccountProfile&, const AccountProfile&) = default;
};

// "<origin>/v1/sso/authorize?client_id=...&redirect_uri=...&state=...
// &code_challenge=...&code_challenge_method=S256", with the redirect address,
// state and challenge percent-encoded. The verifier never appears. Empty when
// `apiOrigin` is empty or any of the three values is.
[[nodiscard]] QString buildAccountAuthorizeUrl(const QString& apiOrigin, const QString& redirectUri,
                                               const PkceTransaction& transaction);

// The bearer session in a token answer: a JSON object whose "access_token"
// is 1 to 16384 characters of A-Z a-z 0-9 - . _ ~ + / followed by optional
// '=' padding, and whose "token_type", when present, is "Bearer" (A to Z
// compared without case). Empty for anything else.
[[nodiscard]] QString parseAccountBearerSession(const QByteArray& payload);

// The profile in a "/v1/auth/me" answer, read from its "user" object or, when
// there is none, from the top level. The id (at most 256 characters, no
// whitespace or control characters) is required; without it the result has
// an empty id. Optional texts that are too long (display name 256, falling
// back to "fullName"; username and plan 128) or carry control characters are
// left empty, and so is an avatar that is neither a safe profile address nor
// a safe inline image.
[[nodiscard]] AccountProfile parseAccountProfile(const QByteArray& payload,
                                                 LoopbackHttp loopback = loopbackHttpForThisBuild());

} // namespace lmp::core::account
