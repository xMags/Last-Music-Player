#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>

namespace LastMusicPlayer::Backend
{
    class ProviderClient
    {
    public:
        ProviderClient();
        explicit ProviderClient(winrt::hstring baseUrl);

        void SetBaseUrl(winrt::hstring const& baseUrl);
        winrt::hstring GetBaseUrl() const;

        void SetBearerToken(winrt::hstring const& token);

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetProvidersAsync();
        winrt::Windows::Foundation::IAsyncOperation<uint32_t> GetProvidersStatusAsync();
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> SearchAsync(winrt::hstring query);
        // Autoplay "radio": related/up-next tracks that follow a seed song.
        // Returns the same { results: [...] } shape as SearchAsync, parsed by
        // ParseProviderTracks. Server falls back to [] for unsupported seeds.
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetRelatedAsync(winrt::hstring sourceUrl);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ResolveUrlAsync(winrt::hstring url);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ImportAlbumAsync(winrt::hstring url);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetLyricsAsync(
            winrt::hstring artist, winrt::hstring title, winrt::hstring album, int64_t durationMs,
            winrt::hstring sourceUrl);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ResolveDiscordArtworkAsync(
            winrt::hstring title, winrt::hstring artist);

    private:
        winrt::hstring BuildUrl(winrt::hstring const& pathAndQuery) const;
        void ApplyAuthHeader();

        winrt::hstring m_baseUrl{ L"http://127.0.0.1:4527" };
        winrt::hstring m_token;
        winrt::Windows::Web::Http::HttpClient m_httpClient;
    };
}
