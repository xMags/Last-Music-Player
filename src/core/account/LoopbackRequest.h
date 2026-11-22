#pragma once

#include <QByteArrayView>
#include <QString>

namespace lmp::core::account {

// The largest request the sign-in callback reads from one connection.
inline constexpr qsizetype kMaxLoopbackRequestBytes = 8192;

// The longest authorization code accepted, in bytes after decoding.
inline constexpr qsizetype kMaxLoopbackCodeBytes = 2048;

enum class LoopbackRequestOutcome {
    // The browser returned a code; `code` holds it.
    Code,
    // The browser returned an error: the person stopped the sign-in.
    Cancelled,
    // Anything else. The listener answers it and keeps waiting.
    Invalid,
};

struct LoopbackRequestResult {
    LoopbackRequestOutcome outcome = LoopbackRequestOutcome::Invalid;
    QString code;
};

// Reads one HTTP request received by the sign-in callback. Accepted only:
// at most kMaxLoopbackRequestBytes; "GET <target> HTTP/1.1" or HTTP/1.0;
// header lines ending in CRLF, each with a colon; exactly one Host header
// equal to `expectedAuthority` (A to Z folded, spaces and tabs trimmed); a
// target without "#" whose path is exactly `expectedPath`; query fields that
// are not empty, percent-decode ("+" is a space) and do not repeat a name;
// `state` equal to `expectedState`. Then an `error` field means Cancelled;
// otherwise `code` must be present, 1 to kMaxLoopbackCodeBytes bytes and
// ASCII.
[[nodiscard]] LoopbackRequestResult parseLoopbackRequest(QByteArrayView request, QByteArrayView expectedAuthority,
                                                         QByteArrayView expectedPath, QByteArrayView expectedState);

} // namespace lmp::core::account
