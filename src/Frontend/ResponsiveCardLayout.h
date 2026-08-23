#pragma once

#include <algorithm>
#include <cmath>

namespace LastMusicPlayer::Frontend
{
    struct ResponsiveCardLayout
    {
        int ColumnCount{};
        double CardWidth{};
        double ItemWidth{};
    };

    [[nodiscard]] inline ResponsiveCardLayout CalculateResponsiveCardLayout(
        double availableWidth,
        double gap,
        double minimumCardWidth) noexcept
    {
        if (availableWidth <= 0.0 || gap < 0.0 || minimumCardWidth <= 0.0)
        {
            return {};
        }

        auto const columnCount = (std::max)(
            1,
            static_cast<int>(std::floor(
                (availableWidth + gap) / (minimumCardWidth + gap))));
        auto const cardWidth =
            (availableWidth - (columnCount - 1) * gap) / columnCount;
        return { columnCount, cardWidth, cardWidth + gap };
    }
}
