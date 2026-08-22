#pragma once
#include "TrackInfo.h"
#include <functional>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    enum class PlaybackState
    {
        Stopped,
        Playing,
        Paused
    };

    class AudioPlayer
    {
    public:
        AudioPlayer() = default;
        ~AudioPlayer() = default;

        // Playback controls
        void Play();
        void Pause();
        void Stop();
        void TogglePlayPause();

        // Seek to position (0.0 to 1.0)
        void Seek(double position);

        // Volume (0.0 to 1.0)
        void SetVolume(double volume);
        double GetVolume() const;

        // Load a track
        void LoadTrack(const TrackInfo& track);
        void ClearTrack();
        void LoadFromUrl(const std::wstring& streamUrl);

        // File picker
        winrt::Windows::Foundation::IAsyncAction OpenFile(HWND hwnd);

        // Access native player. The MediaPlayer must NOT be activated during
        // static initialization (s_audioPlayer is a namespace-scope static):
        // that runs before init_apartment() and yields a broken ABI proxy that
        // access-violates on first use. Create it lazily on first access, which
        // always happens on the UI thread after the apartment is up.
        winrt::Windows::Media::Playback::MediaPlayer GetMediaPlayer()
        {
            if (!m_mediaPlayer)
            {
                m_mediaPlayer = winrt::Windows::Media::Playback::MediaPlayer();
            }
            return m_mediaPlayer;
        }

        // Gapless playback. When enabled, callers feed tracks via
        // PlayGaplessCurrent / SetGaplessNext (instead of MediaPlayer.Source)
        // and listen on GetPlaybackList().CurrentItemChanged to know when the
        // pre-loaded next track has taken over. When disabled the list is
        // cleared and the MediaPlayer reverts to single-source mode.
        void EnableGapless(bool on);
        bool IsGaplessEnabled() const { return m_gaplessEnabled; }

        // The shared MediaPlaybackList. Created lazily on first access; safe
        // to call before EnableGapless. Callers wire CurrentItemChanged once.
        winrt::Windows::Media::Playback::MediaPlaybackList GetPlaybackList();

        // Replace the playback list with the given source as the current item
        // (Items[0]) and start playback. Clears any pre-loaded next item.
        void PlayGaplessCurrent(winrt::Windows::Media::Core::MediaSource const& source);

        // Append (or replace) the next-track lookahead at Items[1]. Passing a
        // null source clears any existing lookahead.
        void SetGaplessNext(winrt::Windows::Media::Core::MediaSource const& source);

        // Attach the 10-band peaking EQ to the MediaPlayer if it isn't already.
        // Subsequent UpdateEqualizerBand / SetEqualizerPreamp calls mutate the
        // shared property set; the effect picks the new values up only when
        // the version counter changes (see BumpEqualizerVersion), keeping the
        // audio-thread hot path one PropertySet lookup per frame instead of
        // refreshing all eleven keys + recomputing biquad coefficients.
        void EnsureEqualizerAttached();
        void UpdateEqualizerBand(int bandIndex, double gainDb);
        // Push all 10 bands at once (used during settings load / reset).
        void ApplyEqualizerBands(double const (&gainsDb)[10]);
        // Master preamp in dB applied before the biquad cascade. Negative
        // values give headroom for boosted bands; positive values raise the
        // signal but risk hitting the safety clamp.
        void SetEqualizerPreamp(double preampDb);

        // State
        PlaybackState GetState() const;
        double GetCurrentPosition() const;   // in seconds
        double GetDuration() const;           // in seconds
        const TrackInfo& GetCurrentTrack() const;

        // Callbacks
        using StateChangedCallback = std::function<void(PlaybackState)>;
        using PositionChangedCallback = std::function<void(double)>;

        void OnStateChanged(StateChangedCallback callback);
        void OnPositionChanged(PositionChangedCallback callback);

    private:
        void EnsureEqualizerPropertySet();
        void BumpEqualizerVersion();

        winrt::Windows::Media::Playback::MediaPlayer m_mediaPlayer{ nullptr };
        winrt::Windows::Media::Playback::MediaPlaybackList m_playbackList{ nullptr };
        winrt::Windows::Foundation::Collections::PropertySet m_equalizerProps{ nullptr };
        bool m_equalizerAttached{ false };
        bool m_gaplessEnabled{ false };
        // Monotonic counter mirrored into m_equalizerProps[L"Version"].
        // Audio-thread ProcessFrame compares this to its last-seen value
        // and only refreshes coefficients when it has changed.
        int64_t m_eqVersion{ 0 };
        TrackInfo m_currentTrack{ nullptr };
        PlaybackState m_state{ PlaybackState::Stopped };
        double m_volume{ 0.7 };
        double m_currentPosition{ 0.0 };

        StateChangedCallback m_stateChangedCallback;
        PositionChangedCallback m_positionChangedCallback;
    };
}
