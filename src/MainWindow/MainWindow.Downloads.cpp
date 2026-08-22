#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/DownloadManager.h"

#include <shobjidl.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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
        namespace MUXM = winrt::Microsoft::UI::Xaml::Media;
        using LastMusicPlayer::Backend::DownloadItemState;

        // Thousands separators for the offline tally, which reaches four and
        // five digits on a real library and reads as a run of digits without them.
        std::wstring GroupedCount(std::size_t value)
        {
            auto text = std::to_wstring(value);
            for (auto position = static_cast<int>(text.size()) - 3; position > 0; position -= 3)
            {
                text.insert(static_cast<std::size_t>(position), L",");
            }
            return text;
        }

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
                    text += L" · " + FormatBytes(job.BytesPerSecond) + L"/s";
                    if (job.BytesTotal > job.BytesDownloaded)
                    {
                        auto const seconds = (job.BytesTotal - job.BytesDownloaded) / job.BytesPerSecond;
                        text += L" · " + std::to_wstring((seconds + 59) / 60) + L" min left";
                    }
                }
                return text;
            }
            case DownloadItemState::Paused:
            {
                auto const percent = job.BytesTotal > 0
                    ? static_cast<int>(job.BytesDownloaded * 100 / job.BytesTotal)
                    : 0;
                return L"Paused · " + std::to_wstring(percent) + L"%";
            }
            case DownloadItemState::Completed:
                return std::to_wstring(total) + (total == 1 ? L" track · " : L" tracks · ")
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

        void AppendColumn(MUXC::Grid const& grid, MUX::GridLength const& width)
        {
            MUXC::ColumnDefinition column;
            column.Width(width);
            grid.ColumnDefinitions().Append(column);
        }

        MUXC::TextBlock RowTitle(std::wstring const& value)
        {
            MUXC::TextBlock text;
            text.Text(winrt::hstring(value));
            text.FontSize(14);
            text.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            text.TextTrimming(MUX::TextTrimming::CharacterEllipsis);
            text.TextWrapping(MUX::TextWrapping::NoWrap);
            return text;
        }

        MUXC::TextBlock RowSubtitle(
            std::wstring const& value,
            MUXM::Brush const& foreground)
        {
            MUXC::TextBlock text;
            text.Text(winrt::hstring(value));
            text.FontSize(12);
            text.Foreground(foreground);
            text.TextTrimming(MUX::TextTrimming::CharacterEllipsis);
            text.TextWrapping(MUX::TextWrapping::NoWrap);
            text.VerticalAlignment(MUX::VerticalAlignment::Center);
            return text;
        }

        // Rows sit inside one shared card, so the frame carries only padding,
        // hover, and the hairline the caller decides to draw beneath it.
        // The handlers capture only the two brushes and read the row back off
        // the sender, so nothing here outlives the row it belongs to.
        MUXC::Border RowFrame(
            MUXC::Grid const& row,
            MUX::Thickness const& padding,
            MUXM::Brush const& hover,
            MUXM::Brush const& idle)
        {
            MUXC::Border frame;
            frame.Padding(padding);
            frame.Background(idle);
            frame.Child(row);
            if (!hover || !idle)
            {
                return frame;
            }
            frame.PointerEntered([hover](
                winrt::Windows::Foundation::IInspectable const& sender,
                MUX::Input::PointerRoutedEventArgs const&)
            {
                if (auto const border = sender.try_as<MUXC::Border>())
                {
                    border.Background(hover);
                }
            });
            frame.PointerExited([idle](
                winrt::Windows::Foundation::IInspectable const& sender,
                MUX::Input::PointerRoutedEventArgs const&)
            {
                if (auto const border = sender.try_as<MUXC::Border>())
                {
                    border.Background(idle);
                }
            });
            return frame;
        }

        void SetStorageSegment(MUXC::ColumnDefinition const& column, double weight)
        {
            column.Width(MUX::GridLengthHelper::FromValueAndType(
                (std::max)(0.0, weight), MUX::GridUnitType::Star));
        }

        // A one-track job is really about that track, so the row shows the
        // item's own title and artist. Multi-track jobs keep the collection
        // label the enqueue gave them.
        LastMusicPlayer::Backend::DownloadItemSnapshot const* LoneItem(
            LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
        {
            return job.Items.size() == 1 ? &job.Items.front() : nullptr;
        }

        std::wstring JobDisplayTitle(
            LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
        {
            auto const* lone = LoneItem(job);
            if (lone && !lone->Title.empty())
            {
                return lone->Title;
            }
            return job.Title;
        }

        std::wstring JobDisplaySubtitle(
            LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
        {
            auto const* lone = LoneItem(job);
            if (lone && !lone->Artist.empty())
            {
                return lone->Artist;
            }
            return job.Subtitle;
        }

        std::wstring JobDisplayArtworkUrl(
            LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
        {
            if (!job.ArtworkUrl.empty())
            {
                return job.ArtworkUrl;
            }
            for (auto const& item : job.Items)
            {
                if (!item.ArtworkUrl.empty())
                {
                    return item.ArtworkUrl;
                }
            }
            return {};
        }

        // Content-Length only arrives once a transfer starts, so a queued job
        // usually has no total yet. Blank beats a confident "0 B".
        std::wstring OptionalSizeText(std::uint64_t bytes)
        {
            return bytes == 0 ? std::wstring{} : FormatBytes(bytes);
        }

        std::wstring CompletedItemCountText(
            LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
        {
            auto const total = job.Items.size();
            return std::to_wstring(total) + (total == 1 ? L" track" : L" tracks");
        }

        std::wstring JobAlbumText(
            LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
        {
            for (auto const& item : job.Items)
            {
                if (!item.Album.empty())
                {
                    return item.Album;
                }
            }
            return job.Subtitle.empty() ? std::wstring{} : job.Subtitle;
        }

        std::wstring JobFinishedText(
            LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
        {
            std::int64_t finished{};
            for (auto const& item : job.Items)
            {
                finished = (std::max)(finished, item.FinishedAtUnix);
            }
            if (finished <= 0)
            {
                return L"--";
            }
            auto const now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto const days = (now - finished) / 86400;
            if (days <= 0) return L"Today";
            if (days == 1) return L"Yesterday";
            if (days < 30) return std::to_wstring(days) + L" days ago";
            auto const months = days / 30;
            return std::to_wstring(months) + (months == 1 ? L" month ago" : L" months ago");
        }

        winrt::hstring EmptyTabHint(std::wstring const& tab)
        {
            if (tab == L"Queued") return L"Anything waiting its turn shows up here.";
            if (tab == L"Completed") return L"Finished downloads are listed here with their size on disk.";
            if (tab == L"Failed") return L"Downloads that could not finish are listed here so you can retry them.";
            return L"Use the download button on a playlist or album to keep it offline.";
        }

        // The reference gives queued and completed rows an overflow affordance
        // rather than a bare icon button. One item each, so the menu says out
        // loud what the click will actually do.
        MUXC::Button OverflowButton(
            winrt::hstring const& itemText,
            winrt::hstring const& accessibleName,
            std::wstring const& jobId,
            MUX::RoutedEventHandler const& handler)
        {
            MUXC::Button button;
            button.Width(34);
            button.Height(34);
            button.Padding(MUX::Thickness{ 0 });
            button.CornerRadius(MUX::CornerRadius{ 8 });
            button.Background(nullptr);
            button.BorderThickness(MUX::Thickness{ 0 });
            MUXC::FontIcon icon;
            icon.Glyph(L"\xE712");
            icon.FontSize(15);
            button.Content(icon);
            MUXA::AutomationProperties::SetName(button, accessibleName);

            MUXC::MenuFlyoutItem item;
            item.Text(itemText);
            item.Tag(winrt::box_value(winrt::hstring(jobId)));
            item.Click(handler);
            MUXC::MenuFlyout flyout;
            flyout.Items().Append(item);
            button.Flyout(flyout);
            return button;
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
        DownloadRuleMetered().IsOn(snapshot.AvoidMeteredNetworks);
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
        // Nothing to clear reads better as an unavailable action than as a
        // button that answers a click with no visible change.
        DownloadsClearCompletedButton().IsEnabled(completed > 0);
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
                std::to_wstring(active) + L" downloading · " + std::to_wstring(queued) + L" queued"));
        }
        else
        {
            DownloadsStatusText().Text(L"Nothing queued");
        }
        DownloadsOfflineCountText().Text(winrt::hstring(
            GroupedCount(snapshot.OfflineTracks)
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
        DownloadSessionTracksText().Text(winrt::hstring(
            std::to_wstring(snapshot.SessionCompletedTracks)
            + (snapshot.SessionCompletedTracks == 1 ? L" track" : L" tracks")));
        DownloadSessionBytesText().Text(winrt::hstring(FormatBytes(snapshot.SessionBytes)));
        DownloadSessionSpeedText().Text(snapshot.SessionAverageBytesPerSecond == 0
            ? winrt::hstring{ L"Nothing yet" }
            : winrt::hstring(FormatBytes(snapshot.SessionAverageBytesPerSecond) + L"/s"));

        ApplyDownloadStorageCard(snapshot);
        ApplyDownloadTabVisuals();

        auto const showsTable = m_downloadsTab == L"Completed";
        DownloadsTableHeader().Visibility(showsTable
            ? MUX::Visibility::Visible
            : MUX::Visibility::Collapsed);

        auto host = DownloadsRowsHost();
        host.Children().Clear();
        std::size_t ordinal{};
        for (auto const& job : snapshot.Jobs)
        {
            if (!InTab(job.State, m_downloadsTab))
            {
                continue;
            }
            ++ordinal;

            MUX::UIElement row{ nullptr };
            if (m_downloadsTab == L"Queued")
            {
                row = BuildQueuedDownloadRow(job, ordinal);
            }
            else if (m_downloadsTab == L"Completed")
            {
                row = BuildCompletedDownloadRow(job);
            }
            else if (m_downloadsTab == L"Failed")
            {
                row = BuildFailedDownloadRow(job);
            }
            else
            {
                row = BuildActiveDownloadRow(job);
            }
            if (!row)
            {
                continue;
            }

            // Every row but the last carries the divider, so the card closes on
            // its own rounded edge rather than on a hairline.
            if (auto framed = row.try_as<MUXC::Border>())
            {
                framed.BorderBrush(m_brushRowDivider);
                framed.BorderThickness(MUX::Thickness{ 0, 0, 0, 1 });
            }
            host.Children().Append(row);
        }

        if (host.Children().Size() > 0)
        {
            auto const last = host.Children().GetAt(host.Children().Size() - 1);
            if (auto framed = last.try_as<MUXC::Border>())
            {
                framed.BorderThickness(MUX::Thickness{ 0 });
            }
        }

        auto const empty = host.Children().Size() == 0;
        DownloadsRowsCard().Visibility(empty ? MUX::Visibility::Collapsed : MUX::Visibility::Visible);
        DownloadsEmptyPanel().Visibility(empty ? MUX::Visibility::Visible : MUX::Visibility::Collapsed);
        DownloadsEmptyTitle().Text(winrt::hstring(L"No " + detail::ToLowerCopy(winrt::hstring(m_downloadsTab)) + L" downloads"));
        DownloadsEmptyHint().Text(EmptyTabHint(m_downloadsTab));
    }

    // The bar spans the whole volume the offline folder lives on. Only the first
    // two segments are this app's doing; the third is what the disk already
    // holds, so the empty tail reads as free space rather than as headroom the
    // downloader owns.
    void MainWindow::ApplyDownloadStorageCard(
        LastMusicPlayer::Backend::DownloadManagerSnapshot const& snapshot)
    {
        auto const cache = detail::StreamCacheService().Usage();

        ULARGE_INTEGER availableToCaller{};
        ULARGE_INTEGER totalBytes{};
        ULARGE_INTEGER totalFree{};
        auto const hasCapacity = GetDiskFreeSpaceExW(
            snapshot.RootFolder.c_str(), &availableToCaller, &totalBytes, &totalFree) != FALSE
            && totalBytes.QuadPart > 0;

        auto const music = snapshot.OfflineBytes;
        auto const cached = cache.Bytes;
        DownloadsMusicSizeText().Text(winrt::hstring(L"Offline music " + FormatBytes(music)));
        DownloadsCacheSizeText().Text(winrt::hstring(L"Stream cache " + FormatBytes(cached)));

        if (!hasCapacity)
        {
            // Without a readable volume the meter has no scale to draw against,
            // so it shows only what this app accounts for.
            auto const tracked = music + cached;
            DownloadsStorageSummaryText().Text(winrt::hstring(
                FormatBytes(tracked) + L" used for offline music on this PC"));
            DownloadsOtherLegend().Visibility(MUX::Visibility::Collapsed);
            SetStorageSegment(DownloadsStorageMusicColumn(), tracked == 0 ? 0.0 : static_cast<double>(music));
            SetStorageSegment(DownloadsStorageCacheColumn(), tracked == 0 ? 0.0 : static_cast<double>(cached));
            SetStorageSegment(DownloadsStorageOtherColumn(), 0.0);
            SetStorageSegment(DownloadsStorageFreeColumn(), tracked == 0 ? 1.0 : 0.0);
            return;
        }

        auto const capacity = static_cast<double>(totalBytes.QuadPart);
        auto const used = capacity - static_cast<double>(totalFree.QuadPart);
        // The volume's used bytes already include this app's files. Subtracting
        // them keeps the three segments from double-counting each other.
        auto const other = (std::max)(0.0, used - static_cast<double>(music) - static_cast<double>(cached));
        auto const free = (std::max)(0.0, capacity - used);

        DownloadsStorageSummaryText().Text(winrt::hstring(
            FormatBytes(static_cast<std::uint64_t>(used))
            + L" of " + FormatBytes(totalBytes.QuadPart) + L" used on this PC"));
        DownloadsOtherLegend().Visibility(MUX::Visibility::Visible);
        DownloadsOtherSizeText().Text(winrt::hstring(
            L"Everything else " + FormatBytes(static_cast<std::uint64_t>(other))));

        SetStorageSegment(DownloadsStorageMusicColumn(), static_cast<double>(music) / capacity);
        SetStorageSegment(DownloadsStorageCacheColumn(), static_cast<double>(cached) / capacity);
        SetStorageSegment(DownloadsStorageOtherColumn(), other / capacity);
        SetStorageSegment(DownloadsStorageFreeColumn(), free / capacity);
    }

    void MainWindow::ApplyDownloadTabVisuals()
    {
        EnsureAccentBrushes();
        auto const white = MUXM::SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255) };

        auto apply = [&](MUXC::Button const& chip,
                         MUXC::Border const& badge,
                         MUXC::TextBlock const& count,
                         bool selected)
        {
            chip.Background(selected ? m_brushAccent : m_brushTransparent);
            chip.Foreground(selected ? white : m_brushLabelIdle);
            chip.BorderBrush(selected ? m_brushAccent : m_brushStroke);
            badge.Background(selected ? m_brushBadgeOnAccent : m_brushNeutralFill);
            count.Foreground(selected ? white : m_brushTextTertiary);
        };

        apply(DownloadsTabActive(), DownloadsActiveBadge(), DownloadsActiveCount(), m_downloadsTab == L"Active");
        apply(DownloadsTabQueued(), DownloadsQueuedBadge(), DownloadsQueuedCount(), m_downloadsTab == L"Queued");
        apply(DownloadsTabCompleted(), DownloadsCompletedBadge(), DownloadsCompletedCount(), m_downloadsTab == L"Completed");
        apply(DownloadsTabFailed(), DownloadsFailedBadge(), DownloadsFailedCount(), m_downloadsTab == L"Failed");
    }

    // A download job carries the artwork URL of whatever was queued, so the
    // rows can show the real cover and fall back to the library glyph while it
    // resolves. The fetch is keyed and cached by the artwork pipeline.
    winrt::Microsoft::UI::Xaml::UIElement MainWindow::JobArtwork(
        LastMusicPlayer::Backend::DownloadJobSnapshot const& job,
        double size,
        double cornerRadius)
    {
        MUXC::Border art;
        art.Width(size);
        art.Height(size);
        art.CornerRadius(MUX::CornerRadius{ cornerRadius });
        art.Background(m_brushNeutralFill);
        art.VerticalAlignment(MUX::VerticalAlignment::Center);

        MUXC::Grid stack;
        MUXC::FontIcon glyph;
        glyph.Glyph(L"\xE8D6");
        glyph.FontSize(size * 0.36);
        glyph.Foreground(m_brushGlyphIdle);
        glyph.IsHitTestVisible(false);
        stack.Children().Append(glyph);

        MUXC::Image cover;
        cover.Stretch(MUXM::Stretch::UniformToFill);
        stack.Children().Append(cover);
        art.Child(stack);

        auto const artworkUrl = JobDisplayArtworkUrl(job);
        if (!artworkUrl.empty())
        {
            winrt::Last_Music_Player::TrackInfo carrier;
            carrier.Title(winrt::hstring(JobDisplayTitle(job)));
            carrier.ArtworkUrl(winrt::hstring(artworkUrl));
            QueueAccountArtworkImage(
                cover,
                carrier.ArtworkUrl(),
                detail::ArtworkDetail::Tile,
                carrier);
        }
        return art;
    }

    winrt::Microsoft::UI::Xaml::UIElement MainWindow::BuildActiveDownloadRow(
        LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
    {
        auto const paused = job.State == DownloadItemState::Paused;

        MUXC::Grid row;
        row.ColumnSpacing(14);
        AppendColumn(row, MUX::GridLengthHelper::FromPixels(44));
        AppendColumn(row, MUX::GridLengthHelper::FromValueAndType(1, MUX::GridUnitType::Star));
        AppendColumn(row, MUX::GridLengthHelper::Auto());

        row.Children().Append(JobArtwork(job, 44, 7));

        MUXC::StackPanel body;
        body.Spacing(7);

        MUXC::StackPanel heading;
        heading.Orientation(MUXC::Orientation::Horizontal);
        heading.Spacing(8);
        heading.Children().Append(RowTitle(JobDisplayTitle(job)));
        auto const heroSubtitle = JobDisplaySubtitle(job);
        if (!heroSubtitle.empty())
        {
            heading.Children().Append(RowSubtitle(heroSubtitle, m_brushTextTertiary));
        }
        body.Children().Append(heading);

        MUXC::Grid progressRow;
        progressRow.ColumnSpacing(14);
        progressRow.VerticalAlignment(MUX::VerticalAlignment::Center);
        AppendColumn(progressRow, MUX::GridLengthHelper::FromValueAndType(1, MUX::GridUnitType::Star));
        AppendColumn(progressRow, MUX::GridLengthHelper::FromPixels(210));

        MUXC::ProgressBar progress;
        progress.Minimum(0);
        progress.Maximum(100);
        progress.Height(5);
        progress.CornerRadius(MUX::CornerRadius{ 3 });
        progress.VerticalAlignment(MUX::VerticalAlignment::Center);
        progress.Value(LastMusicPlayer::Backend::DownloadProgressPercent(
            job.BytesDownloaded, job.BytesTotal));
        progress.IsIndeterminate(!paused && job.BytesTotal == 0);
        // A paused transfer keeps its fill but drops the accent, so a glance at
        // the card separates what is moving from what is only holding position.
        progress.Foreground(paused ? m_brushGlyphIdle : m_brushAccent);
        progress.Background(m_brushNeutralFill);
        progressRow.Children().Append(progress);

        auto status = RowSubtitle(JobStatusText(job), m_brushTextTertiary);
        status.HorizontalAlignment(MUX::HorizontalAlignment::Right);
        status.VerticalAlignment(MUX::VerticalAlignment::Center);
        status.TextTrimming(MUX::TextTrimming::CharacterEllipsis);
        MUXC::Grid::SetColumn(status, 1);
        progressRow.Children().Append(status);
        body.Children().Append(progressRow);

        MUXC::Grid::SetColumn(body, 1);
        row.Children().Append(body);

        MUXC::StackPanel actions;
        actions.Orientation(MUXC::Orientation::Horizontal);
        actions.Spacing(4);
        actions.VerticalAlignment(MUX::VerticalAlignment::Center);
        auto pause = ActionButton(
            paused ? L"\xE768" : L"\xE769",
            paused ? L"Resume download" : L"Pause download",
            job.Id);
        pause.Click({ this, &MainWindow::DownloadJobPause_Click });
        actions.Children().Append(pause);
        auto cancel = ActionButton(L"\xE711", L"Cancel download", job.Id);
        cancel.Click({ this, &MainWindow::DownloadJobCancel_Click });
        actions.Children().Append(cancel);
        MUXC::Grid::SetColumn(actions, 2);
        row.Children().Append(actions);

        return RowFrame(row, MUX::Thickness{ 18, 14, 18, 14 }, m_brushRowHover, m_brushTransparent);
    }

    winrt::Microsoft::UI::Xaml::UIElement MainWindow::BuildQueuedDownloadRow(
        LastMusicPlayer::Backend::DownloadJobSnapshot const& job,
        std::size_t ordinal)
    {
        MUXC::Grid row;
        row.ColumnSpacing(14);
        AppendColumn(row, MUX::GridLengthHelper::FromPixels(26));
        AppendColumn(row, MUX::GridLengthHelper::FromPixels(38));
        AppendColumn(row, MUX::GridLengthHelper::FromValueAndType(1, MUX::GridUnitType::Star));
        AppendColumn(row, MUX::GridLengthHelper::Auto());
        AppendColumn(row, MUX::GridLengthHelper::Auto());

        auto index = RowSubtitle(std::to_wstring(ordinal), m_brushTextTertiary);
        index.FontSize(13);
        index.VerticalAlignment(MUX::VerticalAlignment::Center);
        row.Children().Append(index);

        auto art = JobArtwork(job, 38, 6).as<MUX::FrameworkElement>();
        MUXC::Grid::SetColumn(art, 1);
        row.Children().Append(art);

        MUXC::StackPanel body;
        body.VerticalAlignment(MUX::VerticalAlignment::Center);
        body.Children().Append(RowTitle(JobDisplayTitle(job)));
        auto const queuedSubtitle = JobDisplaySubtitle(job);
        body.Children().Append(RowSubtitle(
            queuedSubtitle.empty() ? JobStatusText(job) : queuedSubtitle,
            m_brushTextTertiary));
        MUXC::Grid::SetColumn(body, 2);
        row.Children().Append(body);

        auto size = RowSubtitle(OptionalSizeText(job.BytesTotal), m_brushTextTertiary);
        size.FontSize(12.5);
        size.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXC::Grid::SetColumn(size, 3);
        row.Children().Append(size);

        auto overflow = OverflowButton(
            L"Cancel download",
            L"Queued download options",
            job.Id,
            { this, &MainWindow::DownloadJobCancel_Click });
        overflow.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXC::Grid::SetColumn(overflow, 4);
        row.Children().Append(overflow);

        return RowFrame(row, MUX::Thickness{ 18, 12, 18, 12 }, m_brushRowHover, m_brushTransparent);
    }

    winrt::Microsoft::UI::Xaml::UIElement MainWindow::BuildCompletedDownloadRow(
        LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
    {
        MUXC::Grid row;
        row.ColumnSpacing(12);
        row.MinHeight(58);
        AppendColumn(row, MUX::GridLengthHelper::FromValueAndType(1, MUX::GridUnitType::Star));
        AppendColumn(row, MUX::GridLengthHelper::FromPixels(160));
        AppendColumn(row, MUX::GridLengthHelper::FromPixels(110));
        AppendColumn(row, MUX::GridLengthHelper::FromPixels(90));
        AppendColumn(row, MUX::GridLengthHelper::FromPixels(40));

        MUXC::Grid item;
        item.ColumnSpacing(13);
        item.VerticalAlignment(MUX::VerticalAlignment::Center);
        AppendColumn(item, MUX::GridLengthHelper::FromPixels(38));
        AppendColumn(item, MUX::GridLengthHelper::FromValueAndType(1, MUX::GridUnitType::Star));
        item.Children().Append(JobArtwork(job, 38, 6));

        MUXC::StackPanel body;
        body.VerticalAlignment(MUX::VerticalAlignment::Center);
        body.Children().Append(RowTitle(JobDisplayTitle(job)));

        MUXC::StackPanel sub;
        sub.Orientation(MUXC::Orientation::Horizontal);
        sub.Spacing(6);
        auto const completedSubtitle = JobDisplaySubtitle(job);
        sub.Children().Append(RowSubtitle(
            completedSubtitle.empty() ? CompletedItemCountText(job) : completedSubtitle,
            m_brushTextTertiary));
        MUXC::FontIcon offline;
        offline.Glyph(L"\xE73E");
        offline.FontSize(10);
        offline.Foreground(m_brushAccent);
        offline.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXA::AutomationProperties::SetName(offline, L"Available offline");
        sub.Children().Append(offline);
        body.Children().Append(sub);

        MUXC::Grid::SetColumn(body, 1);
        item.Children().Append(body);
        row.Children().Append(item);

        auto album = RowSubtitle(JobAlbumText(job), m_brushTextTertiary);
        album.FontSize(13);
        album.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXC::Grid::SetColumn(album, 1);
        row.Children().Append(album);

        auto finished = RowSubtitle(JobFinishedText(job), m_brushTextTertiary);
        finished.FontSize(13);
        finished.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXC::Grid::SetColumn(finished, 2);
        row.Children().Append(finished);

        auto size = RowSubtitle(FormatBytes(job.BytesDownloaded), m_brushTextTertiary);
        size.FontSize(13);
        size.HorizontalAlignment(MUX::HorizontalAlignment::Right);
        size.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXC::Grid::SetColumn(size, 3);
        row.Children().Append(size);

        // CancelJob drops the history entry and leaves completed files on disk,
        // so this says list rather than promising a delete it does not do.
        auto overflow = OverflowButton(
            L"Remove from this list",
            L"Completed download options",
            job.Id,
            { this, &MainWindow::DownloadJobCancel_Click });
        overflow.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXC::Grid::SetColumn(overflow, 4);
        row.Children().Append(overflow);

        return RowFrame(row, MUX::Thickness{ 18, 0, 18, 0 }, m_brushRowHover, m_brushTransparent);
    }

    winrt::Microsoft::UI::Xaml::UIElement MainWindow::BuildFailedDownloadRow(
        LastMusicPlayer::Backend::DownloadJobSnapshot const& job)
    {
        MUXC::Grid row;
        row.ColumnSpacing(14);
        AppendColumn(row, MUX::GridLengthHelper::FromPixels(38));
        AppendColumn(row, MUX::GridLengthHelper::FromValueAndType(1, MUX::GridUnitType::Star));
        AppendColumn(row, MUX::GridLengthHelper::Auto());
        AppendColumn(row, MUX::GridLengthHelper::Auto());

        MUXC::Border tile;
        tile.Width(38);
        tile.Height(38);
        tile.CornerRadius(MUX::CornerRadius{ 6 });
        tile.Background(m_brushNeutralFill);
        tile.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXC::FontIcon errorGlyph;
        errorGlyph.Glyph(L"\xE783");
        errorGlyph.FontSize(15);
        errorGlyph.Foreground(m_brushDanger);
        tile.Child(errorGlyph);
        row.Children().Append(tile);

        MUXC::StackPanel body;
        body.VerticalAlignment(MUX::VerticalAlignment::Center);
        body.Children().Append(RowTitle(JobDisplayTitle(job)));
        body.Children().Append(RowSubtitle(
            job.Error.empty() ? std::wstring(L"Download failed") : job.Error,
            m_brushDanger));
        MUXC::Grid::SetColumn(body, 1);
        row.Children().Append(body);

        MUXC::Button retry;
        retry.Content(winrt::box_value(winrt::hstring(L"Retry")));
        retry.Padding(MUX::Thickness{ 14, 6, 14, 6 });
        retry.MinWidth(0);
        retry.MinHeight(0);
        retry.FontSize(12.5);
        retry.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        retry.CornerRadius(MUX::CornerRadius{ 8 });
        retry.BorderThickness(MUX::Thickness{ 1 });
        retry.BorderBrush(m_brushControlBorder);
        retry.Background(m_brushSurface);
        retry.VerticalAlignment(MUX::VerticalAlignment::Center);
        retry.Tag(winrt::box_value(winrt::hstring(job.Id)));
        retry.Click({ this, &MainWindow::DownloadJobRetry_Click });
        MUXC::Grid::SetColumn(retry, 2);
        row.Children().Append(retry);

        auto dismiss = ActionButton(L"\xE711", L"Dismiss failed download", job.Id);
        dismiss.Click({ this, &MainWindow::DownloadJobDismiss_Click });
        dismiss.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXC::Grid::SetColumn(dismiss, 3);
        row.Children().Append(dismiss);

        return RowFrame(row, MUX::Thickness{ 18, 14, 18, 14 }, m_brushRowHover, m_brushTransparent);
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
        DownloadsSettingsGlyph().Foreground(open ? m_brushAccent : m_brushGlyphIdle);
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
        // The header status line is the queue summary the reference defines, and
        // RefreshDownloadsView rewrites it on every revision tick. Confirmations
        // go to the transient notice instead, which owns its own dwell time.
        ShowPlaybackNotice(detail::StreamCacheService().Clear()
            ? winrt::hstring{ L"Playback cache cleared" }
            : winrt::hstring{ L"Cache is busy. Try again after the current transfer finishes." });
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
            ShowPlaybackNotice(L"Pause active downloads before changing the folder");
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
        if (tag == L"Metered") detail::DownloadManagerService().SetAvoidMeteredNetworks(toggle.IsOn());
        else if (tag == L"Liked") detail::DownloadManagerService().SetAutoDownloadLiked(toggle.IsOn());
        else if (tag == L"Battery") detail::DownloadManagerService().SetDownloadOnBattery(toggle.IsOn());
        else if (tag == L"Recent") detail::DownloadManagerService().SetKeepRecentOffline(toggle.IsOn());
        RefreshDownloadsView(true);
    }

    void MainWindow::DownloadsRuleRow_PointerEntered(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::Input::PointerRoutedEventArgs const& args)
    {
        (void)args;
        if (auto row = sender.try_as<MUXC::Border>())
        {
            EnsureAccentBrushes();
            row.Background(m_brushRowHover);
        }
    }

    void MainWindow::DownloadsRuleRow_PointerExited(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::Input::PointerRoutedEventArgs const& args)
    {
        (void)args;
        if (auto row = sender.try_as<MUXC::Border>())
        {
            row.Background(m_brushTransparent);
        }
    }

    void MainWindow::DownloadsRuleRow_Tapped(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::Input::TappedRoutedEventArgs const& args)
    {
        auto row = sender.try_as<MUXC::Border>();
        if (!row)
        {
            return;
        }

        // A tap that landed on the switch has already toggled it, and the
        // switch marks that tap handled. Flipping again here would undo it.
        if (args.Handled())
        {
            return;
        }

        auto const tag = detail::ReadTagString(row.Tag());
        MUXC::ToggleSwitch toggle{ nullptr };
        if (tag == L"Metered") toggle = DownloadRuleMetered();
        else if (tag == L"Liked") toggle = DownloadRuleLiked();
        else if (tag == L"Battery") toggle = DownloadRuleBattery();
        else if (tag == L"Recent") toggle = DownloadRuleRecent();
        if (!toggle)
        {
            return;
        }

        // Toggled runs from this, which is what persists the rule.
        toggle.IsOn(!toggle.IsOn());
        args.Handled(true);
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

    bool MainWindow::EnqueueAutomaticDownload(
        winrt::Last_Music_Player::TrackInfo const& track,
        winrt::hstring const& reason)
    {
        if (!track
            || detail::ToLowerCopy(track.SourceKind()) != L"remote"
            || !detail::IsHttpUrl(track.SourceUrl()))
        {
            return false;
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
        // Read out the label fields first. Argument evaluation order is
        // unspecified, so passing request.Title alongside a move of request in
        // the same call let the move win and every single-track job landed in
        // the queue titled "Download" with no cover.
        auto title = request.Title;
        auto artworkUrl = request.ArtworkUrl;
        return detail::DownloadManagerService().Enqueue(
            std::move(title),
            reason.c_str(),
            std::move(artworkUrl),
            { std::move(request) });
    }

    void MainWindow::DownloadTrack_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto const track = TrackFromActionSender(sender);
        auto const queued = EnqueueAutomaticDownload(track, L"Single track");
        ShowPlaybackNotice(queued
            ? winrt::hstring{ L"Queued for offline download" }
            : winrt::hstring{ L"This track is already on this PC or already queued" });
        RefreshDownloadsView(true);
    }

    void MainWindow::DiscoverDetailDownload_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        std::vector<LastMusicPlayer::Backend::DownloadTrackRequest> requests;
        requests.reserve(m_discoverDetailAllResults.size());
        auto const scope = detail::RemoteMusicServiceService().CaptureScope();
        for (auto const& track : m_discoverDetailAllResults)
        {
            if (!track
                || detail::ToLowerCopy(track.SourceKind()) != L"remote"
                || !detail::IsHttpUrl(track.SourceUrl()))
            {
                continue;
            }

            LastMusicPlayer::Backend::DownloadTrackRequest request;
            request.StableKey = detail::DownloadStableKey(scope, track);
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
            ShowPlaybackNotice(L"This collection has no downloadable tracks.");
            return;
        }

        auto const title = DiscoverDetailTitleText()
            ? std::wstring{ DiscoverDetailTitleText().Text().c_str() }
            : std::wstring{ L"Catalog collection" };
        auto const subtitle = DiscoverDetailSubtitleText()
            ? std::wstring{ DiscoverDetailSubtitleText().Text().c_str() }
            : std::wstring{};
        auto const artwork = requests.front().ArtworkUrl;
        auto const count = requests.size();
        if (!detail::DownloadManagerService().Enqueue(
                title,
                subtitle,
                artwork,
                std::move(requests)))
        {
            ShowPlaybackNotice(L"These tracks are already queued or offline.");
            return;
        }

        ShowPlaybackNotice(winrt::hstring(
            std::to_wstring(count)
            + (count == 1 ? L" track queued for download." : L" tracks queued for download.")));
        RefreshDownloadsView(true);
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
