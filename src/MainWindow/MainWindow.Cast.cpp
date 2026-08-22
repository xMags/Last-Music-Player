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

    namespace
    {
        bool CanCastTrack(winrt::Last_Music_Player::TrackInfo const& track)
        {
            if (!track
                || RemoteMusicServiceService().Mode()
                    != LastMusicPlayer::Backend::RemoteAccessMode::Account)
            {
                return false;
            }
            if (!ProviderStreamUrlFor(track).empty())
            {
                return true;
            }

            return IsHttpUrl(track.SourceUrl());
        }
    }

    void MainWindow::ClearCastCallbacks()
    {
        m_cast.StatusChanged = {};
        m_cast.Ended = {};
        m_cast.Disconnected = {};
        m_castSession.EngineWired = false;
    }

    void MainWindow::WireCastEngine()
    {
        if (m_castSession.EngineWired) return;
        m_castSession.EngineWired = true;

        auto weakThis = get_weak();
        auto dispatcher = DispatcherQueue();

        m_cast.StatusChanged = [weakThis, dispatcher](winrt::hstring state, double cur, double dur)
        {
            dispatcher.TryEnqueue([weakThis, state, cur, dur]()
            {
                auto self = weakThis.get();
                if (!self || self->m_sink != PlaybackSink::Cast) return;
                bool playing = (state == L"PLAYING");
                self->m_castSession.IsPlaying = playing;
                auto glyph = playing ? L"\xE769" : L"\xE768";
                self->PlayPauseIcon().Glyph(glyph);
                if (self->FsPlayPauseIcon()) self->FsPlayPauseIcon().Glyph(glyph);

                self->m_castSession.CurrentSeconds = cur > 0.0 ? cur : 0.0;
                if (dur > 0.0)
                {
                    self->m_castSession.DurationSeconds = dur;
                }
                else if (self->m_castSession.DurationSeconds <= 0.0)
                {
                    auto current = AudioPlayerService().GetCurrentTrack();
                    if (current && current.DurationSeconds() > 0.0)
                    {
                        self->m_castSession.DurationSeconds = current.DurationSeconds();
                    }
                }
                self->m_castSession.ProgressStampMs = ::GetTickCount64();
                self->ApplyPlaybackProgress(self->m_castSession.CurrentSeconds, self->m_castSession.DurationSeconds);
                self->UpdateDiscordPlaybackState(
                    self->m_castSession.IsPlaying,
                    self->m_castSession.CurrentSeconds,
                    self->m_castSession.DurationSeconds);
            });
        };

        m_cast.Ended = [weakThis, dispatcher]()
        {
            dispatcher.TryEnqueue([weakThis]()
            {
                auto self = weakThis.get();
                if (self && self->m_sink == PlaybackSink::Cast) self->AdvanceQueue(+1, true);
            });
        };

        m_cast.Disconnected = [weakThis, dispatcher]()
        {
            dispatcher.TryEnqueue([weakThis]()
            {
                auto self = weakThis.get();
                if (!self || self->m_sink != PlaybackSink::Cast) return;
                self->m_sink = PlaybackSink::Local;
                self->m_castSession.DeviceId = L"";
                self->m_castSession.DeviceName = L"";
                self->m_castSession.IsPlaying = false;
                self->m_castSession.CurrentSeconds = 0.0;
                self->m_castSession.DurationSeconds = 0.0;
                self->m_castSession.ProgressStampMs = 0;
                self->m_castSession.LastStatusRequestMs = 0;
                self->EnsureAccentBrushes();
                if (self->CastIcon()) self->CastIcon().Foreground(self->m_brushGlyphIdle);
                self->UpdateDiscordPlaybackState(false, 0.0, 0.0);
            });
        };
    }

    void MainWindow::CastButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        PopulateCastMenuAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::PopulateCastMenuAsync()
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();
        WireCastEngine();

        using namespace winrt::Microsoft::UI::Xaml::Controls;
        auto current = AudioPlayerService().GetCurrentTrack();
        if (!CanCastTrack(current))
        {
            MenuFlyout unavailable;
            MenuFlyoutItem message;
            auto apiKeyMode = RemoteMusicServiceService().Mode()
                == LastMusicPlayer::Backend::RemoteAccessMode::ApiKey;
            message.Text(apiKeyMode && current
                ? L"Casting API-key tracks is disabled to keep the API key on this PC."
                : (current
                    ? L"This track cannot be cast. Play an account track with an available source first."
                    : L"Play an account track before connecting a Cast device."));
            message.IsEnabled(false);
            unavailable.Items().Append(message);
            unavailable.ShowAt(CastButton());
            co_return;
        }

        MenuFlyout flyout;
        {
            MenuFlyoutItem scanning;
            scanning.Text(L"Scanning for devices…");
            scanning.IsEnabled(false);
            flyout.Items().Append(scanning);
        }
        flyout.ShowAt(CastButton());

        co_await winrt::resume_background();
        std::vector<LastMusicPlayer::Backend::CastDevice> devices;
        try
        {
            devices = m_cast.Discover(3000);
        }
        catch (...)
        {
        }

        co_await wil::resume_foreground(dispatcher);
        flyout.Hide();

        MenuFlyout result;
        if (m_sink == PlaybackSink::Cast)
        {
            MenuFlyoutItem stop;
            stop.Text(L"Stop casting");
            stop.Tag(winrt::box_value(winrt::hstring{ L"__stop__" }));
            stop.Click({ this, &MainWindow::CastDevice_Click });
            result.Items().Append(stop);
            result.Items().Append(MenuFlyoutSeparator{});
        }

        for (auto const& d : devices)
        {
            MenuFlyoutItem item;
            item.Text(winrt::hstring(d.name));
            item.Tag(winrt::box_value(winrt::hstring(d.host)));
            item.Click({ this, &MainWindow::CastDevice_Click });
            result.Items().Append(item);
        }

        if (devices.empty())
        {
            MenuFlyoutItem none;
            none.Text(L"No Chromecast devices found");
            none.IsEnabled(false);
            result.Items().Append(none);
        }

        result.ShowAt(CastButton());
        co_return;
    }

    void MainWindow::CastDevice_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto item = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem>();
        if (!item) return;
        auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"");
        if (tag == L"__stop__")
        {
            StopCastAsync();
            return;
        }
        StartCastAsync(tag /*host*/, tag, item.Text());
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::StartCastAsync(winrt::hstring deviceId, winrt::hstring host, winrt::hstring deviceName)
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();
        WireCastEngine();
        winrt::hstring connectError;

        auto currentBeforeConnect = AudioPlayerService().GetCurrentTrack();
        if (!CanCastTrack(currentBeforeConnect))
        {
            co_return;
        }

        try
        {
            co_await m_cast.ConnectAsync(host);
            auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            while (m_cast.IsConnected()
                && !m_cast.IsMediaChannelOpen()
                && std::chrono::steady_clock::now() < readyDeadline)
            {
                co_await winrt::resume_after(std::chrono::milliseconds(100));
            }
        }
        catch (winrt::hresult_error const& ex)
        {
            connectError = ex.message();
        }
        catch (...)
        {
            connectError = L"Connection failed";
        }

        co_await wil::resume_foreground(dispatcher);
        if (!m_cast.IsConnected() || !m_cast.IsMediaChannelOpen())
        {
            m_cast.Disconnect();
            m_sink = PlaybackSink::Local;
            EnsureAccentBrushes();
            if (CastIcon()) CastIcon().Foreground(m_brushGlyphIdle);

            winrt::Microsoft::UI::Xaml::Controls::MenuFlyout failedFlyout;
            winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem failed;
            std::wstring text = L"Could not connect";
            if (!deviceName.empty())
            {
                text += L" to ";
                text += deviceName.c_str();
            }
            if (!connectError.empty())
            {
                text += L": ";
                text += connectError.c_str();
            }
            failed.Text(winrt::hstring{ text });
            failed.IsEnabled(false);
            failedFlyout.Items().Append(failed);
            failedFlyout.ShowAt(CastButton());
            co_return;
        }

        auto current = AudioPlayerService().GetCurrentTrack();
        if (!CanCastTrack(current))
        {
            m_cast.Disconnect();
            m_sink = PlaybackSink::Local;
            EnsureAccentBrushes();
            if (CastIcon()) CastIcon().Foreground(m_brushGlyphIdle);

            winrt::Microsoft::UI::Xaml::Controls::MenuFlyout unavailable;
            winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem message;
            message.Text(L"The track changed to one that cannot be cast, so casting was cancelled.");
            message.IsEnabled(false);
            unavailable.Items().Append(message);
            unavailable.ShowAt(CastButton());
            co_return;
        }

        m_castSession.DeviceId = deviceId;
        m_castSession.DeviceName = deviceName;
        m_sink = PlaybackSink::Cast;
        AudioPlayerService().GetMediaPlayer().Pause();
        EnsureAccentBrushes();
        if (CastIcon()) CastIcon().Foreground(m_brushAccent);

        PlayTrack(current);
        co_return;
    }

    void MainWindow::StopCastAsync()
    {
        bool wasCastPlaying = m_castSession.IsPlaying;
        auto current = AudioPlayerService().GetCurrentTrack();
        m_cast.Stop();
        m_cast.Disconnect();
        m_sink = PlaybackSink::Local;
        m_castSession.DeviceId = L"";
        m_castSession.DeviceName = L"";
        m_castSession.IsPlaying = false;
        m_castSession.CurrentSeconds = 0.0;
        m_castSession.DurationSeconds = 0.0;
        m_castSession.ProgressStampMs = 0;
        m_castSession.LastStatusRequestMs = 0;
        EnsureAccentBrushes();
        if (CastIcon()) CastIcon().Foreground(m_brushGlyphIdle);

        if (current && wasCastPlaying)
        {
            PlayTrack(current);
        }
        else
        {
            AudioPlayerService().GetMediaPlayer().Pause();
            PlayPauseIcon().Glyph(L"\xE768");
            if (FsPlayPauseIcon()) FsPlayPauseIcon().Glyph(L"\xE768");
            UpdateDiscordPlaybackState(false, 0.0, current ? current.DurationSeconds() : 0.0);
        }
    }

}
