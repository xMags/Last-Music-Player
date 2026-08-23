#pragma once

#include "Backend/AccountDataLifecycleContracts.h"
#include "Backend/AccountMusicContract.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/RemoteMusicService.h"
#include "Backend/SettingsManager.h"
#include "Backend/UserDataOperationGate.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.Threading.h>

namespace LastMusicPlayer::Backend
{
    enum class AccountSyncLane : std::uint32_t
    {
        None = 0,
        History = 1u << 0,
        Likes = 1u << 1,
        PreferencePush = 1u << 2,
        PreferencePull = 1u << 3,
        Library = 1u << 4,
        All = (1u << 5) - 1u,
    };

    [[nodiscard]] constexpr AccountSyncLane operator|(
        AccountSyncLane left,
        AccountSyncLane right) noexcept
    {
        return static_cast<AccountSyncLane>(
            static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    class MusicSyncService final : public IAccountOutboundSync
    {
    public:
        MusicSyncService(
            RemoteMusicService& remoteMusic,
            DatabaseEngine& database,
            UserDataOperationGate& operationGate,
            SettingsManager& settings);

        winrt::Windows::Foundation::IAsyncOperation<bool> SyncAsync();
        winrt::Windows::Foundation::IAsyncOperation<bool> RequestSyncAsync(AccountSyncLane lanes);
        winrt::Windows::Foundation::IAsyncOperation<bool>
            RequestOutboundSyncAsync() override;
        void ScheduleMutation(AccountSyncLane lane);
        bool SavePortablePreference(PortablePreferenceKey key, std::wstring const& valueJson);

        bool IsSyncing() const noexcept;
        winrt::hstring LastSafeError() const;
        AccountPendingCounts PendingCounts() const;
        std::uint64_t LibraryRevision() const noexcept;
        std::uint64_t PreferenceRevision() const noexcept;

    private:
        struct SyncCompletionState
        {
            winrt::handle Event;
            std::atomic<bool> Succeeded{};
        };

        enum LaneOutcome : int
        {
            Succeeded,
            Failed,
            Unauthorized,
        };

        winrt::Windows::Foundation::IAsyncOperation<int> RunHistoryLaneAsync(
            AccountSyncContext const& context);
        winrt::Windows::Foundation::IAsyncOperation<int> UploadHistoryBatchAsync(
            AccountSyncContext const& context,
            std::vector<PlaybackEventRecord> events);
        winrt::Windows::Foundation::IAsyncOperation<int> RunLikesLaneAsync(
            AccountSyncContext const& context);
        winrt::Windows::Foundation::IAsyncOperation<int> RunPreferencePushLaneAsync(
            AccountSyncContext const& context);
        winrt::Windows::Foundation::IAsyncOperation<int> RunPreferencePullLaneAsync(
            AccountSyncContext const& context);
        winrt::Windows::Foundation::IAsyncOperation<int> RunLibraryLaneAsync(
            AccountSyncContext const& context);

        void Requeue(AccountSyncLane lanes) noexcept;
        void Finish(bool succeeded) noexcept;
        void SetSafeError(winrt::hstring const& message);
        void SaveSyncError(AccountSyncContext const& context, std::wstring const& code);
        void ScheduleRetry(AccountSyncLane lane, std::chrono::seconds delay);
        void ScheduleNextDurableRetry(
            AccountSyncContext const& context,
            AccountSyncLane lane,
            AccountRetryQueue queue);
        int RecordLaneFailure(AccountSyncLane lane) noexcept;
        void ResetLaneFailure(AccountSyncLane lane) noexcept;

        RemoteMusicService& m_remoteMusic;
        DatabaseEngine& m_database;
        UserDataOperationGate& m_operationGate;
        SettingsManager& m_settings;

        mutable std::mutex m_mutex;
        bool m_syncing{};
        std::uint32_t m_pendingLanes{};
        std::uint32_t m_debouncedLanes{};
        std::uint64_t m_libraryRevision{};
        std::uint64_t m_preferenceRevision{};
        winrt::hstring m_lastSafeError;
        winrt::Windows::System::Threading::ThreadPoolTimer m_mutationTimer{ nullptr };
        std::array<winrt::Windows::System::Threading::ThreadPoolTimer, 5> m_retryTimers{
            nullptr, nullptr, nullptr, nullptr, nullptr
        };
        std::array<std::chrono::steady_clock::time_point, 5> m_retryDue{};
        std::array<int, 5> m_laneFailureAttempts{};
        std::shared_ptr<SyncCompletionState> m_syncCompletion;
    };
}
