#pragma once

#include <algorithm>
#include <cmath>

namespace LastMusicPlayer::Frontend
{
    inline constexpr double kStandardCardGap = 20.0;
    inline constexpr double kStandardMinimumCardWidth = 170.0;
    inline constexpr double kBannerCardGap = 16.0;
    // Browse result cards add a 6px optical top offset, two text rows, and a
    // 22px bottom gutter. Their vertical footprint is taller than the standard
    // Library card even though both share the same artwork width and column
    // distribution.
    inline constexpr double kBrowseResultCardContentHeight = 80.0;

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
