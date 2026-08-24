#pragma once

namespace LastMusicPlayer::Frontend
{
    [[nodiscard]] constexpr float ClampRoundedCornerRadius(
        float radius,
        float width,
        float height) noexcept
    {
        if (radius <= 0.0f || width <= 0.0f || height <= 0.0f)
        {
            return 0.0f;
        }
        auto const shortestEdge = width < height ? width : height;
        auto const maximum = shortestEdge * 0.5f;
        return radius < maximum ? radius : maximum;
    }
}
