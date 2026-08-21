#pragma once
#include "MainWindow.g.h"
#include "Backend/TrackInfo.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/PlaybackHistoryQualifier.h"
#include "Backend/AutoSyncPolicy.h"
#include "Frontend/Skeleton.h"
#include "Backend/CastEngine.h"
#include "Backend/CatalogModels.h"
#include "Backend/LyricsService.h"
#include "Backend/RemoteMusicService.h"

#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Search.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <array>
#include <chrono>
#include <deque>
#include <vector>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <optional>
#include <atomic>

namespace LastMusicPlayer::Backend { class TrayIcon; class DiscordPresence; class ProviderClient; }

// Defined in MainWindow.Internal.h, which is far too heavy to pull in here. An
// opaque declaration is enough to name the type in signatures and members.
namespace winrt::Last_Music_Player::implementation::detail { enum class ArtworkDetail; }

namespace winrt::Last_Music_Player::implementation
{
    // What prompted an account library sync, which decides how much of the UI
    // it is allowed to disturb once it finishes.
    enum class AccountSyncMode
    {
        // The user pressed Sync now or signed in. Reports progress in Settings
        // and takes the full post-sync reset.
        Interactive,

        // Something the user did implies it: finishing a track, liking a song,
        // editing a playlist, changing mode. Full reset, but silent.
        Implicit,

        // A timer tick or the window regaining focus. Must never close the view
        // the user is on, reset their scroll position, or open a dialog.
        Background,
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void HomeButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SettingsNav_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OpenLibrarySongs();
        void BrowseButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        // Home's bar is only a way in to Browse; both the click and a tap on the
        // surrounding chrome land on the same place.
        void HomeSearchEntry_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeSearchEntry_Tapped(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args);
        void BrowseCategory_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void SearchListView_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SearchGridView_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SearchSort_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SearchRetry_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction ChangeFolderButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction OpenFolderButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction ScanMusicButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction HomeRecentGridView_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction MusicListView_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void PlayPauseButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void NextButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void PreviousButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Card_PointerEntered(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void Card_PointerExited(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void PlaylistCard_PointerEntered(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void PlaylistCard_PointerExited(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void LibraryArtworkBorder_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void GlobalSearchBox_GotFocus(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void GlobalSearchBox_LostFocus(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void GlobalSearchBox_KeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void GlobalSearchBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void HomeSearchButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeListenAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeMadeForAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeMixCard_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction HomeRecentlyAdded_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction HomeMostPlayed_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction HomeLiked_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction HomeLikedSeeAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction HomeMostPlayedSeeAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction HomeRecentlyAddedSeeAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

        // The one line under the mixes row that says whether the provider fill
        // is running, gave up, or had nothing to add. Empty message hides it.
        void ShowHomeMixStatus(winrt::hstring const& message, bool busy);
        void ClearHomeMixStatus();
        void HomeRowMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void UpdateMadeForCardLabels();
        void UpdateHomeGreeting(winrt::hstring const& displayName);
        void ApplyUserDisplayName();
        void DisplayNameBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void DisplayNameBox_KeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void DisplayNameSave_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction SearchResult_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void RailTab_UpNext_Tapped(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& e);
        void RailTab_Lyrics_Tapped(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& e);
        winrt::Windows::Foundation::IAsyncAction SongsRescan_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SongsPlayAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction SongsGridView_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void SongsList_ContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);
        void SongsGrid_ContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);
        void SongsRowPlay_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void BrowseRowMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void BrowseRowMenuPlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void BrowseRowMenuAddToQueue_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void BrowseRowMenuLike_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void BrowseRowMenuArtist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void BrowseRowMenuAlbum_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SongsListView_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SongsGridView_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SongsSort_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HistoryListView_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HistoryGridView_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HistorySort_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryAddFolder_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryCreateAlbum_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryImportAlbum_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryCreatePlaylist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryImportPlaylist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryAlbumDelete_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryPlaylistRename_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryPlaylistDelete_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryPlaylistCardMenu_Opening(winrt::Windows::Foundation::IInspectable const& sender, winrt::Windows::Foundation::IInspectable const& args);
        void LibraryDetailRowMenu_Opening(winrt::Windows::Foundation::IInspectable const& sender, winrt::Windows::Foundation::IInspectable const& args);
        void LibraryPlaylistFilter_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryTab_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryScope_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryGroup_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction SidebarPlaylist_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void LibraryDetailBack_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryDetailPlay_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryDetailShuffle_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibrarySongsListView_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void LibrarySongs_ContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);
        void LibraryDetailTracks_ContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);
        void LibraryRowMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryRowMenuPlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryRowMenuAddToQueue_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryRowMenuAddToPlaylist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryRowMenuRemoveFromPlaylist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LibraryRowMenuMoveInPlaylist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryRowMenuAlbum_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        // Group-card context-menu handlers — operate on an album / artist /
        // genre / manual or auto playlist sentinel TrackInfo (carries
        // SourceKind + SourceUrl/Title, not a single playable track). Used
        // by the right-click flyouts on Library grid cards and the Sidebar
        // pinned-playlist rows. Mix tiles on Home use the HomeMixMenu_*
        // siblings instead because the source is m_homeMixes, not the DB.
        void LibraryGroupMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryGroupMenuPlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryGroupMenuAddToQueue_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LibraryGroupMenuShuffle_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        // Daily Mix card menu handlers — sender's Tag carries the mix id
        // ("daily1".."daily5", "repeat", "discover", "timecapsule",
        // "fresh"). Tracks come straight from m_homeMixes; no DB hop.
        void HomeMixMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeMixMenuPlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeMixMenuAddToQueue_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeMixMenuShuffle_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void QueuePlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void QueuePlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void QueueMoveUp_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void QueueMoveDown_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void QueueRemove_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void UpNextQueue_DragItemsCompleted(winrt::Microsoft::UI::Xaml::Controls::ListViewBase const& sender, winrt::Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction LikedSongsButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LikeCurrentTrack_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ThemeSegment_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction PlayProviderTest_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void DisconnectProvider_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction AccountSync_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction AccountSignOut_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction AccountClearData_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction AccountManage_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RemoteMode_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void ScanCancel_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction OpenDataFolder_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction ShowLicenses_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction ResetDefaults_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void WipeAllDataReveal_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void WipeAllDataConfirm_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction WipeAllData_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SettingsToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SettingsCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void SettingsSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void FormatChip_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AccentSwatch_Tapped(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args);
        void OnAppWindowClosing(winrt::Microsoft::UI::Windowing::AppWindow const& sender, winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args);
        void AccelSearch_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void AccelPrev_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void AccelNext_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void AccelPlayPause_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void MainBodyGrid_SizeChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
        void RightPanelArtHost_SizeChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
        void LibraryHeaderBar_SizeChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
        void Row_PointerEntered(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void Row_PointerExited(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnMediaEnded(winrt::Windows::Media::Playback::MediaPlayer const& sender, winrt::Windows::Foundation::IInspectable const& args);
        // Auto-recovery for the live-streamed track: on a mid-stream network
        // failure, re-open the source and resume near the last position. The
        // pending-seek is applied in OnMediaOpened once the media is ready.
        void OnMediaFailed(winrt::Windows::Media::Playback::MediaPlayer const& sender, winrt::Windows::Media::Playback::MediaPlayerFailedEventArgs const& args);
        void OnMediaOpened(winrt::Windows::Media::Playback::MediaPlayer const& sender, winrt::Windows::Foundation::IInspectable const& args);
        void VolumeSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void TimelineSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e);
        void ShuffleButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RepeatButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void QueueButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void EqualizerButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void MuteButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void CastButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ExpandButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FsCloseButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void CastDevice_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncOperation<bool> ScanLibraryAsync(winrt::Windows::Storage::StorageFolder folder);
        void PruneMissingLocalTracks();

        winrt::Windows::Foundation::IAsyncAction UpdateSMTCMetadata(winrt::Last_Music_Player::TrackInfo track);
        void EqualizerPreset_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void EqResetButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void EqPreampSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);

        // Artwork and catalog handlers referenced by XAML must be public because
        // the generated connect code lives outside this class.
        void QueueContainerArtwork(winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);
        void AccountArtwork_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        // Rounds an artwork surface to the frame it sits in. Artwork that also
        // needs fetching goes through AccountArtwork_Loaded, which rounds it too.
        void ArtworkImage_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void DiscoverBack_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction HomeCatalogRetry_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction DiscoverStorefront_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction DiscoverItem_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction DiscoverChartMore_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void DiscoverDetailTrack_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
        void DiscoverDetailPlay_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void DiscoverDetailQueue_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);


    private:
        enum class LoadState
        {
            NotLoaded,
            Loading,
            Loaded,
            Dirty
        };

        enum class PlaybackSink
        {
            Local,
            Cast
        };
        struct LibraryScanState
        {
            bool InProgress{ false };
            bool CancelRequested{ false };
            uint64_t Epoch{ 0 };
        };

        struct PlaybackQueueState
        {
            // The playback context: whatever surface the listener last clicked
            // from, such as a Songs query, Library tab or playlist detail. It is
            // replaced by SetPlaybackQueue and is not a library snapshot; search
            // and collection discovery must query DatabaseEngine instead. Queue
            // takes priority over CurrentPlaylist for navigation.
            std::vector<winrt::Last_Music_Player::TrackInfo> CurrentPlaylist;
            int CurrentTrackIndex{ -1 };
            std::vector<winrt::Last_Music_Player::TrackInfo> Queue;
            int QueueIndex{ -1 };

            // Explicit Up Next items remain separate from the active context and
            // stay in memory only. AdvanceQueue consumes them first.
            std::vector<winrt::Last_Music_Player::TrackInfo> UserQueue;

            bool ShuffleEnabled{ false };
            int RepeatMode{ 0 }; // 0 = Off, 1 = Repeat All, 2 = Repeat One
            std::vector<int> ShuffleOrder;
            size_t ShuffleCursor{ 0 };
        };

        // Autoplay fetches related tracks when the active context runs dry.
        // Epoch cancellation drops stale fetches and SeenKeys prevents duplicates.
        struct AutoplayState
        {
            bool InFlight{ false };
            uint64_t Epoch{ 0 };
            bool ResumeWhenReady{ false }; // queue hit the end mid-fetch; auto-start when tracks land
            std::unordered_set<std::wstring> SeenKeys;
        };

        struct CastSessionState
        {
            winrt::hstring DeviceId;
            winrt::hstring DeviceName;
            bool IsPlaying{ false };
            bool EngineWired{ false };
            double CurrentSeconds{ 0.0 };
            double DurationSeconds{ 0.0 };
            uint64_t ProgressStampMs{ 0 };
            uint64_t LastStatusRequestMs{ 0 };
        };

        struct HomeHydrationState
        {
            uint64_t StartupEpoch{ 0 };
            uint64_t HomeEpoch{ 0 };
            uint64_t MixRefreshId{ 0 };
            bool InFlight{ false };
            bool Pending{ false };
            bool PendingRefresh{ false };
        };

        struct AccountPlaylistBinding
        {
            winrt::Last_Music_Player::TrackInfo Playlist{ nullptr };
            LastMusicPlayer::Backend::RemoteScopeSnapshot Scope;
            std::wstring OwnerId;
        };

        struct AccountPlaylistDetailContext
        {
            AccountPlaylistBinding Binding;
            std::wstring DetailKey;
            std::wstring PlaylistId;
            uint64_t DetailEpoch{};
        };

        struct AccountArtworkTarget
        {
            winrt::Microsoft::UI::Xaml::Controls::Image Image{ nullptr };
            winrt::Last_Music_Player::TrackInfo Track{ nullptr };
            // Decode size for this surface alone. Targets of one request may
            // want very different sizes, so each gets its own bitmap.
            detail::ArtworkDetail Detail{};
        };

        // Which queue a pending request is waiting in. Tiles the user is looking
        // at are fetched first and with more parallelism; everything else fills
        // in behind them, so a page still ends up complete without the whole
        // page competing for the relay at once.
        enum class AccountArtworkPriority
        {
            Visible,
            Backfill,
        };

        struct AccountArtworkRequest
        {
            uint64_t Id{};
            winrt::hstring ArtworkUrl;
            winrt::hstring SourceUrl;
            std::vector<AccountArtworkTarget> Targets;
            AccountArtworkPriority Priority{ AccountArtworkPriority::Backfill };
            // Transport attempts already spent. A relay that is refusing a burst
            // fails many tiles at once, and dropping them there is what left
            // permanent gaps, so a transient failure is re-queued rather than
            // discarded until this reaches the ceiling.
            int Attempts{};
            // Set while the request sits in the deferred list, so a promotion to
            // Visible does not also queue a second copy of it.
            bool Deferred{};
        };

        // A request waiting out its backoff before it may be tried again.
        struct DeferredAccountArtworkRequest
        {
            std::wstring Key;
            std::chrono::steady_clock::time_point DueAt;
        };

        // The encoded bytes, not a decoded bitmap. Two Image elements must never
        // share a BitmapImage: XAML decodes it once, at the size of whichever
        // element renders it first, and every other element is then stuck
        // stretching that one decode. Caching the bytes lets each surface decode
        // its own copy, and costs far less memory than caching decoded pixels.
        struct AccountArtworkCacheEntry
        {
            winrt::Windows::Storage::Streams::IBuffer Bytes{ nullptr };
            winrt::hstring ArtworkUrl;
            winrt::hstring SourceUrl;
        };

        struct AppStateTrackSnapshot
        {
            winrt::hstring Title;
            winrt::hstring Artist;
            winrt::hstring Album;
            winrt::hstring Genre;
            winrt::hstring FilePath;
            winrt::hstring ArtworkUrl;
            winrt::hstring DateAdded;
            winrt::hstring Duration;
            winrt::hstring SourceKind;
            winrt::hstring Provider;
            winrt::hstring SourceUrl;
            winrt::hstring SourceLabel;
            winrt::hstring Key;
            double DurationSeconds{ 0.0 };
            double DateAddedSortKey{ 0.0 };
            bool IsLiked{ false };
            uint32_t PlayCount{ 0 };
            uint64_t LastPlayedOrder{ 0 };
        };

        struct AppStateSnapshot
        {
            winrt::hstring LastTrackPath;
            double Volume{ 0.7 };
            uint64_t HomePlaySequence{ 0 };
            std::vector<AppStateTrackSnapshot> HomeHistory;
        };

        void SaveAppState();
        [[nodiscard]] bool ClearSavedAppState();
        void LoadAppState();
        void ApplyAppStateSnapshot(AppStateSnapshot const& snapshot);
        AppStateSnapshot BuildAppStateSnapshot();
        // Resume-on-launch: restore the last-played track into the player bar
        // (paused, ready to resume on the first Play press). Skips tracks that
        // are no longer available — e.g. a local file deleted off disk.
        void RestoreLastPlayedTrack(AppStateSnapshot const& snapshot);
        void RestoreNowPlayingBar(winrt::Last_Music_Player::TrackInfo const& track);
        static winrt::hstring SerializeAppStateSnapshot(AppStateSnapshot const& snapshot);
        static AppStateSnapshot ParseAppStateSnapshot(winrt::hstring const& payload);
        static winrt::Last_Music_Player::TrackInfo TrackFromAppStateSnapshot(AppStateTrackSnapshot const& snapshot);
        void QueueStartupDataLoad(winrt::hstring savedLibraryPath);
        winrt::Windows::Foundation::IAsyncAction HydrateStartupAsync(winrt::hstring savedLibraryPath);
        winrt::Windows::Foundation::IAsyncAction HydrateHomeAsync(bool refreshProvider);
        winrt::Windows::Foundation::IAsyncAction RestoreAccountIntegrationAsync();
        void RefreshAccountSettingsUi();

        // --- Operation-mode setup (MainWindow.ModeSetup.cpp) ---
        // Settings only ever shows the active mode's connection card. API-key
        // credentials are typed straight into that card, so entering the mode is
        // all the setup it needs. Account is different: its session comes from a
        // browser round trip, so picking it while signed out runs sign-in first
        // and commits the switch only once that succeeds.

        // Puts the mode selector back on the committed mode. Always call this
        // instead of refreshing straight from RemoteMode_SelectionChanged: the
        // selector cannot be written while RadioButtons is still resolving its
        // own selection.
        winrt::Windows::Foundation::IAsyncAction ResyncRemoteModeSelectorAsync();
        winrt::Windows::Foundation::IAsyncAction BeginAccountSignInAsync();
        winrt::Windows::Foundation::IAsyncAction ShowAccountSignInDialogAsync();

        // Credential steps. Progress is reported through the caller-supplied
        // TextBlock, so the same logic serves the API-key card's status line and
        // the sign-in dialog's own.
        winrt::Windows::Foundation::IAsyncOperation<bool> ConnectApiKeyProviderAsync(
            winrt::hstring baseUrl,
            winrt::hstring apiKey,
            winrt::Microsoft::UI::Xaml::Controls::TextBlock status);
        winrt::Windows::Foundation::IAsyncOperation<bool> CompleteAccountSignInAsync(
            winrt::Microsoft::UI::Xaml::Controls::TextBlock status);

        winrt::Windows::Foundation::IAsyncAction OfferCompatibleHistoryImportAsync();
        winrt::Windows::Foundation::IAsyncAction SynchronizeAccountLibraryAsync(AccountSyncMode mode);

        // Runs a sync only if EvaluateAutoSync says the current conditions
        // allow it. Both background triggers go through here.
        winrt::Windows::Foundation::IAsyncAction RequestBackgroundAccountSyncAsync(
            LastMusicPlayer::Backend::AutoSyncTrigger trigger);
        void ApplyAutoSyncSetting();
        void OnWindowActivated(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::WindowActivatedEventArgs const& args);
        void FinishHomeHydration();
        winrt::Windows::Foundation::IAsyncAction EnsureSongsHydratedAsync(bool reset);
        winrt::Windows::Foundation::IAsyncAction AppendSongsPageAsync();
        winrt::Windows::Foundation::IAsyncAction LoadSongsQueueAndPlayAsync(winrt::Last_Music_Player::TrackInfo clickedTrack);
        winrt::Windows::Foundation::IAsyncAction HydrateLibraryTabAsync(winrt::hstring tab, bool force);
        winrt::Windows::Foundation::IAsyncAction AppendLibrarySongsPageAsync();
        winrt::Windows::Foundation::IAsyncAction HydrateLibraryDetailTracksAsync();
        winrt::Windows::Foundation::IAsyncAction LoadLibrarySongsQueueAndPlayAsync(winrt::Last_Music_Player::TrackInfo clickedTrack);
        winrt::Windows::Foundation::IAsyncAction LoadLibraryDetailQueueAndPlayAsync(winrt::Last_Music_Player::TrackInfo clickedTrack);
        // Pre-shuffles the detail-view track set then queues + plays it.
        // Drives the Shuffle button next to Play in the library detail
        // header — first played track is random, Up Next rail visibly
        // matches the random order (RebuildUpNextQueue is linear).
        winrt::Windows::Foundation::IAsyncAction LoadLibraryDetailQueueAndShuffleAsync();
        void MarkLibraryViewsDirty();
        void LoadSettingsIntoUi();
        void ApplyThemeFromSetting();
        void ApplyShowAlbumArt();
        void ApplyWindowsMediaControls();
        void ApplyAccentColor(winrt::hstring const& hex);
        std::wstring ScanFormatsCsvFromChips();
        std::vector<winrt::hstring> ScanFileExtensions();
        void EnsureTrayIcon();
        winrt::Windows::Foundation::IAsyncAction PopulateOutputDevicesAsync();
        winrt::Windows::Foundation::IAsyncAction ApplyOutputDeviceAsync();
        void ApplyDiscordPresence();
        void UpdateAboutStats();
        void UpdateDiscordNowPlaying(winrt::Last_Music_Player::TrackInfo const& track);
        bool IsDiscordPlaybackActive(winrt::Windows::Media::Playback::MediaPlaybackState state);
        void SampleDiscordPlaybackSnapshot(bool& isPlaying, double& positionSeconds, double& durationSeconds);
        void UpdateDiscordPlaybackState(bool isPlaying, double positionSeconds, double durationSeconds);
        void RefreshDiscordPresenceIfNeeded(bool isPlaying, double positionSeconds, double durationSeconds);
        winrt::Windows::Foundation::IAsyncAction ResolveDiscordArtworkAsync(
            winrt::hstring trackTitle, winrt::hstring trackArtist);
        void RebuildUpNextQueue();
        // Kick off background disk-prefetch of the next few upcoming remote
        // tracks so they play from a local file (no live stream) when reached.
        void PrefetchUpcomingStreams();
        // Autoplay: if enabled and the context is nearly exhausted, kick off an
        // async fetch of related tracks and append them to the playback queue.
        void MaybeExtendAutoplayQueue();
        winrt::fire_and_forget ExtendAutoplayQueueAsync(winrt::Last_Music_Player::TrackInfo seed, uint64_t epoch);
        void EnsurePlaybackQueueSeeded();
        void CommitQueueOrderFromUpNext();
        void RemoveTrackFromQueue(winrt::Last_Music_Player::TrackInfo const& track);
        void MoveQueueTrack(winrt::Last_Music_Player::TrackInfo const& track, int delta);
        void AdvanceQueue(int direction, bool isAutoAdvance);
        void RebuildShuffleOrder();
        void UpdateShuffleRepeatVisuals();
        void RestorePlaybackPreferences();
        void UpdateVolumeIcon(double volume);
        void QueueVolumePersist(double volume);
        void FlushPendingVolumePersist();
        void UpdateFullScreenNowPlaying(winrt::Last_Music_Player::TrackInfo const& track);
        winrt::Windows::Foundation::IAsyncAction PopulateCastMenuAsync();
        winrt::Windows::Foundation::IAsyncAction StartCastAsync(winrt::hstring deviceId, winrt::hstring host, winrt::hstring deviceName);
        void StopCastAsync();
        void WireCastEngine();
        void ClearCastCallbacks();
        void TransportPlay();
        void TransportPause();
        void TransportSeekSeconds(double seconds);
        void TransportSetVolume(double volume);
        // Opens one top-level page and closes the rest. Every page is a sibling
        // container that is only shown or collapsed, so switching means naming
        // all of them; doing that by hand at each call site left two pages
        // stacked whenever one was missed, and nothing in the build catches it.
        void ShowPrimaryView(winrt::hstring const& key);
        void ResetLibraryScopeToAll();
        void OpenLibraryHistory();
        void OpenLibraryPlaylists(bool autoMixes);
        void OpenLibraryAutoMixes();
        winrt::Windows::Foundation::IAsyncAction OpenLibrarySystemPlaylistAsync(
            winrt::hstring playlistKey);
        void UpdateNavSelection(winrt::hstring const& key);
        void SetRailTab(bool upNext);
        void ApplyRightRailWidth();
        void EnsureAccentBrushes();
        void InvalidateRemoteViewWork();
        void InvalidateRemoteScopeWork();
        void BindAccountPlaylist(
            winrt::Last_Music_Player::TrackInfo const& playlist,
            LastMusicPlayer::Backend::RemoteScopeSnapshot const& scope,
            std::wstring const& ownerId);
        std::optional<AccountPlaylistBinding> AccountPlaylistBindingFor(
            winrt::Last_Music_Player::TrackInfo const& playlist) const;
        bool IsCurrentAccountPlaylistBinding(AccountPlaylistBinding const& binding);
        bool IsAccountPlaylistDetail() const noexcept;
        std::optional<AccountPlaylistDetailContext> CaptureAccountPlaylistDetailContext();
        bool IsCurrentAccountPlaylistDetailContext(
            AccountPlaylistDetailContext const& context);
        void UpdateSongsStats();
        void UpdateHistoryCount();
        void UpdateLibraryHeaderMetrics();
        void LoadCatalogFromDatabase();
        void RefreshLibraryCatalogViews();
        void RefreshAutoPlaylists();
        void UpdateLibraryActionButtons();
        int64_t PersistTrackForPlaylist(winrt::Last_Music_Player::TrackInfo const& track);
        LastMusicPlayer::Backend::TrackQuery CurrentSongsQuery(uint32_t offset, uint32_t limit) const;
        LastMusicPlayer::Backend::TrackQuery CurrentLibrarySongsQuery(uint32_t offset, uint32_t limit) const;
        LastMusicPlayer::Backend::TrackQuery CurrentLibraryDetailQuery(
            uint32_t offset,
            uint32_t limit,
            std::wstring const& accountOwnerId) const;
        void ApplySongsFilterSort();
        void AppendTrackPage(std::vector<winrt::Last_Music_Player::TrackInfo> const& source, winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> target, uint32_t pageSize);
        void AppendSongsPage();
        void MaybeAppendSongsPage(uint32_t itemIndex);
        void AppendLibrarySongsPage();
        void MaybeAppendLibrarySongsPage(uint32_t itemIndex);
        void AppendLibraryDetailPage();
        void MaybeAppendLibraryDetailPage(uint32_t itemIndex);
        void SetSongsGridMode(bool gridMode);
        void SetHistoryGridMode(bool gridMode);
        void ObserveLibraryScroll(winrt::Microsoft::UI::Xaml::FrameworkElement const& surface);
        void UpdateLibraryHeaderForScroll(winrt::Microsoft::UI::Xaml::Controls::ScrollViewer const& scroller);
        void SetLibraryHeaderCollapsed(bool collapsed);
        void QueueAndPlayVisible(std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks, winrt::Last_Music_Player::TrackInfo const& clickedTrack);
        void QueueAndPlayObservable(winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> const& tracks, winrt::Last_Music_Player::TrackInfo const& clickedTrack);
        winrt::Last_Music_Player::TrackInfo TrackFromActionSender(winrt::Windows::Foundation::IInspectable const& sender);
        void PlaySongsTrack(winrt::Last_Music_Player::TrackInfo const& track);
        void PlayNextFromSongs(winrt::Last_Music_Player::TrackInfo const& track);
        void AddSongsTrackToQueue(winrt::Last_Music_Player::TrackInfo const& track);
        // Bulk variants that preserve list order (the single-track Play next
        // path prepends to UserQueue, so a naive loop reverses the input).
        // Both filter through IsPlayableHomeTrack + dedup against UserQueue
        // and call RebuildUpNextQueue once at the end.
        void PlayNextFromSongsBulk(std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks);
        void AddSongsTracksToQueueBulk(std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks);
        // Resolves a group-card sentinel TrackInfo (album / artist / genre /
        // playlist / auto-playlist) to its concrete playable tracks. Uses
        // DatabaseService().LoadTracksForGroup for DB-backed kinds and
        // m_homeMixes for auto-playlists (synthesized client-side).
        std::vector<winrt::Last_Music_Player::TrackInfo> TracksForGroupCard(winrt::Last_Music_Player::TrackInfo const& group);
        // Force-enables shuffle (no toggle) — used by group-menu Shuffle
        // items so the user can shuffle a group without it depending on
        // the prior shuffle state. Persists the setting + refreshes icons.
        void EnsureShuffleOn();
        winrt::Windows::Foundation::IAsyncAction AddTrackToPlaylistAsync(winrt::Last_Music_Player::TrackInfo track);
        void ToggleTrackLiked(winrt::Last_Music_Player::TrackInfo const& track);
        void OpenSongsTrackArtist(winrt::Last_Music_Player::TrackInfo const& track);
        void OpenSongsTrackAlbum(winrt::Last_Music_Player::TrackInfo const& track);
        void ShowLibraryDetail(
            winrt::hstring const& kind,
            winrt::hstring const& key,
            winrt::hstring const& title,
            winrt::hstring const& subtitle,
            winrt::Microsoft::UI::Xaml::Media::ImageSource const& fallbackArt = nullptr,
            winrt::Last_Music_Player::TrackInfo const& sourceGroup = nullptr);
        [[nodiscard]] bool IsAutoMixDetail(std::wstring const& key) const;
        // What follows the song count in the detail header, empty when the
        // count already says everything the reference clients show there.
        [[nodiscard]] std::wstring LibraryDetailMeta(
            std::wstring const& kind,
            std::wstring const& key,
            winrt::hstring const& cardCaption) const;
        [[nodiscard]] winrt::hstring LibraryDetailEyebrow(
            std::wstring const& kind,
            std::wstring const& key) const;
        [[nodiscard]] winrt::hstring LibraryDetailCountText(int count) const;
        void ApplyLibraryDetailPlaylistCollage();
        void HideLibraryDetail();
        void ApplyPlaybackProgress(double currentSeconds, double totalSeconds);
        void RefreshPlaybackProgress();
        void HydrateLyricsForCurrentTrack();
        // Debounced track-change entry point: starts/restarts a 350ms
        // DispatcherTimer that finally calls HydrateLyricsForCurrentTrack().
        // Rapid skips collapse into a single provider request instead
        // of firing N concurrent ones.
        void QueueLyricsHydration();
        winrt::fire_and_forget HydrateLyricsAsync(
            winrt::hstring artist,
            winrt::hstring title,
            winrt::hstring album,
            int64_t durationMs,
            winrt::hstring sourceUrl,
            std::wstring key,
            uint64_t epoch);
        void RenderLyricsResult(LastMusicPlayer::Backend::LyricsResult const& result);
        void UpdateActiveLyricLine(int64_t positionMs);
        void ResetLyricsViewToEmpty(winrt::hstring const& message);
        void PersistTrackPlayback(
            winrt::Last_Music_Player::TrackInfo const& track,
            double positionSeconds,
            std::wstring const& accountOwnerId,
            uint64_t accountGeneration);
        void BeginPlaybackQualification(winrt::Last_Music_Player::TrackInfo const& track);
        void ObservePlaybackQualification(bool playing, double positionSeconds);
        void UpdateLikeButton(winrt::Last_Music_Player::TrackInfo const& track);
        void ApplySettingsResponsiveLayout(double width);
        void ApplyLibraryHeaderResponsive(double width);
        void BuildEqualizerBars();
        void SyncActiveEqualizerPreset();
        void PopulateHomeFromLibrary();
        void BuildHomeMixes();
        void PlayHomeMix(winrt::hstring const& mixId);
        void RecordHomePlayback(winrt::Last_Music_Player::TrackInfo const& track);
        winrt::Windows::Foundation::IAsyncAction RefreshHomeProviderMixesAsync(uint64_t refreshId);
        bool PlayTrack(winrt::Last_Music_Player::TrackInfo const& track, bool gaplessTransitioned = false);
        winrt::Windows::Foundation::IAsyncAction ResolveRemotePlaybackAsync(
            winrt::Last_Music_Player::TrackInfo track,
            bool startAsNewPlay,
            bool gaplessTransitioned,
            double resumeSeconds,
            uint64_t resolveEpoch);
        // Queue account artwork through the authenticated relay. Requests sharing
        // a URL are coalesced, and each target is revalidated after the async work
        // so recycled containers and newer now-playing tracks win.
        void QueueAccountArtworkImage(
            winrt::Microsoft::UI::Xaml::Controls::Image const& image,
            winrt::hstring const& artworkUrl,
            detail::ArtworkDetail artworkDetail,
            winrt::Last_Music_Player::TrackInfo const& track = nullptr);
        // Decodes `bytes` into a bitmap of this target's own, then revalidates
        // the target before assigning it, because the decode yields the thread.
        winrt::fire_and_forget ApplyAccountArtworkTargetAsync(
            AccountArtworkTarget target,
            winrt::Windows::Storage::Streams::IBuffer bytes,
            winrt::hstring requestKey,
            winrt::hstring artworkUrl,
            winrt::hstring sourceUrl);
        void StartAccountArtworkRequests();
        void CompleteAccountArtworkRequest(AccountArtworkPriority priority);
        winrt::fire_and_forget HydrateAccountArtworkAsync(
            winrt::hstring requestKey,
            uint64_t requestId,
            AccountArtworkPriority priority);
        void ClearAccountArtworkCache();

        // Moves a pending request to the front of the visible queue. Called when
        // a tile scrolls into view, so what the user is looking at overtakes the
        // backfill that was queued ahead of it.
        void PromoteAccountArtworkRequest(std::wstring const& key);

        // Requests artwork as containers are realized and recycled.
        //
        // The Loaded event cannot carry this on a virtualized list: it fires
        // once per element, but a GridView reuses one element for many items, so
        // every container realized in a later layout pass or reused on scroll
        // was silently left without artwork. This fires for each container every
        // time it takes on an item, which is exactly when the request is wanted,
        // and it fires as the user scrolls a shelf into view, which is what puts
        // that shelf ahead of the rest.
        void CatalogGrid_ContainerContentChanging(
            winrt::Microsoft::UI::Xaml::Controls::ListViewBase const& sender,
            winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);

        // Attaches the handler above to a grid built in markup or in code.
        void ObserveCatalogGridContainers(
            winrt::Microsoft::UI::Xaml::Controls::ListViewBase const& grid);

        // Wires the container hook onto every virtualized surface declared in
        // markup. Called once during construction, alongside AttachSkeletons.
        void AttachArtworkGridObservers();

        // Puts previous/next buttons on the horizontal rows declared in markup.
        // The catalog shelves get theirs as they are built instead, since they
        // do not exist yet at construction.
        void AttachCarouselArrows();

        // Watches a tile so its artwork is fetched at visible priority once it
        // reaches the viewport. The registration removes itself after it fires,
        // since a tile only needs promoting once.
        void ObserveAccountArtworkViewport(
            winrt::Microsoft::UI::Xaml::Controls::Image const& image,
            std::wstring const& key);

        // Puts a transiently failed request back in line after a backoff, and
        // arms the timer that returns due requests to the queue.
        void DeferAccountArtworkRequest(std::wstring const& key, int attempts);
        void EnsureAccountArtworkRetryTimer();
        void ReleaseDueAccountArtworkRequests();

        // ----- Home catalog discovery -----
        enum class CatalogSurface
        {
            Home,
            ChartGallery,
            Chart,
            Detail,
        };

        void ShowCatalogSurface(CatalogSurface surface, bool pushCurrent = true);
        void ShowHomeCatalogStatus(winrt::hstring const& message, bool loading, bool canRetry);

        // Binds every loading placeholder to its host panel. Called once during
        // construction; the presenters build their trees lazily on first use.
        void AttachSkeletons();

        // Every library tab draws into one shared host, so the placeholder is
        // re-shaped to match whichever tab is about to load.
        void BeginLibraryTabSkeleton(LastMusicPlayer::Frontend::SkeletonShape shape);
        void UpdateCatalogAvailability();
        winrt::hstring CurrentDiscoverStorefront();
        winrt::Windows::Foundation::IAsyncAction HydrateDiscoverAsync(bool force);

        // Puts the last catalog payload that worked back on screen so a relaunch
        // is not a blank wait for the network. Reads and parses off the UI
        // thread, then applies on it, and returns whether anything was applied.
        // Declines to apply if the epoch or account scope moved while it was
        // reading, so it can never overwrite a newer load with an older one.
        winrt::Windows::Foundation::IAsyncOperation<bool> RestoreCachedDiscoveryAsync(
            winrt::hstring storefront,
            std::uint64_t epoch,
            LastMusicPlayer::Backend::RemoteScopeSnapshot scope);

        // Records a payload that parsed into real shelves, so the next launch
        // has something to draw. Fire-and-forget: a failed write only costs the
        // next start a round trip, so it never blocks or reports.
        void CacheDiscoveryPayload(winrt::hstring const& storefront, winrt::hstring const& payload);
        winrt::Windows::Foundation::IAsyncAction OpenDiscoverChartAsync(
            LastMusicPlayer::Backend::CatalogChartRef chart,
            winrt::hstring title);
        void OpenDiscoverChartGallery(
            winrt::hstring const& title,
            std::vector<LastMusicPlayer::Backend::CatalogChartDescriptor> const& charts,
            bool partial);
        void OpenDiscoverShelf(
            LastMusicPlayer::Backend::CatalogShelf const& shelf,
            winrt::hstring const& eyebrow);
        winrt::Windows::Foundation::IAsyncAction LoadDiscoverChartPageAsync(int32_t offset);
        winrt::Windows::Foundation::IAsyncAction OpenDiscoverResourceAsync(
            winrt::hstring kind,
            winrt::hstring catalogId,
            winrt::hstring title);
        void BuildHomeCatalog(LastMusicPlayer::Backend::CatalogDiscovery const& discovery);
        void RebuildCatalogSurfaces();
        winrt::Microsoft::UI::Xaml::Controls::StackPanel BuildCatalogShelfSection(
            LastMusicPlayer::Backend::CatalogShelf const& shelf,
            winrt::hstring const& title,
            winrt::hstring const& pill,
            std::size_t previewLimit,
            bool forceSeeAll);
        winrt::Microsoft::UI::Xaml::Controls::StackPanel BuildCatalogChartSection(
            winrt::hstring const& title,
            std::vector<LastMusicPlayer::Backend::CatalogChartDescriptor> const& charts,
            bool partial);
        winrt::Microsoft::UI::Xaml::Controls::Button BuildCatalogChartCard(
            LastMusicPlayer::Backend::CatalogChartDescriptor const& chart);
        void PopulateDiscoverStorefronts(
            std::vector<LastMusicPlayer::Backend::CatalogStorefront> const& storefronts);
        winrt::Last_Music_Player::TrackInfo CatalogItemToTrack(
            LastMusicPlayer::Backend::CatalogItem const& item,
            int32_t index);


        // Show a short message in the player bar for a few seconds.
        void ShowPlaybackNotice(winrt::hstring const& message);
        // Report an unresolvable remote track and either step past it or stop,
        // instead of leaving the queue parked on a track that never starts.
        void HandleRemoteResolveFailure(
            winrt::Last_Music_Player::TrackInfo const& track,
            bool startAsNewPlay,
            uint64_t resolveEpoch);
        void ApplyEqualizerFromSettings();
        void ApplyEqualizerPreset(std::array<double, 10> const& gains);
        winrt::Windows::Media::Core::MediaSource BuildMediaSourceForTrack(winrt::Last_Music_Player::TrackInfo const& track);
        int ComputeAutoAdvanceIndex(int currentIdx, int queueSize) const;
        void RefreshGaplessLookahead();
        void OnPlaybackListCurrentItemChanged(
            winrt::Windows::Media::Playback::MediaPlaybackList const& sender,
            winrt::Windows::Media::Playback::CurrentMediaPlaybackItemChangedEventArgs const& args);
        void ApplyGaplessSetting();
        void SetPlaybackQueue(std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks, int selectedIndex);
        void EnterSearchMode(winrt::hstring const& query);
        void ExitSearchMode();
        void ShowBrowseLanding(bool showMinimumLengthHint);
        void ShowBrowseSearchLoading();
        void ShowBrowseSearchResults(winrt::hstring const& query);
        void ShowBrowseSearchError(winrt::hstring const& message);
        void SetSearchGridMode(bool gridMode);
        void ApplySearchResultSort();
        winrt::Windows::Foundation::IAsyncAction HydrateBrowseLandingAsync(bool force);
        void RunDebouncedHomeSearch();
        winrt::Windows::Foundation::IAsyncAction RunHomeSearchAsync();
        winrt::Windows::Foundation::IAsyncAction RunHomeSearchNowAsync(winrt::hstring query, uint64_t requestId);
        void SetLibraryScanUi(bool visible, winrt::hstring const& status, bool canCancel);

        LibraryScanState m_libraryScan;
        bool m_cleanupInProgress{ false };
        PlaybackQueueState m_queue;
        AutoplayState m_autoplay;
        // Consecutive PlayTrack-failure counter. Bounded retry inside
        // AdvanceQueue uses this to skip past broken tracks instead of
        // looping on the same one, and to give up after a few failures
        // rather than spinning forever on a fully-broken queue.
        int m_advancePlayFailures{ 0 };
        // Consecutive remote-resolve failures. PlayTrack reports success as soon
        // as it hands a track to the resolver, so m_advancePlayFailures never sees
        // these; without a separate bound, a queue of unresolvable tracks would
        // advance forever. Cleared by any successful resolve.
        int m_remoteResolveFailures{ 0 };
        winrt::Microsoft::UI::Xaml::DispatcherTimer m_playbackNoticeTimer{ nullptr };
        // Encoded account artwork keyed by durable source URL. Realized controls
        // share both in-flight requests and fetched bytes, while the budget
        // below stops a long browsing session retaining every image it has seen.
        std::unordered_map<std::wstring, AccountArtworkCacheEntry>
            m_accountArtworkCache;
        std::size_t m_accountArtworkCacheBytes{ 0 };
        std::unordered_map<std::wstring, AccountArtworkRequest> m_accountArtworkRequests;
        // Two lines rather than one: the visible queue is drained first and to a
        // higher concurrency, so scrolling to a shelf does not wait behind every
        // tile queued before it.
        std::deque<std::wstring> m_accountArtworkVisibleQueue;
        std::deque<std::wstring> m_accountArtworkBackfillQueue;
        std::vector<DeferredAccountArtworkRequest> m_deferredAccountArtwork;
        std::size_t m_activeAccountArtworkRequests{ 0 };
        std::size_t m_activeAccountArtworkBackfill{ 0 };
        uint64_t m_accountArtworkRequestId{ 0 };
        // Returns deferred requests to the queue. Only armed while something is
        // actually waiting, so an idle page runs no timer at all.
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_accountArtworkRetryTimer{ nullptr };

        // ----- Home catalog discovery -----
        // Generation counter for discovery work. A storefront switch or a sign-out
        // while a request is in flight has to be able to discard its result.
        uint64_t m_discoverEpoch{ 0 };
        uint64_t m_discoverNavigationEpoch{ 0 };
        bool m_discoverLoaded{ false };
        bool m_discoverInFlight{ false };
        bool m_discoverPendingRefresh{ false };
        bool m_suppressDiscoverStorefrontChange{ false };
        winrt::hstring m_discoverStorefront;
        LastMusicPlayer::Backend::CatalogDiscovery m_catalogDiscovery;
        CatalogSurface m_catalogSurface{ CatalogSurface::Home };
        std::vector<CatalogSurface> m_catalogBackStack;
        std::vector<LastMusicPlayer::Backend::CatalogChartDescriptor> m_catalogGalleryCharts;
        winrt::hstring m_catalogContentStorefront;
        std::unordered_map<std::wstring, bool> m_catalogLikeOverrides;
        // Chart paging state for the currently open chart page.
        LastMusicPlayer::Backend::CatalogChartRequest m_discoverChartRequest;
        int32_t m_discoverChartNextOffset{ 0 };
        bool m_discoverChartHasMore{ false };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_discoverChartItems{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_discoverDetailTracks{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        // Last sampled playback position (seconds) for the local sink. Used to
        // resume near where a stream dropped when OnMediaFailed re-opens it.
        double m_lastPlaybackPositionSeconds{ 0.0 };
        LastMusicPlayer::Backend::PlaybackHistoryQualifier m_playbackHistoryQualifier;
        winrt::Last_Music_Player::TrackInfo m_pendingPlaybackTrack{ nullptr };
        std::wstring m_pendingPlaybackIdentity;
        std::wstring m_pendingPlaybackEventId;
        std::wstring m_pendingPlaybackOwnerId;
        std::wstring m_pendingPlaybackRemoteId;
        uint64_t m_pendingPlaybackAccountGeneration{};
        // Bounded stream auto-recovery: cap reopen attempts within a window so a
        // genuinely broken/unsupported source can't loop forever.
        int m_streamRecoverAttempts{ 0 };
        unsigned long long m_lastStreamRecoverTickMs{ 0 };
        // >= 0 means "seek here once the (re-opened) media reports MediaOpened".
        double m_pendingResumeSeekSeconds{ -1.0 };
        // Every explicit play or recovery invalidates older remote-resolution
        // work so a late network response cannot replace a newer track.
        uint64_t m_remotePlaybackResolveEpoch{ 0 };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_homeTracks{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_recentlyAddedTracks{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        // Top tracks by PlayCount — populates the Most Played carousel.
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_homeMostPlayedTracks{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        // User-liked tracks — populates the Liked Songs Highlights carousel.
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_homeLikedTracks{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_songsTracks{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_searchTracks{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_browseCategories{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_librarySongs{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_libraryGenres{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        // Display projection for the default playlist segment. The first three
        // entries are read-only system playlists; m_manualPlaylists remains the
        // editable-only authority used by create, rename, delete, and add flows.
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_yourPlaylists{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_manualPlaylists{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_autoPlaylists{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_sidebarPlaylists{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_libraryDetailTracks{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_upNextQueue{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_albums{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> m_artists{
            winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>()
        };
        bool m_isMuted{ false };
        double m_volumeBeforeMute{ 50.0 }; // slider units (0-100)
        // Suppresses VolumeSlider_ValueChanged persistence. Defaults TRUE so
        // the slider's XAML default (Value="50") and any template/startup
        // ValueChanged cannot clobber the saved level before it is restored.
        // The Loaded restore flips it false once the real value is applied;
        // mute also raises it transiently so muting never persists 0.
        bool m_suppressVolumePersist{ true };
        bool m_suppressRemoteModeChange{ false };
        winrt::Microsoft::UI::Xaml::DispatcherTimer m_volumePersistTimer{ nullptr };
        bool m_volumePersistQueued{ false };
        double m_pendingPersistedVolume{ 0.7 };
        bool m_queueRailForced{ false };
        bool m_fullScreenOpen{ false };
        PlaybackSink m_sink{ PlaybackSink::Local };
        CastSessionState m_castSession;
        LastMusicPlayer::Backend::CastEngine m_cast;
        std::vector<winrt::Last_Music_Player::TrackInfo> m_homeRecentHistory;
        std::unordered_map<std::wstring, uint32_t> m_homePlayCounts;
        std::unordered_map<std::wstring, uint64_t> m_homeLastPlayedOrder;
        std::unordered_map<std::wstring, std::vector<winrt::Last_Music_Player::TrackInfo>> m_homeMixes;
        // Genre-clustered Daily Mix inputs, loaded off the UI thread during home
        // hydration. m_homeRankedGenres is play-weighted (LoadTopGenres),
        // m_homeGenrePools maps each ranked genre to its full DB track set, and
        // m_homeMixGenres records which genre (if any) drives each daily mix id
        // — used for the dynamic card labels and the provider-fill queries.
        std::vector<std::wstring> m_homeRankedGenres;
        std::unordered_map<std::wstring, std::vector<winrt::Last_Music_Player::TrackInfo>> m_homeGenrePools;
        std::unordered_map<std::wstring, std::wstring> m_homeMixGenres;
        std::unordered_map<std::wstring, std::vector<winrt::Last_Music_Player::TrackInfo>> m_remoteSearchCache;
        LastMusicPlayer::Backend::LibraryStats m_libraryStats;
        std::vector<winrt::Last_Music_Player::TrackInfo> m_catalogTracks;
        std::vector<winrt::Last_Music_Player::TrackInfo> m_searchAllResults;
        std::vector<winrt::Last_Music_Player::TrackInfo> m_songsAllResults;
        std::vector<winrt::Last_Music_Player::TrackInfo> m_librarySongAllResults;
        std::vector<winrt::Last_Music_Player::TrackInfo> m_libraryDetailAllResults;
        std::wstring m_songsFilter{ L"All" };
        std::wstring m_songsSort{ L"DateAdded" };
        bool m_songsGridMode{ true };
        bool m_catalogLoaded{ false };
        bool m_songsResultsValid{ false };
        bool m_songsPageLoading{ false };
        bool m_librarySongsPageLoading{ false };
        bool m_libraryDetailPageLoading{ false };
        uint64_t m_songsPageLoadId{ 0 };
        uint64_t m_librarySongsPageLoadId{ 0 };
        uint64_t m_libraryDetailPageLoadId{ 0 };
        int m_songsMatchedCount{ 0 };
        double m_songsMatchedSeconds{ 0.0 };
        int m_librarySongsMatchedCount{ 0 };
        double m_librarySongsMatchedSeconds{ 0.0 };
        int m_libraryDetailMatchedCount{ 0 };
        double m_libraryDetailMatchedSeconds{ 0.0 };
        LoadState m_homeLoadState{ LoadState::NotLoaded };
        LoadState m_songsLoadState{ LoadState::NotLoaded };
        LoadState m_libraryAlbumsState{ LoadState::NotLoaded };
        LoadState m_libraryArtistsState{ LoadState::NotLoaded };
        LoadState m_librarySongsState{ LoadState::NotLoaded };
        LoadState m_libraryGenresState{ LoadState::NotLoaded };
        LoadState m_libraryPlaylistsState{ LoadState::NotLoaded };
        LoadState m_libraryDetailState{ LoadState::NotLoaded };
        std::wstring m_libraryPlaylistFilter{ L"Manual" };
        // Which query the shared history/most-played panel is showing.
        bool m_libraryHeaderCollapsed{ false };
        double m_libraryHeaderHeight{ 0.0 };
        std::wstring m_libraryTracksFilter{ L"History" };
        std::wstring m_libraryHistorySort{ L"Relevance" };
        std::wstring m_libraryScope{ L"All" };
        bool m_libraryHistoryGridMode{ true };
        std::vector<AccountPlaylistBinding> m_accountPlaylistBindings;
        std::optional<AccountPlaylistBinding> m_libraryDetailAccountBinding;
        std::wstring m_libraryDetailKind;
        std::wstring m_libraryDetailKey;
        std::wstring m_libraryDetailSubtitle;
        // The subtitle the caller handed us is the collection card's caption,
        // which the detail header cannot print verbatim; this is what it prints.
        std::wstring m_libraryDetailMeta;
        winrt::Microsoft::UI::Xaml::Media::ImageSource m_libraryDetailFallbackArt{ nullptr };
        uint64_t m_homePlaySequence{ 0 };
        // Last-played track restored on launch but not yet loaded into the audio
        // engine; the first Play press plays it. Cleared once any track plays.
        winrt::Last_Music_Player::TrackInfo m_pendingResumeTrack{ nullptr };
        HomeHydrationState m_homeHydration;
        uint64_t m_songsHydrationEpoch{ 0 };
        uint64_t m_libraryHydrationEpoch{ 0 };
        uint64_t m_libraryDetailHydrationEpoch{ 0 };
        uint64_t m_searchDebounceId{ 0 };
        uint64_t m_searchRequestId{ 0 };
        uint64_t m_browseLandingEpoch{ 0 };
        uint64_t m_nowPlayingArtworkEpoch{ 0 };
        winrt::Microsoft::UI::Xaml::DispatcherTimer m_playbackProgressTimer{ nullptr };
        std::wstring m_searchSort{ L"Relevance" };
        bool m_searchGridMode{ true };
        bool m_browseLandingLoaded{ false };
        bool m_isSearchMode{ false };
        bool m_isUpdatingSlider = false;
        bool m_loadingSettings = false;
        // Suppresses re-entrant chip Checked events when SyncActiveEqualizerPreset
        // or ApplyEqualizerPreset programmatically toggles preset chips.
        bool m_skipPresetSync = false;
        bool m_forceExit = false;
        HWND m_hwnd{ nullptr };
        // SMTC bound to our window via ISystemMediaTransportControlsInterop.
        // Unpackaged Win32 / WinAppSDK apps need this to participate in the
        // OS's global media-key routing; the SMTC returned by
        // MediaPlayer.SystemMediaTransportControls is player-internal and
        // doesn't propagate to the Win+V quick-settings session list, so
        // media keys ignore us. Initialized once after m_hwnd is known.
        winrt::Windows::Media::SystemMediaTransportControls m_windowSmtc{ nullptr };
        winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        std::shared_ptr<LastMusicPlayer::Backend::TrayIcon> m_trayIcon;
        std::shared_ptr<LastMusicPlayer::Backend::DiscordPresence> m_discord;
        uint64_t m_discordReconnectAttemptMs{ 0 };
        uint64_t m_discordPresenceRefreshMs{ 0 };
        std::vector<winrt::hstring> m_outputDeviceIds;
        std::vector<winrt::Microsoft::UI::Xaml::Controls::Slider> m_equalizerSliders;
        bool m_xamlReadyForEvents = false;
        winrt::hstring m_currentNav{ L"Home" };

        // Periodic pull for a signed-in account. Owned here so it dies with the
        // window; its tick captures a weak reference back, because a running
        // timer is kept alive by the dispatcher queue and a strong capture
        // would drag the window along with it.
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_autoSyncTimer{ nullptr };

        // Loading placeholders, one per surface. Each owns its own delay gate,
        // so a load that resolves quickly never puts anything on screen.
        LastMusicPlayer::Frontend::SkeletonPresenter m_catalogSkeleton;
        LastMusicPlayer::Frontend::SkeletonPresenter m_listenAgainSkeleton;
        LastMusicPlayer::Frontend::SkeletonPresenter m_recentlyAddedSkeleton;
        LastMusicPlayer::Frontend::SkeletonPresenter m_songsSkeleton;
        // Every library tab shares one host inside LibraryTabsContent, so this
        // is re-attached with the shape of whichever tab is loading. The shape
        // is tracked to avoid rebuilding the tree when it has not changed.
        LastMusicPlayer::Frontend::SkeletonPresenter m_libraryTabsSkeleton;
        LastMusicPlayer::Frontend::SkeletonShape m_libraryTabSkeletonShape{
            LastMusicPlayer::Frontend::SkeletonShape::TrackList };
        LastMusicPlayer::Frontend::SkeletonPresenter m_libraryDetailSkeleton;
        LastMusicPlayer::Frontend::SkeletonPresenter m_discoverDetailSkeleton;
        LastMusicPlayer::Frontend::SkeletonPresenter m_discoverChartSkeleton;
        LastMusicPlayer::Frontend::SkeletonPresenter m_searchSkeleton;

        // Stamped at launch, and thereafter whenever a background sync actually
        // starts, so the focus debounce measures real traffic rather than
        // triggers that were skipped. Optional rather than a sentinel time
        // because subtracting from time_point::min() would overflow.
        std::optional<std::chrono::steady_clock::time_point> m_lastAutoSyncAttempt;

        // Nav/accent brushes captured off the resolved XAML so we never do a
        // programmatic lookup of ThemeDictionaries keys (a WinUI pitfall).
        bool m_accentBrushesCaptured = false;
        winrt::Microsoft::UI::Xaml::Media::Brush m_brushAccent{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Brush m_brushAccentSoft{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Brush m_brushStroke{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Brush m_brushGlyphIdle{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Brush m_brushLabelIdle{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Brush m_brushTransparent{ nullptr };

        // Lyrics state. Service holds HTTP + parse + cache; here we only retain
        // what the highlight tick needs on each frame.
        std::shared_ptr<LastMusicPlayer::Backend::LyricsService> m_lyricsService;
        std::vector<LastMusicPlayer::Backend::LyricLine> m_currentLyricsSynced;
        int32_t m_currentLyricLineIndex{ -1 };
        std::wstring m_lyricsLoadedKey;
        uint64_t m_lyricsHydrationEpoch{ 0 };
        // Debounce timer for lyrics fetches on rapid track changes;
        // see QueueLyricsHydration().
        winrt::Microsoft::UI::Xaml::DispatcherTimer m_lyricsHydrationTimer{ nullptr };
        bool m_railOnLyrics{ false };
    };
}

namespace winrt::Last_Music_Player::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
