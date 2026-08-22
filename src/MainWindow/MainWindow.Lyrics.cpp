#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/LyricsService.h"
#include "Backend/AudioPlayer.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        std::wstring MakeTrackKey(winrt::Last_Music_Player::TrackInfo const& track)
        {
            std::wstring key{ track.Title().c_str() };
            key.push_back(L'|');
            key.append(track.Artist().c_str());
            key.push_back(L'|');
            key.append(std::to_wstring(static_cast<int64_t>(track.DurationSeconds() * 1000.0)));
            return key;
        }

        void ApplyLineForeground(TextBlock const& block, bool active,
            double idleFontSize, double activeFontSize,
            winrt::Microsoft::UI::Xaml::Media::Brush const& accent,
            winrt::Microsoft::UI::Xaml::Media::Brush const& dim)
        {
            if (!block) return;
            block.FontWeight(active
                ? winrt::Microsoft::UI::Text::FontWeights::Bold()
                : winrt::Microsoft::UI::Text::FontWeights::SemiLight());
            block.Foreground(active ? accent : dim);
            block.FontSize(active ? activeFontSize : idleFontSize);
            block.LineHeight((active ? activeFontSize : idleFontSize) + 6.0);
            block.Opacity(active ? 1.0 : 0.4);
        }

        void ClearAndPopulateLines(StackPanel const& panel,
            std::vector<LastMusicPlayer::Backend::LyricLine> const& lines,
            double fontSize)
        {
            if (!panel) return;
            panel.Children().Clear();
            for (auto const& line : lines)
            {
                TextBlock block;
                block.Text(winrt::hstring{ line.Text.empty() ? L"♪" : line.Text });
                block.FontSize(fontSize);
                block.LineHeight(fontSize + 6.0);
                block.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
                block.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiLight());
                block.Opacity(0.4);
                panel.Children().Append(block);
            }
        }

        void ScrollChildIntoCenter(ScrollViewer const& scroller, StackPanel const& panel, int32_t index)
        {
            if (!scroller || !panel) return;
            auto children = panel.Children();
            if (index < 0 || static_cast<uint32_t>(index) >= children.Size()) return;

            auto element = children.GetAt(index).try_as<FrameworkElement>();
            if (!element) return;

            try
            {
                auto transform = element.TransformToVisual(panel);
                auto point = transform.TransformPoint({ 0.0f, 0.0f });
                auto viewportH = scroller.ViewportHeight();
                auto target = static_cast<double>(point.Y) - (viewportH * 0.45) + (element.ActualHeight() * 0.5);
                if (target < 0.0) target = 0.0;
                scroller.ChangeView(
                    nullptr,
                    winrt::box_value(target).as<winrt::Windows::Foundation::IReference<double>>(),
                    nullptr);
            }
            catch (...)
            {
            }
        }
    }

    void MainWindow::ResetLyricsViewToEmpty(winrt::hstring const& message)
    {
        auto applyPanel = [&](StackPanel const& lines, TextBlock const& plain, StackPanel const& empty, TextBlock const& emptyMsg)
        {
            if (lines)
            {
                lines.Children().Clear();
                lines.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (plain)
            {
                plain.Text({});
                plain.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (empty)
            {
                empty.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            if (emptyMsg)
            {
                emptyMsg.Text(message);
            }
        };

        applyPanel(LyricsLinesPanel(), LyricsPlainText(), LyricsEmptyPanel(), LyricsEmptyMessage());
        applyPanel(FsLyricsLinesPanel(), FsLyricsPlainText(), FsLyricsEmptyPanel(), FsLyricsEmptyMessage());

        if (auto tag = LyricsSourceTag()) tag.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        if (auto tag = FsLyricsSourceTag()) tag.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        m_currentLyricsSynced.clear();
        m_currentLyricLineIndex = -1;
    }

    void MainWindow::RenderLyricsResult(LastMusicPlayer::Backend::LyricsResult const& result)
    {
        auto headerTitle = result.TrackName.empty()
            ? std::wstring{ NowPlayingTitle().Text().c_str() }
            : result.TrackName;
        auto headerArtist = result.ArtistName.empty()
            ? std::wstring{ NowPlayingArtist().Text().c_str() }
            : result.ArtistName;

        if (auto t = LyricsTrackTitle()) t.Text(winrt::hstring{ headerTitle });
        if (auto t = LyricsTrackArtist()) t.Text(winrt::hstring{ headerArtist });
        if (auto t = FsLyricsTrackTitle()) t.Text(winrt::hstring{ headerTitle });
        if (auto t = FsLyricsTrackArtist()) t.Text(winrt::hstring{ headerArtist });

        std::wstring sourceLabel;
        auto source = ToLowerCopy(winrt::hstring{ result.Source });
        if (source == L"lyrics" || source == L"provider" || source == L"music-api")
        {
            sourceLabel = L"Source: Music API";
        }
        else if (source == L"account")
        {
            sourceLabel = L"Source: Account";
        }

        auto applySource = [&](TextBlock const& tag)
        {
            if (!tag) return;
            if (sourceLabel.empty())
            {
                tag.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            else
            {
                tag.Text(winrt::hstring{ sourceLabel });
                tag.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
        };
        applySource(LyricsSourceTag());
        applySource(FsLyricsSourceTag());

        if (!result.Found && !result.Instrumental && result.Plain.empty() && result.Synced.empty())
        {
            ResetLyricsViewToEmpty(L"No lyrics found.");
            return;
        }

        if (result.Instrumental && result.Synced.empty() && result.Plain.empty())
        {
            ResetLyricsViewToEmpty(L"Instrumental. No vocals.");
            return;
        }

        if (!result.Synced.empty())
        {
            m_currentLyricsSynced = result.Synced;
            m_currentLyricLineIndex = -1;

            ClearAndPopulateLines(LyricsLinesPanel(), result.Synced, 16.0);
            if (auto p = LyricsLinesPanel()) p.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            if (auto p = LyricsPlainText()) p.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            if (auto p = LyricsEmptyPanel()) p.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

            ClearAndPopulateLines(FsLyricsLinesPanel(), result.Synced, 20.0);
            if (auto p = FsLyricsLinesPanel()) p.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            if (auto p = FsLyricsPlainText()) p.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            if (auto p = FsLyricsEmptyPanel()) p.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

            auto zero = winrt::box_value(0.0).as<winrt::Windows::Foundation::IReference<double>>();
            if (auto sv = LyricsScrollViewer()) sv.ChangeView(nullptr, zero, nullptr, true);
            if (auto sv = FsLyricsScrollViewer()) sv.ChangeView(nullptr, zero, nullptr, true);
            return;
        }

        m_currentLyricsSynced.clear();
        m_currentLyricLineIndex = -1;

        auto plainText = winrt::hstring{ result.Plain };
        auto setupPlain = [&](TextBlock const& plain, StackPanel const& lines, StackPanel const& empty)
        {
            if (plain)
            {
                plain.Text(plainText);
                plain.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            if (lines)
            {
                lines.Children().Clear();
                lines.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (empty)
            {
                empty.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
        };
        setupPlain(LyricsPlainText(), LyricsLinesPanel(), LyricsEmptyPanel());
        setupPlain(FsLyricsPlainText(), FsLyricsLinesPanel(), FsLyricsEmptyPanel());
    }

    void MainWindow::QueueLyricsHydration()
    {
        // 350ms debounce mirrors the volume-persist pattern. Pre-fix,
        // rapid Next clicks fired N concurrent provider requests; only
        // the last response was applied to the UI (via the epoch
        // counter) but the other N-1 still left the wire and could
        // trip rate-limits / waste bandwidth. Settling on a track
        // for 350ms now fires exactly one request.
        if (!m_lyricsHydrationTimer)
        {
            m_lyricsHydrationTimer = winrt::Microsoft::UI::Xaml::DispatcherTimer();
            m_lyricsHydrationTimer.Interval(std::chrono::milliseconds(350));
            m_lyricsHydrationTimer.Tick([this](
                winrt::Windows::Foundation::IInspectable const&,
                winrt::Windows::Foundation::IInspectable const&)
            {
                m_lyricsHydrationTimer.Stop();
                HydrateLyricsForCurrentTrack();
            });
        }
        m_lyricsHydrationTimer.Stop();
        m_lyricsHydrationTimer.Start();
    }

    void MainWindow::HydrateLyricsForCurrentTrack()
    {
        auto track = AudioPlayerService().GetCurrentTrack();
        if (!track || track.Title().empty())
        {
            if (auto t = LyricsTrackTitle()) t.Text(L"Not Playing");
            if (auto t = LyricsTrackArtist()) t.Text(L"Select a track");
            if (auto t = FsLyricsTrackTitle()) t.Text(L"Not Playing");
            if (auto t = FsLyricsTrackArtist()) t.Text(L"Select a track");
            ResetLyricsViewToEmpty(L"Tap Lyrics while a song is playing.");
            m_lyricsLoadedKey.clear();
            return;
        }

        auto key = MakeTrackKey(track);

        if (auto t = LyricsTrackTitle()) t.Text(track.Title());
        if (auto t = LyricsTrackArtist()) t.Text(track.Artist());
        if (auto t = FsLyricsTrackTitle()) t.Text(track.Title());
        if (auto t = FsLyricsTrackArtist()) t.Text(track.Artist());

        if (key == m_lyricsLoadedKey && !m_currentLyricsSynced.empty())
        {
            return;
        }

        if (key != m_lyricsLoadedKey)
        {
            ResetLyricsViewToEmpty(L"Loading lyrics…");
        }

        if (!m_lyricsService)
        {
            m_lyricsService = std::make_shared<LastMusicPlayer::Backend::LyricsService>();
        }
        m_lyricsService->SetRemoteMusicService(&RemoteMusicServiceService());

        auto epoch = ++m_lyricsHydrationEpoch;
        auto durationMs = static_cast<int64_t>(track.DurationSeconds() * 1000.0);
        HydrateLyricsAsync(track.Artist(), track.Title(), track.Album(), durationMs, track.SourceUrl(), key, epoch);
    }

    winrt::fire_and_forget MainWindow::HydrateLyricsAsync(
        winrt::hstring artist,
        winrt::hstring title,
        winrt::hstring album,
        int64_t durationMs,
        winrt::hstring sourceUrl,
        std::wstring key,
        uint64_t epoch)
    {
        auto weak = this->get_weak();
        auto service = m_lyricsService;
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

        winrt::hstring payload;
        try
        {
            payload = co_await service->FetchPayloadAsync(artist, title, album, durationMs, sourceUrl);
        }
        catch (...)
        {
            payload = winrt::hstring{};
        }

        auto result = LastMusicPlayer::Backend::LyricsService::ParseLyrics(payload);

        if (!dispatcher)
        {
            co_return;
        }

        dispatcher.TryEnqueue([weak, result = std::move(result), key = std::move(key), epoch]() mutable
        {
            auto self = weak.get();
            if (!self) return;
            if (epoch != self->m_lyricsHydrationEpoch) return;
            self->m_lyricsLoadedKey = key;
            self->RenderLyricsResult(result);
        });
    }

    void MainWindow::UpdateActiveLyricLine(int64_t positionMs)
    {
        if (m_currentLyricsSynced.empty())
        {
            return;
        }

        auto index = LastMusicPlayer::Backend::LyricsService::ActiveLineIndex(m_currentLyricsSynced, positionMs);
        if (index == m_currentLyricLineIndex)
        {
            return;
        }

        EnsureAccentBrushes();
        auto accent = m_brushAccent ? m_brushAccent : m_brushLabelIdle;
        auto dim = m_brushGlyphIdle ? m_brushGlyphIdle : m_brushLabelIdle;

        auto refreshPanel = [&](StackPanel const& panel, ScrollViewer const& scroller,
            double idleSize, double activeSize)
        {
            if (!panel) return;
            auto children = panel.Children();
            uint32_t size = children.Size();
            if (m_currentLyricLineIndex >= 0 && static_cast<uint32_t>(m_currentLyricLineIndex) < size)
            {
                if (auto tb = children.GetAt(m_currentLyricLineIndex).try_as<TextBlock>())
                {
                    ApplyLineForeground(tb, false, idleSize, activeSize, accent, dim);
                }
            }
            if (index >= 0 && static_cast<uint32_t>(index) < size)
            {
                if (auto tb = children.GetAt(index).try_as<TextBlock>())
                {
                    ApplyLineForeground(tb, true, idleSize, activeSize, accent, dim);
                }
                ScrollChildIntoCenter(scroller, panel, index);
            }
        };

        refreshPanel(LyricsLinesPanel(), LyricsScrollViewer(), 16.0, 22.0);
        refreshPanel(FsLyricsLinesPanel(), FsLyricsScrollViewer(), 20.0, 28.0);

        m_currentLyricLineIndex = index;
    }
}
