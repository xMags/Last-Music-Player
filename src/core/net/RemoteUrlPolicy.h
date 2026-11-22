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

// Starts with "http://" or "https://" (case-sensitive, as before).
[[nodiscard]] bool isHttpUrl(QStringView text);

} // namespace lmp::core::net
