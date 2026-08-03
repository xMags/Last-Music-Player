#pragma once

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    enum class RemoteUrlUse
    {
        EphemeralMedia,
        Durable
    };

    // Accept HTTPS URLs and loopback HTTP URLs only. Ephemeral media URLs may
    // carry short-lived signatures; durable URLs must not contain credentials
    // or signed-delivery parameters.
    bool IsSafeRemoteUrl(winrt::hstring const& url, RemoteUrlUse use) noexcept;

    // Whether a cloud library row can be cached on this machine. Local rows name
    // a path on the machine that scanned them, so they never travel; everything
    // else must survive as a durable URL because cached rows outlive a session.
    bool IsSyncableRemoteSource(
        winrt::hstring const& sourceKind,
        winrt::hstring const& sourceUrl) noexcept;

    winrt::hstring NormalizeProviderBaseUrl(winrt::hstring const& savedBase);

    // Request artwork large enough for high-DPI desktop presentation while
    // preserving the provider URL shape and authentication parameters.
    winrt::hstring NormalizeArtworkUrlForDisplay(winrt::hstring const& artworkUrl);

    // Remove obsolete access_token query credentials from provider-shaped
    // stream or artwork URLs. Other URL shapes are left untouched.
    winrt::hstring RemoveLegacyProviderUrlCredential(winrt::hstring const& url);

    // API-key provider mode. Existing provider paths are rebased to the current
    // host and receive the current API key as an access_token query parameter.
    winrt::hstring BuildProviderStreamUrl(
        winrt::hstring const& filePath,
        winrt::hstring const& sourceUrl,
        winrt::hstring const& provider,
        winrt::hstring const& artworkUrl,
        winrt::hstring const& providerBaseUrl,
        winrt::hstring const& apiToken);

    // Account mode. Return a signed URL only when it belongs to the configured
    // account-media origin. Legacy access_token credentials are removed.
    winrt::hstring BuildProviderStreamUrl(
        winrt::hstring const& filePath,
        winrt::hstring const& providerBaseUrl);

    // API-key provider mode.
    winrt::hstring BuildProviderArtworkUrl(
        winrt::hstring const& artworkUrl,
        winrt::hstring const& providerBaseUrl,
        winrt::hstring const& apiToken);

    // Account mode.
    winrt::hstring BuildProviderArtworkUrl(
        winrt::hstring const& artworkUrl,
        winrt::hstring const& providerBaseUrl);

    // Signed provider media URLs contain `<scope>.<expiry-ms>.<signature>`.
    // When signing is required, refresh missing, malformed, or nearly-expired
    // tokens through an authenticated API request before handing them to a
    // media component that cannot attach Authorization headers.
    bool ProviderMediaUrlNeedsRefresh(
        winrt::hstring const& mediaUrl,
        winrt::hstring const& expectedScope,
        bool signedTokenRequired);

    // Validate the complete account media boundary: safe URL shape, configured
    // origin, expected relay path, token scope, and freshness.
    bool IsTrustedAccountMediaUrl(
        winrt::hstring const& mediaUrl,
        winrt::hstring const& accountMediaOrigin,
        winrt::hstring const& expectedScope) noexcept;
}
