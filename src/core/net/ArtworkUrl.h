#pragma once

#include <QString>

namespace lmp::core::net {

// Asks for artwork large enough for high-DPI display, keeping the address
// shape and any query fields: surrounding white space is trimmed, a size
// segment of the form "=w<n>-h<n>..." (or its percent-encoded "%3Dw" form)
// becomes "=w1200-h1200-l90-rj", and a "<w>x<h>bb." or "<w>x<h>cc." segment of
// a .mzstatic.com address becomes 1200x1200. Anything else is unchanged.
[[nodiscard]] QString normalizeArtworkUrlForDisplay(const QString& artworkUrl);

// An artwork address that is safe to keep and sync, or empty. A server's
// signed artwork relay (a path ending in /v1/artwork) is reduced to the image
// address in its "url" field; every result must pass the durable address
// check of RemoteUrlPolicy.
[[nodiscard]] QString durableArtworkUrl(const QString& artworkUrl);

// The path of an account cover link, "/v1/music/art/<id>" where the id is 22
// characters of A-Z, a-z, 0-9, "_" and "-", given alone or after an http(s)
// origin (surrounding white space ignored); empty for anything else. Such a
// link is opaque and permanent, and loads from the account service's API with
// the session.
[[nodiscard]] QString accountCoverLinkPath(const QString& url);

// A cover the account service sends, as it is kept and synced: its cover
// link path when it is one, else durableArtworkUrl.
[[nodiscard]] QString durableAccountArtworkUrl(const QString& artworkUrl);

} // namespace lmp::core::net
