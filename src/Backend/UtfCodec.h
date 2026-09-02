#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace LastMusicPlayer::Backend::Utf
{
    [[nodiscard]] std::optional<std::string> ToUtf8(std::wstring_view value) noexcept;
    [[nodiscard]] std::optional<std::wstring> FromUtf8(std::string_view value) noexcept;
}
