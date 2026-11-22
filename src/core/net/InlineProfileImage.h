#pragma once

#include <QString>

namespace lmp::core::net {

// Whether a profile picture sent inline, as a base64 data URI, is safe to
// keep: at most 1,500,000 characters; "data:<type>;base64," where the header
// is compared without regard to case and the type is image/jpeg, image/png,
// image/gif, image/webp or image/bmp; then a non-empty standard base64
// payload whose length is a multiple of four with at most two '=' at the end.
// There is no origin to check, so the shape is what is validated, before the
// value reaches the image decoder or the account database.
[[nodiscard]] bool isSafeInlineProfileImage(const QString& value);

// Whether a stored profile picture is inline data rather than an address: it
// starts with "data:", A to Z compared without regard to case. A cheap test
// for callers that must route the two apart; it says nothing about whether
// the payload is safe.
[[nodiscard]] bool isInlineProfileImageData(const QString& value);

} // namespace lmp::core::net
