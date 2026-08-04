#include "pch.h"
#include "Frontend/CarouselArrows.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.h>

#include <memory>

namespace LastMusicPlayer::Frontend
{
    namespace
    {
        namespace xaml = winrt::Microsoft::UI::Xaml;
        namespace controls = winrt::Microsoft::UI::Xaml::Controls;
        namespace media = winrt::Microsoft::UI::Xaml::Media;

        // Matches the web client: a page is most of the viewport, not all of it,
        // so a card stays partly visible across the jump and the eye keeps its
        // place instead of landing on an unfamiliar row.
        constexpr double kPageFraction = 0.85;
        constexpr double kButtonSize = 36.0;

        // The scroll offset is a float, so an exact comparison against the
        // extent would flicker the buttons at the ends.
        constexpr double kEdgeEpsilon = 2.0;

        template <typename T>
        T FindDescendant(xaml::DependencyObject const& root)
        {
            if (!root)
            {
                return nullptr;
            }
            if (auto match = root.try_as<T>())
            {
                return match;
            }
            auto count = media::VisualTreeHelper::GetChildrenCount(root);
            for (int32_t index = 0; index < count; ++index)
            {
                if (auto found = FindDescendant<T>(media::VisualTreeHelper::GetChild(root, index)))
                {
                    return found;
                }
            }
            return nullptr;
        }

        controls::Button MakeArrow(bool forward, double artCenterY)
        {
            controls::Button button;
            button.Width(kButtonSize);
            button.Height(kButtonSize);
            button.Padding(xaml::Thickness{});
            button.BorderThickness(xaml::Thickness{});
            button.CornerRadius(xaml::CornerRadius{ 18, 18, 18, 18 });
            button.VerticalAlignment(xaml::VerticalAlignment::Top);
            button.HorizontalAlignment(forward
                ? xaml::HorizontalAlignment::Right
                : xaml::HorizontalAlignment::Left);
            // Pulled half its own height up so the given centre lands on the
            // button's middle, and a little outside the row so it reads as an
            // overlay on the edge rather than as a card.
            button.Margin(forward
                ? xaml::Thickness{ 0, artCenterY - kButtonSize / 2, -10, 0 }
                : xaml::Thickness{ -10, artCenterY - kButtonSize / 2, 0, 0 });

            controls::FontIcon glyph;
            glyph.FontFamily(media::FontFamily{ L"Segoe Fluent Icons" });
            glyph.FontSize(13);
            // ChevronLeft / ChevronRight.
            glyph.Glyph(forward ? L"" : L"");
            button.Content(glyph);

            button.Opacity(0.0);
            button.IsHitTestVisible(false);
            button.Visibility(xaml::Visibility::Collapsed);
            controls::ToolTipService::SetToolTip(
                button,
                winrt::box_value(winrt::hstring{ forward ? L"Scroll right" : L"Scroll left" }));
            xaml::Automation::AutomationProperties::SetName(
                button,
                forward ? L"Scroll right" : L"Scroll left");
            return button;
        }

        // Kept on the heap because every handler below outlives the call that
        // installed it.
        struct ArrowState
        {
            controls::Button Previous{ nullptr };
            controls::Button Next{ nullptr };
            controls::ScrollViewer Scroller{ nullptr };
        };

        void Refresh(std::shared_ptr<ArrowState> const& state)
        {
            if (!state->Scroller)
            {
                return;
            }

            auto offset = state->Scroller.HorizontalOffset();
            auto max = state->Scroller.ScrollableWidth();
            auto scrollable = max > kEdgeEpsilon;
            auto showPrevious = scrollable && offset > kEdgeEpsilon;
            auto showNext = scrollable && offset < max - kEdgeEpsilon;

            // Shown outright whenever the row can move, rather than only while
            // the pointer is over it as on the web. Hover would have to arrive
            // through a panel with nothing to hit-test against, and an arrow
            // nobody can find is worse than one always present; this way the
            // affordance is visible the moment a row has more than it can show.
            auto apply = [&](controls::Button const& button, bool available)
            {
                // Collapsed rather than transparent when it cannot be used, so
                // it never swallows a click meant for the card beneath it.
                button.Visibility(available ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
                button.Opacity(available ? 1.0 : 0.0);
                button.IsHitTestVisible(available);
            };
            apply(state->Previous, showPrevious);
            apply(state->Next, showNext);
        }

        void Page(std::shared_ptr<ArrowState> const& state, bool forward)
        {
            if (!state->Scroller)
            {
                return;
            }
            auto step = state->Scroller.ViewportWidth() * kPageFraction;
            auto target = state->Scroller.HorizontalOffset() + (forward ? step : -step);
            // The double converts to the IReference the call wants; boxing it
            // to IInspectable first does not.
            state->Scroller.ChangeView(target, nullptr, nullptr);
        }
    }

    void AttachCarouselArrows(
        controls::Grid const& host,
        xaml::FrameworkElement const& shelf,
        double artCenterY)
    {
        if (!host || !shelf)
        {
            return;
        }

        auto state = std::make_shared<ArrowState>();
        state->Previous = MakeArrow(false, artCenterY);
        state->Next = MakeArrow(true, artCenterY);
        host.Children().Append(state->Previous);
        host.Children().Append(state->Next);

        state->Previous.Click([state](auto&&, auto&&) { Page(state, false); });
        state->Next.Click([state](auto&&, auto&&) { Page(state, true); });

        // Resolves the ScrollViewer, which lives inside the shelf's template
        // and so does not exist until the shelf has been through layout once.
        // Declared ahead of the handlers below because they all call it.
        auto bind = [state, weakShelf = winrt::make_weak(shelf)]()
        {
            auto target = weakShelf.get();
            if (!target || state->Scroller)
            {
                return;
            }
            state->Scroller = target.try_as<controls::ScrollViewer>();
            if (!state->Scroller)
            {
                state->Scroller = FindDescendant<controls::ScrollViewer>(target);
            }
            if (!state->Scroller)
            {
                return;
            }
            state->Scroller.ViewChanged([state](auto&&, auto&&) { Refresh(state); });
            Refresh(state);
        };

        shelf.Loaded([bind](auto&&, auto&&) { bind(); });
        // Items usually arrive after the shelf loads, and the extent only then
        // becomes scrollable, so re-check whenever the row is measured again.
        shelf.SizeChanged([state, bind](auto&&, auto&&)
        {
            bind();
            Refresh(state);
        });
        bind();
    }
}
