#pragma once

#include "Backend/DatabaseEngine.h"
#include "Backend/RemoteMusicService.h"
#include "Backend/UserDataOperationGate.h"

#include <mutex>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    class MusicSyncService final
    {
    public:
        MusicSyncService(
            RemoteMusicService& remoteMusic,
            DatabaseEngine& database,
            UserDataOperationGate& operationGate);

        winrt::Windows::Foundation::IAsyncOperation<bool> SyncAsync();
        bool IsSyncing() const noexcept;
        winrt::hstring LastSafeError() const;

    private:
        bool BeginSync() noexcept;
        void FinishSync() noexcept;
        void SetSafeError(winrt::hstring const& message);
        bool SaveSyncError(AccountSyncContext const& context, std::wstring const& code);
        bool FailSync(
            AccountSyncContext const& context,
            winrt::hstring const& message,
            std::wstring const& code);

        RemoteMusicService& m_remoteMusic;
        DatabaseEngine& m_database;
        UserDataOperationGate& m_operationGate;

        mutable std::mutex m_mutex;
        bool m_syncing{};
        winrt::hstring m_lastSafeError;
    };
}
