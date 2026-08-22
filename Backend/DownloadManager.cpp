#include "pch.h"
#include "Backend/DownloadManager.h"

#include <windows.h>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.Power.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        namespace WDJ = winrt::Windows::Data::Json;
        namespace WNC = winrt::Windows::Networking::Connectivity;
        namespace WSP = winrt::Windows::System::Power;
        namespace WSS = winrt::Windows::Storage::Streams;
        namespace WWH = winrt::Windows::Web::Http;

        constexpr std::uint32_t kBufferSize = 64 * 1024;
        constexpr int kMaxConcurrentTransfers = 2;

        std::string ToUtf8(std::wstring const& value)
        {
            if (value.empty())
            {
                return {};
            }
            auto const size = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return {};
            }
            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                result.data(), size, nullptr, nullptr);
            return result;
        }

        std::wstring FromUtf8(std::string const& value)
        {
            if (value.empty())
            {
                return {};
            }
            auto const size = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (size <= 0)
            {
                return {};
            }
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                result.data(), size);
            return result;
        }

        std::wstring NewId()
        {
            GUID guid{};
            if (FAILED(CoCreateGuid(&guid)))
            {
                return std::to_wstring(GetTickCount64());
            }
            wchar_t value[40]{};
            StringFromGUID2(guid, value, static_cast<int>(std::size(value)));
            std::wstring id{ value };
            id.erase(std::remove_if(id.begin(), id.end(), [](wchar_t character)
            {
                return character == L'{' || character == L'}' || character == L'-';
            }), id.end());
            return id;
        }

        std::wstring StateName(DownloadItemState state)
        {
            switch (state)
            {
            case DownloadItemState::Downloading: return L"downloading";
            case DownloadItemState::Paused: return L"paused";
            case DownloadItemState::Completed: return L"completed";
            case DownloadItemState::Failed: return L"failed";
            case DownloadItemState::Queued:
            default: return L"queued";
            }
        }

        DownloadItemState ParseState(std::wstring const& value)
        {
            if (value == L"paused") return DownloadItemState::Paused;
            if (value == L"completed") return DownloadItemState::Completed;
            if (value == L"failed") return DownloadItemState::Failed;
            // An interrupted transfer is deliberately queued on recovery.
            return DownloadItemState::Queued;
        }

        std::wstring SanitizeFileName(std::wstring value)
        {
            for (auto& character : value)
            {
                if (character < 32 || std::wstring_view{ L"<>:\"/\\|?*" }.find(character) != std::wstring_view::npos)
                {
                    character = L'_';
                }
            }
            while (!value.empty() && (value.back() == L' ' || value.back() == L'.'))
            {
                value.pop_back();
            }
            if (value.empty())
            {
                value = L"Track";
            }
            if (value.size() > 80)
            {
                value.resize(80);
            }
            return value;
        }

        std::int64_t UnixNow()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        void StartDetached(winrt::Windows::Foundation::IAsyncAction action)
        {
            [](winrt::Windows::Foundation::IAsyncAction pending) -> winrt::fire_and_forget
            {
                try
                {
                    co_await pending;
                }
                catch (...)
                {
                    OutputDebugStringW(L"[LastMusicPlayer] Download worker failed.\n");
                }
            }(std::move(action));
        }

        void InsertString(WDJ::JsonObject const& object, wchar_t const* key, std::wstring const& value)
        {
            object.Insert(key, WDJ::JsonValue::CreateStringValue(winrt::hstring(value)));
        }

        std::wstring JsonString(WDJ::JsonObject const& object, wchar_t const* key)
        {
            return std::wstring(object.GetNamedString(key, L"").c_str());
        }
    }

    struct DownloadManager::RuntimeItem
    {
        DownloadItemSnapshot Snapshot;
        std::uint64_t AttemptId{};
        bool PauseRequested{};
    };

    struct DownloadManager::RuntimeJob
    {
        std::wstring Id;
        std::wstring Title;
        std::wstring Subtitle;
        std::wstring ArtworkUrl;
        std::vector<RuntimeItem> Items;
        bool Hidden{};
    };

    struct DownloadManager::SharedState
    {
        SharedState(
            SettingsManager& settings,
            RemoteMusicService& remoteMusic,
            UserDataOperationGate& operationGate)
            : Settings(&settings), RemoteMusic(&remoteMusic), OperationGate(&operationGate)
        {
        }

        mutable std::recursive_mutex Mutex;
        SettingsManager* Settings{};
        RemoteMusicService* RemoteMusic{};
        UserDataOperationGate* OperationGate{};
        std::filesystem::path RootFolder;
        std::vector<RuntimeJob> Jobs;
        std::uint64_t Revision{ 1 };
        std::uint64_t Generation{ 1 };
        std::uint64_t OfflineBytes{};
        std::size_t OfflineTracks{};
        // Session tally. SessionTransferSeconds counts only the time transfers
        // were actually running, so an idle queue does not drag the average
        // speed towards zero.
        std::size_t SessionCompletedTracks{};
        std::uint64_t SessionBytes{};
        double SessionTransferSeconds{};
        int Running{};
        bool Initialized{};
        bool Shutdown{};
        bool AllPaused{};
        bool OnlyOnWifi{ true };
        bool AutoDownloadLiked{ true };
        bool DownloadOnBattery{};
        bool KeepRecentOffline{ true };
    };

    namespace
    {
        DownloadManager::RuntimeJob* FindJob(
            DownloadManager::SharedState& state,
            std::wstring const& jobId)
        {
            auto found = std::find_if(state.Jobs.begin(), state.Jobs.end(), [&](auto const& job)
            {
                return job.Id == jobId;
            });
            return found == state.Jobs.end() ? nullptr : &*found;
        }

        DownloadManager::RuntimeItem* FindItem(
            DownloadManager::RuntimeJob& job,
            std::wstring const& itemId)
        {
            auto found = std::find_if(job.Items.begin(), job.Items.end(), [&](auto const& item)
            {
                return item.Snapshot.Id == itemId;
            });
            return found == job.Items.end() ? nullptr : &*found;
        }

        void RefreshStorageUsage(DownloadManager::SharedState& state)
        {
            state.OfflineBytes = 0;
            state.OfflineTracks = 0;
            std::unordered_set<std::wstring> countedPaths;
            for (auto const& job : state.Jobs)
            {
                for (auto const& item : job.Items)
                {
                    if (item.Snapshot.State != DownloadItemState::Completed
                        || item.Snapshot.FilePath.empty()
                        || !countedPaths.insert(item.Snapshot.FilePath).second)
                    {
                        continue;
                    }
                    std::error_code error;
                    auto const path = std::filesystem::path(item.Snapshot.FilePath);
                    if (!std::filesystem::is_regular_file(path, error))
                    {
                        continue;
                    }
                    auto const size = std::filesystem::file_size(path, error);
                    if (!error)
                    {
                        state.OfflineBytes += size;
                        ++state.OfflineTracks;
                    }
                }
            }
        }

        DownloadItemState AggregateState(DownloadManager::RuntimeJob const& job)
        {
            std::vector<DownloadItemState> states;
            states.reserve(job.Items.size());
            for (auto const& item : job.Items)
            {
                states.push_back(item.Snapshot.State);
            }
            return AggregateDownloadState(states);
        }
    }

    DownloadManager::DownloadManager(
        SettingsManager& settings,
        RemoteMusicService& remoteMusic,
        UserDataOperationGate& operationGate)
        : m_settings(settings),
          m_remoteMusic(remoteMusic),
          m_operationGate(operationGate),
          m_state(std::make_shared<SharedState>(settings, remoteMusic, operationGate))
    {
    }

    std::filesystem::path DownloadManager::AppDataDirectory()
    {
        wchar_t* value{};
        std::size_t length{};
        if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") == 0 && value && *value)
        {
            auto result = std::filesystem::path{ value } / L"Last Music Player";
            std::free(value);
            return result;
        }
        std::free(value);
        return std::filesystem::current_path() / L"Last Music Player";
    }

    std::filesystem::path DownloadManager::DefaultDownloadDirectory()
    {
        return AppDataDirectory() / L"Offline";
    }

    std::filesystem::path DownloadManager::StateFilePath()
    {
        return AppDataDirectory() / L"Downloads.json";
    }

    void DownloadManager::Initialize()
    {
        {
            std::lock_guard lock{ m_state->Mutex };
            if (m_state->Initialized)
            {
                return;
            }
            auto configured = std::wstring(m_settings.GetString(L"DownloadFolder", L"").c_str());
            m_state->RootFolder = configured.empty()
                ? DefaultDownloadDirectory()
                : std::filesystem::path{ configured };
            m_state->OnlyOnWifi = m_settings.GetBool(L"DownloadOnlyOnWifi", true);
            m_state->AutoDownloadLiked = m_settings.GetBool(L"AutoDownloadLiked", true);
            m_state->DownloadOnBattery = m_settings.GetBool(L"DownloadOnBattery", false);
            m_state->KeepRecentOffline = m_settings.GetBool(L"KeepRecentOffline", true);
            m_state->Initialized = true;
        }
        Load(m_state);
        {
            std::lock_guard lock{ m_state->Mutex };
            std::error_code error;
            std::filesystem::create_directories(m_state->RootFolder, error);
            for (auto& job : m_state->Jobs)
            {
                for (auto& item : job.Items)
                {
                    if (item.Snapshot.State == DownloadItemState::Completed
                        && (item.Snapshot.FilePath.empty()
                            || !std::filesystem::is_regular_file(item.Snapshot.FilePath, error)))
                    {
                        error.clear();
                        item.Snapshot.State = DownloadItemState::Queued;
                        item.Snapshot.FilePath.clear();
                        item.Snapshot.Format.clear();
                        item.Snapshot.BytesDownloaded = 0;
                        item.Snapshot.BytesTotal = 0;
                        item.Snapshot.FinishedAtUnix = 0;
                        job.Hidden = false;
                    }
                }
            }
            RefreshStorageUsage(*m_state);
        }
        Pump(m_state);
    }

    void DownloadManager::Shutdown()
    {
        std::lock_guard lock{ m_state->Mutex };
        if (m_state->Shutdown)
        {
            return;
        }
        m_state->Shutdown = true;
        ++m_state->Generation;
        for (auto& job : m_state->Jobs)
        {
            for (auto& item : job.Items)
            {
                if (item.Snapshot.State == DownloadItemState::Downloading)
                {
                    item.PauseRequested = true;
                    item.Snapshot.State = DownloadItemState::Queued;
                    item.Snapshot.BytesPerSecond = 0;
                }
            }
        }
        ++m_state->Revision;
        Save(m_state);
    }

    bool DownloadManager::Enqueue(
        std::wstring title,
        std::wstring subtitle,
        std::wstring artworkUrl,
        std::vector<DownloadTrackRequest> tracks)
    {
        Initialize();
        std::lock_guard lock{ m_state->Mutex };
        std::unordered_set<std::wstring> existing;
        for (auto const& job : m_state->Jobs)
        {
            for (auto const& item : job.Items)
            {
                if (item.Snapshot.State != DownloadItemState::Failed)
                {
                    existing.insert(item.Snapshot.StableKey);
                }
            }
        }

        RuntimeJob job;
        job.Id = NewId();
        job.Title = title.empty() ? L"Download" : std::move(title);
        job.Subtitle = std::move(subtitle);
        job.ArtworkUrl = std::move(artworkUrl);
        for (auto& track : tracks)
        {
            if (track.StableKey.empty()
                || track.SourceUrl.empty()
                || !existing.insert(track.StableKey).second)
            {
                continue;
            }
            RuntimeItem item;
            item.Snapshot.Id = NewId();
            item.Snapshot.StableKey = std::move(track.StableKey);
            item.Snapshot.SourceUrl = std::move(track.SourceUrl);
            item.Snapshot.Provider = std::move(track.Provider);
            item.Snapshot.Title = std::move(track.Title);
            item.Snapshot.Artist = std::move(track.Artist);
            item.Snapshot.Album = std::move(track.Album);
            item.Snapshot.ArtworkUrl = std::move(track.ArtworkUrl);
            job.Items.push_back(std::move(item));
        }
        if (job.Items.empty())
        {
            return false;
        }

        m_state->Jobs.push_back(std::move(job));
        ++m_state->Revision;
        Save(m_state);
        Pump(m_state);
        return true;
    }

    void DownloadManager::PauseJob(std::wstring const& jobId)
    {
        std::lock_guard lock{ m_state->Mutex };
        auto job = FindJob(*m_state, jobId);
        if (!job) return;
        for (auto& item : job->Items)
        {
            if (item.Snapshot.State == DownloadItemState::Downloading)
            {
                item.PauseRequested = true;
            }
            else if (item.Snapshot.State == DownloadItemState::Queued)
            {
                item.Snapshot.State = DownloadItemState::Paused;
            }
        }
        ++m_state->Revision;
        Save(m_state);
    }

    void DownloadManager::ResumeJob(std::wstring const& jobId)
    {
        std::lock_guard lock{ m_state->Mutex };
        auto job = FindJob(*m_state, jobId);
        if (!job) return;
        for (auto& item : job->Items)
        {
            if (item.Snapshot.State == DownloadItemState::Paused)
            {
                item.PauseRequested = false;
                item.Snapshot.State = DownloadItemState::Queued;
            }
            else if (item.Snapshot.State == DownloadItemState::Downloading)
            {
                item.PauseRequested = false;
            }
        }
        ++m_state->Revision;
        Save(m_state);
        Pump(m_state);
    }

    void DownloadManager::CancelJob(std::wstring const& jobId)
    {
        std::lock_guard lock{ m_state->Mutex };
        auto found = std::find_if(m_state->Jobs.begin(), m_state->Jobs.end(), [&](auto const& job)
        {
            return job.Id == jobId;
        });
        if (found == m_state->Jobs.end()) return;
        for (auto const& item : found->Items)
        {
            if (item.Snapshot.State != DownloadItemState::Completed)
            {
                std::error_code error;
                std::filesystem::remove(m_state->RootFolder / (L".lmp-part-" + item.Snapshot.Id), error);
            }
        }
        m_state->Jobs.erase(found);
        ++m_state->Revision;
        Save(m_state);
    }

    void DownloadManager::RetryJob(std::wstring const& jobId)
    {
        std::lock_guard lock{ m_state->Mutex };
        auto job = FindJob(*m_state, jobId);
        if (!job) return;
        for (auto& item : job->Items)
        {
            if (item.Snapshot.State == DownloadItemState::Failed)
            {
                item.Snapshot.State = DownloadItemState::Queued;
                item.Snapshot.Error.clear();
                item.PauseRequested = false;
            }
        }
        ++m_state->Revision;
        Save(m_state);
        Pump(m_state);
    }

    void DownloadManager::PauseAll()
    {
        std::lock_guard lock{ m_state->Mutex };
        m_state->AllPaused = true;
        for (auto& job : m_state->Jobs)
        {
            for (auto& item : job.Items)
            {
                if (item.Snapshot.State == DownloadItemState::Downloading)
                {
                    item.PauseRequested = true;
                }
                else if (item.Snapshot.State == DownloadItemState::Queued)
                {
                    item.Snapshot.State = DownloadItemState::Paused;
                }
            }
        }
        ++m_state->Revision;
        Save(m_state);
    }

    void DownloadManager::ResumeAll()
    {
        std::lock_guard lock{ m_state->Mutex };
        m_state->AllPaused = false;
        for (auto& job : m_state->Jobs)
        {
            for (auto& item : job.Items)
            {
                if (item.Snapshot.State == DownloadItemState::Paused)
                {
                    item.PauseRequested = false;
                    item.Snapshot.State = DownloadItemState::Queued;
                }
                else if (item.Snapshot.State == DownloadItemState::Downloading)
                {
                    item.PauseRequested = false;
                }
            }
        }
        ++m_state->Revision;
        Save(m_state);
        Pump(m_state);
    }

    void DownloadManager::ClearCompletedHistory()
    {
        std::lock_guard lock{ m_state->Mutex };
        for (auto& job : m_state->Jobs)
        {
            if (AggregateState(job) == DownloadItemState::Completed)
            {
                job.Hidden = true;
            }
        }
        ++m_state->Revision;
        Save(m_state);
    }

    void DownloadManager::DismissFailed(std::wstring const& jobId)
    {
        std::lock_guard lock{ m_state->Mutex };
        auto const found = std::find_if(m_state->Jobs.begin(), m_state->Jobs.end(), [&](auto const& job)
        {
            return job.Id == jobId && AggregateState(job) == DownloadItemState::Failed;
        });
        if (found != m_state->Jobs.end())
        {
            for (auto const& item : found->Items)
            {
                std::error_code error;
                std::filesystem::remove(m_state->RootFolder / (L".lmp-part-" + item.Snapshot.Id), error);
            }
        }
        std::erase_if(m_state->Jobs, [&](auto const& job)
        {
            return job.Id == jobId && AggregateState(job) == DownloadItemState::Failed;
        });
        ++m_state->Revision;
        Save(m_state);
    }

    void DownloadManager::RefreshScheduling()
    {
        Initialize();
        Pump(m_state);
    }

    bool DownloadManager::SetRootFolder(std::filesystem::path const& folder)
    {
        if (folder.empty()) return false;
        std::lock_guard lock{ m_state->Mutex };
        if (m_state->Running > 0) return false;
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error || !std::filesystem::is_directory(folder, error)) return false;
        m_state->RootFolder = folder;
        m_settings.SetString(L"DownloadFolder", winrt::hstring(folder.wstring()));
        RefreshStorageUsage(*m_state);
        ++m_state->Revision;
        Save(m_state);
        return true;
    }

    DownloadManagerSnapshot DownloadManager::Snapshot() const
    {
        const_cast<DownloadManager*>(this)->Initialize();
        std::lock_guard lock{ m_state->Mutex };
        DownloadManagerSnapshot result;
        result.RootFolder = m_state->RootFolder;
        result.Revision = m_state->Revision;
        result.OfflineBytes = m_state->OfflineBytes;
        result.OfflineTracks = m_state->OfflineTracks;
        result.AllPaused = m_state->AllPaused;
        result.SessionCompletedTracks = m_state->SessionCompletedTracks;
        result.SessionBytes = m_state->SessionBytes;
        result.SessionAverageBytesPerSecond = m_state->SessionTransferSeconds > 0.0
            ? static_cast<std::uint64_t>(
                static_cast<double>(m_state->SessionBytes) / m_state->SessionTransferSeconds)
            : 0;
        result.OnlyOnWifi = m_state->OnlyOnWifi;
        result.AutoDownloadLiked = m_state->AutoDownloadLiked;
        result.DownloadOnBattery = m_state->DownloadOnBattery;
        result.KeepRecentOffline = m_state->KeepRecentOffline;
        result.Jobs.reserve(m_state->Jobs.size());
        for (auto const& source : m_state->Jobs)
        {
            if (source.Hidden) continue;
            DownloadJobSnapshot job;
            job.Id = source.Id;
            job.Title = source.Title;
            job.Subtitle = source.Subtitle;
            job.ArtworkUrl = source.ArtworkUrl;
            job.State = AggregateState(source);
            job.Items.reserve(source.Items.size());
            for (auto const& item : source.Items)
            {
                job.Items.push_back(item.Snapshot);
                job.BytesDownloaded += item.Snapshot.BytesDownloaded;
                job.BytesTotal += item.Snapshot.BytesTotal;
                job.BytesPerSecond += item.Snapshot.BytesPerSecond;
                if (item.Snapshot.State == DownloadItemState::Completed)
                {
                    ++job.CompletedItems;
                }
                if (job.Error.empty() && item.Snapshot.State == DownloadItemState::Failed)
                {
                    job.Error = item.Snapshot.Error;
                }
            }
            result.Jobs.push_back(std::move(job));
        }
        return result;
    }

    std::wstring DownloadManager::ReadyPath(std::wstring const& stableKey) const
    {
        if (stableKey.empty()) return {};
        const_cast<DownloadManager*>(this)->Initialize();
        std::lock_guard lock{ m_state->Mutex };
        for (auto const& job : m_state->Jobs)
        {
            for (auto const& item : job.Items)
            {
                if (item.Snapshot.StableKey != stableKey
                    || item.Snapshot.State != DownloadItemState::Completed
                    || item.Snapshot.FilePath.empty())
                {
                    continue;
                }
                std::error_code error;
                if (std::filesystem::is_regular_file(item.Snapshot.FilePath, error))
                {
                    return item.Snapshot.FilePath;
                }
            }
        }
        return {};
    }

    void DownloadManager::SetOnlyOnWifi(bool value)
    {
        std::lock_guard lock{ m_state->Mutex };
        m_state->OnlyOnWifi = value;
        m_settings.SetBool(L"DownloadOnlyOnWifi", value);
        ++m_state->Revision;
        Pump(m_state);
    }

    void DownloadManager::SetAutoDownloadLiked(bool value)
    {
        std::lock_guard lock{ m_state->Mutex };
        m_state->AutoDownloadLiked = value;
        m_settings.SetBool(L"AutoDownloadLiked", value);
        ++m_state->Revision;
    }

    void DownloadManager::SetDownloadOnBattery(bool value)
    {
        std::lock_guard lock{ m_state->Mutex };
        m_state->DownloadOnBattery = value;
        m_settings.SetBool(L"DownloadOnBattery", value);
        ++m_state->Revision;
        Pump(m_state);
    }

    void DownloadManager::SetKeepRecentOffline(bool value)
    {
        std::lock_guard lock{ m_state->Mutex };
        m_state->KeepRecentOffline = value;
        m_settings.SetBool(L"KeepRecentOffline", value);
        ++m_state->Revision;
    }

    bool DownloadManager::SchedulingAllowed(std::shared_ptr<SharedState> const& state)
    {
        bool onlyOnWifi{};
        bool downloadOnBattery{};
        bool allPaused{};
        bool shuttingDown{};
        {
            std::lock_guard lock{ state->Mutex };
            onlyOnWifi = state->OnlyOnWifi;
            downloadOnBattery = state->DownloadOnBattery;
            allPaused = state->AllPaused;
            shuttingDown = state->Shutdown;
        }

        auto isWifi = true;
        if (onlyOnWifi)
        {
            auto const profile = WNC::NetworkInformation::GetInternetConnectionProfile();
            isWifi = profile && profile.IsWlanConnectionProfile();
        }
        auto const onBattery = WSP::PowerManager::PowerSupplyStatus()
            == WSP::PowerSupplyStatus::NotPresent;
        return DownloadSchedulingAllowed(
            onlyOnWifi,
            isWifi,
            downloadOnBattery,
            onBattery,
            allPaused,
            shuttingDown);
    }

    void DownloadManager::Pump(std::shared_ptr<SharedState> const& state)
    {
        if (!SchedulingAllowed(state)) return;
        auto const remoteScope = state->RemoteMusic->CaptureScope();
        auto const currentScopePrefix = L"download|"
            + RemoteScopeCacheKey(remoteScope)
            + L"\n";
        std::vector<std::tuple<std::wstring, std::wstring, std::uint64_t, std::uint64_t,
            UserDataOperationGate::Lease>> starts;
        {
            std::lock_guard lock{ state->Mutex };
            if (!state->Initialized || state->Shutdown || state->AllPaused) return;

            for (auto& job : state->Jobs)
            {
                for (auto& item : job.Items)
                {
                    if (state->Running >= kMaxConcurrentTransfers) break;
                    if (item.Snapshot.State != DownloadItemState::Queued) continue;
                    if (item.Snapshot.StableKey.starts_with(L"download|")
                        && !item.Snapshot.StableKey.starts_with(currentScopePrefix))
                    {
                        continue;
                    }
                    auto lease = state->OperationGate->TryEnter();
                    if (!lease) return;
                    item.Snapshot.State = DownloadItemState::Downloading;
                    item.Snapshot.Error.clear();
                    item.Snapshot.BytesPerSecond = 0;
                    item.PauseRequested = false;
                    auto const attemptId = ++item.AttemptId;
                    ++state->Running;
                    ++state->Revision;
                    starts.emplace_back(
                        job.Id,
                        item.Snapshot.Id,
                        state->Generation,
                        attemptId,
                        std::move(*lease));
                }
                if (state->Running >= kMaxConcurrentTransfers) break;
            }
            if (!starts.empty()) Save(state);
        }

        for (auto& start : starts)
        {
            StartDetached(TransferAsync(
                state,
                std::move(std::get<0>(start)),
                std::move(std::get<1>(start)),
                std::get<2>(start),
                std::get<3>(start),
                std::move(std::get<4>(start))));
        }
    }

    void DownloadManager::Load(std::shared_ptr<SharedState> const& state)
    {
        auto const path = StateFilePath();
        std::ifstream input(path, std::ios::binary);
        if (!input) return;
        std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        try
        {
            auto const root = WDJ::JsonObject::Parse(winrt::hstring(FromUtf8(bytes)));
            if (root.GetNamedNumber(L"schemaVersion", 0) != 1) return;
            auto const jobs = root.GetNamedArray(L"jobs", nullptr);
            if (!jobs) return;

            std::lock_guard lock{ state->Mutex };
            state->AllPaused = root.GetNamedBoolean(L"allPaused", false);
            state->Jobs.clear();
            for (auto const& value : jobs)
            {
                auto const object = value.GetObject();
                RuntimeJob job;
                job.Id = JsonString(object, L"id");
                job.Title = JsonString(object, L"title");
                job.Subtitle = JsonString(object, L"subtitle");
                job.ArtworkUrl = JsonString(object, L"artworkUrl");
                job.Hidden = object.GetNamedBoolean(L"hidden", false);
                auto const items = object.GetNamedArray(L"items", nullptr);
                if (job.Id.empty() || !items) continue;
                for (auto const& itemValue : items)
                {
                    auto const itemObject = itemValue.GetObject();
                    RuntimeItem item;
                    item.Snapshot.Id = JsonString(itemObject, L"id");
                    item.Snapshot.StableKey = JsonString(itemObject, L"stableKey");
                    item.Snapshot.SourceUrl = JsonString(itemObject, L"sourceUrl");
                    item.Snapshot.Provider = JsonString(itemObject, L"provider");
                    item.Snapshot.Title = JsonString(itemObject, L"title");
                    item.Snapshot.Artist = JsonString(itemObject, L"artist");
                    item.Snapshot.Album = JsonString(itemObject, L"album");
                    item.Snapshot.ArtworkUrl = JsonString(itemObject, L"artworkUrl");
                    item.Snapshot.FilePath = JsonString(itemObject, L"filePath");
                    item.Snapshot.Format = JsonString(itemObject, L"format");
                    item.Snapshot.Error = JsonString(itemObject, L"error");
                    item.Snapshot.State = ParseState(JsonString(itemObject, L"state"));
                    item.Snapshot.BytesDownloaded = static_cast<std::uint64_t>(itemObject.GetNamedNumber(L"bytesDownloaded", 0));
                    item.Snapshot.BytesTotal = static_cast<std::uint64_t>(itemObject.GetNamedNumber(L"bytesTotal", 0));
                    item.Snapshot.FinishedAtUnix = static_cast<std::int64_t>(itemObject.GetNamedNumber(L"finishedAt", 0));
                    if (!item.Snapshot.Id.empty() && !item.Snapshot.StableKey.empty())
                    {
                        job.Items.push_back(std::move(item));
                    }
                }
                if (!job.Items.empty()) state->Jobs.push_back(std::move(job));
            }
            ++state->Revision;
        }
        catch (...)
        {
            auto const quarantine = path.wstring() + L".corrupt." + std::to_wstring(UnixNow());
            MoveFileExW(path.c_str(), quarantine.c_str(), MOVEFILE_WRITE_THROUGH);
        }
    }

    void DownloadManager::Save(std::shared_ptr<SharedState> const& state)
    {
        WDJ::JsonObject root;
        WDJ::JsonArray jobs;
        {
            std::lock_guard lock{ state->Mutex };
            root.Insert(L"schemaVersion", WDJ::JsonValue::CreateNumberValue(1));
            root.Insert(L"allPaused", WDJ::JsonValue::CreateBooleanValue(state->AllPaused));
            for (auto const& source : state->Jobs)
            {
                WDJ::JsonObject job;
                InsertString(job, L"id", source.Id);
                InsertString(job, L"title", source.Title);
                InsertString(job, L"subtitle", source.Subtitle);
                InsertString(job, L"artworkUrl", source.ArtworkUrl);
                job.Insert(L"hidden", WDJ::JsonValue::CreateBooleanValue(source.Hidden));
                WDJ::JsonArray items;
                for (auto const& runtime : source.Items)
                {
                    auto const& sourceItem = runtime.Snapshot;
                    WDJ::JsonObject item;
                    InsertString(item, L"id", sourceItem.Id);
                    InsertString(item, L"stableKey", sourceItem.StableKey);
                    InsertString(item, L"sourceUrl", sourceItem.SourceUrl);
                    InsertString(item, L"provider", sourceItem.Provider);
                    InsertString(item, L"title", sourceItem.Title);
                    InsertString(item, L"artist", sourceItem.Artist);
                    InsertString(item, L"album", sourceItem.Album);
                    InsertString(item, L"artworkUrl", sourceItem.ArtworkUrl);
                    InsertString(item, L"filePath", sourceItem.FilePath);
                    InsertString(item, L"format", sourceItem.Format);
                    InsertString(item, L"error", sourceItem.Error);
                    InsertString(item, L"state", StateName(sourceItem.State));
                    item.Insert(L"bytesDownloaded", WDJ::JsonValue::CreateNumberValue(static_cast<double>(sourceItem.BytesDownloaded)));
                    item.Insert(L"bytesTotal", WDJ::JsonValue::CreateNumberValue(static_cast<double>(sourceItem.BytesTotal)));
                    item.Insert(L"finishedAt", WDJ::JsonValue::CreateNumberValue(static_cast<double>(sourceItem.FinishedAtUnix)));
                    items.Append(item);
                }
                job.Insert(L"items", items);
                jobs.Append(job);
            }
            root.Insert(L"jobs", jobs);
        }

        auto const path = StateFilePath();
        auto const temporary = std::filesystem::path(path.wstring() + L".tmp");
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return;
        auto const bytes = ToUtf8(std::wstring(root.Stringify().c_str()));
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        output.close();
        if (!output) return;
        if (!MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(temporary, error);
        }
    }

    winrt::Windows::Foundation::IAsyncAction DownloadManager::TransferAsync(
        std::shared_ptr<SharedState> state,
        std::wstring jobId,
        std::wstring itemId,
        std::uint64_t generation,
        std::uint64_t attemptId,
        UserDataOperationGate::Lease operationLease)
    {
        DownloadItemSnapshot itemSnapshot;
        std::filesystem::path rootFolder;
        RemoteScopeSnapshot remoteScope;
        {
            std::lock_guard lock{ state->Mutex };
            auto job = FindJob(*state, jobId);
            auto item = job ? FindItem(*job, itemId) : nullptr;
            if (!item || generation != state->Generation || attemptId != item->AttemptId)
            {
                co_return;
            }
            itemSnapshot = item->Snapshot;
            rootFolder = state->RootFolder;
            remoteScope = state->RemoteMusic->CaptureScope();
        }

        // Only the wall clock this transfer occupies feeds the session average,
        // so a queue that sat idle between two files does not look slow.
        auto const transferStartedAt = std::chrono::steady_clock::now();
        auto partPath = rootFolder / (L".lmp-part-" + itemId);
        auto complete = false;
        auto paused = false;
        auto canceled = false;
        std::wstring finalPath;
        std::wstring format;
        std::wstring safeError;

        auto controlState = [&]()
        {
            std::lock_guard lock{ state->Mutex };
            auto job = FindJob(*state, jobId);
            auto item = job ? FindItem(*job, itemId) : nullptr;
            if (!item || generation != state->Generation || attemptId != item->AttemptId)
            {
                return 2;
            }
            return item->PauseRequested || state->AllPaused ? 1 : 0;
        };

        try
        {
            auto const streamUrl = co_await state->RemoteMusic->ResolveStreamUrlAsync(
                remoteScope,
                winrt::hstring(itemSnapshot.SourceUrl),
                winrt::hstring(itemSnapshot.Provider));
            if (streamUrl.empty() || !state->RemoteMusic->IsCurrent(remoteScope))
            {
                safeError = L"Could not resolve this track";
            }
            else
            {
                std::error_code error;
                std::filesystem::create_directories(rootFolder, error);
                if (error)
                {
                    safeError = L"Download folder is unavailable";
                }
                else
                {
                    auto existingBytes = std::filesystem::is_regular_file(partPath, error)
                        ? std::filesystem::file_size(partPath, error)
                        : 0;
                    error.clear();

                    WWH::HttpClient client;
                    WWH::HttpRequestMessage request{ WWH::HttpMethod::Get(), winrt::Windows::Foundation::Uri(streamUrl) };
                    if (existingBytes > 0)
                    {
                        request.Headers().Append(L"Range", winrt::hstring(L"bytes=" + std::to_wstring(existingBytes) + L"-"));
                    }
                    auto response = co_await client.SendRequestAsync(
                        request,
                        WWH::HttpCompletionOption::ResponseHeadersRead);
                    if (!response.IsSuccessStatusCode())
                    {
                        safeError = L"The media server rejected the download";
                    }
                    else
                    {
                        auto const status = static_cast<std::uint32_t>(response.StatusCode());
                        auto const append = existingBytes > 0 && status == 206;
                        if (!append) existingBytes = 0;
                        auto const contentLength = response.Content().Headers().ContentLength();
                        auto const totalBytes = contentLength
                            ? existingBytes + contentLength.Value()
                            : 0;
                        auto const contentType = response.Content().Headers().ContentType();
                        format = DownloadExtensionForMediaType(contentType ? std::wstring(contentType.MediaType().c_str()) : std::wstring{});

                        auto const suffix = itemId.size() > 8 ? itemId.substr(itemId.size() - 8) : itemId;
                        finalPath = (rootFolder / (SanitizeFileName(itemSnapshot.Title) + L"-" + suffix + format)).wstring();
                        std::ofstream output(
                            partPath,
                            std::ios::binary | (append ? std::ios::app : std::ios::trunc));
                        if (!output)
                        {
                            safeError = L"Could not write to the download folder";
                        }
                        else
                        {
                            auto input = co_await response.Content().ReadAsInputStreamAsync();
                            auto downloaded = existingBytes;
                            auto lastBytes = downloaded;
                            auto lastSample = std::chrono::steady_clock::now();
                            while (safeError.empty())
                            {
                                auto const control = controlState();
                                if (control == 1)
                                {
                                    paused = true;
                                    break;
                                }
                                if (control == 2)
                                {
                                    canceled = true;
                                    break;
                                }

                                WSS::Buffer buffer{ kBufferSize };
                                auto const read = co_await input.ReadAsync(
                                    buffer,
                                    kBufferSize,
                                    WSS::InputStreamOptions::Partial);
                                if (read.Length() == 0)
                                {
                                    complete = true;
                                    break;
                                }
                                WSS::DataReader reader = WSS::DataReader::FromBuffer(read);
                                std::vector<std::uint8_t> bytes(read.Length());
                                reader.ReadBytes(bytes);
                                // std::ofstream's byte-oriented API accepts char
                                // storage. The vector owns the same raw bytes for
                                // this call, so no object or lifetime conversion occurs.
                                output.write(
                                    reinterpret_cast<char const*>(bytes.data()),
                                    static_cast<std::streamsize>(bytes.size()));
                                if (!output)
                                {
                                    safeError = L"Could not write to the download folder";
                                    break;
                                }
                                downloaded += bytes.size();

                                auto const now = std::chrono::steady_clock::now();
                                auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSample);
                                if (elapsed >= std::chrono::milliseconds(400))
                                {
                                    auto const speed = elapsed.count() > 0
                                        ? static_cast<std::uint64_t>((downloaded - lastBytes) * 1000 / elapsed.count())
                                        : 0;
                                    std::lock_guard lock{ state->Mutex };
                                    auto job = FindJob(*state, jobId);
                                    auto item = job ? FindItem(*job, itemId) : nullptr;
                                    if (item && generation == state->Generation && attemptId == item->AttemptId)
                                    {
                                        item->Snapshot.BytesDownloaded = downloaded;
                                        item->Snapshot.BytesTotal = totalBytes;
                                        item->Snapshot.BytesPerSecond = speed;
                                        ++state->Revision;
                                    }
                                    lastSample = now;
                                    lastBytes = downloaded;
                                }
                            }
                            output.flush();
                            output.close();
                            if (complete && safeError.empty() && !paused && !canceled)
                            {
                                std::filesystem::remove(finalPath, error);
                                error.clear();
                                std::filesystem::rename(partPath, finalPath, error);
                                if (error)
                                {
                                    complete = false;
                                    safeError = L"Could not publish the downloaded file";
                                }
                            }
                        }
                    }
                }
            }
        }
        catch (winrt::hresult_canceled const&)
        {
            canceled = true;
        }
        catch (...)
        {
            safeError = L"The download was interrupted";
        }

        operationLease = {};
        {
            std::lock_guard lock{ state->Mutex };
            auto job = FindJob(*state, jobId);
            auto item = job ? FindItem(*job, itemId) : nullptr;
            if (item && generation == state->Generation && attemptId == item->AttemptId)
            {
                if (complete)
                {
                    std::error_code error;
                    auto const size = std::filesystem::file_size(finalPath, error);
                    item->Snapshot.State = DownloadItemState::Completed;
                    item->Snapshot.FilePath = finalPath;
                    item->Snapshot.Format = format.starts_with(L".") ? format.substr(1) : format;
                    item->Snapshot.BytesDownloaded = error ? item->Snapshot.BytesDownloaded : size;
                    item->Snapshot.BytesTotal = item->Snapshot.BytesDownloaded;
                    item->Snapshot.BytesPerSecond = 0;
                    item->Snapshot.Error.clear();
                    item->Snapshot.FinishedAtUnix = UnixNow();
                    ++state->SessionCompletedTracks;
                    state->SessionBytes += item->Snapshot.BytesDownloaded;
                    state->SessionTransferSeconds += std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - transferStartedAt).count();
                    RefreshStorageUsage(*state);
                }
                else if (paused)
                {
                    item->Snapshot.State = DownloadItemState::Paused;
                    item->Snapshot.BytesPerSecond = 0;
                    item->PauseRequested = false;
                }
                else if (!canceled)
                {
                    item->Snapshot.State = DownloadItemState::Failed;
                    item->Snapshot.BytesPerSecond = 0;
                    item->Snapshot.Error = safeError.empty() ? L"The download was interrupted" : safeError;
                }
                if (state->Running > 0) --state->Running;
                ++state->Revision;
                Save(state);
            }
            else
            {
                if (state->Running > 0) --state->Running;
                if (canceled)
                {
                    std::error_code error;
                    std::filesystem::remove(partPath, error);
                }
            }
        }
        Pump(state);
    }
}
