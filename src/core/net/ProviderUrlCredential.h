#pragma once

#include <QLatin1StringView>
#include <QString>

#include <initializer_list>

namespace lmp::core::net {

// Removes the obsolete access_token query field from a server stream or
// artwork address (an http(s) address whose path holds "/v1/stream/" or
// "/v1/artwork", compared without regard to case). Every other address, and
// every other field, is returned unchanged. Older databases stored addresses
// with that field, so rows are cleaned as they are written and read.
[[nodiscard]] QString removeLegacyProviderUrlCredential(const QString& url);

// Drops every query field whose name is one of `loweredNames` (compared
// without regard to case) and every empty field, keeping the order of the
// rest and any fragment.
[[nodiscard]] QString stripQueryFields(const QString& url, std::initializer_list<QLatin1StringView> loweredNames);

} // namespace lmp::core::net
