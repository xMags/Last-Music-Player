#pragma once

#include <QString>
#include <QStringView>

namespace lmp::core::net {

enum class RemoteUrlUse {
    // Played now and thrown away: may carry a short-lived signature.
    EphemeralMedia,
    // Saved to disk or synced: must carry no credential and no signature.
    Durable,
};

// Whether a remote address may be used or kept: HTTPS, or HTTP to this machine
// only; no user name or password; no credential-like query or fragment field
// (access_token, api_key, apikey, authorization, bearer, password,
// refresh_token, session, jwt). A durable address must also carry no signed
// delivery field (token, media_token, signature, sig, credential, expires,
// auth, key, or any name starting x-amz- or x-goog-). Field names are compared
// whole and without regard to case.
[[nodiscard]] bool isSafeRemoteUrl(const QString& url, RemoteUrlUse use);

// What a query or fragment field name says about its value.
enum class QueryFieldKind {
    Ordinary,
    // access_token, api_key, apikey, authorization, bearer, password,
    // refresh_token, session, jwt.
    Credential,
    // token, media_token, signature, sig, credential, expires, auth, key, or
    // any name starting x-amz- or x-goog-.
    SignedDelivery,
};

// Classifies a field name that is already lower-cased; names are compared
// whole, prefixes only where listed. This is the one list of sensitive field
// names; every address check uses it.
[[nodiscard]] QueryFieldKind classifyQueryFieldName(QStringView loweredName);

// Whether a host names this machine: 127.0.0.1, ::1 or localhost, with A to Z
// compared without regard to case.
[[nodiscard]] bool isLoopbackHost(QStringView host);

// Starts with "http://" or "https://" (case-sensitive, as before).
[[nodiscard]] bool isHttpUrl(QStringView text);

} // namespace lmp::core::net
