#include "pch.h"
#include "Backend/StreamCache.h"

#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        // Map the upstream Content-Type to a container extension the
        // MediaPlayer recognises. A wrong/unknown type falls back to .bin —
        // the player still sniffs the byte signature for the common formats,
        // but a correct extension avoids ambiguity.
        std::wstring ExtensionForContentType(winrt::hstring const& mediaType)
        {
            std::wstring t{ mediaType.c_str() };
            std::transform(t.begin(), t.end(), t.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            if (t.find(L"mpeg") != std::wstring::npos || t.find(L"mp3") != std::wstring::npos) return L".mp3";
            if (t.find(L"mp4") != std::wstring::npos || t.find(L"m4a") != std::wstring::npos || t.find(L"aac") != std::wstring::npos) return L".m4a";
            if (t.find(L"webm") != std::wstring::npos) return L".webm";
            if (t.find(L"ogg") != std::wstring::npos || t.find(L"opus") != std::wstring::npos) return L".ogg";
            if (t.find(L"wav") != std::wstring::npos) return L".wav";
            if (t.find(L"flac") != std::wstring::npos) return L".flac";
            return L".bin";
        }

        class DownloadCompletionGuard final
        {
        public:
            DownloadCompletionGuard(
                std::atomic_uint32_t& active,
                UserDataOperationGate::Lease operationLease) noexcept
                : m_active(active),
                  m_operationLease(std::move(operationLease))
            {
            }

            DownloadCompletionGuard(DownloadCompletionGuard const&) = delete;
            DownloadCompletionGuard& operator=(DownloadCompletionGuard const&) = delete;

            ~DownloadCompletionGuard()
            {
                // Clear() checks the active count after WaitForIdle(). Publish the
                // count first, then let the lease member release and signal idle.
                m_active.fetch_sub(1, std::memory_order_release);
            }

        private:
            std::atomic_uint32_t& m_active;
            UserDataOperationGate::Lease m_operationLease;
        };

        class HttpStreamCacheTransport final : public IStreamCacheTransport
        {
        public:
            winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> DownloadAsync(
                std::wstring streamUrl,
                std::filesystem::path partPath) override
            {
                namespace WWH = winrt::Windows::Web::Http;
                WWH::HttpClient client;
                auto uri = winrt::Windows::Foundation::Uri{ winrt::hstring{ streamUrl } };
                auto response = co_await client.GetAsync(
                    uri,
                    WWH::HttpCompletionOption::ResponseHeadersRead);
                if (!response.IsSuccessStatusCode())
                {
                    co_return winrt::hstring{};
                }

                std::wstring extension = L".bin";
                try
                {
                    auto contentType = response.Content().Headers().ContentType();
                    if (contentType)
                    {
                        extension = ExtensionForContentType(contentType.MediaType());
                    }
                }
                catch (...)
                {
                }

                auto input = co_await response.Content().ReadAsInputStreamAsync();
                auto folder = co_await winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(
                    winrt::hstring{ partPath.parent_path().wstring() });
                auto file = co_await folder.CreateFileAsync(
                    winrt::hstring{ partPath.filename().wstring() },
                    winrt::Windows::Storage::CreationCollisionOption::ReplaceExisting);
                auto output = co_await file.OpenAsync(
                    winrt::Windows::Storage::FileAccessMode::ReadWrite);
                co_await winrt::Windows::Storage::Streams::RandomAccessStream::CopyAsync(
                    input,
                    output.GetOutputStreamAt(0));
                co_await output.FlushAsync();
                output.Close();
                input.Close();
                co_return winrt::hstring{ extension };
            }
        };
    }

    struct StreamCache::SharedState final
    {
        explicit SharedState(std::shared_ptr<IStreamCacheTransport> cacheTransport)
            : Transport(std::move(cacheTransport))
        {
        }

        std::shared_ptr<IStreamCacheTransport> Transport;
        std::mutex Mutex;
        std::unordered_map<std::wstring, Entry> Entries;
        std::vector<winrt::Windows::Foundation::IAsyncAction> RetiredActions;
        std::atomic_uint32_t ActiveDownloads{ 0 };
        std::uint64_t Generation{ 0 };
        std::uint64_t NextAttemptId{ 1 };
    };

    StreamCache::StreamCache(UserDataOperationGate& operationGate)
        : StreamCache(operationGate, std::make_shared<HttpStreamCacheTransport>())
    {
    }

    StreamCache::StreamCache(
        UserDataOperationGate& operationGate,
        std::shared_ptr<IStreamCacheTransport> transport)
        : m_operationGate(operationGate),
          m_state(std::make_shared<SharedState>(std::move(transport)))
    {
        if (!m_state->Transport)
        {
            throw std::invalid_argument("Stream cache transport is required.");
        }
    }

    std::filesystem::path StreamCache::CacheDir()
    {
        std::filesystem::path base;
        wchar_t* localAppData{};
        size_t length{};
        if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 && localAppData && *localAppData)
        {
            base = std::filesystem::path{ localAppData } / L"Last Music Player";
        }
        else
        {
            base = std::filesystem::current_path() / L"Last Music Player";
        }
        std::free(localAppData);

        return base / L"stream-cache";
    }

    std::filesystem::path StreamCache::EnsureCacheDir()
    {
        auto dir = CacheDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    // Same FNV-1a-over-UTF16 as Backend::StableFnv1a64 (ProviderHelpers) so the
    // filename is stable and collision-resistant across sessions.
    std::wstring StreamCache::HashKey(std::wstring const& sourceKey)
    {
        uint64_t hash = 1469598103934665603ull;
        for (wchar_t ch : sourceKey)
        {
            auto code = static_cast<uint32_t>(ch);
            for (int shift = 0; shift < 16; shift += 8)
            {
                hash ^= static_cast<uint8_t>((code >> shift) & 0xFF);
                hash *= 1099511628211ull;
            }
        }
        wchar_t buf[17]{};
        swprintf_s(buf, L"%016llx", static_cast<unsigned long long>(hash));
        return buf;
    }

    std::wstring StreamCache::FindOnDisk(std::wstring const& hash)
    {
        std::error_code ec;
        auto dir = CacheDir();
        std::wstring newestPath;
        std::filesystem::file_time_type newestWriteTime{};
        bool found{};
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (ec) break;
            std::error_code fe;
            if (!entry.is_regular_file(fe)) continue;

            auto filename = entry.path().filename().wstring();
            if (!filename.starts_with(hash + L".")
                || entry.path().extension() == L".part")
            {
                continue;
            }

            auto writeTime = entry.last_write_time(fe);
            if (fe) continue;
            if (!found || writeTime > newestWriteTime)
            {
                newestPath = entry.path().wstring();
                newestWriteTime = writeTime;
                found = true;
            }
        }
        return newestPath;
    }

    std::wstring StreamCache::ReadyPath(std::wstring const& sourceKey)
    {
        if (sourceKey.empty()) return {};

        std::uint64_t generation{};
        std::uint64_t attemptId{};
        std::wstring readyPath;
        {
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            generation = m_state->Generation;
            auto it = m_state->Entries.find(sourceKey);
            if (it != m_state->Entries.end())
            {
                if (it->second.status == Status::Ready)
                {
                    readyPath = it->second.path;
                    attemptId = it->second.attemptId;
                }
                else if (it->second.status == Status::InFlight
                    || it->second.status == Status::Publishing)
                {
                    return {};
                }
                // Failed -> fall through and re-check disk (a prior session may
                // have a file even though this session's attempt failed).
            }
        }

        if (!readyPath.empty())
        {
            std::error_code existsError;
            auto exists = std::filesystem::is_regular_file(readyPath, existsError);
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            auto it = m_state->Entries.find(sourceKey);
            if (generation != m_state->Generation
                || it == m_state->Entries.end()
                || it->second.status != Status::Ready
                || it->second.attemptId != attemptId
                || it->second.path != readyPath)
            {
                return {};
            }
            if (exists)
            {
                return readyPath;
            }
            m_state->Entries.erase(it);
        }

        auto onDisk = FindOnDisk(HashKey(sourceKey));
        if (onDisk.empty())
        {
            return {};
        }

        std::lock_guard<std::mutex> lock(m_state->Mutex);
        if (generation != m_state->Generation)
        {
            return {};
        }
        auto it = m_state->Entries.find(sourceKey);
        if (it != m_state->Entries.end()
            && it->second.status != Status::Failed)
        {
            return {};
        }
        m_state->Entries[sourceKey] = Entry{ Status::Ready, onDisk };
        return onDisk;
    }

    void StreamCache::Prefetch(std::wstring const& sourceKey, std::wstring const& streamUrl)
    {
        if (sourceKey.empty() || streamUrl.empty()) return;

        auto operationLease = m_operationGate.TryEnter();
        if (!operationLease)
        {
            return;
        }

        // A previously-downloaded file (this or a prior session) means nothing
        // to do. Revalidate the cache generation after the disk lookup so a
        // concurrent scope invalidation cannot adopt a stale lookup result.
        std::uint64_t lookupGeneration{};
        {
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            lookupGeneration = m_state->Generation;
        }
        auto existing = FindOnDisk(HashKey(sourceKey));
        if (!existing.empty())
        {
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            if (lookupGeneration != m_state->Generation)
            {
                return;
            }
            auto it = m_state->Entries.find(sourceKey);
            if (it == m_state->Entries.end()
                || it->second.status == Status::Failed)
            {
                m_state->Entries[sourceKey] = Entry{ Status::Ready, existing };
            }
            return;
        }

        std::uint64_t generation{};
        std::uint64_t attemptId{};
        {
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            if (lookupGeneration != m_state->Generation)
            {
                return;
            }
            auto it = m_state->Entries.find(sourceKey);
            if (it != m_state->Entries.end()
                && (it->second.status == Status::Ready
                    || it->second.status == Status::InFlight
                    || it->second.status == Status::Publishing))
            {
                return;
            }

            if (m_state->ActiveDownloads.load(std::memory_order_acquire) >= kMaxInFlight)
            {
                return;
            }

            generation = m_state->Generation;
            attemptId = m_state->NextAttemptId++;
            m_state->ActiveDownloads.fetch_add(1, std::memory_order_acq_rel);
            m_state->Entries[sourceKey] = Entry{ Status::InFlight, {}, attemptId };
        }

        winrt::Windows::Foundation::IAsyncAction action{ nullptr };
        try
        {
            action = DownloadAsync(
                m_state,
                sourceKey,
                streamUrl,
                generation,
                attemptId,
                std::move(*operationLease));
        }
        catch (...)
        {
            m_state->ActiveDownloads.fetch_sub(1, std::memory_order_release);
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            auto it = m_state->Entries.find(sourceKey);
            if (it != m_state->Entries.end() && it->second.attemptId == attemptId)
            {
                it->second = Entry{ Status::Failed };
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            auto it = m_state->Entries.find(sourceKey);
            if (it != m_state->Entries.end()
                && it->second.status == Status::InFlight
                && it->second.attemptId == attemptId
                && generation == m_state->Generation)
            {
                it->second.action = action;
            }
            else
            {
                m_state->RetiredActions.push_back(action);
            }
        }
    }

    winrt::Windows::Foundation::IAsyncAction StreamCache::DownloadAsync(
        std::shared_ptr<SharedState> state,
        std::wstring sourceKey,
        std::wstring streamUrl,
        std::uint64_t generation,
        std::uint64_t attemptId,
        UserDataOperationGate::Lease operationLease)
    {
        DownloadCompletionGuard completion{
            state->ActiveDownloads,
            std::move(operationLease)
        };
        co_await winrt::resume_background();

        {
            std::lock_guard<std::mutex> lock(state->Mutex);
            auto it = state->Entries.find(sourceKey);
            if (generation != state->Generation
                || it == state->Entries.end()
                || it->second.status != Status::InFlight
                || it->second.attemptId != attemptId)
            {
                co_return;
            }
        }

        auto hash = HashKey(sourceKey);
        auto attemptSuffix = L"." + std::to_wstring(generation)
            + L"." + std::to_wstring(attemptId);
        auto partName = hash + attemptSuffix + L".part";
        auto dir = EnsureCacheDir();
        auto partPath = dir / partName;
        std::wstring extension;
        std::wstring finalPath;
        bool transferComplete{};
        bool publicationReserved{};
        bool renamed{};
        bool published{};

        try
        {
            auto downloadedExtension = co_await state->Transport->DownloadAsync(streamUrl, partPath);
            if (!downloadedExtension.empty())
            {
                extension = downloadedExtension.c_str();
                transferComplete = true;
            }
        }
        catch (...)
        {
            transferComplete = false;
        }

        if (transferComplete)
        {
            {
                std::lock_guard<std::mutex> lock(state->Mutex);
                auto it = state->Entries.find(sourceKey);
                if (generation == state->Generation
                    && it != state->Entries.end()
                    && it->second.status == Status::InFlight
                    && it->second.attemptId == attemptId)
                {
                    it->second.status = Status::Publishing;
                    publicationReserved = true;
                }
            }

            if (publicationReserved)
            {
                finalPath = (dir / (hash + attemptSuffix + extension)).wstring();
                std::error_code renameError;
                std::filesystem::rename(partPath, finalPath, renameError);
                renamed = !renameError;

                if (renamed)
                {
                    std::lock_guard<std::mutex> lock(state->Mutex);
                    auto it = state->Entries.find(sourceKey);
                    if (generation == state->Generation
                        && it != state->Entries.end()
                        && it->second.status == Status::Publishing
                        && it->second.attemptId == attemptId)
                    {
                        auto action = it->second.action;
                        it->second = Entry{ Status::Ready, finalPath, attemptId, action };
                        published = true;
                    }
                }
            }
        }

        if (!published)
        {
            std::error_code removeError;
            std::filesystem::remove(partPath, removeError);
            if (renamed)
            {
                removeError.clear();
                std::filesystem::remove(finalPath, removeError);
            }

            std::lock_guard<std::mutex> lock(state->Mutex);
            auto it = state->Entries.find(sourceKey);
            if (generation == state->Generation
                && it != state->Entries.end()
                && (it->second.status == Status::InFlight
                    || it->second.status == Status::Publishing)
                && it->second.attemptId == attemptId)
            {
                auto action = it->second.action;
                it->second = Entry{ Status::Failed, {}, attemptId, action };
            }
        }
        else
        {
            TrimToCap(state);
        }
    }

    void StreamCache::TrimToCap(std::shared_ptr<SharedState> const& state)
    {
        std::error_code ec;
        auto dir = CacheDir();

        struct FileInfo { std::filesystem::path path; unsigned long long size; std::filesystem::file_time_type writeTime; };
        std::vector<FileInfo> files;
        unsigned long long total = 0;
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (ec) break;
            std::error_code fe;
            if (!entry.is_regular_file(fe)) continue;
            if (entry.path().extension() == L".part") continue;
            auto sz = static_cast<unsigned long long>(entry.file_size(fe));
            if (fe) continue;
            auto wt = entry.last_write_time(fe);
            if (fe) continue;
            files.push_back({ entry.path(), sz, wt });
            total += sz;
        }

        if (files.size() <= kMaxFiles && total <= kMaxBytes) return;

        // Evict oldest-first until back under both the file-count and byte caps.
        std::sort(files.begin(), files.end(),
            [](FileInfo const& a, FileInfo const& b) { return a.writeTime < b.writeTime; });

        size_t count = files.size();
        for (auto const& f : files)
        {
            if (count <= kMaxFiles && total <= kMaxBytes) break;
            std::error_code re;
            std::filesystem::remove(f.path, re);
            if (re) continue;
            --count;
            total -= (f.size <= total ? f.size : total);

            // Forget any in-memory entry that pointed at this file so a later
            // ReadyPath misses and the track gets re-prefetched if needed.
            std::lock_guard<std::mutex> lock(state->Mutex);
            for (auto it = state->Entries.begin(); it != state->Entries.end(); )
            {
                if (it->second.path == f.path.wstring()) it = state->Entries.erase(it);
                else ++it;
            }
        }
    }

    void StreamCache::PruneOnStartup()
    {
        std::error_code ec;
        auto dir = CacheDir();
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (ec) break;
            std::error_code fe;
            if (entry.is_regular_file(fe) && entry.path().extension() == L".part")
            {
                std::error_code re;
                std::filesystem::remove(entry.path(), re);
            }
        }
        TrimToCap(m_state);
    }

    void StreamCache::InvalidateInFlight()
    {
        std::vector<winrt::Windows::Foundation::IAsyncAction> retiredActions;
        {
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            ++m_state->Generation;
            retiredActions.swap(m_state->RetiredActions);
            for (auto const& item : m_state->Entries)
            {
                if ((item.second.status == Status::InFlight
                        || item.second.status == Status::Publishing)
                    && item.second.action)
                {
                    retiredActions.push_back(item.second.action);
                }
            }
            m_state->Entries.clear();
        }

        retiredActions.erase(
            std::remove_if(
                retiredActions.begin(),
                retiredActions.end(),
                [](winrt::Windows::Foundation::IAsyncAction const& action)
                {
                    try
                    {
                        return !action
                            || action.Status()
                                != winrt::Windows::Foundation::AsyncStatus::Started;
                    }
                    catch (...)
                    {
                        return true;
                    }
                }),
            retiredActions.end());

        if (!retiredActions.empty())
        {
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            m_state->RetiredActions.insert(
                m_state->RetiredActions.end(),
                retiredActions.begin(),
                retiredActions.end());
        }
    }

    bool StreamCache::Clear()
    {
        if (m_state->ActiveDownloads.load(std::memory_order_acquire) != 0)
        {
            return false;
        }

        std::filesystem::path dir;
        {
            std::lock_guard<std::mutex> lock(m_state->Mutex);
            if (m_state->ActiveDownloads.load(std::memory_order_acquire) != 0)
            {
                return false;
            }
            ++m_state->Generation;
            m_state->Entries.clear();
            m_state->RetiredActions.clear();
            dir = CacheDir();
        }

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        if (ec)
        {
            return false;
        }

        std::error_code existsError;
        auto remains = std::filesystem::exists(dir, existsError);
        return !existsError && !remains;
    }

    StreamCacheUsage StreamCache::Usage() const
    {
        StreamCacheUsage usage;
        std::error_code error;
        auto const directory = CacheDir();
        if (!std::filesystem::is_directory(directory, error))
        {
            return usage;
        }
        for (std::filesystem::directory_iterator it{ directory, error }, end;
             !error && it != end;
             it.increment(error))
        {
            if (!it->is_regular_file(error) || it->path().extension() == L".part")
            {
                error.clear();
                continue;
            }
            auto const size = it->file_size(error);
            if (!error)
            {
                usage.Bytes += size;
                ++usage.Files;
            }
            error.clear();
        }
        return usage;
    }
}
