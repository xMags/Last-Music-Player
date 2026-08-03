#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    class AccountClient;
    struct PkceTransaction;

    struct AccountProfile
    {
        winrt::hstring Id;
        winrt::hstring DisplayName;
        winrt::hstring Username;
        winrt::hstring AvatarUrl;
        winrt::hstring PlanLabel;
    };

    winrt::hstring BuildAccountAuthorizeUri(
        AccountClient const& client,
        winrt::hstring const& redirectUri,
        PkceTransaction const& transaction);
    winrt::hstring ParseAccountBearerSession(winrt::hstring const& payload);
    AccountProfile ParseAccountProfile(winrt::hstring const& payload);

    // Owns the browser, callback, and transport mechanics required by the
    // account lifecycle. AccountSessionService only receives opaque sessions
    // and validated profiles.
    struct IAccountSessionGateway
    {
        virtual ~IAccountSessionGateway() = default;

        virtual bool IsConfigured() const noexcept = 0;
        virtual winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
            AcquireBearerSessionAsync(std::uint64_t intent) = 0;
        virtual void CancelAcquireBearerSession(std::uint64_t throughIntent) noexcept = 0;
        virtual winrt::Windows::Foundation::IAsyncAction GetProfileAsync(
            winrt::hstring const& bearerSession,
            std::function<void(AccountProfile const&)> receiveProfile) = 0;
        virtual winrt::Windows::Foundation::IAsyncAction LogoutAsync(
            winrt::hstring const& bearerSession) = 0;
    };

    std::unique_ptr<IAccountSessionGateway> CreateAccountSessionGateway(
        AccountClient& client);
}
