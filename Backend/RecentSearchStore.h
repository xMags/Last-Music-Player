#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    inline constexpr std::size_t kRecentSearchLimit = 6;

    // Returns a newest-first, case-insensitively deduplicated history after
    // adding `query`. Leading/trailing whitespace is removed and an empty
    // query leaves the history unchanged.
    [[nodiscard]] std::vector<std::wstring> RecordRecentSearch(
        std::vector<std::wstring> const& history,
        std::wstring query,
        std::size_t limit = kRecentSearchLimit);

    // Compact escaped-line format for SettingsManager. Backslash and newline
    // are escaped, so even pasted multi-line text round-trips without needing
    // a JSON dependency in this small policy module.
    [[nodiscard]] std::wstring EncodeRecentSearches(
        std::vector<std::wstring> const& history);
    [[nodiscard]] std::vector<std::wstring> DecodeRecentSearches(
        std::wstring const& encoded,
        std::size_t limit = kRecentSearchLimit);
}
