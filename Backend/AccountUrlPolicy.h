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
}
