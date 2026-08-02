#pragma once

#include <string>

namespace LastMusicPlayer::Backend
{
    std::wstring CreateHistoryImportEventId(
        std::wstring const& accountId,
        std::wstring const& sourceKey);
}
