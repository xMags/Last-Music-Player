#pragma once

#include "Backend/AccountSyncTransport.h"
#include "Backend/AccountUrlPolicy.h"
#include "Backend/CatalogModels.h"

#include <cstdint>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>

namespace LastMusicPlayer::Backend
{
    // Storefront used when the user has not chosen one. Matches the service
    // default, so an unconfigured client and the service agree on what it sees.
    inline constexpr wchar_t DefaultCatalogStorefront[] = L"in";

    // Absolute URL of the hosted account management page, or an empty string when
    // this build has no trusted frontend origin configured.
    winrt::hstring AccountManagementUrl() noexcept;
    winrt::hstring SafeAccountErrorMessage(std::uint32_t status) noexcept;
    bool IsAccountUnauthorized(winrt::hresult_error const& error) noexcept;

    class AccountClient final : public IAccountSyncTransport
    {
    public:
        AccountClient();
        explicit AccountClient(winrt::hstring const& baseOrigin);

        bool IsConfigured() const noexcept;
        winrt::hstring BaseOrigin() const;

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ExchangeCodeAsync(
            winrt::hstring const& code,
            winrt::hstring const& redirectUri,
            winrt::hstring const& verifier);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetMeAsync(
            winrt::hstring const& bearerSession);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> LogoutAsync(
            winrt::hstring const& bearerSession);

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> SearchAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& query);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ResolveUrlAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& sourceUrl);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ImportAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& sourceUrl);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetRelatedAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& sourceUrl);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetLyricsAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& artist,
            winrt::hstring const& title,
            winrt::hstring const& album,
            std::int64_t durationMs,
            winrt::hstring const& sourceUrl);
        // Catalog discovery. Every one of these needs a session: the whole
        // /v1/music router is behind authentication.
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetCatalogStorefrontsAsync(
            winrt::hstring const& bearerSession);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetCatalogDiscoveryAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& storefront);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetCatalogChartAsync(
            winrt::hstring const& bearerSession,
            CatalogChartRequest const& request);
        // kind is "albums" or "playlists"; anything else is rejected before a
        // request is built.
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetCatalogResourceAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& kind,
            winrt::hstring const& id,
            winrt::hstring const& storefront);

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetLibraryAsync(
            winrt::hstring const& bearerSession) override;
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> SetLikedAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& trackJson,
            bool liked,
            winrt::hstring const& remoteId) override;
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> CreatePlaylistAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& playlistJson);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> UpdatePlaylistAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& playlistId,
            winrt::hstring const& playlistJson);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> DeletePlaylistAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& playlistId);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AddPlaylistTrackAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& playlistId,
            winrt::hstring const& trackJson);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PostHistoryBatchAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& batchJson) override;
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::Streams::IBuffer> GetArtworkAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& artworkUrl);
        winrt::Windows::Foundation::IAsyncOperation<std::uint32_t> ProbeStreamAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& streamUrl);

    private:
        winrt::hstring BuildUrl(winrt::hstring const& trustedPathAndQuery) const;
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Web::Http::HttpResponseMessage> SendAsync(
            winrt::Windows::Web::Http::HttpMethod const& method,
            winrt::hstring const& trustedPathAndQuery,
            winrt::hstring const& bearerSession,
            winrt::hstring const& jsonBody,
            winrt::Windows::Web::Http::HttpCompletionOption completion =
                winrt::Windows::Web::Http::HttpCompletionOption::ResponseContentRead);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod const& method,
            winrt::hstring const& trustedPathAndQuery,
            winrt::hstring const& bearerSession,
            winrt::hstring const& jsonBody = L"");

        winrt::hstring m_baseOrigin;
        winrt::Windows::Web::Http::HttpClient m_httpClient;
    };
}
