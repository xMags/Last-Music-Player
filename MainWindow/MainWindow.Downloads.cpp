#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include <shobjidl.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    namespace
    {
        namespace MUX = winrt::Microsoft::UI::Xaml;
        namespace MUXA = winrt::Microsoft::UI::Xaml::Automation;
        namespace MUXC = winrt::Microsoft::UI::Xaml::Controls;
        using LastMusicPlayer::Backend::DownloadItemState;

        std::wstring FormatBytes(std::uint64_t bytes)
        {
            static constexpr wchar_t const* units[]{ L"B", L"KB", L"MB", L"GB", L"TB" };
            auto value = static_cast<double>(bytes);
            std::size_t unit{};
            while (value >= 1024.0 && unit + 1 < std::size(units))
            {
                value /= 1024.0;
                ++unit;
            }
            wchar_t text[48]{};
            auto const decimals = unit == 0 ? 0 : (value >= 10.0 ? 1 : 2);
            swprintf_s(text, L"%.*f %s", decimals, value, units[unit]);
            return text;
        }

        std::wstring JobStatusText(LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
        {
            auto const total = job.Items.size();
            auto const completed = job.CompletedItems;
            switch (job.State)
            {
            case DownloadItemState::Downloading:
            {
                auto text = std::to_wstring(completed) + L" of " + std::to_wstring(total);
                if (job.BytesPerSecond > 0)
                {
                    text += L"  ·  " + FormatBytes(job.BytesPerSecond) + L"/s";
                    if (job.BytesTotal > job.BytesDownloaded)
                    {
                        auto const seconds = (job.BytesTotal - job.BytesDownloaded) / job.BytesPerSecond;
                        text += L"  ·  " + std::to_wstring((seconds + 59) / 60) + L" min left";
                    }
                }
                return text;
            }
            case DownloadItemState::Paused:
            {
                auto const percent = job.BytesTotal > 0
                    ? static_cast<int>(job.BytesDownloaded * 100 / job.BytesTotal)
                    : 0;
                return L"Paused  ·  " + std::to_wstring(percent) + L"%";
            }
            case DownloadItemState::Completed:
                return std::to_wstring(total) + (total == 1 ? L" track  ·  " : L" tracks  ·  ")
                    + FormatBytes(job.BytesDownloaded);
            case DownloadItemState::Failed:
                return job.Error.empty() ? L"Download failed" : job.Error;
            case DownloadItemState::Queued:
            default:
                return std::to_wstring(total) + (total == 1 ? L" track queued" : L" tracks queued");
            }
        }

        bool InTab(DownloadItemState state, std::wstring const& tab)
        {
            if (tab == L"Active")
            {
                return state == DownloadItemState::Downloading || state == DownloadItemState::Paused;
            }
            if (tab == L"Queued") return state == DownloadItemState::Queued;
            if (tab == L"Completed") return state == DownloadItemState::Completed;
            if (tab == L"Failed") return state == DownloadItemState::Failed;
            return false;
        }

        MUXC::Button ActionButton(
            winrt::hstring const& glyph,
            winrt::hstring const& accessibleName,
            std::wstring const& jobId)
        {
            MUXC::Button button;
            button.Width(34);
            button.Height(34);
            button.Padding(MUX::Thickness{ 0 });
            button.CornerRadius(MUX::CornerRadius{ 8 });
            button.Background(nullptr);
            button.BorderThickness(MUX::Thickness{ 0 });
            button.Tag(winrt::box_value(winrt::hstring(jobId)));
            MUXC::FontIcon icon;
            icon.Glyph(glyph);
            icon.FontSize(13);
            button.Content(icon);
            MUXA::AutomationProperties::SetName(button, accessibleName);
            return button;
        }
    }

    void MainWindow::InitializeDownloads()
    {
        auto& manager = detail::DownloadManagerService();
        manager.Initialize();

        auto const snapshot = manager.Snapshot();
        m_loadingDownloadRules = true;
        DownloadRuleWifi().IsOn(snapshot.OnlyOnWifi);
        DownloadRuleLiked().IsOn(snapshot.AutoDownloadLiked);
        DownloadRuleBattery().IsOn(snapshot.DownloadOnBattery);
        DownloadRuleRecent().IsOn(snapshot.KeepRecentOffline);
        m_loadingDownloadRules = false;

        m_downloadsTimer = DispatcherQueue().CreateTimer();
        m_downloadsTimer.Interval(std::chrono::milliseconds(750));
        auto weak = get_weak();
        m_downloadsTimer.Tick([weak](auto const&, auto const&)
        {
            if (auto self = weak.get())
            {
                detail::DownloadManagerService().RefreshScheduling();
                self->RefreshDownloadsView(false);
            }
        });
        m_downloadsTimer.Start();
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadsNav_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        ShowPrimaryView(L"Downloads");
        RefreshDownloadsView(true);
    }

    void MainWindow::RefreshDownloadsView(bool force)
    {
        auto const snapshot = detail::DownloadManagerService().Snapshot();
        if (!force && snapshot.Revision == m_downloadsRevision)
        {
            return;
        }
        m_downloadsRevision = snapshot.Revision;

        std::size_t active{};
        std::size_t queued{};
        std::size_t completed{};
        std::size_t failed{};
        std::set<std::wstring> formats;
        for (auto const& job : snapshot.Jobs)
        {
            switch (job.State)
            {
            case DownloadItemState::Downloading:
            case DownloadItemState::Paused: ++active; break;
            case DownloadItemState::Queued: ++queued; break;
            case DownloadItemState::Completed: ++completed; break;
            case DownloadItemState::Failed: ++failed; break;
            }
            for (auto const& item : job.Items)
            {
                if (item.State == DownloadItemState::Completed && !item.Format.empty())
                {
                    formats.insert(item.Format);
                }
            }
        }

        DownloadsActiveCount().Text(winrt::to_hstring(active));
        DownloadsQueuedCount().Text(winrt::to_hstring(queued));
        DownloadsCompletedCount().Text(winrt::to_hstring(completed));
        DownloadsFailedCount().Text(winrt::to_hstring(failed));
        auto const pending = active + queued;
        DownloadsNavCount().Text(winrt::to_hstring(pending));
        DownloadsNavBadge().Visibility(pending == 0 ? MUX::Visibility::Collapsed : MUX::Visibility::Visible);
        EnsureAccentBrushes();
        auto const downloadsSelected = m_currentNav == L"Downloads";
        DownloadsNavBadge().Background(downloadsSelected ? m_brushAccent : m_brushNeutralFill);
        DownloadsNavCount().Foreground(downloadsSelected
            ? winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
                winrt::Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255) }
            : m_brushTextTertiary);

        if (snapshot.AllPaused && pending > 0)
        {
            DownloadsStatusText().Text(L"All downloads paused");
        }
        else if (active > 0 || queued > 0)
        {
            DownloadsStatusText().Text(winrt::hstring(
                std::to_wstring(active) + L" active  ·  " + std::to_wstring(queued) + L" queued"));
        }
        else
        {
            DownloadsStatusText().Text(L"Nothing queued");
        }
        DownloadsOfflineCountText().Text(winrt::hstring(
            std::to_wstring(snapshot.OfflineTracks)
            + (snapshot.OfflineTracks == 1 ? L" track offline" : L" tracks offline")));
        if (formats.empty())
        {
            DownloadsFormatText().Text(L"Source quality");
        }
        else
        {
            std::wstring text;
            for (auto const& format : formats)
            {
                if (!text.empty()) text += L", ";
                text += format;
            }
            DownloadsFormatText().Text(winrt::hstring(text));
        }

        DownloadsPauseAllGlyph().Glyph(snapshot.AllPaused ? L"\xE768" : L"\xE769");
        DownloadsPauseAllLabel().Text(snapshot.AllPaused ? L"Resume all" : L"Pause all");
        DownloadsPauseAllButton().IsEnabled(pending > 0);
        DownloadsFolderText().Text(winrt::hstring(snapshot.RootFolder.wstring()));
        DownloadSettingsFolderText().Text(winrt::hstring(snapshot.RootFolder.wstring()));

        auto const cache = detail::StreamCacheService().Usage();
        DownloadsStorageSummaryText().Text(winrt::hstring(
            FormatBytes(snapshot.OfflineBytes) + L" used for offline music"));
        DownloadsMusicSizeText().Text(winrt::hstring(L"Music " + FormatBytes(snapshot.OfflineBytes)));
        DownloadsCacheSizeText().Text(winrt::hstring(L"Cache " + FormatBytes(cache.Bytes)));
        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        ULARGE_INTEGER totalFree{};
        auto const hasCapacity = GetDiskFreeSpaceExW(
            snapshot.RootFolder.c_str(), &freeBytes, &totalBytes, &totalFree) != FALSE;
        auto const percent = hasCapacity && totalBytes.QuadPart > 0
            ? static_cast<double>(snapshot.OfflineBytes) * 100.0 / static_cast<double>(totalBytes.QuadPart)
            : 0.0;
        DownloadsStorageBar().Value((std::min)(100.0, percent));

        auto selectTab = [&](MUXC::Button const& button, bool selected)
        {
            button.Background(selected ? m_brushAccent : m_brushTransparent);
            button.Foreground(selected ? winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
                winrt::Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255) } : m_brushLabelIdle);
        };
        selectTab(DownloadsTabActive(), m_downloadsTab == L"Active");
        selectTab(DownloadsTabQueued(), m_downloadsTab == L"Queued");
        selectTab(DownloadsTabCompleted(), m_downloadsTab == L"Completed");
        selectTab(DownloadsTabFailed(), m_downloadsTab == L"Failed");

        auto host = DownloadsRowsHost();
        host.Children().Clear();
        for (auto const& job : snapshot.Jobs)
        {
            if (!InTab(job.State, m_downloadsTab)) continue;

            MUXC::Border card;
            card.Background(m_brushSurface);
            card.BorderBrush(m_brushStroke);
            card.BorderThickness(MUX::Thickness{ 1 });
            card.CornerRadius(MUX::CornerRadius{ 12 });
            card.Padding(MUX::Thickness{ 18, 14, 18, 14 });

            MUXC::Grid row;
            row.ColumnSpacing(14);
            row.ColumnDefinitions().Append(MUXC::ColumnDefinition{});
            row.ColumnDefinitions().GetAt(0).Width(MUX::GridLengthHelper::FromPixels(44));
            row.ColumnDefinitions().Append(MUXC::ColumnDefinition{});
            row.ColumnDefinitions().GetAt(1).Width(MUX::GridLengthHelper::FromValueAndType(1, MUX::GridUnitType::Star));
            row.ColumnDefinitions().Append(MUXC::ColumnDefinition{});
            row.ColumnDefinitions().GetAt(2).Width(MUX::GridLengthHelper::Auto());

            MUXC::Border art;
            art.Width(44);
            art.Height(44);
            art.CornerRadius(MUX::CornerRadius{ 7 });
            art.Background(m_brushNeutralFill);
            MUXC::FontIcon artIcon;
            artIcon.Glyph(job.State == DownloadItemState::Failed ? L"\xE783" : L"\xE8D6");
            artIcon.FontSize(16);
            artIcon.Foreground(job.State == DownloadItemState::Failed
                ? winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{ winrt::Windows::UI::ColorHelper::FromArgb(255, 192, 57, 43) }
                : m_brushGlyphIdle);
            art.Child(artIcon);
            row.Children().Append(art);

            MUXC::StackPanel body;
            body.Spacing(6);
            MUXC::TextBlock title;
            title.Text(winrt::hstring(job.Title));
            title.FontSize(14);
            title.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            title.TextTrimming(MUX::TextTrimming::CharacterEllipsis);
            MUXC::TextBlock status;
            status.Text(winrt::hstring(JobStatusText(job)));
            status.FontSize(12);
            status.Foreground(job.State == DownloadItemState::Failed
                ? winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{ winrt::Windows::UI::ColorHelper::FromArgb(255, 192, 57, 43) }
                : m_brushTextTertiary);
            body.Children().Append(title);
            body.Children().Append(status);
            if (job.State == DownloadItemState::Downloading || job.State == DownloadItemState::Paused)
            {
                MUXC::ProgressBar progress;
                progress.Minimum(0);
                progress.Maximum(100);
                progress.Height(5);
                progress.Value(LastMusicPlayer::Backend::DownloadProgressPercent(
                    job.BytesDownloaded,
                    job.BytesTotal));
                progress.IsIndeterminate(job.BytesTotal == 0 && job.State == DownloadItemState::Downloading);
                body.Children().Append(progress);
            }
            MUXC::Grid::SetColumn(body, 1);
            row.Children().Append(body);

            MUXC::StackPanel actions;
            actions.Orientation(MUXC::Orientation::Horizontal);
            actions.Spacing(4);
            if (job.State == DownloadItemState::Downloading || job.State == DownloadItemState::Paused)
            {
                auto pause = ActionButton(
                    job.State == DownloadItemState::Paused ? L"\xE768" : L"\xE769",
                    job.State == DownloadItemState::Paused ? L"Resume download" : L"Pause download",
                    job.Id);
                pause.Click({ this, &MainWindow::DownloadJobPause_Click });
                actions.Children().Append(pause);
                auto cancel = ActionButton(L"\xE711", L"Cancel download", job.Id);
                cancel.Click({ this, &MainWindow::DownloadJobCancel_Click });
                actions.Children().Append(cancel);
            }
            else if (job.State == DownloadItemState::Queued)
            {
                auto cancel = ActionButton(L"\xE711", L"Cancel queued download", job.Id);
                cancel.Click({ this, &MainWindow::DownloadJobCancel_Click });
                actions.Children().Append(cancel);
            }
            else if (job.State == DownloadItemState::Failed)
            {
                auto retry = ActionButton(L"\xE72C", L"Retry download", job.Id);
                retry.Click({ this, &MainWindow::DownloadJobRetry_Click });
                actions.Children().Append(retry);
                auto dismiss = ActionButton(L"\xE711", L"Dismiss failed download", job.Id);
                dismiss.Click({ this, &MainWindow::DownloadJobDismiss_Click });
                actions.Children().Append(dismiss);
            }
            MUXC::Grid::SetColumn(actions, 2);
            row.Children().Append(actions);
            card.Child(row);
            host.Children().Append(card);
        }

        auto const empty = host.Children().Size() == 0;
        DownloadsEmptyPanel().Visibility(empty ? MUX::Visibility::Visible : MUX::Visibility::Collapsed);
        DownloadsEmptyTitle().Text(winrt::hstring(L"No " + std::wstring(m_downloadsTab) + L" downloads"));
    }

    void MainWindow::DownloadsPauseAll_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto const snapshot = detail::DownloadManagerService().Snapshot();
        if (snapshot.AllPaused) detail::DownloadManagerService().ResumeAll();
        else detail::DownloadManagerService().PauseAll();
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadsSettings_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetDownloadSettingsOpen(!m_downloadsSettingsOpen);
    }

    void MainWindow::DownloadsSettingsClose_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetDownloadSettingsOpen(false);
    }

    void MainWindow::SetDownloadSettingsOpen(bool open)
    {
        m_downloadsSettingsOpen = open;
        EnsureAccentBrushes();
        DownloadsSettingsButton().Background(open ? m_brushAccentSoft : m_brushSurface);
        DownloadsSettingsButton().BorderBrush(open ? m_brushAccentBorder : m_brushStroke);
        RailTabHost().Visibility(open ? MUX::Visibility::Collapsed : MUX::Visibility::Visible);
        PlayerRailContent().Visibility(open ? MUX::Visibility::Collapsed : MUX::Visibility::Visible);
        DownloadSettingsPanel().Visibility(open ? MUX::Visibility::Visible : MUX::Visibility::Collapsed);
    }

    void MainWindow::DownloadsTab_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto element = sender.try_as<MUX::FrameworkElement>();
        if (!element) return;
        auto const tab = detail::ReadTagString(element.Tag());
        if (tab.empty()) return;
        m_downloadsTab.assign(tab.c_str());
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadsClearCompleted_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        detail::DownloadManagerService().ClearCompletedHistory();
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadsClearCache_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        DownloadsStatusText().Text(detail::StreamCacheService().Clear()
            ? L"Playback cache cleared"
            : L"Cache is busy. Try again after the current transfer finishes.");
        RefreshDownloadsView(true);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::DownloadsChangeFolder_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        winrt::Windows::Storage::Pickers::FolderPicker picker;
        if (m_hwnd)
        {
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            initializeWithWindow->Initialize(m_hwnd);
        }
        picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::MusicLibrary);
        picker.FileTypeFilter().Append(L"*");
        auto const folder = co_await picker.PickSingleFolderAsync();
        if (!folder) co_return;
        if (!detail::DownloadManagerService().SetRootFolder(std::filesystem::path(folder.Path().c_str())))
        {
            DownloadsStatusText().Text(L"Pause active downloads before changing the folder");
            co_return;
        }
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadsRule_Toggled(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        if (m_loadingDownloadRules) return;
        auto toggle = sender.try_as<MUXC::ToggleSwitch>();
        if (!toggle) return;
        auto const tag = detail::ReadTagString(toggle.Tag());
        if (tag == L"Wifi") detail::DownloadManagerService().SetOnlyOnWifi(toggle.IsOn());
        else if (tag == L"Liked") detail::DownloadManagerService().SetAutoDownloadLiked(toggle.IsOn());
        else if (tag == L"Battery") detail::DownloadManagerService().SetDownloadOnBattery(toggle.IsOn());
        else if (tag == L"Recent") detail::DownloadManagerService().SetKeepRecentOffline(toggle.IsOn());
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadJobPause_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto element = sender.try_as<MUX::FrameworkElement>();
        auto const id = element ? std::wstring(detail::ReadTagString(element.Tag()).c_str()) : std::wstring{};
        if (id.empty()) return;
        auto const snapshot = detail::DownloadManagerService().Snapshot();
        auto const job = std::find_if(snapshot.Jobs.begin(), snapshot.Jobs.end(), [&](auto const& item) { return item.Id == id; });
        if (job != snapshot.Jobs.end() && job->State == DownloadItemState::Paused)
            detail::DownloadManagerService().ResumeJob(id);
        else
            detail::DownloadManagerService().PauseJob(id);
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadJobCancel_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto element = sender.try_as<MUX::FrameworkElement>();
        auto const id = element ? std::wstring(detail::ReadTagString(element.Tag()).c_str()) : std::wstring{};
        if (!id.empty()) detail::DownloadManagerService().CancelJob(id);
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadJobRetry_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto element = sender.try_as<MUX::FrameworkElement>();
        auto const id = element ? std::wstring(detail::ReadTagString(element.Tag()).c_str()) : std::wstring{};
        if (!id.empty()) detail::DownloadManagerService().RetryJob(id);
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadJobDismiss_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto element = sender.try_as<MUX::FrameworkElement>();
        auto const id = element ? std::wstring(detail::ReadTagString(element.Tag()).c_str()) : std::wstring{};
        if (!id.empty()) detail::DownloadManagerService().DismissFailed(id);
        RefreshDownloadsView(true);
    }

    void MainWindow::EnqueueAutomaticDownload(
        winrt::Last_Music_Player::TrackInfo const& track,
        winrt::hstring const& reason)
    {
        if (!track
            || detail::ToLowerCopy(track.SourceKind()) != L"remote"
            || !detail::IsHttpUrl(track.SourceUrl()))
        {
            return;
        }
        LastMusicPlayer::Backend::DownloadTrackRequest request;
        request.StableKey = detail::DownloadStableKey(
            detail::RemoteMusicServiceService().CaptureScope(),
            track);
        request.SourceUrl = track.SourceUrl().c_str();
        request.Provider = track.Provider().c_str();
        request.Title = track.Title().c_str();
        request.Artist = track.Artist().c_str();
        request.Album = track.Album().c_str();
        request.ArtworkUrl = track.ArtworkUrl().c_str();
        detail::DownloadManagerService().Enqueue(
            request.Title,
            reason.c_str(),
            request.ArtworkUrl,
            { std::move(request) });
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryDetailDownload_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        auto const epoch = m_libraryDetailHydrationEpoch;
        auto const title = LibraryDetailTitleText().Text();
        auto const subtitle = m_libraryDetailSubtitle;
        auto const kind = m_libraryDetailKind;
        auto const key = m_libraryDetailKey;
        std::optional<AccountPlaylistDetailContext> accountContext;
        if (IsAccountPlaylistDetail())
        {
            accountContext = CaptureAccountPlaylistDetailContext();
            if (!accountContext) co_return;
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;
        auto dispatcher = DispatcherQueue();
        if (kind == L"auto-playlist")
        {
            auto const found = m_homeMixes.find(key);
            if (found != m_homeMixes.end()) tracks = found->second;
        }
        else
        {
            auto query = CurrentLibraryDetailQuery(
                0,
                0,
                accountContext ? accountContext->Binding.OwnerId : std::wstring{});
            query.SearchText.clear();
            co_await winrt::resume_background();
            tracks = detail::DatabaseService().LoadTracksForQuery(query);
            co_await wil::resume_foreground(dispatcher);
        }
        if (epoch != m_libraryDetailHydrationEpoch || key != m_libraryDetailKey) co_return;

        std::vector<LastMusicPlayer::Backend::DownloadTrackRequest> requests;
        for (auto const& track : tracks)
        {
            if (!track
                || detail::ToLowerCopy(track.SourceKind()) != L"remote"
                || !detail::IsHttpUrl(track.SourceUrl()))
            {
                continue;
            }
            LastMusicPlayer::Backend::DownloadTrackRequest request;
            request.StableKey = detail::DownloadStableKey(
                detail::RemoteMusicServiceService().CaptureScope(),
                track);
            request.SourceUrl = track.SourceUrl().c_str();
            request.Provider = track.Provider().c_str();
            request.Title = track.Title().c_str();
            request.Artist = track.Artist().c_str();
            request.Album = track.Album().c_str();
            request.ArtworkUrl = track.ArtworkUrl().c_str();
            requests.push_back(std::move(request));
        }
        if (requests.empty())
        {
            LibraryImportStatusText().Text(L"Everything in this collection is already on this PC");
            co_return;
        }
        auto const artwork = requests.front().ArtworkUrl;
        auto const count = requests.size();
        if (!detail::DownloadManagerService().Enqueue(
                title.c_str(),
                subtitle,
                artwork,
                std::move(requests)))
        {
            LibraryImportStatusText().Text(L"These tracks are already queued or offline");
            co_return;
        }
        LibraryImportStatusText().Text(winrt::hstring(
            std::to_wstring(count) + (count == 1 ? L" track queued for download" : L" tracks queued for download")));
        RefreshDownloadsView(true);
    }
}
