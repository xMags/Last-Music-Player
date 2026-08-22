#pragma once

#include "Backend/UserDataOperationGate.h"

#include <winrt/Windows.Foundation.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace LastMusicPlayer::Backend
{
    struct StreamCacheUsage
    {
        std::uint64_t Bytes{};
        std::size_t Files{};
    };

    class IStreamCacheTransport
    {
    public:
        virtual ~IStreamCacheTransport() = default;

        // Writes a complete response body to partPath and returns its container
        // extension. An empty extension means the transfer did not complete.
        virtual winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> DownloadAsync(
            std::wstring streamUrl,
            std::filesystem::path partPath) = 0;
    };

    // Disk prefetch cache for remote (provider-streamed) tracks.
    //
    // Progressive HTTP streams rebuffer on any network jitter — a fraction of
    // a second of silence, then playback resumes. We can't deepen the
    // MediaPlayer's progressive buffer, but we CAN download the *upcoming*
    // tracks to local files while the current one plays: by the time the user
    // reaches them they are fully on disk and play with no live network at all,
    // so jitter can't stall them. The currently-streaming track is handled
    // separately by MediaFailed auto-recovery in the UI layer.
    //
    // A cache file is published (made visible to ReadyPath) only after a
    // complete, successful download + atomic rename, so playback always reuses
    // the well-tested local-file path on a *whole* file — never a partial one.
    class StreamCache
    {
    public:
        explicit StreamCache(UserDataOperationGate& operationGate);
        StreamCache(
            UserDataOperationGate& operationGate,
            std::shared_ptr<IStreamCacheTransport> transport);

        // Start a background download of `streamUrl`, identified by a stable
        // remote-scope-and-track `sourceKey`. No-op if the
        // track is already cached, a download for this key is already running,
        // or the global in-flight cap is hit. Fire-and-forget; safe to call
        // repeatedly (e.g. every time the Up Next queue is rebuilt).
        void Prefetch(std::wstring const& sourceKey, std::wstring const& streamUrl);

        // Full path of a completed cache file for `sourceKey`, or empty if not
        // cached. Survives restarts: on a cold cache it also checks disk for a
        // previously-downloaded file matching the key.
        std::wstring ReadyPath(std::wstring const& sourceKey);

        // Delete leftover *.part files and trim the cache to the size/count cap.
        // Call once at startup.
        void PruneOnStartup();

        // Invalidate cached lookups and retire active downloads. Existing transfers
        // are allowed to unwind because canceling a WinRT parent action does not
        // prove that its underlying transport has stopped writing. This does not
        // delete files. Callers that need deletion must wait for the shared
        // user-data operation gate to become idle before calling Clear().
        void InvalidateInFlight();

        // Delete all cache files. Returns false while a download is still active,
        // so callers cannot race recursive deletion against a transfer.
        [[nodiscard]] bool Clear();
        [[nodiscard]] StreamCacheUsage Usage() const;

    private:
        enum class Status { InFlight, Publishing, Ready, Failed };
        struct Entry
        {
            Status status{ Status::InFlight };
            std::wstring path;
            std::uint64_t attemptId{};
            winrt::Windows::Foundation::IAsyncAction action{ nullptr };
        };

        struct SharedState;

        static std::filesystem::path CacheDir();
        static std::filesystem::path EnsureCacheDir();
        static std::wstring HashKey(std::wstring const& sourceKey);
        static std::wstring FindOnDisk(std::wstring const& hash);
        static void TrimToCap(std::shared_ptr<SharedState> const& state);
        static winrt::Windows::Foundation::IAsyncAction DownloadAsync(
            std::shared_ptr<SharedState> state,
            std::wstring sourceKey,
            std::wstring streamUrl,
            std::uint64_t generation,
            std::uint64_t attemptId,
            UserDataOperationGate::Lease operationLease);

        UserDataOperationGate& m_operationGate;
        std::shared_ptr<SharedState> m_state;

        // Keep concurrent prefetch downloads (and total disk footprint) modest:
        // we only need a couple of tracks of lookahead, and the box is a music
        // player, not a downloader.
        static constexpr int kMaxInFlight = 2;
        static constexpr int kMaxFiles = 24;
        static constexpr unsigned long long kMaxBytes = 256ull * 1024 * 1024;  // 256 MiB
    };
}
