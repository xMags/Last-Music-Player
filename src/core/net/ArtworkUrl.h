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

} // namespace lmp::core::net
