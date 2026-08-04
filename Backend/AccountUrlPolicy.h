#pragma once

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    // Configured account origins are HTTPS in all builds. Debug builds also
    // permit loopback HTTP for local protocol and UI verification.
    bool IsTrustedAccountOrigin(winrt::hstring const& origin) noexcept;

    // Return a normalized trusted origin without trailing slashes, or empty
    // when the configured value is not a safe origin for this build.
    winrt::hstring NormalizeTrustedAccountOrigin(
        winrt::hstring const& origin) noexcept;

    // Verify that an absolute URL belongs to the exact configured account
    // origin by scheme, host, and effective port.
    bool IsUrlFromTrustedAccountOrigin(
        winrt::hstring const& url,
        winrt::hstring const& origin) noexcept;

    // Profile images may use an external HTTPS CDN, but must not contain
    // embedded credentials, signed-delivery fields, or unsafe schemes.
    bool IsSafeAccountProfileUrl(winrt::hstring const& url) noexcept;

    // The account API may also send the profile picture inline, as a base64
    // data URI, instead of a URL to fetch. That form needs a different check:
    // there is no origin to trust and no query to strip, and the real risks
    // are an unexpected media type, a malformed payload reaching the image
    // decoder, and an unbounded string being held in memory and written to
    // the account database.
    bool IsSafeInlineProfileImage(winrt::hstring const& value) noexcept;

    // Whether a stored profile picture is inline image data rather than a
    // URL. Cheap prefix test for callers that must route the two apart; it
    // says nothing about whether the payload is safe.
    bool IsInlineProfileImageData(winrt::hstring const& value) noexcept;
}
