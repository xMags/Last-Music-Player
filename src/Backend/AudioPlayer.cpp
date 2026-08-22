#include "pch.h"
#include "Backend/AudioPlayer.h"
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Media.Core.h>
#include <shobjidl.h> // For IInitializeWithWindow

namespace LastMusicPlayer::Backend
{
    winrt::Windows::Foundation::IAsyncAction AudioPlayer::OpenFile(HWND hwnd)
    {
        winrt::Windows::Storage::Pickers::FileOpenPicker picker;

        // Associate the picker with the current HWND natively using standard COM
        auto initializeWithWindow{ picker.as<IInitializeWithWindow>() };
        initializeWithWindow->Initialize(hwnd);

        picker.ViewMode(winrt::Windows::Storage::Pickers::PickerViewMode::List);
        picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::MusicLibrary);
        picker.FileTypeFilter().Append(L".mp3");
        picker.FileTypeFilter().Append(L".flac");

        winrt::Windows::Storage::StorageFile file = co_await picker.PickSingleFileAsync();
        if (file)
        {
            auto source = winrt::Windows::Media::Core::MediaSource::CreateFromStorageFile(file);
            GetMediaPlayer().Source(source);

            // Create a basic TrackInfo for the opened file (you could read real ID3 tags here)
            TrackInfo info;
            info.Title(file.DisplayName());
            info.FilePath(file.Path());
            info.File(file);
            LoadTrack(info);
        }
    }
    void AudioPlayer::Play()
    {
        if (m_state != PlaybackState::Playing)
        {
            m_state = PlaybackState::Playing;
            if (m_stateChangedCallback) m_stateChangedCallback(m_state);
        }
    }

    void AudioPlayer::Pause()
    {
        if (m_state == PlaybackState::Playing)
        {
            m_state = PlaybackState::Paused;
            if (m_stateChangedCallback) m_stateChangedCallback(m_state);
        }
    }

    void AudioPlayer::Stop()
    {
        m_state = PlaybackState::Stopped;
        m_currentPosition = 0.0;
        if (m_stateChangedCallback) m_stateChangedCallback(m_state);
    }

    void AudioPlayer::TogglePlayPause()
    {
        if (m_state == PlaybackState::Playing)
            Pause();
        else
            Play();
    }

    void AudioPlayer::Seek(double position)
    {
        if (!m_currentTrack)
        {
            m_currentPosition = 0.0;
            return;
        }

        m_currentPosition = position * m_currentTrack.DurationSeconds();
        if (m_positionChangedCallback) m_positionChangedCallback(m_currentPosition);
    }

    void AudioPlayer::SetVolume(double volume)
    {
        m_volume = (volume < 0.0) ? 0.0 : (volume > 1.0) ? 1.0 : volume;
    }

    double AudioPlayer::GetVolume() const
    {
        return m_volume;
    }

    void AudioPlayer::LoadTrack(const TrackInfo& track)
    {
        Stop();
        m_currentTrack = track;
    }

    void AudioPlayer::ClearTrack()
    {
        try
        {
            GetMediaPlayer().Pause();
            GetMediaPlayer().Source(nullptr);
        }
        catch (...)
        {
        }

        Stop();
        m_currentTrack = nullptr;
    }

    void AudioPlayer::LoadFromUrl(const std::wstring& streamUrl)
    {
        auto uri = winrt::Windows::Foundation::Uri(winrt::hstring(streamUrl));
        auto source = winrt::Windows::Media::Core::MediaSource::CreateFromUri(uri);
        GetMediaPlayer().Source(source);
    }

    PlaybackState AudioPlayer::GetState() const
    {
        return m_state;
    }

    double AudioPlayer::GetCurrentPosition() const
    {
        return m_currentPosition;
    }

    double AudioPlayer::GetDuration() const
    {
        if (!m_currentTrack)
        {
            return 0.0;
        }

        return m_currentTrack.DurationSeconds();
    }

    const TrackInfo& AudioPlayer::GetCurrentTrack() const
    {
        return m_currentTrack;
    }

    void AudioPlayer::OnStateChanged(StateChangedCallback callback)
    {
        m_stateChangedCallback = std::move(callback);
    }

    void AudioPlayer::OnPositionChanged(PositionChangedCallback callback)
    {
        m_positionChangedCallback = std::move(callback);
    }

    winrt::Windows::Media::Playback::MediaPlaybackList AudioPlayer::GetPlaybackList()
    {
        if (!m_playbackList)
        {
            m_playbackList = winrt::Windows::Media::Playback::MediaPlaybackList();
            // The app drives shuffle/repeat in MainWindow code; keep the
            // native list passive so item transitions stay 1→2→3.
            m_playbackList.AutoRepeatEnabled(false);
            m_playbackList.ShuffleEnabled(false);
        }
        return m_playbackList;
    }

    void AudioPlayer::EnableGapless(bool on)
    {
        // Gapless via MediaPlaybackList introduced reliability issues across
        // rescans and source transitions; the toggle now just records the
        // preference. A future revisit can re-route playback through the
        // list once the state-management bugs are properly understood.
        m_gaplessEnabled = on;
    }

    void AudioPlayer::PlayGaplessCurrent(winrt::Windows::Media::Core::MediaSource const& source)
    {
        // Direct-source playback path. No MediaPlaybackList wrapping. The
        // function name is preserved so callers stay stable when gapless is
        // re-enabled in the future.
        auto mp = GetMediaPlayer();
        try { mp.Pause(); } catch (...) {}
        if (source)
        {
            mp.Source(source);
            mp.Play();
        }
        else
        {
            try { mp.Source(nullptr); } catch (...) {}
        }
    }

    void AudioPlayer::SetGaplessNext(winrt::Windows::Media::Core::MediaSource const&)
    {
        // No-op until we re-introduce the lookahead via MediaPlaybackList.
    }

    void AudioPlayer::EnsureEqualizerPropertySet()
    {
        if (m_equalizerProps) return;
        m_equalizerProps = winrt::Windows::Foundation::Collections::PropertySet();
        for (int i = 0; i < 10; ++i)
        {
            wchar_t key[16];
            std::swprintf(key, 16, L"Band%d", i);
            m_equalizerProps.Insert(winrt::hstring{ key }, winrt::box_value(0.0));
        }
        m_equalizerProps.Insert(L"Preamp", winrt::box_value(0.0));
        m_equalizerProps.Insert(L"Version", winrt::box_value<int64_t>(0));
    }

    void AudioPlayer::BumpEqualizerVersion()
    {
        ++m_eqVersion;
        if (m_equalizerProps)
        {
            // Audio-thread ProcessFrame polls this single key to decide
            // whether to re-read all 10 bands + preamp and recompute biquad
            // coefficients. Skipping refresh when unchanged drops the hot
            // path from ~30µs/frame to ~3µs/frame.
            m_equalizerProps.Insert(L"Version", winrt::box_value<int64_t>(m_eqVersion));
        }
    }

    void AudioPlayer::EnsureEqualizerAttached()
    {
        if (m_equalizerAttached) return;
        EnsureEqualizerPropertySet();
        try
        {
            // effectOptional=true: if RoActivateInstance can't find the class
            // (e.g. running unpackaged without reg-free WinRT plumbing),
            // MediaPlayer logs and skips it instead of throwing.
            GetMediaPlayer().AddAudioEffect(
                L"Last_Music_Player.EqualizerEffect",
                true,
                m_equalizerProps);
            m_equalizerAttached = true;
        }
        catch (...)
        {
            // Effect attach failed — leave m_equalizerAttached false so the
            // next slider change won't try to mutate an unattached effect's
            // property set. UI sliders still update m_equalizerProps via
            // UpdateEqualizerBand below, so a later re-attach picks up the
            // user's current values.
        }
    }

    void AudioPlayer::UpdateEqualizerBand(int bandIndex, double gainDb)
    {
        if (bandIndex < 0 || bandIndex >= 10) return;
        EnsureEqualizerPropertySet();
        wchar_t key[16];
        std::swprintf(key, 16, L"Band%d", bandIndex);
        m_equalizerProps.Insert(winrt::hstring{ key }, winrt::box_value(gainDb));
        BumpEqualizerVersion();
        if (!m_equalizerAttached && std::abs(gainDb) > 0.01)
        {
            EnsureEqualizerAttached();
        }
    }

    void AudioPlayer::ApplyEqualizerBands(double const (&gainsDb)[10])
    {
        for (int i = 0; i < 10; ++i)
        {
            UpdateEqualizerBand(i, gainsDb[i]);
        }
    }

    void AudioPlayer::SetEqualizerPreamp(double preampDb)
    {
        EnsureEqualizerPropertySet();
        m_equalizerProps.Insert(L"Preamp", winrt::box_value(preampDb));
        BumpEqualizerVersion();
        if (!m_equalizerAttached && std::abs(preampDb) > 0.01)
        {
            EnsureEqualizerAttached();
        }
    }
}
