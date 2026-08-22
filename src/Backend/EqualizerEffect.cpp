#include "pch.h"
#include "EqualizerEffect.h"
#include "EqualizerEffect.g.cpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <Unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.MediaProperties.h>

// The COM byte-access interface that AudioFrame buffer references implement.
// Declared inline here to avoid pulling in the entire ABI header set.
struct __declspec(uuid("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")) IMemoryBufferByteAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetBuffer(uint8_t** value, uint32_t* capacity) = 0;
};

namespace winrt::Last_Music_Player::implementation
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        // Robert Bristow-Johnson peaking-EQ cookbook coefficients, normalized
        // by a0. Gain is in dB. Frequencies above Nyquist produce a passthrough
        // (zero-impact) biquad so we never alias or blow up on low-rate streams.
        EqualizerEffect::BiquadCoefficients PeakingCoefficients(double gainDb, double centerHz, double sampleRate, double q)
        {
            EqualizerEffect::BiquadCoefficients out;
            if (sampleRate <= 0.0 || centerHz <= 0.0 || centerHz >= sampleRate * 0.5)
            {
                return out; // passthrough (b0=1, b1=b2=a1=a2=0)
            }
            double A = std::pow(10.0, gainDb / 40.0);
            double w0 = 2.0 * kPi * centerHz / sampleRate;
            double cosw0 = std::cos(w0);
            double alpha = std::sin(w0) / (2.0 * q);

            double b0 =  1.0 + alpha * A;
            double b1 = -2.0 * cosw0;
            double b2 =  1.0 - alpha * A;
            double a0 =  1.0 + alpha / A;
            double a1 = -2.0 * cosw0;
            double a2 =  1.0 - alpha / A;

            out.b0 = b0 / a0;
            out.b1 = b1 / a0;
            out.b2 = b2 / a0;
            out.a1 = a1 / a0;
            out.a2 = a2 / a0;
            return out;
        }

        // Locks an AudioFrame's underlying buffer and yields a writable
        // float pointer + element count. AudioFrame data is interleaved PCM.
        struct AudioFrameLock
        {
            winrt::Windows::Media::AudioBuffer buffer{ nullptr };
            winrt::Windows::Foundation::IMemoryBufferReference reference{ nullptr };
            float* data{ nullptr };
            uint32_t lengthBytes{ 0 };

            ~AudioFrameLock()
            {
                if (reference) reference.Close();
                if (buffer) buffer.Close();
            }
        };

        bool LockFrame(winrt::Windows::Media::AudioFrame const& frame, AudioFrameLock& out)
        {
            if (!frame) return false;
            out.buffer = frame.LockBuffer(winrt::Windows::Media::AudioBufferAccessMode::ReadWrite);
            if (!out.buffer) return false;
            out.reference = out.buffer.CreateReference();
            if (!out.reference) return false;
            uint8_t* raw{};
            auto byteAccess = out.reference.as<IMemoryBufferByteAccess>();
            byteAccess->GetBuffer(&raw, &out.lengthBytes);
            out.data = reinterpret_cast<float*>(raw);
            return out.data != nullptr;
        }
    }

    winrt::Windows::Foundation::Collections::IVectorView<
        winrt::Windows::Media::MediaProperties::AudioEncodingProperties>
    EqualizerEffect::SupportedEncodingProperties()
    {
        auto vec = winrt::single_threaded_vector<
            winrt::Windows::Media::MediaProperties::AudioEncodingProperties>();
        // Float32 PCM at all common decoded rates. Narrow lists used to
        // cause silence for sources MediaPlayer transcoded to a rate we
        // hadn't advertised — listing the full range lets the framework
        // negotiate any standard source rate down to our cascade.
        const uint32_t rates[] = { 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
        const uint32_t channelCounts[] = { 1, 2 };
        for (auto rate : rates)
        {
            for (auto channels : channelCounts)
            {
                auto props = winrt::Windows::Media::MediaProperties::AudioEncodingProperties::CreatePcm(rate, channels, 32);
                props.Subtype(L"Float");
                vec.Append(props);
            }
        }
        return vec.GetView();
    }

    void EqualizerEffect::SetProperties(
        winrt::Windows::Foundation::Collections::IPropertySet const& configuration)
    {
        std::scoped_lock lock{ m_mutex };
        m_props = configuration;
        RefreshGainsFromProps();
    }

    void EqualizerEffect::RefreshGainsFromProps()
    {
        m_bandGainsDb.fill(0.0);
        m_preampDb = 0.0;
        if (m_props)
        {
            for (size_t i = 0; i < kNumBands; ++i)
            {
                wchar_t key[16];
                std::swprintf(key, 16, L"Band%zu", i);
                auto k = winrt::hstring{ key };
                try
                {
                    if (m_props.HasKey(k))
                    {
                        auto value = m_props.Lookup(k);
                        if (auto ref = value.try_as<winrt::Windows::Foundation::IReference<double>>())
                        {
                            m_bandGainsDb[i] = ref.Value();
                        }
                    }
                }
                catch (...) {}
            }
            try
            {
                if (m_props.HasKey(L"Preamp"))
                {
                    auto value = m_props.Lookup(L"Preamp");
                    if (auto ref = value.try_as<winrt::Windows::Foundation::IReference<double>>())
                    {
                        m_preampDb = ref.Value();
                    }
                }
            }
            catch (...) {}
        }
        m_preampLinear = (std::abs(m_preampDb) < 0.0001)
            ? 1.0
            : std::pow(10.0, m_preampDb / 20.0);
        m_passthrough = std::all_of(m_bandGainsDb.begin(), m_bandGainsDb.end(),
                                    [](double g) { return std::abs(g) < 0.01; });
        RecomputeCoefficients();
    }

    int64_t EqualizerEffect::ReadVersionFromProps() const
    {
        if (!m_props) return 0;
        try
        {
            if (m_props.HasKey(L"Version"))
            {
                auto value = m_props.Lookup(L"Version");
                if (auto ref = value.try_as<winrt::Windows::Foundation::IReference<int64_t>>())
                {
                    return ref.Value();
                }
            }
        }
        catch (...) {}
        return 0;
    }

    void EqualizerEffect::SetEncodingProperties(
        winrt::Windows::Media::MediaProperties::AudioEncodingProperties const& encodingProperties)
    {
        std::scoped_lock lock{ m_mutex };
        m_sampleRate = encodingProperties.SampleRate();
        m_channels = encodingProperties.ChannelCount();
        // Reset filter state on format change so we don't get a click from
        // stale memory at a new rate.
        m_states.assign(static_cast<size_t>(m_channels) * kNumBands, BiquadState{});
        RecomputeCoefficients();
    }

    void EqualizerEffect::ProcessFrame(
        winrt::Windows::Media::Effects::ProcessAudioFrameContext const& context)
    {
        // ProcessFrame must never throw across the WinRT boundary — an
        // unhandled exception tears down the MediaPlayer effect chain
        // mid-stream and yields permanent silence until track change.
        try
        {
            if (!context) return;
            auto frame = context.InputFrame();
            if (!frame) return;

            AudioFrameLock lock;
            if (!LockFrame(frame, lock)) return;

            if (lock.lengthBytes < sizeof(float)) return;
            size_t totalSamples = lock.lengthBytes / sizeof(float);
            if (m_channels == 0) return;
            size_t frameCount = totalSamples / m_channels;
            if (frameCount == 0) return;

            std::scoped_lock guard{ m_mutex };
            // Cheap one-key polling check: re-read all 11 props + recompute
            // biquad coefficients ONLY when the producer has bumped the
            // version. Otherwise the cached coefficients and m_preampLinear
            // are still current and we go straight to filtering.
            int64_t version = ReadVersionFromProps();
            if (version != m_lastVersionSeen)
            {
                RefreshGainsFromProps();
                m_lastVersionSeen = version;
            }
            if (m_passthrough && std::abs(m_preampLinear - 1.0) < 0.0001) return;

            // Defensive: if anything ever leaves m_states the wrong size
            // (e.g. a channel-count change MediaPlayer didn't report to us
            // via SetEncodingProperties), re-allocate so the index math
            // below stays in bounds. Reset state too, to avoid carrying
            // stale history into the new layout.
            size_t expectedStateCount = static_cast<size_t>(m_channels) * kNumBands;
            if (m_states.size() != expectedStateCount)
            {
                m_states.assign(expectedStateCount, BiquadState{});
            }

            double preamp = m_preampLinear;

            for (size_t ch = 0; ch < m_channels; ++ch)
            {
                for (size_t i = 0; i < frameCount; ++i)
                {
                    float* sample = &lock.data[i * m_channels + ch];
                    double inputF = static_cast<double>(*sample);
                    // Sanitize input. Decoded PCM occasionally produces NaN
                    // or Inf (lossy decoder edge cases, sub-normal flush
                    // anomalies); without this, a single bad sample would
                    // poison the biquad state forever — clamp + zero
                    // comparisons silently fail for NaN.
                    if (!std::isfinite(inputF)) inputF = 0.0;
                    double x = inputF * preamp;
                    // Cascade all 10 biquads on this channel.
                    for (size_t band = 0; band < kNumBands; ++band)
                    {
                        auto& c = m_coefficients[band];
                        auto& s = m_states[ch * kNumBands + band];
                        double y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
                        if (!std::isfinite(y))
                        {
                            // The biquad's history corrupted — most likely
                            // a transient NaN/Inf in the input fed through
                            // a prior cascade band. Reset this band's
                            // state and pass the (sanitized) input through
                            // so audio self-heals on the next sample
                            // instead of going silent until track change.
                            s = {};
                            y = x;
                        }
                        s.x2 = s.x1;
                        s.x1 = x;
                        s.y2 = s.y1;
                        s.y1 = y;
                        x = y;
                    }
                    // Hard safety clamp at full scale. The preamp slider
                    // gives the user explicit headroom for boost; anything
                    // still clipping here is user-driven, and we match the
                    // downstream mixer's behavior (it would clip too).
                    if (x > 1.0) x = 1.0;
                    else if (x < -1.0) x = -1.0;
                    *sample = static_cast<float>(x);
                }
            }
        }
        catch (...)
        {
            // Swallow — the next frame will retry. Letting an exception
            // escape would silence the chain for the whole session.
        }
    }

    void EqualizerEffect::Close(winrt::Windows::Media::Effects::MediaEffectClosedReason const&)
    {
        std::scoped_lock lock{ m_mutex };
        m_states.clear();
    }

    void EqualizerEffect::DiscardQueuedFrames()
    {
        std::scoped_lock lock{ m_mutex };
        for (auto& s : m_states) s = {};
    }

    void EqualizerEffect::RecomputeCoefficients()
    {
        for (size_t i = 0; i < kNumBands; ++i)
        {
            m_coefficients[i] = PeakingCoefficients(
                m_bandGainsDb[i], kBandFrequenciesHz[i],
                m_sampleRate > 0 ? static_cast<double>(m_sampleRate) : 48000.0,
                kBandQ);
        }
    }
}
