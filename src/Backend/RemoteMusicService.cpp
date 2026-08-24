#include "pch.h"
#include "Backend/RemoteMusicService.h"

#include "Backend/BuildConfig.h"
#include "Backend/ProviderClient.h"
#include "Backend/ProviderHelpers.h"

#include <windows.h>
#include <bcrypt.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Json.h>

#include <array>
#include <limits>
#include <vector>

#pragma comment(lib, "Bcrypt.lib")

namespace LastMusicPlayer::Backend
{
    namespace
    {
        std::wstring ApiKeyCachePartition(
            winrt::hstring const& baseUrl,
            winrt::hstring const& apiKey)
        {
            if (baseUrl.empty() || apiKey.empty())
            {
                return {};
            }

            auto normalizedBase = NormalizeProviderBaseUrl(baseUrl);

            std::wstring input{ normalizedBase.c_str() };
            input.push_back(L'\n');
            input.append(apiKey.c_str());
            auto byteLength = input.size() * sizeof(wchar_t);
            if (byteLength > (std::numeric_limits<ULONG>::max)())
            {
                throw winrt::hresult_invalid_argument(L"The provider configuration is too large.");
            }

            BCRYPT_ALG_HANDLE algorithm{};
            winrt::check_nt(::BCryptOpenAlgorithmProvider(
                &algorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                0));

            DWORD objectLength{};
            DWORD resultLength{};
            auto status = ::BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength),
                sizeof(objectLength),
                &resultLength,
                0);
            if (status < 0)
            {
                ::BCryptCloseAlgorithmProvider(algorithm, 0);
                winrt::check_nt(status);
            }

            std::vector<std::uint8_t> object(objectLength);
            BCRYPT_HASH_HANDLE hash{};
            status = ::BCryptCreateHash(
                algorithm,
                &hash,
                object.data(),
                static_cast<ULONG>(object.size()),
                nullptr,
                0,
                0);
            if (status >= 0)
            {
                status = ::BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(input.data()),
                    static_cast<ULONG>(byteLength),
                    0);
            }

            std::array<std::uint8_t, 32> digest{};
            if (status >= 0)
            {
                status = ::BCryptFinishHash(
                    hash,
                    digest.data(),
                    static_cast<ULONG>(digest.size()),
                    0);
            }
            if (hash)
            {
                ::BCryptDestroyHash(hash);
            }
            ::BCryptCloseAlgorithmProvider(algorithm, 0);
            winrt::check_nt(status);

            static constexpr wchar_t hex[] = L"0123456789abcdef";
            std::wstring partition;
            partition.reserve(digest.size() * 2);
            for (auto byte : digest)
            {
                partition.push_back(hex[(byte >> 4) & 0x0f]);
                partition.push_back(hex[byte & 0x0f]);
            }
            return partition;
        }

        void RequireMode(RemoteAccessMode actual, RemoteAccessMode expected)
        {
            if (actual != expected)
            {
                throw winrt::hresult_error(E_NOT_VALID_STATE, L"The selected remote mode cannot perform this action.");
            }
        }

        ProviderClient ProviderFor(
            winrt::hstring const& baseUrl,
            winrt::hstring const& apiKey)
        {
            if (baseUrl.empty() || apiKey.empty())
            {
                throw winrt::hresult_error(E_NOT_VALID_STATE, L"API-key mode is not configured.");
            }
            ProviderClient client{ baseUrl };
            client.SetBearerToken(apiKey);
            return client;
        }
    }

    RemoteAccessMode ParseRemoteAccessMode(winrt::hstring const& value) noexcept
    {
        if (value == L"Account") return RemoteAccessMode::Account;
        if (value == L"ApiKey") return RemoteAccessMode::ApiKey;
        return RemoteAccessMode::LocalOnly;
    }

    winrt::hstring RemoteAccessModeName(RemoteAccessMode mode) noexcept
    {
        switch (mode)
        {
        case RemoteAccessMode::Account:
            return L"Account";
        case RemoteAccessMode::ApiKey:
            return L"ApiKey";
        default:
            return L"LocalOnly";
        }
    }

    std::wstring RemoteScopeCacheKey(RemoteScopeSnapshot const& scope)
    {
        std::wstring key{ RemoteAccessModeName(scope.Mode).c_str() };
        if (scope.Mode == RemoteAccessMode::ApiKey)
        {
            key += L":";
            key += scope.CachePartition;
        }
        key += L":";
        key += std::to_wstring(scope.Generation);
        if (scope.Mode == RemoteAccessMode::Account)
        {
            key += L":";
            key += std::to_wstring(scope.AccountGeneration);
        }
        return key;
    }

    bool IsSafeEphemeralMediaUrl(winrt::hstring const& value) noexcept
    {
        return IsSafeRemoteUrl(value, RemoteUrlUse::EphemeralMedia);
    }

    RemoteMusicService::RemoteMusicService(
        SettingsManager& settings,
        ICredentialStore& credentials,
        AccountSessionService& accountSession,
        AccountClient& accountClient,
        IAccountSyncTransport& accountSyncTransport)
        : m_settings(settings),
          m_credentials(credentials),
          m_accountSession(accountSession),
          m_accountClient(accountClient),
          m_accountSyncTransport(accountSyncTransport)
    {
    }

    winrt::hstring RemoteMusicService::ProviderBaseUrl() const
    {
        return m_settings.GetString(L"ProviderBaseUrl", L"");
    }

    winrt::hstring RemoteMusicService::ProviderApiKey() const
    {
        return m_credentials.ReadProviderApiKey();
    }

    RemoteAccessMode RemoteMusicService::Mode()
    {
        auto stored = m_settings.GetString(L"RemoteAccessMode", L"");
        if (!stored.empty())
        {
            return ParseRemoteAccessMode(stored);
        }

        auto initial = !ProviderBaseUrl().empty() && !ProviderApiKey().empty()
            ? RemoteAccessMode::ApiKey
            : RemoteAccessMode::LocalOnly;
        m_settings.SetString(L"RemoteAccessMode", RemoteAccessModeName(initial));
        return initial;
    }

    bool RemoteMusicService::SetMode(RemoteAccessMode mode)
    {
        if (!IsModeSelectable(mode))
        {
            return false;
        }
        m_settings.SetString(L"RemoteAccessMode", RemoteAccessModeName(mode));
        InvalidateScope();
        return true;
    }

    void RemoteMusicService::InvalidateScope() noexcept
    {
        m_scopeGeneration.fetch_add(1, std::memory_order_acq_rel);
    }

    RemoteScopeSnapshot RemoteMusicService::CaptureScope()
    {
        auto mode = Mode();
        auto cachePartition = mode == RemoteAccessMode::ApiKey
            ? ApiKeyCachePartition(ProviderBaseUrl(), ProviderApiKey())
            : std::wstring{};
        return {
            mode,
            m_scopeGeneration.load(std::memory_order_acquire),
            m_accountSession.Generation(),
            std::move(cachePartition)
        };
    }

    bool RemoteMusicService::IsCurrent(RemoteScopeSnapshot const& scope)
    {
        if (scope.Generation != m_scopeGeneration.load(std::memory_order_acquire)
            || scope.Mode != Mode())
        {
            return false;
        }

        switch (scope.Mode)
        {
        case RemoteAccessMode::ApiKey:
            return !scope.CachePartition.empty()
                && scope.CachePartition
                    == ApiKeyCachePartition(ProviderBaseUrl(), ProviderApiKey());
        case RemoteAccessMode::Account:
            return scope.CachePartition.empty()
                && scope.AccountGeneration == m_accountSession.Generation();
        default:
            return scope.CachePartition.empty();
        }
    }

    bool RemoteMusicService::IsModeAvailable(RemoteAccessMode mode) const
    {
        switch (mode)
        {
        case RemoteAccessMode::Account:
        {
            auto status = m_accountSession.Status();
            return m_accountSession.IsAccountIntegrationAvailable()
                && (status == AccountSessionStatus::Validated || status == AccountSessionStatus::Offline);
        }
        case RemoteAccessMode::ApiKey:
            return !ProviderBaseUrl().empty() && !ProviderApiKey().empty();
        default:
            return true;
        }
    }

    bool RemoteMusicService::IsModeSelectable(RemoteAccessMode mode) const
    {
        // Account still needs its session first: it is established through a
        // browser round trip, not through anything the user can type into
        // settings, so there is nothing to enter once the mode is active.
        return mode == RemoteAccessMode::ApiKey || IsModeAvailable(mode);
    }

    bool RemoteMusicService::HasRemoteAccess()
    {
        auto mode = Mode();
        return mode != RemoteAccessMode::LocalOnly && IsModeAvailable(mode);
    }

    AccountSyncContext RemoteMusicService::CaptureAccountSyncContext()
    {
        auto remoteScopeGeneration = m_scopeGeneration.load(std::memory_order_acquire);
        RequireMode(Mode(), RemoteAccessMode::Account);
        auto session = AccountContext();
        if (remoteScopeGeneration != m_scopeGeneration.load(std::memory_order_acquire)
            || Mode() != RemoteAccessMode::Account
            || !m_accountSession.IsCurrent(session))
        {
            throw winrt::hresult_canceled();
        }
        return AccountSyncContext{ std::move(session), remoteScopeGeneration };
    }

    bool RemoteMusicService::IsCurrent(AccountSyncContext const& context)
    {
        return context.m_remoteScopeGeneration == m_scopeGeneration.load(std::memory_order_acquire)
            && Mode() == RemoteAccessMode::Account
            && m_accountSession.IsCurrent(context.m_session);
    }

    AccountSessionStatus RemoteMusicService::StatusFor(AccountSyncContext const& context)
    {
        CheckCurrent(context);
        auto status = m_accountSession.Status();
        CheckCurrent(context);
        return status;
    }

    AccountProfile RemoteMusicService::ProfileFor(AccountSyncContext const& context)
    {
        CheckCurrent(context);
        auto profile = m_accountSession.CurrentProfile();
        CheckCurrent(context);
        if (profile.Id != context.OwnerId())
        {
            throw winrt::hresult_canceled();
        }
        return profile;
    }

    AccountOperationContext RemoteMusicService::AccountContext() const
    {
        return m_accountSession.CaptureOperation();
    }

    void RemoteMusicService::CheckCurrent(AccountOperationContext const& context) const
    {
        if (!m_accountSession.IsCurrent(context))
        {
            throw winrt::hresult_canceled();
        }
    }

    void RemoteMusicService::CheckCurrent(AccountSyncContext const& context)
    {
        if (!IsCurrent(context))
        {
            throw winrt::hresult_canceled();
        }
    }

    void RemoteMusicService::CheckScope(RemoteScopeSnapshot const& scope)
    {
        if (!IsCurrent(scope))
        {
            throw winrt::hresult_canceled();
        }
    }

    void RemoteMusicService::CheckScope(
        RemoteScopeSnapshot const& scope,
        AccountSyncContext const& context)
    {
        if (scope.Mode != RemoteAccessMode::Account
            || scope.Generation != context.m_remoteScopeGeneration
            || scope.AccountGeneration != context.m_session.Generation)
        {
            throw winrt::hresult_canceled();
        }
        CheckScope(scope);
        CheckCurrent(context);
    }

    void RemoteMusicService::HandleAccountError(
        AccountOperationContext const& context,
        winrt::hresult_error const& error) noexcept
    {
        if (IsAccountUnauthorized(error))
        {
            m_accountSession.HandleUnauthorized(context);
        }
    }

    void RemoteMusicService::HandleAccountError(
        AccountSyncContext const& context,
        winrt::hresult_error const& error) noexcept
    {
        if (IsCurrent(context))
        {
            HandleAccountError(context.m_session, error);
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<std::uint32_t> RemoteMusicService::TestApiKeyAsync(
        winrt::hstring const& baseUrl,
        winrt::hstring const& apiKey)
    {
        ProviderClient client{ baseUrl };
        client.SetBearerToken(apiKey);
        co_return co_await client.GetProvidersStatusAsync();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::SearchAsync(
        winrt::hstring const& query)
    {
        co_return co_await SearchAsync(CaptureScope(), query);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::SearchAsync(
        RemoteScopeSnapshot const& scope,
        winrt::hstring const& query)
    {
        CheckScope(scope);
        if (scope.Mode == RemoteAccessMode::ApiKey)
        {
            auto client = ProviderFor(ProviderBaseUrl(), ProviderApiKey());
            auto payload = co_await client.SearchAsync(query);
            CheckScope(scope);
            co_return payload;
        }
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.SearchAsync(context.m_session.BearerSession, query);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetRelatedAsync(
        winrt::hstring const& sourceUrl)
    {
        auto scope = CaptureScope();
        if (scope.Mode == RemoteAccessMode::ApiKey)
        {
            auto client = ProviderFor(ProviderBaseUrl(), ProviderApiKey());
            auto payload = co_await client.GetRelatedAsync(sourceUrl);
            CheckScope(scope);
            co_return payload;
        }
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.GetRelatedAsync(context.m_session.BearerSession, sourceUrl);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::ResolveUrlAsync(
        winrt::hstring const& sourceUrl,
        bool lowQuality)
    {
        auto scope = CaptureScope();
        if (scope.Mode == RemoteAccessMode::ApiKey)
        {
            auto client = ProviderFor(ProviderBaseUrl(), ProviderApiKey());
            auto payload = co_await client.ResolveUrlAsync(sourceUrl);
            CheckScope(scope);
            co_return payload;
        }
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.ResolveUrlAsync(
                context.m_session.BearerSession,
                sourceUrl,
                lowQuality);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
        RemoteMusicService::ResolveStreamUrlAsync(
            RemoteScopeSnapshot const& scope,
            winrt::hstring const& sourceUrl,
            winrt::hstring const& provider,
            bool lowQuality)
    {
        if (sourceUrl.empty() || scope.Mode == RemoteAccessMode::LocalOnly || !IsCurrent(scope))
        {
            co_return winrt::hstring{};
        }

        auto const baseUrl = scope.Mode == RemoteAccessMode::ApiKey
            ? ProviderBaseUrl()
            : winrt::hstring{};
        auto const apiKey = scope.Mode == RemoteAccessMode::ApiKey
            ? ProviderApiKey()
            : winrt::hstring{};

        auto const payload = co_await ResolveUrlAsync(sourceUrl, lowQuality);
        if (payload.empty() || !IsCurrent(scope))
        {
            co_return winrt::hstring{};
        }

        winrt::hstring streamUrl;
        winrt::hstring artworkUrl;
        try
        {
            auto const root = winrt::Windows::Data::Json::JsonObject::Parse(payload);
            auto const result = root.GetNamedObject(L"result", nullptr);
            if (result)
            {
                streamUrl = result.GetNamedString(L"streamUrl", L"");
                artworkUrl = result.GetNamedString(L"artworkUrl", L"");
            }
        }
        catch (...)
        {
            co_return winrt::hstring{};
        }

        if (scope.Mode == RemoteAccessMode::Account)
        {
            co_return IsTrustedAccountMediaUrl(
                streamUrl,
                BuildConfig::AccountMediaOrigin,
                L"stream")
                ? streamUrl
                : winrt::hstring{};
        }

        co_return BuildProviderStreamUrl(
            streamUrl,
            sourceUrl,
            provider,
            artworkUrl,
            baseUrl,
            apiKey);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::ImportAsync(
        winrt::hstring const& sourceUrl)
    {
        co_return co_await ImportAsync(CaptureScope(), sourceUrl);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::ImportAsync(
        RemoteScopeSnapshot const& scope,
        winrt::hstring const& sourceUrl)
    {
        CheckScope(scope);
        if (scope.Mode == RemoteAccessMode::ApiKey)
        {
            auto client = ProviderFor(ProviderBaseUrl(), ProviderApiKey());
            auto payload = co_await client.ImportAlbumAsync(sourceUrl);
            CheckScope(scope);
            co_return payload;
        }
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.ImportAsync(context.m_session.BearerSession, sourceUrl);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetLyricsAsync(
        winrt::hstring const& artist,
        winrt::hstring const& title,
        winrt::hstring const& album,
        std::int64_t durationMs,
        winrt::hstring const& sourceUrl)
    {
        auto scope = CaptureScope();
        if (scope.Mode == RemoteAccessMode::ApiKey)
        {
            auto client = ProviderFor(ProviderBaseUrl(), ProviderApiKey());
            auto payload = co_await client.GetLyricsAsync(artist, title, album, durationMs, sourceUrl);
            CheckScope(scope);
            co_return payload;
        }
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.GetLyricsAsync(
                context.m_session.BearerSession,
                artist,
                title,
                album,
                durationMs,
                sourceUrl);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }


    winrt::Windows::Foundation::IAsyncOperation<bool> RemoteMusicService::SupportsRemoteDetailAsync(
        RemoteScopeSnapshot const& scope,
        std::wstring_view capability)
    {
        auto cancellation = co_await winrt::get_cancellation_token();
        cancellation.enable_propagation();
        CheckScope(scope);
        if (scope.Mode == RemoteAccessMode::Account)
        {
            // Account endpoints are versioned with this contract. An older
            // deployment returns a sanitized 404 from the detail route, which
            // the page treats exactly like unsupported capability.
            co_return true;
        }
        RequireMode(scope.Mode, RemoteAccessMode::ApiKey);

        auto const cacheKey = RemoteScopeCacheKey(scope);
        {
            std::scoped_lock lock{ m_capabilityMutex };
            auto found = m_capabilityCache.find(cacheKey);
            if (found != m_capabilityCache.end())
            {
                co_return capability == ArtistDetailCapability
                    ? found->second.ArtistDetail
                    : found->second.AlbumDetail;
            }
        }

        auto client = ProviderFor(ProviderBaseUrl(), ProviderApiKey());
        auto payload = co_await client.GetProvidersAsync();
        CheckScope(scope);
        CapabilitySnapshot supported{
            ProviderSupportsCapability(payload, ArtistDetailCapability),
            ProviderSupportsCapability(payload, AlbumDetailCapability)
        };
        {
            std::scoped_lock lock{ m_capabilityMutex };
            if (m_capabilityCache.size() >= 16)
            {
                m_capabilityCache.clear();
            }
            m_capabilityCache[cacheKey] = supported;
        }
        co_return capability == ArtistDetailCapability
            ? supported.ArtistDetail
            : supported.AlbumDetail;
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetArtistDetailAsync(
        RemoteScopeSnapshot const& scope,
        RemoteDetailIdentity const& identity)
    {
        auto cancellation = co_await winrt::get_cancellation_token();
        cancellation.enable_propagation();
        CheckScope(scope);
        if (!co_await SupportsRemoteDetailAsync(scope, ArtistDetailCapability))
        {
            throw winrt::hresult_error(E_NOTIMPL, L"This provider does not support artist pages.");
        }
        CheckScope(scope);
        if (scope.Mode == RemoteAccessMode::ApiKey)
        {
            auto client = ProviderFor(ProviderBaseUrl(), ProviderApiKey());
            auto payload = co_await client.GetArtistDetailAsync(identity);
            CheckScope(scope);
            co_return payload;
        }

        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.GetArtistDetailAsync(
                context.m_session.BearerSession,
                identity);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetAlbumDetailAsync(
        RemoteScopeSnapshot const& scope,
        RemoteDetailIdentity const& identity)
    {
        auto cancellation = co_await winrt::get_cancellation_token();
        cancellation.enable_propagation();
        CheckScope(scope);
        if (!co_await SupportsRemoteDetailAsync(scope, AlbumDetailCapability))
        {
            throw winrt::hresult_error(E_NOTIMPL, L"This provider does not support album pages.");
        }
        CheckScope(scope);
        if (scope.Mode == RemoteAccessMode::ApiKey)
        {
            auto client = ProviderFor(ProviderBaseUrl(), ProviderApiKey());
            auto payload = co_await client.GetAlbumDetailAsync(identity);
            CheckScope(scope);
            co_return payload;
        }

        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.GetAlbumDetailAsync(
                context.m_session.BearerSession,
                identity);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetCatalogStorefrontsAsync()
    {
        RequireMode(Mode(), RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckCurrent(context);
        try
        {
            auto payload = co_await m_accountClient.GetCatalogStorefrontsAsync(context.m_session.BearerSession);
            CheckCurrent(context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetCatalogDiscoveryAsync(
        winrt::hstring const& storefront)
    {
        RequireMode(Mode(), RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckCurrent(context);
        try
        {
            auto payload = co_await m_accountClient.GetCatalogDiscoveryAsync(context.m_session.BearerSession, storefront);
            CheckCurrent(context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetCatalogChartAsync(
        CatalogChartRequest const& request)
    {
        RequireMode(Mode(), RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckCurrent(context);
        try
        {
            auto payload = co_await m_accountClient.GetCatalogChartAsync(
                context.m_session.BearerSession,
                request);
            CheckCurrent(context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetCatalogResourceAsync(
        winrt::hstring const& kind,
        winrt::hstring const& id,
        winrt::hstring const& storefront)
    {
        RequireMode(Mode(), RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckCurrent(context);
        try
        {
            auto payload = co_await m_accountClient.GetCatalogResourceAsync(
                context.m_session.BearerSession,
                kind,
                id,
                storefront);
            CheckCurrent(context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::Streams::IBuffer>
        RemoteMusicService::GetAccountArtworkAsync(winrt::hstring const& artworkUrl)
    {
        RequireMode(Mode(), RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckCurrent(context);
        try
        {
            auto buffer = co_await m_accountClient.GetArtworkAsync(context.m_session.BearerSession, artworkUrl);
            CheckCurrent(context);
            co_return buffer;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetAccountLibraryAsync(
        AccountSyncContext const& context)
    {
        CheckCurrent(context);
        try
        {
            auto payload = co_await m_accountSyncTransport.GetLibraryAsync(context.m_session.BearerSession);
            CheckCurrent(context);
            co_return payload;
        }
        catch (winrt::hresult_error const&)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetLibraryArtistsAsync(
        RemoteScopeSnapshot const& scope,
        LibraryArtistListRequest const& request)
    {
        CheckScope(scope);
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.GetLibraryArtistsAsync(
                context.m_session.BearerSession,
                request);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(scope) || !IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::GetLibraryArtistTracksAsync(
        RemoteScopeSnapshot const& scope,
        winrt::hstring const& publicArtistId)
    {
        CheckScope(scope);
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.GetLibraryArtistTracksAsync(
                context.m_session.BearerSession,
                publicArtistId);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(scope) || !IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::SetLibraryArtistFollowedAsync(
        RemoteScopeSnapshot const& scope,
        winrt::hstring const& publicArtistId,
        bool followed)
    {
        CheckScope(scope);
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.SetLibraryArtistFollowedAsync(
                context.m_session.BearerSession,
                publicArtistId,
                followed);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(scope) || !IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::SetAccountLikedAsync(
        AccountSyncContext const& context,
        winrt::hstring const& trackJson,
        bool liked,
        winrt::hstring const& remoteId)
    {
        CheckCurrent(context);
        try
        {
            auto payload = co_await m_accountSyncTransport.SetLikedAsync(
                context.m_session.BearerSession,
                trackJson,
                liked,
                remoteId);
            CheckCurrent(context);
            co_return payload;
        }
        catch (winrt::hresult_error const&)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::CreateAccountPlaylistAsync(
        RemoteScopeSnapshot const& scope,
        winrt::hstring const& playlistJson)
    {
        CheckScope(scope);
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.CreatePlaylistAsync(context.m_session.BearerSession, playlistJson);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(scope) || !IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::UpdateAccountPlaylistAsync(
        RemoteScopeSnapshot const& scope,
        winrt::hstring const& playlistId,
        winrt::hstring const& playlistJson)
    {
        CheckScope(scope);
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.UpdatePlaylistAsync(
                context.m_session.BearerSession,
                playlistId,
                playlistJson);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(scope) || !IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::DeleteAccountPlaylistAsync(
        RemoteScopeSnapshot const& scope,
        winrt::hstring const& playlistId)
    {
        CheckScope(scope);
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.DeletePlaylistAsync(context.m_session.BearerSession, playlistId);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(scope) || !IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::AddAccountPlaylistTrackAsync(
        RemoteScopeSnapshot const& scope,
        winrt::hstring const& playlistId,
        winrt::hstring const& trackJson)
    {
        CheckScope(scope);
        RequireMode(scope.Mode, RemoteAccessMode::Account);
        auto context = CaptureAccountSyncContext();
        CheckScope(scope, context);
        try
        {
            auto payload = co_await m_accountClient.AddPlaylistTrackAsync(
                context.m_session.BearerSession,
                playlistId,
                trackJson);
            CheckScope(scope, context);
            co_return payload;
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsCurrent(scope) || !IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            HandleAccountError(context, error);
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RemoteMusicService::PostAccountHistoryBatchAsync(
        AccountSyncContext const& context,
        winrt::hstring const& batchJson)
    {
        CheckCurrent(context);
        try
        {
            auto payload = co_await m_accountSyncTransport.PostHistoryBatchAsync(
                context.m_session.BearerSession,
                batchJson);
            CheckCurrent(context);
            co_return payload;
        }
        catch (winrt::hresult_error const&)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
        RemoteMusicService::GetAccountPreferencesAsync(AccountSyncContext const& context)
    {
        CheckCurrent(context);
        try
        {
            auto payload = co_await m_accountSyncTransport.GetPreferencesAsync(
                context.m_session.BearerSession);
            CheckCurrent(context);
            co_return payload;
        }
        catch (winrt::hresult_error const&)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
        RemoteMusicService::PutAccountPreferencesAsync(
            AccountSyncContext const& context,
            winrt::hstring const& preferencesJson)
    {
        CheckCurrent(context);
        try
        {
            auto payload = co_await m_accountSyncTransport.PutPreferencesAsync(
                context.m_session.BearerSession,
                preferencesJson);
            CheckCurrent(context);
            co_return payload;
        }
        catch (winrt::hresult_error const&)
        {
            if (!IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            throw;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> RemoteMusicService::RefreshAccountSessionAsync()
    {
        co_await m_accountSession.RestoreAsync();
        co_return Mode() == RemoteAccessMode::Account
            && m_accountSession.Status() == AccountSessionStatus::Validated;
    }

    void RemoteMusicService::HandleAccountSyncUnauthorized(AccountSyncContext const& context) noexcept
    {
        if (IsCurrent(context))
        {
            m_accountSession.HandleUnauthorized(context.m_session);
        }
    }
}
