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
#include <filesystem>
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
    void MainWindow::SetPlaybackQueue(std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks, int selectedIndex)
    {
        m_queue.Queue = tracks;
        m_queue.QueueIndex = selectedIndex;
        // A new playback context starts a fresh radio lineage: cancel any
        // in-flight fetch (epoch bump), forget injected-track keys, and drop a
        // stale resume request so a late fetch can't hijack this context.
        ++m_autoplay.Epoch;
        m_autoplay.SeenKeys.clear();
        m_autoplay.ResumeWhenReady = false;
        if (m_queue.ShuffleEnabled)
        {
            RebuildShuffleOrder();
        }
        RebuildUpNextQueue();
        MaybeExtendAutoplayQueue();
    }

    void MainWindow::QueueAndPlayVisible(std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks, winrt::Last_Music_Player::TrackInfo const& clickedTrack)
    {
        if (tracks.empty())
        {
            return;
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> queue;
        queue.reserve(tracks.size());
        std::unordered_map<std::wstring, int> queuedKeys;
        auto clickedKey = CatalogSourceKey(clickedTrack);
        int selectedIndex = 0;
        bool clickedQueued = false;

        for (auto const& track : tracks)
        {
            if (!IsPlayableHomeTrack(track))
            {
                continue;
            }

            auto key = CatalogSourceKey(track);
            if (key.empty())
            {
                continue;
            }

            auto existing = queuedKeys.find(key);
            if (existing != queuedKeys.end())
            {
                if (key == clickedKey)
                {
                    selectedIndex = existing->second;
                    clickedQueued = true;
                }
                continue;
            }

            if (key == clickedKey)
            {
                selectedIndex = static_cast<int>(queue.size());
                clickedQueued = true;
            }

            queuedKeys.emplace(std::move(key), static_cast<int>(queue.size()));
            queue.push_back(track);
        }

        if (!clickedQueued && IsPlayableHomeTrack(clickedTrack))
        {
            selectedIndex = static_cast<int>(queue.size());
            queue.push_back(clickedTrack);
        }

        if (queue.empty())
        {
            return;
        }

        if (PlayTrack(clickedTrack))
        {
            SetPlaybackQueue(queue, selectedIndex);
            MusicListView().SelectedItem(clickedTrack);
            LibrarySongsListView().SelectedItem(clickedTrack);
            LibraryDetailTracksListView().SelectedItem(clickedTrack);
        }
    }

    void MainWindow::QueueAndPlayObservable(winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> const& tracks, winrt::Last_Music_Player::TrackInfo const& clickedTrack)
    {
        std::vector<winrt::Last_Music_Player::TrackInfo> visible;
        visible.reserve(tracks.Size());
        for (uint32_t i = 0; i < tracks.Size(); ++i)
        {
            visible.push_back(tracks.GetAt(i));
        }
        QueueAndPlayVisible(visible, clickedTrack);
    }

    void MainWindow::NextButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        AdvanceQueue(+1, false);
    }

    void MainWindow::OnMediaEnded(winrt::Windows::Media::Playback::MediaPlayer const& sender, winrt::Windows::Foundation::IInspectable const& args)
    {
        (void)args;
        // Sample NaturalDuration on the audio thread before we hop to the
        // UI dispatcher — the playback session can transition between
        // events and surface 0 once a new item starts loading.
        double duration = 0.0;
        try
        {
            duration = static_cast<double>(sender.PlaybackSession().NaturalDuration().count()) / 10000000.0;
        }
        catch (...) {}

        DispatcherQueue().TryEnqueue([this, duration]()
        {
            // Played to the end, so there is nothing to come back to. Clearing
            // here is what stops a finished track from sitting in the resume
            // shelf offering its last few seconds.
            ClearResumePosition(AudioPlayerService().GetCurrentTrack());
            // Snap the timeline to 100% before AdvanceQueue runs. The 500 ms
            // progress poll typically leaves Slider.Value 0.1–0.5 s short
            // of NaturalDuration at end-of-track, which renders as a
            // visible unfilled stub when AdvanceQueue ends up pausing
            // (queue exhausted, repeat off). If a next track actually
            // starts, ApplyPlaybackProgress will overwrite this with the
            // new track's 0/duration almost immediately.
            if (duration > 0.0)
            {
                ApplyPlaybackProgress(duration, duration);
            }
            AdvanceQueue(+1, true);
        });
    }

    void MainWindow::OnPlaybackListCurrentItemChanged(
        winrt::Windows::Media::Playback::MediaPlaybackList const& sender,
        winrt::Windows::Media::Playback::CurrentMediaPlaybackItemChangedEventArgs const& args)
    {
        (void)sender;
        // EndOfStream is the only reason that corresponds to a natural gapless
        // transition. Manual skips (UserSkipped) and resets (InitialItem) go
        // through PlayTrack already and don't need this handler.
        if (args.Reason() != winrt::Windows::Media::Playback::MediaPlaybackItemChangedReason::EndOfStream)
        {
            return;
        }
        DispatcherQueue().TryEnqueue([this]()
        {
            auto const& queue = m_queue.Queue.empty() ? m_queue.CurrentPlaylist : m_queue.Queue;
            if (queue.empty())
            {
                return;
            }
            int& index = m_queue.Queue.empty() ? m_queue.CurrentTrackIndex : m_queue.QueueIndex;
            int size = static_cast<int>(queue.size());
            int nextIdx = ComputeAutoAdvanceIndex(index, size);
            if (nextIdx < 0 || nextIdx >= size)
            {
                // No more items expected; the trailing MediaEnded will close
                // out the session via AdvanceQueue.
                return;
            }
            auto track = queue[static_cast<size_t>(nextIdx)];
            index = nextIdx;
            // gaplessTransitioned=true skips audio-source reload; PlayTrack
            // just hydrates the UI for the new current and refreshes lookahead.
            PlayTrack(track, /*gaplessTransitioned=*/true);
            MusicListView().SelectedItem(track);
            RebuildUpNextQueue();
            MaybeExtendAutoplayQueue();
        });
    }

    void MainWindow::PreviousButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto& queue = m_queue.Queue.empty() ? m_queue.CurrentPlaylist : m_queue.Queue;
        if (queue.empty()) return;

        if (m_sink == PlaybackSink::Local)
        {
            auto session = AudioPlayerService().GetMediaPlayer().PlaybackSession();
            // If we are more than 3 seconds in, just restart the current song.
            if (session.Position().count() > 30000000) // 3 seconds in 100-ns ticks
            {
                session.Position(winrt::Windows::Foundation::TimeSpan{0});
                return;
            }
        }

        AdvanceQueue(-1, false);
    }

    void MainWindow::AdvanceQueue(int direction, bool isAutoAdvance)
    {
        auto& queue = m_queue.Queue.empty() ? m_queue.CurrentPlaylist : m_queue.Queue;
        auto& index = m_queue.Queue.empty() ? m_queue.CurrentTrackIndex : m_queue.QueueIndex;

        // A manual Next/Previous cancels any pending autoplay auto-resume so a
        // late radio fetch can't restart playback the user didn't ask for.
        if (!isAutoAdvance)
        {
            m_autoplay.ResumeWhenReady = false;
        }

        // Repeat-One short-circuits before anything else, including the
        // user queue — Repeat One is "loop THIS track" by definition.
        if (isAutoAdvance && m_queue.RepeatMode == 2 && !queue.empty()
            && index >= 0 && index < static_cast<int>(queue.size()))
        {
            PlayTrack(queue[index]);
            return;
        }

        // Forward navigation (auto-advance at track-end OR a manual Next)
        // pops the UserQueue first. The user explicitly said "play this
        // next" via right-click → Add to Queue / Play Next, so honour
        // that ahead of walking the playback context. Previous-button
        // navigation never touches UserQueue — you don't put items back.
        if (direction > 0 && !m_queue.UserQueue.empty())
        {
            auto next = m_queue.UserQueue.front();
            // Pop the user-queued track regardless of play result; if it
            // failed, the bounded retry below moves to whatever's next
            // (the rest of UserQueue, or the playback context).
            m_queue.UserQueue.erase(m_queue.UserQueue.begin());
            MusicListView().SelectedItem(next);
            if (PlayTrack(next))
            {
                m_advancePlayFailures = 0;
                RebuildUpNextQueue();
                MaybeExtendAutoplayQueue();
                return;
            }
            if (++m_advancePlayFailures < 3)
            {
                // Keep walking forward — into another UserQueue item or
                // back into the main queue path. Bounded so a queue full
                // of broken URLs can't infinite-recurse.
                AdvanceQueue(direction, isAutoAdvance);
            }
            else
            {
                m_advancePlayFailures = 0;
                TransportPause();
            }
            return;
        }

        if (queue.empty()) return;
        int size = static_cast<int>(queue.size());

        int nextIndex;
        if (m_queue.ShuffleEnabled && size > 1)
        {
            if (m_queue.ShuffleOrder.size() != static_cast<size_t>(size))
            {
                RebuildShuffleOrder();
            }
            auto it = std::find(m_queue.ShuffleOrder.begin(), m_queue.ShuffleOrder.end(), index);
            long long pos = (it == m_queue.ShuffleOrder.end()) ? 0 : std::distance(m_queue.ShuffleOrder.begin(), it);
            long long np = pos + direction;
            if (np >= static_cast<long long>(m_queue.ShuffleOrder.size()))
            {
                if (isAutoAdvance && m_queue.RepeatMode == 0)
                {
                    // End of the shuffle order. If autoplay is on, ask the radio
                    // to keep the music going; resume once tracks land. Either
                    // way pause now — the in-flight fetch restarts playback.
                    if (SettingsManagerService().GetBool(L"Autoplay", true))
                    {
                        m_autoplay.ResumeWhenReady = true;
                        MaybeExtendAutoplayQueue();
                    }
                    TransportPause();
                    return;
                }
                np = 0; // wrap (manual next, or repeat-all / shuffle loop)
            }
            else if (np < 0)
            {
                np = static_cast<long long>(m_queue.ShuffleOrder.size()) - 1;
            }
            nextIndex = m_queue.ShuffleOrder[static_cast<size_t>(np)];
        }
        else
        {
            nextIndex = index + direction;
            if (nextIndex >= size)
            {
                if (isAutoAdvance && m_queue.RepeatMode == 0)
                {
                    // End of the queue. If autoplay is on, ask the radio to keep
                    // the music going; resume once tracks land. Either way pause
                    // now — the in-flight fetch restarts playback.
                    if (SettingsManagerService().GetBool(L"Autoplay", true))
                    {
                        m_autoplay.ResumeWhenReady = true;
                        MaybeExtendAutoplayQueue();
                    }
                    TransportPause();
                    return;
                }
                nextIndex = 0; // wrap
            }
            else if (nextIndex < 0)
            {
                nextIndex = size - 1;
            }
        }

        auto track = queue[nextIndex];
        // Advance the cursor *before* trying to play. Pre-fix this update
        // only happened on PlayTrack success, which meant a single broken
        // track left the queue position unchanged — every later Next click
        // re-tried the same broken track, and there was no way out
        // without manually clicking another row.
        index = nextIndex;
        MusicListView().SelectedItem(track);
        if (PlayTrack(track))
        {
            m_advancePlayFailures = 0;
            RebuildUpNextQueue();
            MaybeExtendAutoplayQueue();
            return;
        }
        if (++m_advancePlayFailures < 3)
        {
            // Re-enter to step past the broken track. Capped so a
            // completely broken queue can't infinite-recurse.
            AdvanceQueue(direction, isAutoAdvance);
        }
        else
        {
            m_advancePlayFailures = 0;
            TransportPause();
        }
    }

    void MainWindow::RebuildShuffleOrder()
    {
        auto const& queue = m_queue.Queue.empty() ? m_queue.CurrentPlaylist : m_queue.Queue;
        m_queue.ShuffleOrder.clear();
        m_queue.ShuffleOrder.reserve(queue.size());
        for (int i = 0; i < static_cast<int>(queue.size()); ++i)
        {
            m_queue.ShuffleOrder.push_back(i);
        }
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(m_queue.ShuffleOrder.begin(), m_queue.ShuffleOrder.end(), gen);
        m_queue.ShuffleCursor = 0;
    }

    void MainWindow::UpdateShuffleRepeatVisuals()
    {
        using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
        EnsureAccentBrushes();
        auto accent = m_brushAccent;
        auto idle = m_brushGlyphIdle;

        if (ShuffleIcon())
        {
            ShuffleIcon().Foreground(m_queue.ShuffleEnabled ? accent : idle);
        }
        if (FsShuffleIcon())
        {
            FsShuffleIcon().Foreground(m_queue.ShuffleEnabled ? accent : idle);
        }

        // Repeat: glyph E8EE (off/all) or E8ED (repeat one). Accent when not Off.
        auto repeatGlyph = (m_queue.RepeatMode == 2) ? L"\xE8ED" : L"\xE8EE";
        if (RepeatIcon())
        {
            RepeatIcon().Glyph(repeatGlyph);
            RepeatIcon().Foreground(m_queue.RepeatMode != 0 ? accent : idle);
        }
        if (FsRepeatIcon())
        {
            FsRepeatIcon().Glyph(repeatGlyph);
            FsRepeatIcon().Foreground(m_queue.RepeatMode != 0 ? accent : idle);
        }
    }

    void MainWindow::RestorePlaybackPreferences()
    {
        m_queue.ShuffleEnabled = SettingsManagerService().GetBool(L"ShuffleEnabled", false);
        m_queue.RepeatMode = SettingsManagerService().GetInt(L"RepeatMode", 0);
        if (m_queue.RepeatMode < 0 || m_queue.RepeatMode > 2) m_queue.RepeatMode = 0;
        if (m_queue.ShuffleEnabled)
        {
            RebuildShuffleOrder();
        }
        UpdateShuffleRepeatVisuals();
    }

    void MainWindow::ShuffleButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        m_queue.ShuffleEnabled = !m_queue.ShuffleEnabled;
        if (m_queue.ShuffleEnabled)
        {
            RebuildShuffleOrder();
        }
        SettingsManagerService().SetBool(L"ShuffleEnabled", m_queue.ShuffleEnabled);
        SettingsManagerService().Save();
        UpdateShuffleRepeatVisuals();
    }

    void MainWindow::EnsureShuffleOn()
    {
        if (m_queue.ShuffleEnabled)
        {
            return;
        }
        m_queue.ShuffleEnabled = true;
        RebuildShuffleOrder();
        SettingsManagerService().SetBool(L"ShuffleEnabled", true);
        SettingsManagerService().Save();
        UpdateShuffleRepeatVisuals();
    }

    void MainWindow::RepeatButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        m_queue.RepeatMode = (m_queue.RepeatMode + 1) % 3; // Off -> All -> One -> Off
        SettingsManagerService().SetInt(L"RepeatMode", m_queue.RepeatMode);
        SettingsManagerService().Save();
        UpdateShuffleRepeatVisuals();
    }

    void MainWindow::QueueButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        using winrt::Microsoft::UI::Xaml::GridLengthHelper;
        m_queueRailForced = !m_queueRailForced;
        if (m_queueRailForced)
        {
            if (RightRailColumn())
            {
                RightRailColumn().Width(GridLengthHelper::FromPixels(320.0));
            }
            SetRailTab(true);
        }
        else if (RightRailColumn())
        {
            RightRailColumn().Width(GridLengthHelper::FromPixels(0.0));
        }
    }

    void MainWindow::RebuildUpNextQueue()
    {
        m_upNextQueue.Clear();

        // 1. UserQueue items first (the explicit "Next in Queue" — what
        //    the user added via right-click Add to Queue / Play Next).
        //    AdvanceQueue pops these before walking the context, so this
        //    order matches actual play order.
        for (auto const& track : m_queue.UserQueue)
        {
            auto copy = track;
            ResolveArtworkPresentation(copy, L"track");
            m_upNextQueue.Append(copy);
        }

        // 2. Context items after the currently playing one. This is the
        //    "Next Up from <wherever the user was browsing>" segment —
        //    auto-derived from whatever surface seeded the playback
        //    context (Songs query, Library, Playlist).
        auto const& queue = m_queue.Queue.empty() ? m_queue.CurrentPlaylist : m_queue.Queue;
        auto currentIndex = m_queue.Queue.empty() ? m_queue.CurrentTrackIndex : m_queue.QueueIndex;

        if (queue.empty())
        {
            PrefetchUpcomingStreams();
            return;
        }

        if (currentIndex < 0 || currentIndex >= static_cast<int>(queue.size()))
        {
            for (auto const& track : queue)
            {
                auto copy = track;
                ResolveArtworkPresentation(copy, L"track");
                m_upNextQueue.Append(copy);
            }
            PrefetchUpcomingStreams();
            return;
        }

        auto selectedIndex = static_cast<size_t>(currentIndex);
        for (size_t offset = 1; offset < queue.size(); ++offset)
        {
            auto nextIndex = (selectedIndex + offset) % queue.size();
            auto copy = queue[nextIndex];
            ResolveArtworkPresentation(copy, L"track");
            m_upNextQueue.Append(copy);
        }

        PrefetchUpcomingStreams();
    }

    void MainWindow::PrefetchUpcomingStreams()
    {
        // Only meaningful for local playback — a Chromecast pulls the stream
        // itself, server-side. Prefetch a small lookahead so the next couple of
        // tracks are fully on disk by the time the user reaches them, and so
        // play from a local file (no live network) immune to connection jitter.
        auto& remoteMusic = RemoteMusicServiceService();
        auto remoteScope = remoteMusic.CaptureScope();
        if (m_sink != PlaybackSink::Local
            || remoteScope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::ApiKey
            || !remoteMusic.IsCurrent(remoteScope))
        {
            return;
        }

        constexpr uint32_t kLookahead = 3;
        uint32_t count = (std::min<uint32_t>)(kLookahead, m_upNextQueue.Size());
        for (uint32_t i = 0; i < count; ++i)
        {
            auto track = m_upNextQueue.GetAt(i);
            if (!track || track.File())
            {
                continue;  // local file: nothing to prefetch
            }
            auto key = ApiKeyStreamCacheKey(remoteScope, track);
            if (key.empty())
            {
                continue;
            }
            auto streamUrl = ProviderStreamUrlFor(track);
            if (streamUrl.empty())
            {
                continue;
            }
            if (!remoteMusic.IsCurrent(remoteScope))
            {
                return;
            }
            StreamCacheService().Prefetch(key, std::wstring{ streamUrl.c_str() });
        }
    }

    void MainWindow::MaybeExtendAutoplayQueue()
    {
        // Gated by the user setting and enabled by default.
        if (!SettingsManagerService().GetBool(L"Autoplay", true))
        {
            return;
        }
        // Repeat-All / Repeat-One already loop the queue, so radio injection
        // would fight them; only extend when repeat is Off.
        if (m_queue.RepeatMode != 0 || m_autoplay.InFlight)
        {
            return;
        }

        auto const& queue = m_queue.Queue.empty() ? m_queue.CurrentPlaylist : m_queue.Queue;
        if (queue.empty())
        {
            return;
        }
        int index = m_queue.Queue.empty() ? m_queue.CurrentTrackIndex : m_queue.QueueIndex;
        int size = static_cast<int>(queue.size());

        // Count how many tracks are still upcoming. With shuffle on, "upcoming"
        // is measured along ShuffleOrder, not the raw index.
        int ahead;
        if (m_queue.ShuffleEnabled && m_queue.ShuffleOrder.size() == static_cast<size_t>(size))
        {
            auto it = std::find(m_queue.ShuffleOrder.begin(), m_queue.ShuffleOrder.end(), index);
            long long pos = (it == m_queue.ShuffleOrder.end()) ? 0 : std::distance(m_queue.ShuffleOrder.begin(), it);
            ahead = static_cast<int>(static_cast<long long>(size) - 1 - pos);
        }
        else
        {
            ahead = size - 1 - index;
        }
        ahead += static_cast<int>(m_queue.UserQueue.size());
        if (ahead > 2)
        {
            return;
        }

        // Seed the radio from whatever is currently playing (fall back to the
        // queue cursor when the engine hasn't loaded a track yet).
        auto seed = AudioPlayerService().GetCurrentTrack();
        if (!seed && index >= 0 && index < size)
        {
            seed = queue[static_cast<size_t>(index)];
        }
        if (!seed)
        {
            return;
        }

        m_autoplay.InFlight = true;
        ++m_autoplay.Epoch;
        ExtendAutoplayQueueAsync(seed, m_autoplay.Epoch);
    }

    winrt::fire_and_forget MainWindow::ExtendAutoplayQueueAsync(winrt::Last_Music_Player::TrackInfo seed, uint64_t epoch)
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();

        auto seedSourceUrl = seed.SourceUrl();
        auto seedArtist = seed.Artist();
        auto& remoteMusic = RemoteMusicServiceService();

        bool seedIsRemote = !seedSourceUrl.empty() && IsHttpUrl(seedSourceUrl);
        if (!remoteMusic.HasRemoteAccess() || (!seedIsRemote && seedArtist.empty()))
        {
            m_autoplay.InFlight = false;
            co_return;
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> candidates;

        // Primary fetch: real YT-Music radio for remote seeds, artist search
        // for local ones. Parse on the UI thread (track build touches XAML
        // bitmap objects), so only the network call runs on the background.
        {
            co_await winrt::resume_background();
            winrt::hstring payload;
            try
            {
                payload = seedIsRemote
                    ? co_await remoteMusic.GetRelatedAsync(seedSourceUrl)
                    : co_await remoteMusic.SearchAsync(seedArtist);
            }
            catch (...)
            {
                payload = {};
            }

            co_await wil::resume_foreground(dispatcher);
            if (epoch != m_autoplay.Epoch)
            {
                m_autoplay.InFlight = false;
                co_return;
            }
            if (!payload.empty())
            {
                try { candidates = ParseProviderTracks(payload, 25); } catch (...) { candidates.clear(); }
            }
        }

        // Fallback: a remote seed with no radio mix → artist search.
        if (candidates.empty() && seedIsRemote && !seedArtist.empty())
        {
            co_await winrt::resume_background();
            winrt::hstring payload;
            try { payload = co_await remoteMusic.SearchAsync(seedArtist); }
            catch (...) { payload = {}; }

            co_await wil::resume_foreground(dispatcher);
            if (epoch != m_autoplay.Epoch)
            {
                m_autoplay.InFlight = false;
                co_return;
            }
            if (!payload.empty())
            {
                try { candidates = ParseProviderTracks(payload, 25); } catch (...) { candidates.clear(); }
            }
        }

        // The user may have switched autoplay off while we were fetching.
        if (!SettingsManagerService().GetBool(L"Autoplay", true))
        {
            m_autoplay.InFlight = false;
            co_return;
        }

        EnsurePlaybackQueueSeeded();

        // Dedupe against everything already queued + recently played + radio
        // already injected this session, so the lineage keeps evolving instead
        // of looping the same handful of songs.
        std::unordered_set<std::wstring> existing(m_autoplay.SeenKeys);
        auto note = [&](winrt::Last_Music_Player::TrackInfo const& t)
        {
            auto k = CatalogSourceKey(t);
            if (!k.empty()) existing.insert(k);
        };
        for (auto const& t : m_queue.Queue) note(t);
        for (auto const& t : m_queue.UserQueue) note(t);
        for (auto const& t : m_homeRecentHistory) note(t);

        size_t beforeCount = m_queue.Queue.size();
        int appended = 0;
        for (auto const& track : candidates)
        {
            if (appended >= 20) break;
            if (!IsPlayableHomeTrack(track)) continue;
            auto key = CatalogSourceKey(track);
            if (key.empty() || existing.count(key)) continue;
            existing.insert(key);
            m_autoplay.SeenKeys.insert(key);
            auto copy = track;
            ResolveArtworkPresentation(copy, L"track");
            m_queue.Queue.push_back(copy);
            ++appended;
        }

        if (appended > 0)
        {
            // Extend the shuffle order in place (append the new indices) so we
            // don't reshuffle already-played history mid-session.
            if (m_queue.ShuffleEnabled)
            {
                if (m_queue.ShuffleOrder.size() == beforeCount)
                {
                    for (int i = static_cast<int>(beforeCount); i < static_cast<int>(m_queue.Queue.size()); ++i)
                    {
                        m_queue.ShuffleOrder.push_back(i);
                    }
                }
                else
                {
                    RebuildShuffleOrder();
                }
            }
            RebuildUpNextQueue();
            RefreshGaplessLookahead();

            // Reactive resume: the queue had hit its end mid-fetch and asked us
            // to start playback once radio tracks landed.
            if (m_autoplay.ResumeWhenReady)
            {
                m_autoplay.ResumeWhenReady = false;
                AdvanceQueue(+1, true);
            }
        }

        m_autoplay.InFlight = false;
    }

    void MainWindow::EnsurePlaybackQueueSeeded()
    {
        // The visible queue is derived from m_queue.Queue when present,
        // else from m_queue.CurrentPlaylist. Any queue mutation needs the
        // authoritative m_queue.Queue materialized first. Mirrors the
        // seeding already done by AddSongsTrackToQueue / PlayNextFromSongs.
        if (!m_queue.Queue.empty())
        {
            return;
        }
        if (!m_queue.CurrentPlaylist.empty())
        {
            m_queue.Queue = m_queue.CurrentPlaylist;
            m_queue.QueueIndex = m_queue.CurrentTrackIndex;
            return;
        }
        if (auto current = AudioPlayerService().GetCurrentTrack())
        {
            m_queue.Queue.push_back(current);
            m_queue.QueueIndex = 0;
        }
    }

    void MainWindow::CommitQueueOrderFromUpNext()
    {
        // The user just reordered the visible up-next list (drag-drop or
        // Move up/down). The visible list is [UserQueue items, ...context
        // items after the current track], so split the post-reorder
        // sequence back into the same two buckets: the first N entries
        // (where N is the prior UserQueue size, clamped to the new size)
        // stay in UserQueue, the rest become the playback context (with
        // the currently-playing track pinned at index 0). This keeps the
        // "Next in Queue" section persistent across context replacements
        // while still respecting whatever new order the user dragged into.
        size_t total = static_cast<size_t>(m_upNextQueue.Size());
        size_t userN = (std::min)(m_queue.UserQueue.size(), total);

        std::vector<winrt::Last_Music_Player::TrackInfo> newUserQueue;
        newUserQueue.reserve(userN);
        for (size_t i = 0; i < userN; ++i)
        {
            newUserQueue.push_back(m_upNextQueue.GetAt(static_cast<uint32_t>(i)));
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> newContext;
        newContext.reserve(total - userN + 1);
        auto current = AudioPlayerService().GetCurrentTrack();
        if (current)
        {
            newContext.push_back(current);
        }
        for (size_t i = userN; i < total; ++i)
        {
            newContext.push_back(m_upNextQueue.GetAt(static_cast<uint32_t>(i)));
        }

        m_queue.UserQueue = std::move(newUserQueue);
        m_queue.Queue = std::move(newContext);
        m_queue.QueueIndex = current ? 0 : -1;
        SaveAppState();
    }

    void MainWindow::RemoveTrackFromQueue(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track)
        {
            return;
        }

        auto trackKey = CatalogSourceKey(track);

        // 1. UserQueue first — items added via right-click Add to Queue /
        //    Play Next. No "current track" exception here since the user's
        //    explicit queue never contains the currently-playing track.
        for (size_t i = 0; i < m_queue.UserQueue.size(); ++i)
        {
            auto const& candidate = m_queue.UserQueue[i];
            bool sameObject = (candidate == track);
            bool sameKey = !trackKey.empty() && CatalogSourceKey(candidate) == trackKey;
            if (sameObject || sameKey)
            {
                m_queue.UserQueue.erase(m_queue.UserQueue.begin() + i);
                RebuildUpNextQueue();
                SaveAppState();
                return;
            }
        }

        // 2. Fall back to the playback context (Queue / CurrentPlaylist),
        //    guarding the currently-playing track from being yanked.
        EnsurePlaybackQueueSeeded();
        if (m_queue.Queue.empty())
        {
            return;
        }

        auto current = AudioPlayerService().GetCurrentTrack();
        for (size_t i = 0; i < m_queue.Queue.size(); ++i)
        {
            auto const& candidate = m_queue.Queue[i];
            bool sameObject = (candidate == track);
            bool sameKey = !trackKey.empty() && CatalogSourceKey(candidate) == trackKey;
            if (!sameObject && !sameKey)
            {
                continue;
            }

            // Never yank the track that is currently playing.
            if (static_cast<int>(i) == m_queue.QueueIndex ||
                (current && candidate == current))
            {
                continue;
            }

            m_queue.Queue.erase(m_queue.Queue.begin() + i);
            if (static_cast<int>(i) < m_queue.QueueIndex)
            {
                --m_queue.QueueIndex;
            }
            break;
        }

        RebuildUpNextQueue();
        SaveAppState();
    }

    void MainWindow::MoveQueueTrack(winrt::Last_Music_Player::TrackInfo const& track, int delta)
    {
        if (!track || delta == 0 || m_upNextQueue.Size() == 0)
        {
            return;
        }

        uint32_t index = 0;
        if (!m_upNextQueue.IndexOf(track, index))
        {
            return;
        }

        int lastIndex = static_cast<int>(m_upNextQueue.Size()) - 1;
        int target = static_cast<int>(index) + delta;
        if (target < 0)
        {
            target = 0;
        }
        if (target > lastIndex)
        {
            target = lastIndex;
        }
        if (target == static_cast<int>(index))
        {
            return;
        }

        auto moved = m_upNextQueue.GetAt(index);
        m_upNextQueue.RemoveAt(index);
        m_upNextQueue.InsertAt(static_cast<uint32_t>(target), moved);
        CommitQueueOrderFromUpNext();
    }

    void MainWindow::QueuePlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        PlaySongsTrack(TrackFromActionSender(sender));
    }

    void MainWindow::QueuePlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        PlayNextFromSongs(TrackFromActionSender(sender));
    }

    void MainWindow::QueueMoveUp_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        MoveQueueTrack(TrackFromActionSender(sender), -1);
    }

    void MainWindow::QueueMoveDown_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        MoveQueueTrack(TrackFromActionSender(sender), 1);
    }

    void MainWindow::QueueRemove_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        RemoveTrackFromQueue(TrackFromActionSender(sender));
    }

    void MainWindow::UpNextQueue_DragItemsCompleted(winrt::Microsoft::UI::Xaml::Controls::ListViewBase const& sender, winrt::Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        // The ListView already reordered the shared m_upNextQueue in place.
        CommitQueueOrderFromUpNext();
    }

}
