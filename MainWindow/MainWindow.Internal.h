#pragma once

#include "Backend/AudioPlayer.h"
#include "Backend/AccountClient.h"
#include "Backend/AccountSessionService.h"
#include "Backend/CredentialStore.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/ProviderHelpers.h"
#include "Backend/SettingsManager.h"
#include "Backend/RemoteMusicService.h"
#include "Backend/MusicSyncService.h"
#include "Backend/StreamCache.h"
#include "Backend/TrackInfo.h"
#include "Backend/UserDataOperationGate.h"
#include "Frontend/NavigationService.h"
#include "Frontend/UIHelpers.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace winrt::Last_Music_Player::implementation::detail
{
    inline constexpr int kDefaultWindowWidth = 1600;
    inline constexpr int kDefaultWindowHeight = 1000;
    inline constexpr uint32_t kSongsListPageSize = 150;
    inline constexpr uint32_t kSongsGridPageSize = 80;
    inline constexpr uint32_t kLibrarySongPageSize = 150;
    inline constexpr uint32_t kPageAppendThreshold = 25;
    inline constexpr size_t kRemoteSearchCacheLimit = 64;

    void InstallMinimumWindowSize(HWND hwnd);
    void RunDetached(winrt::Windows::Foundation::IAsyncAction action);

    LastMusicPlayer::Backend::AudioPlayer& AudioPlayerService();
    LastMusicPlayer::Backend::SettingsManager& SettingsManagerService();
    LastMusicPlayer::Backend::CredentialStore& CredentialStoreService();
    LastMusicPlayer::Backend::AccountClient& AccountClientService();
    LastMusicPlayer::Backend::AccountSessionService& AccountSessionService();
    LastMusicPlayer::Backend::RemoteMusicService& RemoteMusicServiceService();
    LastMusicPlayer::Backend::DatabaseEngine& DatabaseService();
    LastMusicPlayer::Backend::MusicSyncService& MusicSyncServiceService();
    LastMusicPlayer::Backend::StreamCache& StreamCacheService();
    LastMusicPlayer::Backend::UserDataOperationGate& UserDataOperationGateService();
    LastMusicPlayer::Frontend::NavigationService& NavigationService();

    std::wstring ToLowerCopy(winrt::hstring const& value);
    bool ContainsFolded(winrt::hstring const& haystack, winrt::hstring const& needle);
    winrt::hstring TrimQuery(winrt::hstring const& value);
    bool IsHttpUrl(winrt::hstring const& value);
    std::wstring QueryValue(std::wstring const& url, std::wstring const& name);
    winrt::hstring CanonicalProviderCollectionSourceUrl(winrt::hstring const& value);
    std::wstring CanonicalQueueText(winrt::hstring const& value);
    winrt::hstring UpperArtworkText(winrt::hstring const& value, winrt::hstring const& fallback);
    bool TryReplaceMusicArtworkSize(std::wstring& text, std::wstring const& marker, std::wstring const& replacement);
    winrt::hstring NormalizeMusicArtworkUrl(winrt::hstring const& value);
    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage CreateMusicArtworkBitmap();
    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage CreateMusicArtworkBitmap(winrt::hstring const& artworkUrl);
    void ResolveArtworkPresentation(winrt::Last_Music_Player::TrackInfo const& track, winrt::hstring const& context);
    winrt::Microsoft::UI::Xaml::Media::ImageSource ApprovedDetailArtwork(winrt::Last_Music_Player::TrackInfo const& track, winrt::hstring const& context);
    std::wstring HomeQueueDedupeKey(winrt::Last_Music_Player::TrackInfo const& track);
    std::wstring CatalogSourceKey(winrt::Last_Music_Player::TrackInfo const& track);
    std::wstring ApiKeyStreamCacheKey(
        LastMusicPlayer::Backend::RemoteScopeSnapshot const& scope,
        winrt::Last_Music_Player::TrackInfo const& track);
    std::wstring FilePathToUri(winrt::hstring const& filePath);

    std::filesystem::path AppDataDirectory();
    std::filesystem::path StateFilePath();
    std::string ToUtf8(winrt::hstring const& value);
    winrt::hstring FromUtf8(std::string const& value);
    winrt::hstring ReadTextFile(std::filesystem::path const& path);
    void WriteTextFile(std::filesystem::path const& path, winrt::hstring const& value);

    winrt::hstring ReadAppSettingString(wchar_t const* key);
    void WriteAppSettingString(wchar_t const* key, winrt::hstring const& value);
    winrt::hstring CurrentProviderBaseUrl();
    winrt::hstring ProviderStreamUrlFor(winrt::Last_Music_Player::TrackInfo const& track);
    winrt::hstring ProviderArtworkUrlFor(winrt::hstring const& artworkUrl);

    void ApplyMusicArtwork(winrt::Last_Music_Player::TrackInfo const& track, winrt::hstring const& artworkUrl, winrt::hstring const& context);
    void ApplyMusicArtworkImage(
        winrt::Last_Music_Player::TrackInfo const& track,
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage const& albumArt,
        winrt::hstring const& context);
    winrt::hstring ReadTagString(winrt::Windows::Foundation::IInspectable const& value);
    bool IsPlayableHomeTrack(winrt::Last_Music_Player::TrackInfo const& track);
    // True only for a local track whose backing file no longer exists on disk
    // (remote/streaming tracks always return false). Used to prune local music
    // deleted off disk so it stops lingering in history/library/queue.
    bool LocalFileMissing(winrt::Last_Music_Player::TrackInfo const& track);
    bool IsCompatibleAccountRemoteTrack(
        winrt::Last_Music_Player::TrackInfo const& track,
        bool requireRemoteId = true);

    void InsertJsonString(winrt::Windows::Data::Json::JsonObject const& object, wchar_t const* key, winrt::hstring const& value);
    winrt::Windows::Data::Json::JsonObject TrackSnapshotToJson(winrt::Last_Music_Player::TrackInfo const& track);
    winrt::hstring BuildAccountTrackJson(winrt::Last_Music_Player::TrackInfo const& track);
    winrt::hstring BuildAccountPlaylistJson(winrt::hstring const& name);
    winrt::hstring BuildAccountPlaylistImportJson(winrt::hstring const& sourceUrl);
    winrt::hstring BuildAccountPlaylistUpdateJson(
        winrt::hstring const& name,
        std::vector<winrt::hstring> const& remoteTrackIds);
    winrt::Last_Music_Player::TrackInfo TrackSnapshotFromJson(winrt::Windows::Data::Json::JsonObject const& object);
    winrt::Last_Music_Player::TrackInfo TrackFromProviderJson(winrt::Windows::Data::Json::JsonObject const& item);
    std::vector<winrt::Last_Music_Player::TrackInfo> ParseProviderTracks(winrt::hstring const& payload, size_t limit);
    std::vector<winrt::Last_Music_Player::TrackInfo> ParseProviderTrackArray(winrt::Windows::Data::Json::JsonArray const& results);
    std::vector<std::wstring> RankedHomeArtists(
        std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks,
        std::unordered_map<std::wstring, uint32_t> const& playCounts);

    std::wstring GetAppAssetPath(wchar_t const* relativePath);
}