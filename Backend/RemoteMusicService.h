#pragma once

#include "Backend/AccountClient.h"
#include "Backend/AccountSessionService.h"
#include "Backend/CredentialStore.h"
#include "Backend/SettingsManager.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

namespace LastMusicPlayer::Backend
{
    enum class RemoteAccessMode
    {
        LocalOnly,
        ApiKey,
        Account,
    };

    RemoteAccessMode ParseRemoteAccessMode(winrt::hstring const& value) noexcept;
    winrt::hstring RemoteAccessModeName(RemoteAccessMode mode) noexcept;
    bool IsSafeEphemeralMediaUrl(winrt::hstring const& value) noexcept;

    struct RemoteScopeSnapshot
    {
        RemoteAccessMode Mode{ RemoteAccessMode::LocalOnly };
        std::uint64_t Generation{};
        std::uint64_t AccountGeneration{};
        std::wstring CachePartition;
    };

    std::wstring RemoteScopeCacheKey(RemoteScopeSnapshot const& scope);

    // Carries one account identity through a multi-request operation. The bearer
    // remains private so callers can only use the owner that was captured with it.
    class AccountSyncContext final
    {
    public:
        AccountSyncContext(AccountSyncContext const&) = default;
        AccountSyncContext(AccountSyncContext&&) = default;
        AccountSyncContext& operator=(AccountSyncContext const&) = delete;
        AccountSyncContext& operator=(AccountSyncContext&&) = delete;

        winrt::hstring const& OwnerId() const noexcept
        {
            return m_session.OwnerId;
        }

    private:
        friend class RemoteMusicService;

        AccountSyncContext(
            AccountOperationContext session,
            std::uint64_t remoteScopeGeneration)
            : m_session(std::move(session)),
              m_remoteScopeGeneration(remoteScopeGeneration)
        {
        }

        AccountOperationContext m_session;
        std::uint64_t m_remoteScopeGeneration{};
    };

    class RemoteMusicService final
    {
    public:
        RemoteMusicService(
            SettingsManager& settings,
            ICredentialStore& credentials,
            AccountSessionService& accountSession,
            AccountClient& accountClient,
            IAccountSyncTransport& accountSyncTransport);

        RemoteAccessMode Mode();
        bool SetMode(RemoteAccessMode mode);
        void InvalidateScope() noexcept;
        RemoteScopeSnapshot CaptureScope();
        bool IsCurrent(RemoteScopeSnapshot const& scope);
        bool IsModeAvailable(RemoteAccessMode mode) const;
        bool HasRemoteAccess();

        AccountSyncContext CaptureAccountSyncContext();
        bool IsCurrent(AccountSyncContext const& context);
        AccountSessionStatus StatusFor(AccountSyncContext const& context);
        AccountProfile ProfileFor(AccountSyncContext const& context);

        winrt::Windows::Foundation::IAsyncOperation<std::uint32_t> TestApiKeyAsync(
            winrt::hstring const& baseUrl,
            winrt::hstring const& apiKey);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> SearchAsync(
            winrt::hstring const& query);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> SearchAsync(
            RemoteScopeSnapshot const& scope,
            winrt::hstring const& query);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetRelatedAsync(
            winrt::hstring const& sourceUrl);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ResolveUrlAsync(
            winrt::hstring const& sourceUrl);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ImportAsync(
            winrt::hstring const& sourceUrl);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ImportAsync(
            RemoteScopeSnapshot const& scope,
            winrt::hstring const& sourceUrl);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetLyricsAsync(
            winrt::hstring const& artist,
            winrt::hstring const& title,
            winrt::hstring const& album,
            std::int64_t durationMs,
            winrt::hstring const& sourceUrl);

        // Catalog discovery. Account mode only: the catalog endpoints live behind
        // the account session, and an API-key provider has no catalog contract.
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetCatalogStorefrontsAsync();
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetCatalogDiscoveryAsync(
            winrt::hstring const& storefront);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetCatalogChartAsync(
            winrt::hstring const& storefront,
            winrt::hstring const& type,
            std::int32_t limit,
            std::int32_t offset);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetCatalogResourceAsync(
            winrt::hstring const& kind,
            winrt::hstring const& id,
            winrt::hstring const& storefront);
        // Artwork bytes through the session-authenticated relay. BitmapImage
        // cannot attach the session header itself, so image URLs are fetched here
        // and handed to it as an already-decoded buffer.
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::Streams::IBuffer>
            GetAccountArtworkAsync(winrt::hstring const& artworkUrl);

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetAccountLibraryAsync(
            AccountSyncContext const& context);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> SetAccountLikedAsync(
            AccountSyncContext const& context,
            winrt::hstring const& trackJson,
            bool liked,
            winrt::hstring const& remoteId);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> CreateAccountPlaylistAsync(
            RemoteScopeSnapshot const& scope,
            winrt::hstring const& playlistJson);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> UpdateAccountPlaylistAsync(
            RemoteScopeSnapshot const& scope,
            winrt::hstring const& playlistId,
            winrt::hstring const& playlistJson);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> DeleteAccountPlaylistAsync(
            RemoteScopeSnapshot const& scope,
            winrt::hstring const& playlistId);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AddAccountPlaylistTrackAsync(
            RemoteScopeSnapshot const& scope,
            winrt::hstring const& playlistId,
            winrt::hstring const& trackJson);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PostAccountHistoryBatchAsync(
            AccountSyncContext const& context,
            winrt::hstring const& batchJson);

    private:
        winrt::hstring ProviderBaseUrl() const;
        winrt::hstring ProviderApiKey() const;
        AccountOperationContext AccountContext() const;
        void CheckCurrent(AccountOperationContext const& context) const;
        void CheckCurrent(AccountSyncContext const& context);
        void CheckScope(RemoteScopeSnapshot const& scope);
        void CheckScope(
            RemoteScopeSnapshot const& scope,
            AccountSyncContext const& context);
        void HandleAccountError(
            AccountOperationContext const& context,
            winrt::hresult_error const& error) noexcept;
        void HandleAccountError(
            AccountSyncContext const& context,
            winrt::hresult_error const& error) noexcept;

        SettingsManager& m_settings;
        ICredentialStore& m_credentials;
        AccountSessionService& m_accountSession;
        AccountClient& m_accountClient;
        IAccountSyncTransport& m_accountSyncTransport;
        std::atomic<std::uint64_t> m_scopeGeneration{ 1 };
    };
}
