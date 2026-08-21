#include "pch.h"
#include "ChipWrapPanel.h"
#include "ChipWrapPanel.g.cpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    namespace
    {
        // A child that reports NaN or an infinite desired size would poison the
        // running line width for every sibling after it, so clamp once here
        // rather than guarding at each use.
        float SanitizeExtent(float value) noexcept
        {
            if (std::isnan(value) || std::isinf(value) || value < 0.0f)
            {
                return 0.0f;
            }
            return value;
        }
    }

    void ChipWrapPanel::HorizontalSpacing(double value)
    {
        if (m_horizontalSpacing == value)
        {
            return;
        }
        m_horizontalSpacing = value;
        InvalidateMeasure();
    }

    void ChipWrapPanel::VerticalSpacing(double value)
    {
        if (m_verticalSpacing == value)
        {
            return;
        }
        m_verticalSpacing = value;
        InvalidateMeasure();
    }

    winrt::Windows::Foundation::Size ChipWrapPanel::MeasureOverride(
        winrt::Windows::Foundation::Size const& availableSize)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;

        auto const gapX = static_cast<float>(m_horizontalSpacing);
        auto const gapY = static_cast<float>(m_verticalSpacing);
        // An unconstrained width means "never wrap", which is what a horizontally
        // scrolling or auto-width host asks for.
        auto const lineLimit = std::isinf(availableSize.Width)
            ? (std::numeric_limits<float>::max)()
            : availableSize.Width;

        winrt::Windows::Foundation::Size childBudget{
            lineLimit,
            (std::numeric_limits<float>::infinity)() };

        float lineWidth = 0.0f;
        float lineHeight = 0.0f;
        float totalWidth = 0.0f;
        float totalHeight = 0.0f;

        for (auto const& child : Children())
        {
            if (child.Visibility() == Visibility::Collapsed)
            {
                continue;
            }

            child.Measure(childBudget);
            auto const width = SanitizeExtent(child.DesiredSize().Width);
            auto const height = SanitizeExtent(child.DesiredSize().Height);

            auto const advance = lineWidth > 0.0f ? lineWidth + gapX + width : width;
            if (lineWidth > 0.0f && advance > lineLimit)
            {
                totalWidth = (std::max)(totalWidth, lineWidth);
                totalHeight += lineHeight + gapY;
                lineWidth = width;
                lineHeight = height;
                continue;
            }

            lineWidth = advance;
            lineHeight = (std::max)(lineHeight, height);
        }

        totalWidth = (std::max)(totalWidth, lineWidth);
        totalHeight += lineHeight;

        return { totalWidth, totalHeight };
    }

    winrt::Windows::Foundation::Size ChipWrapPanel::ArrangeOverride(
        winrt::Windows::Foundation::Size const& finalSize)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;

        auto const gapX = static_cast<float>(m_horizontalSpacing);
        auto const gapY = static_cast<float>(m_verticalSpacing);

        // Two passes: a line's height is only known once every child on it has
        // been seen, and children are centred within their line the way the
        // reference's align-items:center rows are.
        struct Placement
        {
            winrt::Microsoft::UI::Xaml::UIElement element{ nullptr };
            float x{ 0.0f };
            float width{ 0.0f };
            float height{ 0.0f };
        };

        std::vector<Placement> line;
        float y = 0.0f;
        float lineHeight = 0.0f;

        auto flush = [&]()
        {
            for (auto const& placement : line)
            {
                auto const offset = std::floor((lineHeight - placement.height) / 2.0f);
                placement.element.Arrange({ placement.x, y + offset, placement.width, placement.height });
            }
            y += lineHeight;
            line.clear();
            lineHeight = 0.0f;
        };

        float x = 0.0f;
        for (auto const& child : Children())
        {
            if (child.Visibility() == Visibility::Collapsed)
            {
                continue;
            }

            auto const width = SanitizeExtent(child.DesiredSize().Width);
            auto const height = SanitizeExtent(child.DesiredSize().Height);

            if (x > 0.0f && x + width > finalSize.Width)
            {
                flush();
                y += gapY;
                x = 0.0f;
            }

            line.push_back({ child, x, width, height });
            x += width + gapX;
            lineHeight = (std::max)(lineHeight, height);
        }
        flush();

        return { finalSize.Width, y };
    }
}
