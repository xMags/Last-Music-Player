#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/ProviderClient.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <shobjidl.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        // Extract a local audio file's embedded album art (or shell-cached
        // thumbnail) into a small on-disk cache file and return its
        // file:/// URI. BitmapImage can't decode embedded art directly
        // from an MP3/M4A, so we hand it a real JPEG it CAN decode. Cache
        // is keyed by a hash of the source path, so repeated scans are
        // cheap. On any failure we return an empty hstring and the caller
        // falls back to the generic placeholder glyph.
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
        EnsureLocalThumbnailUriAsync(winrt::Windows::Storage::StorageFile file)
        {
            try
            {
                auto cacheDir = AppDataDirectory() / L"thumbs";
                std::error_code ec;
                std::filesystem::create_directories(cacheDir, ec);

                std::wstring path{ file.Path().c_str() };
                auto h = std::hash<std::wstring>{}(path);
                auto cachePath = cacheDir / (std::to_wstring(h) + L".jpg");

                auto pathToFileUri = [](std::filesystem::path const& p)
                {
                    std::wstring s = p.wstring();
                    std::replace(s.begin(), s.end(), L'\\', L'/');
                    std::wstring out;
                    out.reserve(s.size() + 16);
                    for (wchar_t ch : s)
                    {
                        if (ch == L' ') out.append(L"%20");
                        else out.push_back(ch);
                    }
                    return winrt::hstring{ L"file:///" + out };
                };

                if (std::filesystem::exists(cachePath, ec) &&
                    std::filesystem::file_size(cachePath, ec) > 0)
                {
                    co_return pathToFileUri(cachePath);
                }

                auto thumb = co_await file.GetThumbnailAsync(
                    winrt::Windows::Storage::FileProperties::ThumbnailMode::MusicView,
                    256,
                    winrt::Windows::Storage::FileProperties::ThumbnailOptions::ResizeThumbnail);
                if (!thumb ||
                    thumb.Type() != winrt::Windows::Storage::FileProperties::ThumbnailType::Image ||
                    thumb.Size() == 0)
                {
                    co_return winrt::hstring{};
                }

                uint32_t total = static_cast<uint32_t>(thumb.Size());
                winrt::Windows::Storage::Streams::DataReader reader(thumb.GetInputStreamAt(0));
                co_await reader.LoadAsync(total);
                std::vector<uint8_t> bytes(total);
                reader.ReadBytes(bytes);

                std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
                if (!out) co_return winrt::hstring{};
                out.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
                out.close();
                if (!out)
                {
                    std::filesystem::remove(cachePath, ec);
                    co_return winrt::hstring{};
                }

                co_return pathToFileUri(cachePath);
            }
            catch (...) {}
            co_return winrt::hstring{};
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ChangeFolderButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        winrt::Windows::Storage::Pickers::FolderPicker picker;

        // Associate with window natively
        if (m_hwnd)
        {
            auto initializeWithWindow{ picker.as<IInitializeWithWindow>() };
            initializeWithWindow->Initialize(m_hwnd);
        }

        picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::MusicLibrary);
        picker.FileTypeFilter().Append(L"*"); // FolderPicker needs at least 1 extension

        winrt::Windows::Storage::StorageFolder folder = co_await picker.PickSingleFolderAsync();
        if (folder)
        {
            auto lease = UserDataOperationGateService().TryEnter();
            if (!lease)
            {
                co_return;
            }

            WriteAppSettingString(L"MusicLibraryPath", folder.Path());

            // Update the Settings TextBox
            MusicFolderPathBox().Text(folder.Path());
            lease.reset();

            co_await ScanLibraryAsync(folder);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OpenFolderButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        winrt::hstring path = MusicFolderPathBox().Text();
        if (!path.empty())
        {
            try
            {
                auto folder = co_await winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(path);
                co_await winrt::Windows::System::Launcher::LaunchFolderAsync(folder);
            }
            catch (...) { /* Folder may not exist */ }
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ScanMusicButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto savedPath = ReadAppSettingString(L"MusicLibraryPath");
        if (!savedPath.empty())
        {
            try
            {
                auto folder = co_await winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(savedPath);
                co_await ScanLibraryAsync(folder);
            }
            catch (...) { /* Folder may have been deleted */ }
        }
    }

    void MainWindow::PruneMissingLocalTracks()
    {
        auto lease = UserDataOperationGateService().TryEnter();
        if (!lease)
        {
            return;
        }

        // Local tracks are only deactivated during an explicit folder scan, and
        // that scan no-ops when the saved folder was deleted (GetFolderFromPath
        // throws). So without this, music removed from disk lingers forever in
        // History/Albums/mixes and can be queued. Runs once at startup on the
        // background thread: deactivate every local row whose file is gone by
        // reusing CompleteLocalScan (deactivate-all-local then reactivate the
        // survivors). Handles whole-folder deletion and individual files alike.
        if (!DatabaseService().IsInitialized())
        {
            return;
        }

        auto locals = DatabaseService().LoadTracks(false /*includeRemote*/, true /*activeOnly*/);
        if (locals.empty())
        {
            return; // streaming-only library — nothing to check
        }

        std::vector<std::wstring> survivors;
        survivors.reserve(locals.size());
        for (auto const& track : locals)
        {
            if (LocalFileMissing(track))
            {
                continue;
            }
            auto key = CatalogSourceKey(track);
            if (!key.empty())
            {
                survivors.push_back(std::move(key));
            }
        }

        if (survivors.size() == locals.size())
        {
            return; // every local file still present — no DB write needed
        }

        DatabaseService().CompleteLocalScan(survivors);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryAddFolder_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        co_await ChangeFolderButton_Click(sender, args);
        MarkLibraryViewsDirty();
        co_await HydrateLibraryTabAsync(L"Playlists", true);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::SongsRescan_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto savedPath = ReadAppSettingString(L"MusicLibraryPath");
        if (!savedPath.empty())
        {
            try
            {
                auto folder = co_await winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(savedPath);
                co_await ScanLibraryAsync(folder);
            }
            catch (...) {}
        }
    }

    void MainWindow::SetLibraryScanUi(bool visible, winrt::hstring const& status, bool canCancel)
    {
        auto visibility = visible
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

        if (SettingsScanCard())
        {
            SettingsScanCard().Visibility(visibility);
        }
        if (ScanStatusText() && !status.empty())
        {
            ScanStatusText().Text(status);
        }
        if (ScanCancelButton())
        {
            ScanCancelButton().IsEnabled(canCancel);
        }

        bool controlsEnabled = !visible;
        if (ScanMusicButton()) ScanMusicButton().IsEnabled(controlsEnabled);
        if (SongsRescanButton()) SongsRescanButton().IsEnabled(controlsEnabled);
        if (ChangeFolderButton()) ChangeFolderButton().IsEnabled(controlsEnabled);
        if (visible && SongsViewContainer().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible && !status.empty())
        {
            SongsSubtitle().Text(status);
        }
        else if (!visible && SongsViewContainer().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            UpdateSongsStats();
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MainWindow::ScanLibraryAsync(winrt::Windows::Storage::StorageFolder folder)
    {
        auto lifetime = get_strong();
        auto dispatcher = DispatcherQueue();
        if (!folder)
        {
            co_return false;
        }

        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            co_return false;
        }

        if (m_libraryScan.InProgress)
        {
            SetLibraryScanUi(true, L"A library scan is already running.", true);
            co_return false;
        }

        m_libraryScan.InProgress = true;
        m_libraryScan.CancelRequested = false;
        auto scanEpoch = ++m_libraryScan.Epoch;
        auto isCancelled = [this, scanEpoch]()
        {
            return m_libraryScan.CancelRequested || scanEpoch != m_libraryScan.Epoch;
        };
        auto finishScan = [this]()
        {
            m_libraryScan.CancelRequested = false;
            m_libraryScan.InProgress = false;
            SetLibraryScanUi(false, L"", false);
        };
        auto updateProgress = [this](uint32_t done, uint32_t total)
        {
            std::wstring text = L"Scanning " + std::to_wstring(done) + L" / " + std::to_wstring(total) + L" tracks...";
            SetLibraryScanUi(true, winrt::hstring{ text }, true);
        };

        try
        {
            SetLibraryScanUi(true, L"Scanning your music folder...", true);
            auto scanExtensions = ScanFileExtensions();
            auto queryOptions = winrt::Windows::Storage::Search::QueryOptions(
                winrt::Windows::Storage::Search::CommonFileQuery::OrderByName,
                scanExtensions
            );
            queryOptions.FolderDepth(winrt::Windows::Storage::Search::FolderDepth::Deep);
            auto queryResult = folder.CreateFileQueryWithOptions(queryOptions);
            auto files = co_await queryResult.GetFilesAsync();
            if (isCancelled())
            {
                finishScan();
                co_return false;
            }

            std::vector<LastMusicPlayer::Backend::TrackInfo> newLibrary;
            newLibrary.reserve(files.Size());

            int trackIndex = 0;
            uint32_t scannedCount = 0;
            uint32_t totalCount = files.Size();
            updateProgress(0, totalCount);
            for (auto const& file : files)
            {
                if (isCancelled())
                {
                    finishScan();
                    co_return false;
                }

                winrt::Windows::Storage::FileProperties::MusicProperties props{ nullptr };
                try
                {
                    props = co_await file.Properties().GetMusicPropertiesAsync();
                }
                catch (...)
                {
                    ++scannedCount;
                    continue;
                }
                if (isCancelled())
                {
                    finishScan();
                    co_return false;
                }

                // Per-property try blocks: each WinRT accessor can throw
                // independently (COM transients, locked file handle freed
                // mid-iteration, etc.). Pre-fix any one throw aborted the
                // whole iteration; now we keep whatever fields we got
                // and fall through to add a partially-populated track.
                LastMusicPlayer::Backend::TrackInfo track;
                winrt::hstring title;
                winrt::hstring artist;
                winrt::hstring album;
                winrt::hstring genre{ L"Unknown Genre" };
                double durSecs = 0.0;
                try { title = props.Title(); } catch (...) {}
                try { artist = props.Artist(); } catch (...) {}
                try { album = props.Album(); } catch (...) {}
                try
                {
                    auto genres = props.Genre();
                    if (genres && genres.Size() > 0) genre = genres.GetAt(0);
                }
                catch (...) {}
                try { durSecs = static_cast<double>(props.Duration().count()) / 10000000.0; } catch (...) {}

                if (title.empty())
                {
                    try { title = file.DisplayName(); } catch (...) {}
                }
                if (artist.empty()) artist = L"Unknown Artist";
                if (album.empty()) album = L"Unknown Album";

                track.Title(title);
                track.Artist(artist);
                try { track.File(file); } catch (...) {}
                try { track.FilePath(file.Path()); } catch (...) {}

                track.Album(album);
                track.Genre(genre);
                track.DurationSeconds(durSecs);
                track.Duration(LastMusicPlayer::Frontend::UIHelpers::FormatTime(durSecs));
                track.Index(++trackIndex);
                track.SourceKind(L"local");
                track.SourceLabel(L"Local");
                // Local tracks: extract embedded album art (or shell
                // thumbnail) into a disk-cached JPEG and hand BitmapImage
                // its URI. We deliberately leave AlbumArt null here so
                // CreateMusicArtworkBitmap's lazy UriSource decode runs
                // only when the track actually renders.
                winrt::hstring localThumbUri{};
                try { localThumbUri = co_await EnsureLocalThumbnailUriAsync(file); } catch (...) {}
                if (isCancelled())
                {
                    finishScan();
                    co_return false;
                }
                if (!localThumbUri.empty())
                {
                    try { track.ArtworkUrl(localThumbUri); } catch (...) {}
                }
                winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage noAlbumArt{ nullptr };
                try { ApplyMusicArtworkImage(track, noAlbumArt, L"track"); } catch (...) {}
                try
                {
                    auto created = file.DateCreated();
                    auto basic = co_await file.GetBasicPropertiesAsync();
                    if (isCancelled())
                    {
                        finishScan();
                        co_return false;
                    }

                    auto modified = basic.DateModified();
                    auto sortDate = modified > created ? modified : created;
                    track.DateAddedSortKey(static_cast<double>(winrt::clock::to_time_t(sortDate)));
                    std::time_t ct = winrt::clock::to_time_t(sortDate);
                    std::tm tmv{};
                    localtime_s(&tmv, &ct);
                    wchar_t dbuf[32];
                    if (std::wcsftime(dbuf, 32, L"%b %d %Y", &tmv))
                    {
                        track.DateAdded(winrt::hstring(dbuf));
                    }
                }
                catch (...) {}

                newLibrary.push_back(track);
                ++scannedCount;
                if (scannedCount == totalCount || scannedCount % 25 == 0)
                {
                    updateProgress(scannedCount, totalCount);
                }
            }

            if (isCancelled())
            {
                finishScan();
                co_return false;
            }


            SetLibraryScanUi(true, L"Updating library database...", false);
            auto persistedLibrary = std::move(newLibrary);
            bool dbSucceeded = true;
            co_await winrt::resume_background();
            try
            {
                DatabaseService().BeginLocalScan();
                std::vector<std::wstring> activeSourceKeys;
                activeSourceKeys.reserve(persistedLibrary.size());
                for (auto& track : persistedLibrary)
                {
                    auto sourceKey = CatalogSourceKey(track);
                    track.IsLiked(DatabaseService().IsLiked(sourceKey));
                    track.CatalogId(DatabaseService().UpsertLocalTrack(track, sourceKey));
                    if (!sourceKey.empty())
                    {
                        activeSourceKeys.push_back(sourceKey);
                    }
                }
                DatabaseService().CompleteLocalScan(activeSourceKeys);
            }
            catch (...)
            {
                dbSucceeded = false;
            }
            co_await wil::resume_foreground(dispatcher);


            if (!dbSucceeded || isCancelled())
            {
                finishScan();
                co_return false;
            }

            newLibrary = std::move(persistedLibrary);

            winrt::Last_Music_Player::TrackInfo currentTrack{ nullptr };
            try { currentTrack = AudioPlayerService().GetCurrentTrack(); } catch (...) {}
            std::wstring currentKey;
            // CatalogSourceKey dereferences track.FilePath() etc. without a
            // null check. Pre-fix: when nothing was playing the call AV'd
            // and the user saw "Scan crashed" with no breadcrumb past
            // "DB persist OK". Guard the projected-null case explicitly.
            if (currentTrack)
            {
                try { currentKey = CatalogSourceKey(currentTrack); } catch (...) {}
            }
            int currentIndex = -1;
            if (!currentKey.empty())
            {
                try
                {
                    for (size_t i = 0; i < newLibrary.size(); ++i)
                    {
                        if (CatalogSourceKey(newLibrary[i]) == currentKey)
                        {
                            currentIndex = static_cast<int>(i);
                            break;
                        }
                    }
                }
                catch (...) {}
            }

            try
            {
                m_queue.CurrentPlaylist = newLibrary;
                m_queue.CurrentTrackIndex = currentIndex;
            }
            catch (...) {}
            if (m_queue.Queue.empty())
            {

                try { RebuildUpNextQueue(); } catch (...) {}

            }

            try { MarkLibraryViewsDirty(); } catch (...) {}

            try { co_await HydrateHomeAsync(false); } catch (...) {}

            if (isCancelled())
            {
                finishScan();
                co_return false;
            }
            if (SongsViewContainer().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
            {

                try { ApplySongsFilterSort(); } catch (...) {}

            }
            if (LibraryViewContainer().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
            {

                try { co_await HydrateLibraryTabAsync(L"Playlists", true); } catch (...) {}

            }
            finishScan();
            co_return true;
        }
        catch (...)
        {
            finishScan();
            co_return false;
        }
    }

    void MainWindow::ScanCancel_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_libraryScan.InProgress)
        {
            m_libraryScan.CancelRequested = true;
            ++m_libraryScan.Epoch;
            SetLibraryScanUi(true, L"Cancelling scan...", false);
            return;
        }

        SetLibraryScanUi(false, L"", false);
    }

    void MainWindow::FormatChip_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_loadingSettings)
        {
            return;
        }
        SettingsManagerService().SetString(L"ScanFormats", winrt::hstring{ ScanFormatsCsvFromChips() });
    }

    std::wstring MainWindow::ScanFormatsCsvFromChips()
    {
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton chips[] = {
            FmtMp3(), FmtFlac(), FmtWav(), FmtM4a(), FmtAac(), FmtOgg(), FmtOpus(), FmtWma()
        };
        std::wstring csv;
        for (auto const& c : chips)
        {
            if (c && c.IsChecked() && c.IsChecked().Value())
            {
                auto tag = ReadTagString(c.Tag());
                if (!tag.empty())
                {
                    if (!csv.empty())
                    {
                        csv += L",";
                    }
                    csv += tag.c_str();
                }
            }
        }
        return csv;
    }

    std::vector<winrt::hstring> MainWindow::ScanFileExtensions()
    {
        std::wstring csv{ SettingsManagerService().GetString(L"ScanFormats", L".mp3,.flac").c_str() };
        std::vector<winrt::hstring> exts;
        size_t start = 0;
        while (start <= csv.size())
        {
            auto comma = csv.find(L',', start);
            auto token = csv.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
            token.erase(token.begin(), std::find_if(token.begin(), token.end(), [](wchar_t ch) { return !std::iswspace(ch); }));
            token.erase(std::find_if(token.rbegin(), token.rend(), [](wchar_t ch) { return !std::iswspace(ch); }).base(), token.end());
            std::transform(token.begin(), token.end(), token.begin(), [](wchar_t ch)
            {
                return static_cast<wchar_t>(std::towlower(ch));
            });
            if (!token.empty())
            {
                if (token.front() != L'.')
                {
                    token.insert(token.begin(), L'.');
                }
                exts.push_back(winrt::hstring{ token });
            }
            if (comma == std::wstring::npos)
            {
                break;
            }
            start = comma + 1;
        }
        if (exts.empty())
        {
            exts.push_back(L".mp3");
            exts.push_back(L".flac");
        }
        return exts;
    }

}
