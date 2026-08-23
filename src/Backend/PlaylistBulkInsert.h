#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace LastMusicPlayer::Backend
{
    [[nodiscard]] bool AddTracksToPlaylistTransactional(
        sqlite3* database,
        std::wstring const& playlistKey,
        std::vector<std::int64_t> const& trackIds);
}
