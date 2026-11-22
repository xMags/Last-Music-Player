#pragma once

#include <QByteArrayView>
#include <QDateTime>

#include <optional>

namespace lmp::core::net {

// The longest wait a server can ask for: one day. Longer requests are cut to
// it, so a broken header cannot stop retries for good.
inline constexpr int MaxRetryAfterSeconds = 86400;

// The delay a Retry-After header asks for, in whole seconds from `nowUtc`:
// either delay-seconds ("120") or an HTTP date in the standard form
// ("Sun, 06 Nov 1994 08:49:37 GMT"). The result is clamped to 0 to one day,
// so a date in the past is 0. Nothing for an absent or unreadable header.
[[nodiscard]] std::optional<int> parseRetryAfterSeconds(QByteArrayView value, const QDateTime& nowUtc);

} // namespace lmp::core::net
