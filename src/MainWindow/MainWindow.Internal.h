#pragma once

#include "Backend/AudioPlayer.h"
#include "Backend/AccountClient.h"
#include "Backend/AccountDataLifecycleService.h"
#include "Backend/AccountSessionService.h"
#include "Backend/CredentialStore.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/DownloadManager.h"
#include "Backend/ProfileIdentity.h"
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
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace winrt::Last_Music_Player::implementation::detail
{
    // Effective pixels: the size the window opens at with no saved geometry.
    // Same unit as the minimum below so the two can be compared at a glance;
    // a default under the minimum would just be clamped on first launch.
    // Matches the minimum because the full layout needs this width to keep four
    // current playlist-detail cards on one row with both side rails visible.
    inline constexpr int kDefaultWindowWidthEpx = 1361;
    inline constexpr int kDefaultWindowHeightEpx = 880;
    inline constexpr int kPreviousDefaultWindowWidthEpx = 1300;
    inline constexpr int kPreviousDefaultWindowHeightEpx = 900;
    inline constexpr int kLargerDefaultWindowWidthEpx = 1500;
    inline constexpr int kLargerDefaultWindowHeightEpx = 900;
    inline constexpr int kOversizedDefaultWindowWidthEpx = 1720;
    inline constexpr int kOversizedDefaultWindowHeightEpx = 900;
    inline constexpr int kLegacyDefaultWindowWidthEpx = 1483;
    inline constexpr int kLegacyDefaultWindowHeightEpx = 900;
    inline constexpr int kEarlierDefaultWindowWidthEpx = 1440;
    inline constexpr int kEarlierDefaultWindowHeightEpx = 900;
    inline constexpr int kOldestDefaultWindowWidthEpx = 1370;
    inline constexpr int kOldestDefaultWindowHeightEpx = 880;

    // Effective pixels, converted for the window's DPI where they are applied.
    //
    // At this width the wide layout consumes 240 for navigation, 300 for the
    // Now Playing rail, and 32 for the library's horizontal padding. The 789
    // pixels left for content hold four 150-pixel minimum cards and their
    // 18-pixel gutters, but not a fifth card.
    //
    // Changing any of those pitches changes what this number has to be.
    inline constexpr int kMinWindowWidthEpx = 1361;
    inline constexpr int kMinWindowHeightEpx = 880;
    inline constexpr uint32_t kSongsListPageSize = 150;
    inline constexpr uint32_t kSongsGridPageSize = 80;
    inline constexpr uint32_t kLibrarySongPageSize = 150;
    inline constexpr uint32_t kPageAppendThreshold = 25;
    inline constexpr size_t kRemoteSearchCacheLimit = 64;

    // How many pixels tall an artwork bitmap should be decoded to.
    //
    // Two things make this explicit rather than automatic. A BitmapImage with no
    // stated decode size is decoded to fit whichever Image element happens to
    // render it first, and that single decode is then reused everywhere else the
    // same bitmap appears; album art is shown from a 38 pixel list row up to the
    // 340 pixel full-screen player, so leaving it implicit lets a list row
    // decide how sharp the full-screen artwork looks.
    //
    // Height is the dimension that matters because every artwork host is square
    // and stretches UniformToFill. Filling a square with a landscape image
    // scales it by height, so a 1280x720 cover only ever contributes 720 pixels
    // of detail no matter how wide it is. Only the height is ever set, which
    // leaves XAML to derive the width from the source's own aspect ratio;
    // setting both would stretch any cover that is not square.
    enum class ArtworkDetail
    {
        // List rows, grid tiles, and detail page headers. The largest of these
        // is 184 effective pixels, so this covers them at 200% display scaling.
        Tile = 384,

        // The player bar, the now playing panel, and the full-screen player,
        // which share one bitmap. The largest is 340 effective pixels, so this
        // holds up to 300% display scaling.
        Hero = 1024
    };


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
    LastMusicPlayer::Backend::AccountDataLifecycleService& AccountDataLifecycleService();
    LastMusicPlayer::Backend::StreamCache& StreamCacheService();
    LastMusicPlayer::Backend::DownloadManager& DownloadManagerService();
    LastMusicPlayer::Backend::UserDataOperationGate& UserDataOperationGateService();
    LastMusicPlayer::Frontend::NavigationService& NavigationService();

    // Who the app currently presents the user as: the account profile while
    // account mode is connected, otherwise the manually typed display name.
    // Reads live services, so it must be called on the UI thread alongside the
    // other state these services back.
    LastMusicPlayer::Backend::ProfileIdentity ResolveProfileIdentity();

    // Points an avatar Image at a profile picture, keeping the silhouette
    // fallback element visible until the bitmap has actually decoded and
    // restoring it if the download or decode fails. An empty or unusable URL
    // simply leaves the fallback up.
    void ApplyAvatarPicture(
        winrt::Microsoft::UI::Xaml::Controls::Image const& image,
        winrt::Microsoft::UI::Xaml::UIElement const& fallback,
        winrt::hstring const& avatarUrl);

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
    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage CreateMusicArtworkBitmap(ArtworkDetail detail);
    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage CreateMusicArtworkBitmap(
        winrt::hstring const& artworkUrl,
        ArtworkDetail detail);
    void ResolveArtworkPresentation(winrt::Last_Music_Player::TrackInfo const& track, winrt::hstring const& context);
    winrt::Microsoft::UI::Xaml::Media::ImageSource ApprovedDetailArtwork(winrt::Last_Music_Player::TrackInfo const& track, winrt::hstring const& context);
    std::wstring HomeQueueDedupeKey(winrt::Last_Music_Player::TrackInfo const& track);
    std::wstring CatalogSourceKey(winrt::Last_Music_Player::TrackInfo const& track);
    std::wstring ApiKeyStreamCacheKey(
        LastMusicPlayer::Backend::RemoteScopeSnapshot const& scope,
        winrt::Last_Music_Player::TrackInfo const& track);
    std::wstring DownloadStableKey(
        LastMusicPlayer::Backend::RemoteScopeSnapshot const& scope,
        winrt::Last_Music_Player::TrackInfo const& track);
    std::wstring FilePathToUri(winrt::hstring const& filePath);

    // Where the library scan puts the cover art it copies out of local files.
    std::filesystem::path ArtworkCacheDirectory();

    // file:// URI for a path inside the artwork cache. Escapes more than
    // FilePathToUri does because that one also serves media paths whose
    // existing callers may pass an already-encoded URI back through it.
    winrt::hstring ArtworkCacheFileUri(std::filesystem::path const& path);

    // Whether an artwork URL names a file in the artwork cache. Artwork URLs
    // also arrive from account sync, so a file:// URL is only ever handed to an
    // image decoder when it points at something this app wrote itself.
    bool IsArtworkCacheUri(winrt::hstring const& artworkUrl);

    std::filesystem::path AppDataDirectory();
    std::filesystem::path StateFilePath();
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
        std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks);
    winrt::Last_Music_Player::TrackInfo TrackSnapshotFromJson(winrt::Windows::Data::Json::JsonObject const& object);
    winrt::Last_Music_Player::TrackInfo TrackFromProviderJson(winrt::Windows::Data::Json::JsonObject const& item);
    std::vector<winrt::Last_Music_Player::TrackInfo> ParseProviderTracks(winrt::hstring const& payload, size_t limit);
    std::vector<winrt::Last_Music_Player::TrackInfo> ParseProviderTrackArray(winrt::Windows::Data::Json::JsonArray const& results);
    std::vector<std::wstring> RankedHomeArtists(
        std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks,
        std::unordered_map<std::wstring, uint32_t> const& playCounts);

    std::wstring GetAppAssetPath(wchar_t const* relativePath);
}
