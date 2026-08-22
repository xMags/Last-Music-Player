#pragma once
#include "EqualizerEffect.g.h"

#include <array>
#include <mutex>
#include <vector>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Effects.h>
#include <winrt/Windows.Media.MediaProperties.h>

namespace winrt::Last_Music_Player::implementation
{
    struct EqualizerEffect : EqualizerEffectT<EqualizerEffect>
    {
        EqualizerEffect() = default;

        struct BiquadCoefficients
        {
            double b0{ 1.0 }, b1{ 0.0 }, b2{ 0.0 };
            double a1{ 0.0 }, a2{ 0.0 };
        };

        // IBasicAudioEffect
        bool UseInputFrameForOutput() noexcept { return true; }
        bool TimeIndependent() noexcept { return true; }

        winrt::Windows::Foundation::Collections::IVectorView<
            winrt::Windows::Media::MediaProperties::AudioEncodingProperties>
        SupportedEncodingProperties();

        void SetProperties(
            winrt::Windows::Foundation::Collections::IPropertySet const& configuration);
        void SetEncodingProperties(
            winrt::Windows::Media::MediaProperties::AudioEncodingProperties const& encodingProperties);
        void ProcessFrame(
            winrt::Windows::Media::Effects::ProcessAudioFrameContext const& context);
        void Close(winrt::Windows::Media::Effects::MediaEffectClosedReason const& reason);
        void DiscardQueuedFrames();

    private:
        static constexpr size_t kNumBands = 10;
        static constexpr double kBandFrequenciesHz[kNumBands] = {
            32.0, 64.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
        };
        // Q≈1.41 yields ~1-octave bandwidth on a peaking EQ; matches the
        // standard "graphic equalizer" feel without ringing at the band edges.
        static constexpr double kBandQ = 1.41;

        struct BiquadState
        {
            // Direct Form I state per channel: x[n-1], x[n-2], y[n-1], y[n-2].
            double x1{ 0.0 }, x2{ 0.0 }, y1{ 0.0 }, y2{ 0.0 };
        };

        void RecomputeCoefficients();
        void RefreshGainsFromProps();
        int64_t ReadVersionFromProps() const;

        std::mutex m_mutex;
        // Held reference so ProcessFrame can pick up live slider changes
        // without the host re-issuing SetProperties on every tweak.
        winrt::Windows::Foundation::Collections::IPropertySet m_props{ nullptr };
        std::array<double, kNumBands> m_bandGainsDb{};
        std::array<BiquadCoefficients, kNumBands> m_coefficients{};
        // m_states[channel * kNumBands + band]
        std::vector<BiquadState> m_states;
        uint32_t m_sampleRate{ 0 };
        uint32_t m_channels{ 0 };
        bool m_passthrough{ true };

        // Master preamp applied to each input sample before the biquad
        // cascade. Gives the user explicit headroom for boosted bands.
        double m_preampDb{ 0.0 };
        double m_preampLinear{ 1.0 };

        // Versioning so ProcessFrame can skip the expensive PropertySet
        // re-read + coefficient recompute when nothing has changed. The
        // sentinel -1 forces a refresh on the first frame regardless of
        // what the producer started at.
        int64_t m_lastVersionSeen{ -1 };
    };
}

namespace winrt::Last_Music_Player::factory_implementation
{
    struct EqualizerEffect : EqualizerEffectT<EqualizerEffect, implementation::EqualizerEffect>
    {
    };
}
