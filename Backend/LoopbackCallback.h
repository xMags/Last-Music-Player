#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    enum class LoopbackCallbackStatus
    {
        Success,
        Canceled,
        InvalidRequest,
        TimedOut,
        TransportError,
    };

    struct LoopbackCallbackResult
    {
        LoopbackCallbackStatus Status{ LoopbackCallbackStatus::TransportError };
        winrt::hstring Code;
    };

    LoopbackCallbackResult ParseLoopbackHttpRequest(
        std::string_view request,
        std::string_view expectedAuthority,
        std::string_view expectedPath,
        std::string_view expectedState);

    class LoopbackCallback final
    {
    public:
        LoopbackCallback() = default;
        ~LoopbackCallback();
        LoopbackCallback(LoopbackCallback const&) = delete;
        LoopbackCallback& operator=(LoopbackCallback const&) = delete;

        bool Start();
        void Cancel() noexcept;
        winrt::hstring RedirectUri() const;
        LoopbackCallbackResult WaitForCallback(
            winrt::hstring const& expectedState,
            std::chrono::steady_clock::time_point deadline);

    private:
        std::atomic<std::uintptr_t> m_socket{ static_cast<std::uintptr_t>(-1) };
        std::uint16_t m_port{};
        bool m_ipv6{};
    };
}
