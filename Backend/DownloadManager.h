#pragma once

#include "Backend/DownloadPolicy.h"
#include "Backend/RemoteMusicService.h"
#include "Backend/SettingsManager.h"
#include "Backend/UserDataOperationGate.h"

#include <winrt/Windows.Foundation.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    struct DownloadTrackRequest
    {
        std::wstring StableKey;
        std::wstring SourceUrl;
        std::wstring Provider;
        std::wstring Title;
        std::wstring Artist;
        std::wstring Album;
        std::wstring ArtworkUrl;
    };

    struct DownloadItemSnapshot
    {
        std::wstring Id;
        std::wstring StableKey;
        std::wstring SourceUrl;
        std::wstring Provider;
        std::wstring Title;
        std::wstring Artist;
        std::wstring Album;
        std::wstring ArtworkUrl;
        std::wstring FilePath;
        std::wstring Format;
        std::wstring Error;
        DownloadItemState State{ DownloadItemState::Queued };
        std::uint64_t BytesDownloaded{};
        std::uint64_t BytesTotal{};
        std::uint64_t BytesPerSecond{};
        std::int64_t FinishedAtUnix{};
    };

    struct DownloadJobSnapshot
    {
        std::wstring Id;
        std::wstring Title;
        std::wstring Subtitle;
        std::wstring ArtworkUrl;
        std::vector<DownloadItemSnapshot> Items;
        DownloadItemState State{ DownloadItemState::Queued };
        std::uint64_t BytesDownloaded{};
        std::uint64_t BytesTotal{};
        std::uint64_t BytesPerSecond{};
        std::size_t CompletedItems{};
        std::wstring Error;
    };

    struct DownloadManagerSnapshot
    {
        std::vector<DownloadJobSnapshot> Jobs;
        std::filesystem::path RootFolder;
        std::uint64_t Revision{};
        std::uint64_t OfflineBytes{};
        std::size_t OfflineTracks{};
        bool AllPaused{};
        // Counters for this run of the app only. They are deliberately not
        // persisted: the panel reports what this session moved, so a restart
        // starts the tally again.
        std::size_t SessionCompletedTracks{};
        std::uint64_t SessionBytes{};
        std::uint64_t SessionAverageBytesPerSecond{};
        bool OnlyOnWifi{ true };
        bool AutoDownloadLiked{ true };
        bool DownloadOnBattery{};
        bool KeepRecentOffline{ true };
    };

    // Persistent offline-download coordinator. Signed media URLs exist only in
    // the active transfer coroutine; the durable state stores the catalog URL
    // needed to resolve a fresh URL after pause, failure, or process restart.
    class DownloadManager final
    {
    public:
        DownloadManager(
            SettingsManager& settings,
            RemoteMusicService& remoteMusic,
            UserDataOperationGate& operationGate);

        void Initialize();
        void Shutdown();

        [[nodiscard]] bool Enqueue(
            std::wstring title,
            std::wstring subtitle,
            std::wstring artworkUrl,
            std::vector<DownloadTrackRequest> tracks);

        void PauseJob(std::wstring const& jobId);
        void ResumeJob(std::wstring const& jobId);
        void CancelJob(std::wstring const& jobId);
        void RetryJob(std::wstring const& jobId);
        void PauseAll();
        void ResumeAll();
        void ClearCompletedHistory();
        void DismissFailed(std::wstring const& jobId);
        void RefreshScheduling();

        [[nodiscard]] bool SetRootFolder(std::filesystem::path const& folder);
        [[nodiscard]] DownloadManagerSnapshot Snapshot() const;
        [[nodiscard]] std::wstring ReadyPath(std::wstring const& stableKey) const;

        void SetOnlyOnWifi(bool value);
        void SetAutoDownloadLiked(bool value);
        void SetDownloadOnBattery(bool value);
        void SetKeepRecentOffline(bool value);

        // Opaque runtime records are public only so translation-unit helpers can
        // operate on them. Their definitions remain private to the .cpp file.
        struct RuntimeItem;
        struct RuntimeJob;
        struct SharedState;

    private:
        static std::filesystem::path AppDataDirectory();
        static std::filesystem::path DefaultDownloadDirectory();
        static std::filesystem::path StateFilePath();
        static void Load(std::shared_ptr<SharedState> const& state);
        static void Save(std::shared_ptr<SharedState> const& state);
        static void Pump(std::shared_ptr<SharedState> const& state);
        static bool SchedulingAllowed(std::shared_ptr<SharedState> const& state);
        static winrt::Windows::Foundation::IAsyncAction TransferAsync(
            std::shared_ptr<SharedState> state,
            std::wstring jobId,
            std::wstring itemId,
            std::uint64_t generation,
            std::uint64_t attemptId,
            UserDataOperationGate::Lease operationLease);

        SettingsManager& m_settings;
        RemoteMusicService& m_remoteMusic;
        UserDataOperationGate& m_operationGate;
        std::shared_ptr<SharedState> m_state;
    };
}
