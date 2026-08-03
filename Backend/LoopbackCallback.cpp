#include "pch.h"
#include "Backend/LoopbackCallback.h"

#include "Backend/BuildConfig.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace LastMusicPlayer::Backend
{
    namespace
    {
        constexpr std::uintptr_t InvalidSocketValue = static_cast<std::uintptr_t>(INVALID_SOCKET);
        constexpr std::size_t MaxRequestBytes = 8192;
        constexpr std::size_t MaxInvalidRequests = 8;
        constexpr auto AcceptedReadTimeout = std::chrono::seconds(2);
        constexpr auto CancellationPollInterval = std::chrono::milliseconds(50);

        bool EnsureWinsock()
        {
            static std::once_flag flag;
            static bool initialized{};
            std::call_once(flag, []
            {
                WSADATA data{};
                initialized = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
            });
            return initialized;
        }

        std::string LowerAscii(std::string_view value)
        {
            std::string lowered(value);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return lowered;
        }

        std::string TrimAscii(std::string_view value)
        {
            auto first = value.find_first_not_of(" \t");
            if (first == std::string_view::npos)
            {
                return {};
            }
            auto last = value.find_last_not_of(" \t");
            return std::string(value.substr(first, last - first + 1));
        }

        int HexValue(char character)
        {
            if (character >= '0' && character <= '9') return character - '0';
            if (character >= 'a' && character <= 'f') return character - 'a' + 10;
            if (character >= 'A' && character <= 'F') return character - 'A' + 10;
            return -1;
        }

        std::optional<std::string> PercentDecode(std::string_view value)
        {
            std::string decoded;
            decoded.reserve(value.size());
            for (std::size_t index{}; index < value.size(); ++index)
            {
                auto character = value[index];
                if (character == '%')
                {
                    if (index + 2 >= value.size()) return std::nullopt;
                    auto high = HexValue(value[index + 1]);
                    auto low = HexValue(value[index + 2]);
                    if (high < 0 || low < 0) return std::nullopt;
                    decoded.push_back(static_cast<char>((high << 4) | low));
                    index += 2;
                }
                else
                {
                    decoded.push_back(character == '+' ? ' ' : character);
                }
            }
            return decoded;
        }

        winrt::hstring AsciiToHstring(std::string_view value)
        {
            std::wstring wide;
            wide.reserve(value.size());
            for (unsigned char character : value)
            {
                if (character > 0x7f)
                {
                    return {};
                }
                wide.push_back(static_cast<wchar_t>(character));
            }
            return winrt::hstring{ wide };
        }

        void SendPage(SOCKET socket, bool success) noexcept
        {
            static constexpr char SuccessPage[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Cache-Control: no-store\r\n"
                "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\n"
                "Referrer-Policy: no-referrer\r\n"
                "X-Content-Type-Options: nosniff\r\n"
                "Connection: close\r\n\r\n"
                "<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign-in complete</title></head>"
                "<body><main><h1>Sign-in complete</h1><p>You can return to Last Music Player.</p></main></body></html>";
            static constexpr char FailurePage[] =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Cache-Control: no-store\r\n"
                "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\n"
                "Referrer-Policy: no-referrer\r\n"
                "X-Content-Type-Options: nosniff\r\n"
                "Connection: close\r\n\r\n"
                "<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign-in stopped</title></head>"
                "<body><main><h1>Sign-in stopped</h1><p>Return to Last Music Player and try again.</p></main></body></html>";
            auto page = success ? SuccessPage : FailurePage;
            auto length = success ? sizeof(SuccessPage) - 1 : sizeof(FailurePage) - 1;
            ::send(socket, page, static_cast<int>(length), 0);
        }

        SOCKET ToSocket(std::uintptr_t value)
        {
            return static_cast<SOCKET>(value);
        }

        void CloseNativeSocket(std::uintptr_t value) noexcept
        {
            if (value == InvalidSocketValue)
            {
                return;
            }

            auto socket = ToSocket(value);
            ::shutdown(socket, SD_BOTH);
            ::closesocket(socket);
        }

        std::string CallbackPathAscii()
        {
            std::string path;
            for (auto character : std::wstring_view{ BuildConfig::DesktopCallbackPath })
            {
                if (character > 0x7f)
                {
                    return {};
                }
                path.push_back(static_cast<char>(character));
            }
            return path;
        }
    }

    LoopbackCallbackResult ParseLoopbackHttpRequest(
        std::string_view request,
        std::string_view expectedAuthority,
        std::string_view expectedPath,
        std::string_view expectedState)
    {
        LoopbackCallbackResult invalid{ LoopbackCallbackStatus::InvalidRequest, {} };
        if (request.empty() || request.size() > MaxRequestBytes)
        {
            return invalid;
        }

        auto requestLineEnd = request.find("\r\n");
        if (requestLineEnd == std::string_view::npos)
        {
            return invalid;
        }
        auto requestLine = request.substr(0, requestLineEnd);
        auto firstSpace = requestLine.find(' ');
        auto secondSpace = firstSpace == std::string_view::npos
            ? std::string_view::npos
            : requestLine.find(' ', firstSpace + 1);
        if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos
            || requestLine.substr(0, firstSpace) != "GET")
        {
            return invalid;
        }
        auto target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        auto version = requestLine.substr(secondSpace + 1);
        if (version != "HTTP/1.1" && version != "HTTP/1.0")
        {
            return invalid;
        }

        std::string host;
        bool sawHost{};
        std::size_t lineStart = requestLineEnd + 2;
        while (lineStart < request.size())
        {
            auto lineEnd = request.find("\r\n", lineStart);
            if (lineEnd == std::string_view::npos)
            {
                return invalid;
            }
            if (lineEnd == lineStart)
            {
                break;
            }
            auto line = request.substr(lineStart, lineEnd - lineStart);
            auto colon = line.find(':');
            if (colon == std::string_view::npos)
            {
                return invalid;
            }
            auto name = LowerAscii(line.substr(0, colon));
            if (name == "host")
            {
                if (sawHost)
                {
                    return invalid;
                }
                sawHost = true;
                host = LowerAscii(TrimAscii(line.substr(colon + 1)));
            }
            lineStart = lineEnd + 2;
        }
        if (!sawHost || host != LowerAscii(expectedAuthority))
        {
            return invalid;
        }

        if (target.find('#') != std::string_view::npos)
        {
            return invalid;
        }
        auto queryStart = target.find('?');
        auto path = target.substr(0, queryStart);
        if (path != expectedPath)
        {
            return invalid;
        }

        std::map<std::string, std::string> values;
        if (queryStart != std::string_view::npos)
        {
            auto query = target.substr(queryStart + 1);
            std::size_t partStart{};
            while (partStart <= query.size())
            {
                auto partEnd = query.find('&', partStart);
                auto part = query.substr(
                    partStart,
                    partEnd == std::string_view::npos ? query.size() - partStart : partEnd - partStart);
                if (part.empty())
                {
                    return invalid;
                }
                auto equals = part.find('=');
                auto encodedName = part.substr(0, equals);
                auto encodedValue = equals == std::string_view::npos ? std::string_view{} : part.substr(equals + 1);
                auto name = PercentDecode(encodedName);
                auto value = PercentDecode(encodedValue);
                if (!name || !value || values.contains(*name))
                {
                    return invalid;
                }
                values.emplace(std::move(*name), std::move(*value));
                if (partEnd == std::string_view::npos)
                {
                    break;
                }
                partStart = partEnd + 1;
            }
        }

        auto state = values.find("state");
        if (state == values.end() || state->second != expectedState)
        {
            return invalid;
        }
        if (values.contains("error"))
        {
            return { LoopbackCallbackStatus::Canceled, {} };
        }
        auto code = values.find("code");
        if (code == values.end() || code->second.empty() || code->second.size() > 2048)
        {
            return invalid;
        }
        auto wideCode = AsciiToHstring(code->second);
        if (wideCode.empty())
        {
            return invalid;
        }
        return { LoopbackCallbackStatus::Success, wideCode };
    }

    LoopbackCallback::~LoopbackCallback()
    {
        Cancel();
    }

    bool LoopbackCallback::Start()
    {
        Cancel();
        m_canceled.store(false, std::memory_order_release);
        m_port = 0;
        m_ipv6 = false;
        if (!EnsureWinsock())
        {
            return false;
        }

        auto bindListener = [&](int family) -> SOCKET
        {
            auto listener = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
            if (listener == INVALID_SOCKET)
            {
                return INVALID_SOCKET;
            }
            BOOL exclusive = TRUE;
            if (::setsockopt(
                listener,
                SOL_SOCKET,
                SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<char const*>(&exclusive),
                sizeof(exclusive)) != 0)
            {
                ::closesocket(listener);
                return INVALID_SOCKET;
            }

            if (family == AF_INET)
            {
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                address.sin_port = 0;
                if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
                {
                    ::closesocket(listener);
                    return INVALID_SOCKET;
                }
            }
            else
            {
                DWORD onlyV6 = 1;
                ::setsockopt(
                    listener,
                    IPPROTO_IPV6,
                    IPV6_V6ONLY,
                    reinterpret_cast<char const*>(&onlyV6),
                    sizeof(onlyV6));
                sockaddr_in6 address{};
                address.sin6_family = AF_INET6;
                address.sin6_addr = in6addr_loopback;
                address.sin6_port = 0;
                if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
                {
                    ::closesocket(listener);
                    return INVALID_SOCKET;
                }
            }
            if (::listen(listener, 1) != 0)
            {
                ::closesocket(listener);
                return INVALID_SOCKET;
            }
            u_long nonBlocking = 1;
            if (::ioctlsocket(listener, FIONBIO, &nonBlocking) != 0)
            {
                ::closesocket(listener);
                return INVALID_SOCKET;
            }
            return listener;
        };

        auto listener = bindListener(AF_INET);
        if (listener == INVALID_SOCKET)
        {
            listener = bindListener(AF_INET6);
            m_ipv6 = listener != INVALID_SOCKET;
        }
        if (listener == INVALID_SOCKET)
        {
            return false;
        }

        if (m_ipv6)
        {
            sockaddr_in6 address{};
            int length = sizeof(address);
            if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
            {
                ::closesocket(listener);
                return false;
            }
            m_port = ntohs(address.sin6_port);
        }
        else
        {
            sockaddr_in address{};
            int length = sizeof(address);
            if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
            {
                ::closesocket(listener);
                return false;
            }
            m_port = ntohs(address.sin_port);
        }

        {
            std::lock_guard guard{ m_socketMutex };
            m_socket = static_cast<std::uintptr_t>(listener);
        }
        return m_port != 0;
    }

    void LoopbackCallback::CloseListener() noexcept
    {
        std::uintptr_t listener{ InvalidSocketValue };
        {
            std::lock_guard guard{ m_socketMutex };
            listener = std::exchange(m_socket, InvalidSocketValue);
        }
        CloseNativeSocket(listener);
    }

    void LoopbackCallback::CloseAcceptedSocket(
        std::uintptr_t expected) noexcept
    {
        bool ownsSocket{};
        {
            std::lock_guard guard{ m_socketMutex };
            if (m_acceptedSocket == expected)
            {
                m_acceptedSocket = InvalidSocketValue;
                ownsSocket = true;
            }
        }
        if (ownsSocket)
        {
            CloseNativeSocket(expected);
        }
    }

    void LoopbackCallback::Cancel() noexcept
    {
        m_canceled.store(true, std::memory_order_release);

        std::uintptr_t listener{ InvalidSocketValue };
        {
            std::lock_guard guard{ m_socketMutex };
            listener = std::exchange(m_socket, InvalidSocketValue);
        }
        CloseNativeSocket(listener);
    }

#ifdef LAST_MUSIC_NATIVE_ACCOUNT_TESTS
    bool LoopbackCallback::HasAcceptedClientForTesting() noexcept
    {
        std::lock_guard guard{ m_socketMutex };
        return m_acceptedSocket != InvalidSocketValue;
    }
#endif

    winrt::hstring LoopbackCallback::RedirectUri() const
    {
        if (m_port == 0)
        {
            return {};
        }
        std::wstring uri = m_ipv6 ? L"http://[::1]:" : L"http://127.0.0.1:";
        uri += std::to_wstring(m_port);
        uri += BuildConfig::DesktopCallbackPath;
        return winrt::hstring{ uri };
    }

    LoopbackCallbackResult LoopbackCallback::WaitForCallback(
        winrt::hstring const& expectedState,
        std::chrono::steady_clock::time_point deadline)
    {
        std::uintptr_t listenerValue{ InvalidSocketValue };
        {
            std::lock_guard guard{ m_socketMutex };
            listenerValue = m_socket;
        }
        if (listenerValue == InvalidSocketValue || m_port == 0)
        {
            return {
                m_canceled.load(std::memory_order_acquire)
                    ? LoopbackCallbackStatus::Canceled
                    : LoopbackCallbackStatus::TransportError,
                {}
            };
        }
        auto listener = ToSocket(listenerValue);

        auto callbackPath = CallbackPathAscii();
        std::string state;
        state.reserve(expectedState.size());
        for (auto character : expectedState)
        {
            if (character > 0x7f)
            {
                CloseListener();
                return { LoopbackCallbackStatus::InvalidRequest, {} };
            }
            state.push_back(static_cast<char>(character));
        }
        if (callbackPath.empty() || state.empty())
        {
            CloseListener();
            return { LoopbackCallbackStatus::InvalidRequest, {} };
        }

        std::size_t invalidRequests{};
        for (;;)
        {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                CloseListener();
                return { LoopbackCallbackStatus::TimedOut, {} };
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now);
            timeval timeout{};
            timeout.tv_sec = static_cast<long>(remaining.count() / 1000);
            timeout.tv_usec = static_cast<long>((remaining.count() % 1000) * 1000);
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listener, &readSet);
            auto selected = ::select(0, &readSet, nullptr, nullptr, &timeout);
            if (selected == 0)
            {
                auto canceled = m_canceled.load(std::memory_order_acquire);
                CloseListener();
                return {
                    canceled
                        ? LoopbackCallbackStatus::Canceled
                        : LoopbackCallbackStatus::TimedOut,
                    {}
                };
            }
            if (selected < 0)
            {
                auto canceled = m_canceled.load(std::memory_order_acquire);
                CloseListener();
                return {
                    canceled
                        ? LoopbackCallbackStatus::Canceled
                        : LoopbackCallbackStatus::TransportError,
                    {}
                };
            }

            SOCKET accepted{ INVALID_SOCKET };
            int acceptError{};
            {
                std::lock_guard guard{ m_socketMutex };
                if (m_canceled.load(std::memory_order_acquire)
                    || m_socket != listenerValue)
                {
                    return { LoopbackCallbackStatus::Canceled, {} };
                }

                accepted = ::accept(listener, nullptr, nullptr);
                if (accepted == INVALID_SOCKET)
                {
                    acceptError = ::WSAGetLastError();
                }
                else
                {
                    u_long nonBlocking = 1;
                    if (::ioctlsocket(accepted, FIONBIO, &nonBlocking) != 0)
                    {
                        acceptError = ::WSAGetLastError();
                    }
                    else
                    {
                        m_acceptedSocket = static_cast<std::uintptr_t>(accepted);
                    }
                }
            }

            if (accepted == INVALID_SOCKET)
            {
                if (acceptError == WSAEWOULDBLOCK)
                {
                    continue;
                }
                auto canceled = m_canceled.load(std::memory_order_acquire);
                CloseListener();
                return {
                    canceled
                        ? LoopbackCallbackStatus::Canceled
                        : LoopbackCallbackStatus::TransportError,
                    {}
                };
            }

            auto acceptedValue = static_cast<std::uintptr_t>(accepted);
            if (acceptError != 0)
            {
                CloseNativeSocket(acceptedValue);
                CloseListener();
                return { LoopbackCallbackStatus::TransportError, {} };
            }

            now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                CloseAcceptedSocket(acceptedValue);
                CloseListener();
                return { LoopbackCallbackStatus::TimedOut, {} };
            }
            auto acceptedDeadline = (std::min)(
                deadline,
                now + AcceptedReadTimeout);

            bool overallDeadlineReached{};
            std::string request;
            request.reserve(2048);
            std::array<char, 1024> buffer{};
            while (request.size() < MaxRequestBytes
                && request.find("\r\n\r\n") == std::string::npos)
            {
                if (m_canceled.load(std::memory_order_acquire))
                {
                    break;
                }

                now = std::chrono::steady_clock::now();
                if (now >= acceptedDeadline)
                {
                    overallDeadlineReached = acceptedDeadline == deadline;
                    break;
                }

                auto waitBudget = (std::min)(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        acceptedDeadline - now),
                    CancellationPollInterval);
                auto waitMilliseconds = (std::max<std::int64_t>)(
                    1,
                    waitBudget.count());
                timeval readTimeout{};
                readTimeout.tv_sec = static_cast<long>(waitMilliseconds / 1000);
                readTimeout.tv_usec = static_cast<long>(
                    (waitMilliseconds % 1000) * 1000);
                fd_set acceptedReadSet;
                FD_ZERO(&acceptedReadSet);
                FD_SET(accepted, &acceptedReadSet);
                auto readable = ::select(
                    0,
                    &acceptedReadSet,
                    nullptr,
                    nullptr,
                    &readTimeout);
                if (readable < 0)
                {
                    break;
                }
                if (readable == 0)
                {
                    continue;
                }

                auto available = (std::min)(
                    buffer.size(),
                    MaxRequestBytes - request.size());
                auto received = ::recv(
                    accepted,
                    buffer.data(),
                    static_cast<int>(available),
                    0);
                if (received > 0)
                {
                    request.append(
                        buffer.data(),
                        static_cast<std::size_t>(received));
                    continue;
                }
                if (received < 0 && ::WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    continue;
                }
                break;
            }

            if (m_canceled.load(std::memory_order_acquire))
            {
                CloseAcceptedSocket(acceptedValue);
                CloseListener();
                return { LoopbackCallbackStatus::Canceled, {} };
            }
            if (overallDeadlineReached)
            {
                CloseAcceptedSocket(acceptedValue);
                CloseListener();
                return { LoopbackCallbackStatus::TimedOut, {} };
            }

            std::string authority = m_ipv6 ? "[::1]:" : "127.0.0.1:";
            authority += std::to_string(m_port);
            auto result = ParseLoopbackHttpRequest(
                request,
                authority,
                callbackPath,
                state);
            if (m_canceled.load(std::memory_order_acquire))
            {
                CloseAcceptedSocket(acceptedValue);
                CloseListener();
                return { LoopbackCallbackStatus::Canceled, {} };
            }
            SendPage(
                accepted,
                result.Status == LoopbackCallbackStatus::Success);
            auto canceled = m_canceled.load(std::memory_order_acquire);
            CloseAcceptedSocket(acceptedValue);
            if (canceled)
            {
                CloseListener();
                return { LoopbackCallbackStatus::Canceled, {} };
            }

            if (result.Status == LoopbackCallbackStatus::Success
                || result.Status == LoopbackCallbackStatus::Canceled)
            {
                CloseListener();
                return result;
            }

            ++invalidRequests;
            if (invalidRequests >= MaxInvalidRequests)
            {
                CloseListener();
                return { LoopbackCallbackStatus::InvalidRequest, {} };
            }
        }
    }
}
