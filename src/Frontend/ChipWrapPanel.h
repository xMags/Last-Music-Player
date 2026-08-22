#pragma once
#include "ChipWrapPanel.g.h"

namespace winrt::Last_Music_Player::implementation
{
    // Left-to-right flow layout: children keep their natural width and wrap to a
    // new line when the current one runs out of room. WinUI ships no wrapping
    // panel that preserves per-child width (VariableSizedWrapGrid and
    // UniformGridLayout both impose a uniform cell), which is what the chip rows
    // in Settings need.
    struct ChipWrapPanel : ChipWrapPanelT<ChipWrapPanel>
    {
        ChipWrapPanel() = default;

        double HorizontalSpacing() const noexcept { return m_horizontalSpacing; }
        void HorizontalSpacing(double value);

        double VerticalSpacing() const noexcept { return m_verticalSpacing; }
        void VerticalSpacing(double value);

        winrt::Windows::Foundation::Size MeasureOverride(
            winrt::Windows::Foundation::Size const& availableSize);
        winrt::Windows::Foundation::Size ArrangeOverride(
            winrt::Windows::Foundation::Size const& finalSize);

    private:
        double m_horizontalSpacing{ 8.0 };
        double m_verticalSpacing{ 8.0 };
    };
}

namespace winrt::Last_Music_Player::factory_implementation
{
    struct ChipWrapPanel : ChipWrapPanelT<ChipWrapPanel, implementation::ChipWrapPanel>
    {
    };
}
