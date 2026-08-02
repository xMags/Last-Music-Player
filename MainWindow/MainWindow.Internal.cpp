#include "pch.h"
#include "MainWindow.Internal.h"

#include "Backend/BuildConfig.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace winrt::Last_Music_Player::implementation::detail
{    static WNDPROC s_originalMainWindowProc = nullptr;

    static LRESULT CALLBACK MainWindowMinSizeProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_GETMINMAXINFO)
        {
            auto info = reinterpret_cast<MINMAXINFO*>(lParam);
            if (info)
            {
                info->ptMinTrackSize.x = kDefaultWindowWidth;
                info->ptMinTrackSize.y = kDefaultWindowHeight;
            }
        }

        return s_originalMainWindowProc
            ? CallWindowProcW(s_originalMainWindowProc, hwnd, message, wParam, lParam)
            : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void InstallMinimumWindowSize(HWND hwnd)
    {
        if (!hwnd || s_originalMainWindowProc)
        {
            return;
        }

        auto currentProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
        if (currentProc && currentProc != MainWindowMinSizeProc)
        {
            s_originalMainWindowProc = currentProc;
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MainWindowMinSizeProc));
        }
    }

    void RunDetached(winrt::Windows::Foundation::IAsyncAction action)
    {
        [](winrt::Windows::Foundation::IAsyncAction action) -> winrt::fire_and_forget
        {
            try
            {
                co_await action;
            }
            catch (...)
            {
                ::OutputDebugStringW(L"[LastMusicPlayer] Detached async action failed.\n");
            }
        }(std::move(action));
    }

    // Services are lazy so no WinRT-adjacent object graph is constructed during
    // process static initialization, before the app apartment is ready.
    LastMusicPlayer::Backend::AudioPlayer& AudioPlayerService()
    {
        static LastMusicPlayer::Backend::AudioPlayer service;
        return service;
    }

    LastMusicPlayer::Backend::SettingsManager& SettingsManagerService()
    {
        static LastMusicPlayer::Backend::SettingsManager service;
        return service;
    }
    LastMusicPlayer::Backend::CredentialStore& CredentialStoreService()
    {
        static LastMusicPlayer::Backend::CredentialStore service;
        return service;
    }

    LastMusicPlayer::Backend::UserDataOperationGate& UserDataOperationGateService()
    {
        static LastMusicPlayer::Backend::UserDataOperationGate service;
        return service;
    }

    LastMusicPlayer::Backend::AccountClient& AccountClientService()
    {
        static LastMusicPlayer::Backend::AccountClient service;
        return service;
    }

    LastMusicPlayer::Backend::IAccountSessionGateway& AccountSessionGatewayService()
    {
        static auto service = LastMusicPlayer::Backend::CreateAccountSessionGateway(AccountClientService());
        return *service;
    }

    LastMusicPlayer::Backend::AccountSessionService& AccountSessionService()
    {
        static LastMusicPlayer::Backend::AccountSessionService service{
            SettingsManagerService(),
            CredentialStoreService(),
            AccountSessionGatewayService(),
            UserDataOperationGateService()
        };
        return service;
    }

    LastMusicPlayer::Backend::RemoteMusicService& RemoteMusicServiceService()
    {
        static LastMusicPlayer::Backend::RemoteMusicService service{
            SettingsManagerService(),
            CredentialStoreService(),
            AccountSessionService(),
            AccountClientService(),
            AccountClientService()
        };
        return service;
    }

    LastMusicPlayer::Backend::MusicSyncService& MusicSyncServiceService()
    {
        static LastMusicPlayer::Backend::MusicSyncService service{
            RemoteMusicServiceService(),
            DatabaseService(),
            UserDataOperationGateService()
        };
        return service;
    }



    LastMusicPlayer::Backend::DatabaseEngine& DatabaseService()
    {
        static LastMusicPlayer::Backend::DatabaseEngine service;
        return service;
    }

    LastMusicPlayer::Backend::StreamCache& StreamCacheService()
    {
        static LastMusicPlayer::Backend::StreamCache service{
            UserDataOperationGateService()
        };
        return service;
    }

    LastMusicPlayer::Frontend::NavigationService& NavigationService()
    {
        static LastMusicPlayer::Frontend::NavigationService service;
        return service;
    }

    std::wstring ToLowerCopy(winrt::hstring const& value)
    {
        std::wstring lowered{ value.c_str() };
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return lowered;
    }

    bool ContainsFolded(winrt::hstring const& haystack, winrt::hstring const& needle)
    {
        auto loweredHaystack = ToLowerCopy(haystack);
        auto loweredNeedle = ToLowerCopy(needle);
        return !loweredNeedle.empty() && loweredHaystack.find(loweredNeedle) != std::wstring::npos;
    }

    winrt::hstring TrimQuery(winrt::hstring const& value)
    {
        std::wstring text{ value.c_str() };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](wchar_t ch) { return !std::iswspace(ch); }));
        text.erase(std::find_if(text.rbegin(), text.rend(), [](wchar_t ch) { return !std::iswspace(ch); }).base(), text.end());
        return winrt::hstring{ text };
    }

    bool IsHttpUrl(winrt::hstring const& value)
    {
        std::wstring text{ value.c_str() };
        return text.rfind(L"http://", 0) == 0 || text.rfind(L"https://", 0) == 0;
    }


    winrt::hstring CanonicalProviderCollectionSourceUrl(winrt::hstring const& value)
    {
        auto trimmed = TrimQuery(value);
        return IsHttpUrl(trimmed) ? trimmed : winrt::hstring{};
    }

    std::wstring CanonicalQueueText(winrt::hstring const& value)
    {
        auto lowered = ToLowerCopy(value);
        std::wstring canonical;
        canonical.reserve(lowered.size());
        bool previousWasSpace = true;
        for (auto ch : lowered)
        {
            if (std::iswalnum(ch))
            {
                canonical.push_back(ch);
                previousWasSpace = false;
            }
            else if (!previousWasSpace)
            {
                canonical.push_back(L' ');
                previousWasSpace = true;
            }
        }
        while (!canonical.empty() && canonical.back() == L' ')
        {
            canonical.pop_back();
        }
        return canonical;
    }

    winrt::hstring UpperArtworkText(winrt::hstring const& value, winrt::hstring const& fallback)
    {
        std::wstring text{ value.empty() ? fallback.c_str() : value.c_str() };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](wchar_t ch) { return !std::iswspace(ch); }));
        text.erase(std::find_if(text.rbegin(), text.rend(), [](wchar_t ch) { return !std::iswspace(ch); }).base(), text.end());
        if (text.empty())
        {
            text = fallback.c_str();
        }
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towupper(ch));
        });
        return winrt::hstring{ text };
    }

    bool TryReplaceMusicArtworkSize(std::wstring& text, std::wstring const& marker, std::wstring const& replacement)
    {
        auto lowered = text;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });

        auto pos = lowered.find(marker);
        while (pos != std::wstring::npos)
        {
            auto cursor = pos + marker.size();
            auto widthStart = cursor;
            while (cursor < lowered.size() && std::iswdigit(lowered[cursor]))
            {
                ++cursor;
            }
            if (cursor == widthStart || cursor + 2 > lowered.size() || lowered.compare(cursor, 2, L"-h") != 0)
            {
                pos = lowered.find(marker, pos + marker.size());
                continue;
            }

            cursor += 2;
            auto heightStart = cursor;
            while (cursor < lowered.size() && std::iswdigit(lowered[cursor]))
            {
                ++cursor;
            }
            if (cursor == heightStart)
            {
                pos = lowered.find(marker, pos + marker.size());
                continue;
            }

            auto end = cursor;
            while (end < text.size() && text[end] != L'?' && text[end] != L'&' && text[end] != L'#')
            {
                ++end;
            }
            text.replace(pos, end - pos, replacement);
            return true;
        }

        return false;
    }

    winrt::hstring NormalizeMusicArtworkUrl(winrt::hstring const& value)
    {
        std::wstring text{ value.c_str() };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](wchar_t ch) { return !std::iswspace(ch); }));
        text.erase(std::find_if(text.rbegin(), text.rend(), [](wchar_t ch) { return !std::iswspace(ch); }).base(), text.end());
        if (text.empty())
        {
            return {};
        }

        // Some remote artwork URLs encode their requested crop in the URL.
        // Normalize known encoded sizes to the same square artwork contract;
        // XAML still owns the visible UniformToFill crop.
        TryReplaceMusicArtworkSize(text, L"=w", L"=w512-h512-l90-rj")
            || TryReplaceMusicArtworkSize(text, L"%3dw", L"%3Dw512-h512-l90-rj");
        return winrt::hstring{ text };
    }

    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage CreateMusicArtworkBitmap()
    {
        try
        {
            return winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage();
        }
        catch (...)
        {
            return nullptr;
        }
    }

    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage CreateMusicArtworkBitmap(winrt::hstring const& artworkUrl)
    {
        auto normalized = NormalizeMusicArtworkUrl(artworkUrl);
        if (normalized.empty())
        {
            return nullptr;
        }

        // Provider-proxied artwork is usable only with the short-lived signed
        // media URL returned by the authenticated API. Never place the API key
        // in an image URL; expired artwork safely falls back to the generated
        // placeholder until the track is refreshed.
        auto fresh = ProviderArtworkUrlFor(normalized);
        if (fresh.empty())
        {
            return nullptr;
        }

        try
        {
            auto albumArt = CreateMusicArtworkBitmap();
            if (!albumArt)
            {
                return nullptr;
            }
            albumArt.UriSource(winrt::Windows::Foundation::Uri(fresh));
            return albumArt;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void ResolveArtworkPresentation(winrt::Last_Music_Player::TrackInfo const& track, winrt::hstring const& context)
    {
        if (!track)
        {
            return;
        }

        auto sourceKind = ToLowerCopy(track.SourceKind());
        auto provider = ToLowerCopy(track.Provider());
        auto contextKey = ToLowerCopy(context);
        auto isAlbumSurface = contextKey == L"album" || contextKey == L"album-collection" || sourceKind == L"album" || sourceKind == L"album-collection";
        auto isPlaylistSurface = contextKey == L"playlist" || contextKey == L"auto-playlist" || sourceKind == L"playlist" || sourceKind == L"auto-playlist";
        auto isCollectionSurface = isAlbumSurface || isPlaylistSurface;
        auto isManualAlbum = sourceKind == L"album-collection" && provider == L"manual";
        auto isManualPlaylist = sourceKind == L"playlist" && (provider.empty() || provider == L"manual");
        auto isManualGeneratedCollection = isManualAlbum || isManualPlaylist;
        auto rawArtworkUrl = NormalizeMusicArtworkUrl(track.ArtworkUrl());
        if (rawArtworkUrl != track.ArtworkUrl())
        {
            track.ArtworkUrl(rawArtworkUrl);
        }
        auto hasImage = !isManualGeneratedCollection && (!rawArtworkUrl.empty() || track.AlbumArt() != nullptr);

        if (hasImage && track.AlbumArt() == nullptr && !rawArtworkUrl.empty())
        {
            auto albumArt = CreateMusicArtworkBitmap(rawArtworkUrl);
            if (albumArt)
            {
                track.AlbumArt(albumArt);
            }
            else
            {
                hasImage = false;
            }
        }

        track.ArtworkGlyph(L"\xE8D6");
        track.ArtworkTitle(UpperArtworkText(track.Title(), isCollectionSurface ? winrt::hstring{ L"MUSIC" } : winrt::hstring{ L"MUSIC" }));

        if (isManualGeneratedCollection)
        {
            track.ArtworkMode(L"manual-album");
            track.ArtworkCaption(track.Artist().empty() ? winrt::hstring{ L"Your hand-picked songs live here." } : track.Artist());
        }
        else if (isCollectionSurface)
        {
            track.ArtworkMode(hasImage ? L"image" : L"album-fallback");
            if (track.TrackCount() > 0)
            {
                track.ArtworkCaption(winrt::hstring(std::to_wstring(track.TrackCount()) + (track.TrackCount() == 1 ? L" song" : L" songs")));
            }
            else if (!track.Artist().empty())
            {
                track.ArtworkCaption(track.Artist());
            }
            else
            {
                track.ArtworkCaption(track.SourceLabel().empty() ? winrt::hstring{ L"Album collection" } : track.SourceLabel());
            }
        }
        else
        {
            track.ArtworkMode(hasImage ? L"image" : L"track-placeholder");
            track.ArtworkCaption(track.Artist().empty() ? winrt::hstring{ L"Select a track" } : track.Artist());
        }

        track.ImageArtworkOpacity(hasImage ? 1.0 : 0.0);
        track.GeneratedArtworkOpacity(hasImage ? 0.0 : 1.0);
    }

    winrt::Microsoft::UI::Xaml::Media::ImageSource ApprovedDetailArtwork(
        winrt::Last_Music_Player::TrackInfo const& track,
        winrt::hstring const& context)
    {
        if (!track)
        {
            return nullptr;
        }

        ResolveArtworkPresentation(track, context);
        if (track.ImageArtworkOpacity() <= 0.0 || track.AlbumArt() == nullptr)
        {
            return nullptr;
        }

        return track.AlbumArt();
    }

    std::wstring HomeQueueDedupeKey(winrt::Last_Music_Player::TrackInfo const& track)
    {
        auto filePath = track.FilePath();
        auto remote = ToLowerCopy(track.SourceKind()) == L"remote" ||
            (!track.File() && (IsHttpUrl(filePath) || IsHttpUrl(track.SourceUrl())));
        if (!remote)
        {
            return L"local|" + std::wstring(filePath.c_str()) + L"|" + CanonicalQueueText(track.Title()) + L"|" + CanonicalQueueText(track.Artist());
        }

        if (!track.RemoteId().empty())
        {
            return L"remote-id|" + std::wstring(track.RemoteId().c_str());
        }
        if (!track.SourceUrl().empty())
        {
            return L"remote-url|" + std::wstring(track.SourceUrl().c_str());
        }
        auto title = CanonicalQueueText(track.Title());
        auto artist = CanonicalQueueText(track.Artist());
        return title.empty()
            ? L"remote-url|" + std::wstring(filePath.c_str())
            : L"remote|" + title + L"|" + artist;
    }

    bool IsCompatibleAccountRemoteTrack(
        winrt::Last_Music_Player::TrackInfo const& track,
        bool requireRemoteId)
    {
        if (!track || ToLowerCopy(track.SourceKind()) != L"remote"
            || (requireRemoteId && track.RemoteId().empty()))
        {
            return false;
        }

        return LastMusicPlayer::Backend::IsSafeRemoteUrl(
            track.SourceUrl(), LastMusicPlayer::Backend::RemoteUrlUse::Durable);
    }

    std::wstring CatalogSourceKey(winrt::Last_Music_Player::TrackInfo const& track)
    {
        auto filePath = track.FilePath();
        auto sourceKind = ToLowerCopy(track.SourceKind());
        auto remote = sourceKind == L"remote" || (!track.File() && IsHttpUrl(filePath));
        if (remote)
        {
            auto provider = ToLowerCopy(track.Provider());
            if (provider.empty())
            {
                provider = L"provider";
            }

            auto sourceUrl = std::wstring(track.SourceUrl().c_str());
            if (!sourceUrl.empty())
            {
                return L"remote|" + provider + L"|" + sourceUrl;
            }

            std::wstring path{ filePath.c_str() };
            return path.empty() ? HomeQueueDedupeKey(track) : (L"remote|" + provider + L"|" + path);
        }

        std::wstring path{ filePath.c_str() };
        std::transform(path.begin(), path.end(), path.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return path.empty() ? HomeQueueDedupeKey(track) : (L"local|" + path);
    }

    std::wstring ApiKeyStreamCacheKey(
        LastMusicPlayer::Backend::RemoteScopeSnapshot const& scope,
        winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track
            || scope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::ApiKey
            || track.SourceUrl().empty())
        {
            return {};
        }

        auto key = LastMusicPlayer::Backend::RemoteScopeCacheKey(scope);
        key += L"\n";
        key += track.SourceUrl().c_str();
        return key;
    }

    std::wstring FilePathToUri(winrt::hstring const& filePath)
    {
        std::wstring path{ filePath.c_str() };
        std::replace(path.begin(), path.end(), L'\\', L'/');
        std::wstring encoded;
        encoded.reserve(path.size() + 16);
        for (wchar_t ch : path)
        {
            if (ch == L' ')
            {
                encoded.append(L"%20");
            }
            else
            {
                encoded.push_back(ch);
            }
        }
        if (encoded.rfind(L"file:///", 0) == 0)
        {
            return encoded;
        }
        return L"file:///" + encoded;
    }

    std::filesystem::path AppDataDirectory()
    {
        wchar_t* localAppData{};
        size_t length{};
        if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 && localAppData && *localAppData)
        {
            auto path = std::filesystem::path{ localAppData } / L"Last Music Player";
            std::free(localAppData);
            return path;
        }
        std::free(localAppData);

        return std::filesystem::current_path() / L"Last Music Player";
    }

    std::filesystem::path StateFilePath()
    {
        return AppDataDirectory() / L"LastMusicState.json";
    }

    std::string ToUtf8(winrt::hstring const& value)
    {
        auto text = std::wstring{ value.c_str() };
        if (text.empty())
        {
            return {};
        }

        auto required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }

        std::string utf8(static_cast<size_t>(required - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), required, nullptr, nullptr);
        return utf8;
    }

    winrt::hstring FromUtf8(std::string const& value)
    {
        if (value.empty())
        {
            return {};
        }

        auto required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (required <= 0)
        {
            return {};
        }

        std::wstring wide(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), required);
        return winrt::hstring{ wide };
    }

    winrt::hstring ReadTextFile(std::filesystem::path const& path)
    {
        try
        {
            std::ifstream file{ path, std::ios::binary };
            if (!file)
            {
                return {};
            }

            std::ostringstream buffer;
            buffer << file.rdbuf();
            return FromUtf8(buffer.str());
        }
        catch (...)
        {
            return {};
        }
    }

    void WriteTextFile(std::filesystem::path const& path, winrt::hstring const& value)
    {
        // Atomic write: stream into <path>.tmp, fsync via MoveFileExW
        // with MOVEFILE_WRITE_THROUGH. Pre-fix this opened the
        // destination with std::ios::trunc, which truncates on open —
        // a crash between truncate and write left LastMusicState.json
        // empty and the user lost queue + history on next launch.
        // Mirrors the WriteSettingsText pattern in SettingsManager.cpp.
        try
        {
            std::filesystem::create_directories(path.parent_path());
            auto tempPath = path;
            tempPath += L".tmp";
            {
                std::ofstream file{ tempPath, std::ios::binary | std::ios::trunc };
                auto utf8 = ToUtf8(value);
                file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
                file.close();
                if (!file)
                {
                    std::error_code ec;
                    std::filesystem::remove(tempPath, ec);
                    return;
                }
            }
            if (!::MoveFileExW(
                tempPath.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                std::error_code ec;
                std::filesystem::remove(tempPath, ec);
            }
        }
        catch (...)
        {
        }
    }

    // All app settings now flow through the typed SettingsManager (single
    // source of truth, backed by the same Settings.json). These remain as
    // thin string wrappers so existing callers stay unchanged.
    winrt::hstring ReadAppSettingString(wchar_t const* key)
    {
        return SettingsManagerService().GetString(winrt::hstring{ key }, L"");
    }

    void WriteAppSettingString(wchar_t const* key, winrt::hstring const& value)
    {
        SettingsManagerService().SetString(winrt::hstring{ key }, value);
    }

    winrt::hstring CurrentProviderBaseUrl()
    {
        return LastMusicPlayer::Backend::NormalizeProviderBaseUrl(ReadAppSettingString(L"ProviderBaseUrl"));
    }

    winrt::hstring ProviderStreamUrlFor(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track)
        {
            return {};
        }

        switch (RemoteMusicServiceService().Mode())
        {
        case LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly:
            return {};
        case LastMusicPlayer::Backend::RemoteAccessMode::Account:
        {
            auto signedUrl = LastMusicPlayer::Backend::BuildProviderStreamUrl(
                track.FilePath(),
                LastMusicPlayer::Backend::BuildConfig::AccountMediaOrigin);
            return LastMusicPlayer::Backend::IsTrustedAccountMediaUrl(
                signedUrl,
                LastMusicPlayer::Backend::BuildConfig::AccountMediaOrigin,
                L"stream")
                ? signedUrl
                : winrt::hstring{};
        }
        case LastMusicPlayer::Backend::RemoteAccessMode::ApiKey:
            return LastMusicPlayer::Backend::BuildProviderStreamUrl(
                track.FilePath(),
                track.SourceUrl(),
                track.Provider(),
                track.ArtworkUrl(),
                CurrentProviderBaseUrl(),
                CredentialStoreService().ReadProviderApiKey());
        default:
            return {};
        }
    }

    winrt::hstring ProviderArtworkUrlFor(winrt::hstring const& artworkUrl)
    {
        switch (RemoteMusicServiceService().Mode())
        {
        case LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly:
        case LastMusicPlayer::Backend::RemoteAccessMode::Account:
            return {};
        case LastMusicPlayer::Backend::RemoteAccessMode::ApiKey:
            return LastMusicPlayer::Backend::BuildProviderArtworkUrl(
                artworkUrl,
                CurrentProviderBaseUrl(),
                CredentialStoreService().ReadProviderApiKey());
        default:
            return {};
        }
    }

    void ApplyMusicArtwork(winrt::Last_Music_Player::TrackInfo const& track, winrt::hstring const& artworkUrl, winrt::hstring const& context)
    {
        if (!track)
        {
            return;
        }

        auto normalized = NormalizeMusicArtworkUrl(artworkUrl);
        track.ArtworkUrl(normalized);
        if (normalized.empty())
        {
            track.AlbumArt(nullptr);
            ResolveArtworkPresentation(track, context);
            return;
        }

        track.AlbumArt(CreateMusicArtworkBitmap(normalized));
        ResolveArtworkPresentation(track, context);
    }

    void ApplyMusicArtworkImage(
        winrt::Last_Music_Player::TrackInfo const& track,
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage const& albumArt,
        winrt::hstring const& context)
    {
        if (!track)
        {
            return;
        }

        track.AlbumArt(albumArt);
        ResolveArtworkPresentation(track, context);
    }

    winrt::hstring ReadTagString(winrt::Windows::Foundation::IInspectable const& value)
    {
        try
        {
            return winrt::unbox_value_or<winrt::hstring>(value, L"");
        }
        catch (...)
        {
            return {};
        }
    }
    winrt::hstring BuildAccountTrackJson(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!IsCompatibleAccountRemoteTrack(track))
        {
            return {};
        }
        winrt::Windows::Data::Json::JsonObject object;
        InsertJsonString(object, L"id", track.RemoteId());
        InsertJsonString(object, L"title", track.Title());
        InsertJsonString(object, L"artist", track.Artist());
        InsertJsonString(object, L"album", track.Album());
        InsertJsonString(object, L"sourceUrl", track.SourceUrl());
        object.Insert(L"durationMs", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
            (std::max)(0.0, track.DurationSeconds()) * 1000.0));
        return object.Stringify();
    }

    winrt::hstring BuildAccountPlaylistJson(winrt::hstring const& name)
    {
        auto title = TrimQuery(name);
        if (title.empty())
        {
            return {};
        }
        winrt::Windows::Data::Json::JsonObject object;
        InsertJsonString(object, L"name", title);
        return object.Stringify();
    }
    winrt::hstring BuildAccountPlaylistImportJson(winrt::hstring const& sourceUrl)
    {
        auto canonical = CanonicalProviderCollectionSourceUrl(sourceUrl);
        if (canonical.empty())
        {
            return {};
        }
        winrt::Windows::Data::Json::JsonObject object;
        InsertJsonString(object, L"sourceUrl", canonical);
        return object.Stringify();
    }

    winrt::hstring BuildAccountPlaylistUpdateJson(
        winrt::hstring const& name,
        std::vector<winrt::hstring> const& remoteTrackIds)
    {
        winrt::Windows::Data::Json::JsonObject object;
        auto title = TrimQuery(name);
        if (!title.empty())
        {
            InsertJsonString(object, L"name", title);
        }
        winrt::Windows::Data::Json::JsonArray trackIds;
        for (auto const& remoteId : remoteTrackIds)
        {
            if (!remoteId.empty())
            {
                trackIds.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(remoteId));
            }
        }
        object.Insert(L"trackIds", trackIds);
        return object.Stringify();
    }




    bool IsPlayableHomeTrack(winrt::Last_Music_Player::TrackInfo const& track)
    {
        // Durable remote tracks remain visible and selectable even while offline;
        // playback resolves a fresh ephemeral URL when connectivity returns.
        auto remote = ToLowerCopy(track.SourceKind()) == L"remote";
        return track.File() || !track.FilePath().empty() ||
            (remote && !track.SourceUrl().empty()) || !ProviderStreamUrlFor(track).empty();
    }

    bool LocalFileMissing(winrt::Last_Music_Player::TrackInfo const& track)
    {
        // Remote/streaming tracks have no on-disk file, so they're never
        // "missing" in this sense — only judge genuine local-file tracks.
        if (ToLowerCopy(track.SourceKind()) == L"remote")
        {
            return false;
        }
        auto path = track.FilePath();
        if (path.empty() || IsHttpUrl(path))
        {
            return false;
        }
        std::error_code ec;
        return !std::filesystem::exists(std::filesystem::path(std::wstring(path.c_str())), ec);
    }

    void InsertJsonString(winrt::Windows::Data::Json::JsonObject const& object, wchar_t const* key, winrt::hstring const& value)
    {
        object.Insert(key, winrt::Windows::Data::Json::JsonValue::CreateStringValue(value));
    }

    winrt::Windows::Data::Json::JsonObject TrackSnapshotToJson(winrt::Last_Music_Player::TrackInfo const& track)
    {
        auto remote = ToLowerCopy(track.SourceKind()) == L"remote";
        auto safeFilePath = remote ? winrt::hstring{} : track.FilePath();
        auto safeArtworkUrl = track.ArtworkUrl();
        auto safeSourceUrl = track.SourceUrl();
        if (remote)
        {
            if (!LastMusicPlayer::Backend::IsSafeRemoteUrl(
                safeArtworkUrl, LastMusicPlayer::Backend::RemoteUrlUse::Durable))
            {
                safeArtworkUrl = {};
            }
            if (!LastMusicPlayer::Backend::IsSafeRemoteUrl(
                safeSourceUrl, LastMusicPlayer::Backend::RemoteUrlUse::Durable))
            {
                safeSourceUrl = {};
            }
        }
        winrt::Windows::Data::Json::JsonObject object;
        InsertJsonString(object, L"title", track.Title());
        InsertJsonString(object, L"artist", track.Artist());
        InsertJsonString(object, L"album", track.Album());
        InsertJsonString(object, L"genre", track.Genre());
        InsertJsonString(object, L"filePath", safeFilePath);
        InsertJsonString(object, L"artworkUrl", safeArtworkUrl);
        InsertJsonString(object, L"dateAdded", track.DateAdded());
        InsertJsonString(object, L"duration", track.Duration());
        InsertJsonString(object, L"sourceKind", track.SourceKind());
        InsertJsonString(object, L"provider", track.Provider());
        InsertJsonString(object, L"sourceUrl", safeSourceUrl);
        InsertJsonString(object, L"sourceLabel", track.SourceLabel());
        InsertJsonString(object, L"remoteId", track.RemoteId());
        object.Insert(L"durationSeconds", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(track.DurationSeconds()));
        object.Insert(L"dateAddedSortKey", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(track.DateAddedSortKey()));
        object.Insert(L"isLiked", winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(track.IsLiked()));
        return object;
    }

    winrt::Last_Music_Player::TrackInfo TrackSnapshotFromJson(winrt::Windows::Data::Json::JsonObject const& object)
    {
        LastMusicPlayer::Backend::TrackInfo track;
        track.Title(object.GetNamedString(L"title", L""));
        track.Artist(object.GetNamedString(L"artist", L""));
        track.Album(object.GetNamedString(L"album", L""));
        track.Genre(object.GetNamedString(L"genre", L""));
        track.FilePath(object.GetNamedString(L"filePath", L""));
        track.ArtworkUrl(object.GetNamedString(L"artworkUrl", L""));
        track.DateAdded(object.GetNamedString(L"dateAdded", L""));
        track.Duration(object.GetNamedString(L"duration", L""));
        track.SourceKind(object.GetNamedString(L"sourceKind", IsHttpUrl(track.FilePath()) ? L"remote" : L"local"));
        track.Provider(object.GetNamedString(L"provider", L""));
        track.SourceUrl(object.GetNamedString(L"sourceUrl", L""));
        track.RemoteId(object.GetNamedString(L"remoteId", L""));
        track.SourceLabel(object.GetNamedString(L"sourceLabel", track.SourceKind() == L"remote" ? L"Remote" : L"Local"));
        track.IsLiked(object.GetNamedBoolean(L"isLiked", false));
        track.DurationSeconds(object.GetNamedNumber(L"durationSeconds", 0.0));
        track.DateAddedSortKey(object.GetNamedNumber(L"dateAddedSortKey", 0.0));

        ApplyMusicArtwork(track, track.ArtworkUrl(), L"track");
        return track;
    }

    winrt::Last_Music_Player::TrackInfo TrackFromProviderJson(winrt::Windows::Data::Json::JsonObject const& item)
    {
        auto streamUrl = item.GetNamedString(L"streamUrl", L"");
        if (streamUrl.empty())
        {
            return nullptr;
        }

        auto provider = item.GetNamedString(L"provider", L"provider");
        if (provider == L"provider-error")
        {
            return nullptr;
        }

        LastMusicPlayer::Backend::TrackInfo track;
        track.Title(item.GetNamedString(L"title", L"Provider track"));
        track.Artist(item.GetNamedString(L"artist", L"Provider"));
        track.Album(item.GetNamedString(L"album", provider));
        track.Genre(L"Remote");
        track.FilePath(streamUrl);
        track.SourceUrl(item.GetNamedString(L"sourceUrl", L""));
        track.RemoteId(item.GetNamedString(L"id", L""));
        if (track.SourceUrl().empty())
        {
            track.SourceUrl(streamUrl);
        }
        track.SourceKind(L"remote");
        track.Provider(provider);
        track.SourceLabel(L"Music API");
        // Keep the visible label provider-neutral. Routing identity remains in
        // the internal provider and source fields used by remote resolution.
        track.DateAdded(L"Music API");
        auto durationMs = item.GetNamedNumber(L"durationMs", 0.0);
        track.DurationSeconds(durationMs > 0.0 ? durationMs / 1000.0 : 0.0);
        track.Duration(track.DurationSeconds() > 0.0
            ? winrt::hstring{ LastMusicPlayer::Frontend::UIHelpers::FormatTime(track.DurationSeconds()) }
            : winrt::hstring{});

        ApplyMusicArtwork(track, item.GetNamedString(L"artworkUrl", L""), L"track");
        return track;
    }

    std::vector<winrt::Last_Music_Player::TrackInfo> ParseProviderTracks(winrt::hstring const& payload, size_t limit)
    {
        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;
        auto root = winrt::Windows::Data::Json::JsonObject::Parse(payload);
        auto results = root.GetNamedArray(L"results");
        for (uint32_t i = 0; i < results.Size() && tracks.size() < limit; ++i)
        {
            auto track = TrackFromProviderJson(results.GetObjectAt(i));
            if (track)
            {
                tracks.push_back(track);
            }
        }
        return tracks;
    }

    std::vector<winrt::Last_Music_Player::TrackInfo> ParseProviderTrackArray(winrt::Windows::Data::Json::JsonArray const& results)
    {
        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;
        tracks.reserve(results.Size());
        for (uint32_t i = 0; i < results.Size(); ++i)
        {
            auto track = TrackFromProviderJson(results.GetObjectAt(i));
            if (track)
            {
                track.Index(static_cast<int32_t>(tracks.size() + 1));
                tracks.push_back(track);
            }
        }
        return tracks;
    }

    std::vector<std::wstring> RankedHomeArtists(
        std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks,
        std::unordered_map<std::wstring, uint32_t> const& playCounts)
    {
        std::unordered_map<std::wstring, uint32_t> scores;
        std::unordered_map<std::wstring, std::wstring> displayNames;
        for (auto const& track : tracks)
        {
            auto artist = CanonicalQueueText(track.Artist());
            if (artist.empty() || artist == L"unknown artist")
            {
                continue;
            }

            auto key = HomeQueueDedupeKey(track);
            auto playIt = playCounts.find(key);
            auto score = 1u + (playIt == playCounts.end() ? 0u : playIt->second);
            scores[artist] += score;
            if (displayNames.find(artist) == displayNames.end())
            {
                displayNames.emplace(artist, std::wstring(track.Artist().c_str()));
            }
        }

        std::vector<std::pair<std::wstring, uint32_t>> ranked{ scores.begin(), scores.end() };
        std::sort(ranked.begin(), ranked.end(), [](auto const& left, auto const& right)
        {
            if (left.second != right.second)
            {
                return left.second > right.second;
            }
            return left.first < right.first;
        });

        std::vector<std::wstring> artists;
        artists.reserve(ranked.size());
        for (auto const& item : ranked)
        {
            auto displayIt = displayNames.find(item.first);
            artists.push_back(displayIt == displayNames.end() ? item.first : displayIt->second);
        }
        return artists;
    }



    std::wstring GetAppAssetPath(wchar_t const* relativePath)
    {
        wchar_t modulePath[MAX_PATH]{};
        DWORD const length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        std::wstring path(modulePath, length);
        auto const slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            path.resize(slash + 1);
        }
        else
        {
            path.clear();
        }
        path.append(relativePath);
        return path;
    }

}
