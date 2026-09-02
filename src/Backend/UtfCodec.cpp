#include "pch.h"
#include "Backend/UtfCodec.h"

#include <windows.h>

#include <limits>

namespace LastMusicPlayer::Backend::Utf
{
    std::optional<std::string> ToUtf8(std::wstring_view value) noexcept
    {
        if (value.empty())
        {
            return std::string{};
        }
        if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return std::nullopt;
        }

        auto const inputLength = static_cast<int>(value.size());
        auto const required = ::WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            inputLength,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 0)
        {
            return std::nullopt;
        }

        std::string result(static_cast<std::size_t>(required), '\0');
        auto const written = ::WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            inputLength,
            result.data(),
            required,
            nullptr,
            nullptr);
        if (written != required)
        {
            return std::nullopt;
        }
        return result;
    }

    std::optional<std::wstring> FromUtf8(std::string_view value) noexcept
    {
        if (value.empty())
        {
            return std::wstring{};
        }
        if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return std::nullopt;
        }

        auto const inputLength = static_cast<int>(value.size());
        auto const required = ::MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            inputLength,
            nullptr,
            0);
        if (required <= 0)
        {
            return std::nullopt;
        }

        std::wstring result(static_cast<std::size_t>(required), L'\0');
        auto const written = ::MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            inputLength,
            result.data(),
            required);
        if (written != required)
        {
            return std::nullopt;
        }
        return result;
    }
}
