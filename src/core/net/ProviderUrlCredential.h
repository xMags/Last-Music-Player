#pragma once

#include <QString>

namespace lmp::core::net {

// Removes the obsolete access_token query field from a server stream or
// artwork address (an http(s) address whose path holds "/v1/stream/" or
// "/v1/artwork", compared without regard to case). Every other address, and
// every other field, is returned unchanged. Older databases stored addresses
// with that field, so rows are cleaned as they are written and read.
[[nodiscard]] QString removeLegacyProviderUrlCredential(const QString& url);

} // namespace lmp::core::net
