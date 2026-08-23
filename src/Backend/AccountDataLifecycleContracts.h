#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    class IAccountOutboundSync
    {
    public:
        virtual ~IAccountOutboundSync() = default;

        virtual winrt::Windows::Foundation::IAsyncOperation<bool>
            RequestOutboundSyncAsync() = 0;
    };

    class IAccountDataLifecycleStorage
    {
    public:
        virtual ~IAccountDataLifecycleStorage() = default;

        [[nodiscard]] virtual bool IsAccountDataStoreReady() const noexcept = 0;
        [[nodiscard]] virtual std::optional<std::size_t> RecoverablePendingChangeCount(
            std::wstring const& accountId) const = 0;
        virtual bool RemoveSignedOutAccountData(std::wstring const& accountId) = 0;
    };
}
