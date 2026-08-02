#pragma once

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    // Narrow transport used by account synchronization. Keeping this separate
    // from the full account client lets the owner/bearer race be tested without
    // exposing browser or catalog transport details.
    struct IAccountSyncTransport
    {
        virtual ~IAccountSyncTransport() = default;

        virtual winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetLibraryAsync(
            winrt::hstring const& bearerSession) = 0;
        virtual winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> SetLikedAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& trackJson,
            bool liked,
            winrt::hstring const& remoteId) = 0;
        virtual winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PostHistoryBatchAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& batchJson) = 0;
    };
}
