#pragma once

#include "core/async/Result.h"

#include <QString>

// Proof key for code exchange (RFC 7636, method S256) for the browser
// sign-in.
namespace lmp::core::account {

struct PkceTransaction {
    // base64url of 32 random bytes: 43 characters.
    QString verifier;
    // base64url of the SHA-256 of the verifier: 43 characters.
    QString challenge;
    // base64url of 24 random bytes: 32 characters. The callback must return
    // it unchanged.
    QString state;
};

// base64url (no padding) of the SHA-256 of the verifier's ASCII bytes. Fails
// when the verifier is shorter than 43 or longer than 128 characters, or has
// a character outside ASCII.
[[nodiscard]] Result<QString> createPkceChallenge(const QString& verifier);

// A fresh verifier, its challenge and a state value, from the operating
// system's cryptographic random source.
[[nodiscard]] PkceTransaction createPkceTransaction();

} // namespace lmp::core::account
