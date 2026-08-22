#include "pch.h"
#include "Frontend/RoundedCornerClip.h"

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Numerics.h>

namespace LastMusicPlayer::Frontend
{
    namespace
    {
        // How far up to look for the frame that defines the rounding. Artwork is
        // at most a Grid and a Border away from it. Stopping short keeps a small
        // thumbnail from adopting the much larger radius of the card it sits on.
        constexpr int kMaxAncestorSearchDepth = 3;

        float LargestCorner(winrt::Microsoft::UI::Xaml::CornerRadius const& radius) noexcept
        {
            auto largest = radius.TopLeft;
            largest = radius.TopRight > largest ? radius.TopRight : largest;
            largest = radius.BottomRight > largest ? radius.BottomRight : largest;
            largest = radius.BottomLeft > largest ? radius.BottomLeft : largest;
            return static_cast<float>(largest);
        }

        // Border and Grid are the two containers in this app that carry a corner
        // radius; neither shares a base class that exposes it.
        float DeclaredCornerRadius(winrt::Microsoft::UI::Xaml::DependencyObject const& element) noexcept
        {
            if (auto border = element.try_as<winrt::Microsoft::UI::Xaml::Controls::Border>())
            {
                return LargestCorner(border.CornerRadius());
            }
            if (auto grid = element.try_as<winrt::Microsoft::UI::Xaml::Controls::Grid>())
            {
                return LargestCorner(grid.CornerRadius());
            }
            return 0.0f;
        }

        // Artwork sits in a rounded frame one of two ways in this app. A list row
        // nests the image inside the rounded Border, so the radius is found by
        // walking up. A card instead layers the image over a rounded placeholder
        // Border inside a plain Grid, so there the radius belongs to a sibling.
        // Both mean the same thing: match the frame the image is filling.
        float ResolveFrameCornerRadius(winrt::Microsoft::UI::Xaml::FrameworkElement const& element)
        {
            winrt::Microsoft::UI::Xaml::DependencyObject current{ element };
            for (int depth = 0; depth < kMaxAncestorSearchDepth; ++depth)
            {
                auto parent = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(current);
                if (!parent)
                {
                    return 0.0f;
                }

                if (auto radius = DeclaredCornerRadius(parent); radius > 0.0f)
                {
                    return radius;
                }

                // Only the immediate parent's children are considered. Further
                // up, a rounded element is a neighbouring card rather than the
                // frame behind this image.
                if (depth == 0)
                {
                    if (auto panel = parent.try_as<winrt::Microsoft::UI::Xaml::Controls::Panel>())
                    {
                        for (auto const& sibling : panel.Children())
                        {
                            if (auto radius = DeclaredCornerRadius(sibling); radius > 0.0f)
                            {
                                return radius;
                            }
                        }
                    }
                }

                current = parent;
            }
            return 0.0f;
        }
    }

    void ApplyRoundedCornerClip(winrt::Microsoft::UI::Xaml::FrameworkElement const& element)
    {
        if (!element)
        {
            return;
        }
        ApplyRoundedCornerClip(element, ResolveFrameCornerRadius(element));
    }

    void ApplyRoundedCornerClip(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
        float radius)
    {
        if (!element)
        {
            return;
        }

        try
        {
            auto visual = winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(element);
            if (!visual)
            {
                return;
            }

            if (radius <= 0.0f)
            {
                visual.Clip(nullptr);
                return;
            }

            // A recycled container still carries the clip from its previous use.
            // Reuse that geometry so the SizeChanged handler below is only ever
            // attached once per element.
            if (auto existing = visual.Clip().try_as<winrt::Microsoft::UI::Composition::CompositionGeometricClip>())
            {
                if (auto geometry = existing.Geometry()
                    .try_as<winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry>())
                {
                    geometry.CornerRadius({ radius, radius });
                    geometry.Size(element.ActualSize());
                    return;
                }
            }

            auto compositor = visual.Compositor();
            auto geometry = compositor.CreateRoundedRectangleGeometry();
            geometry.CornerRadius({ radius, radius });
            // Loaded can run before the first arrange, most often for artwork in
            // a panel that starts collapsed. A zero-sized clip hides nothing,
            // because a zero-sized element has nothing to draw, and the handler
            // below corrects it as soon as the element is measured.
            geometry.Size(element.ActualSize());
            visual.Clip(compositor.CreateGeometricClip(geometry));

            // Keeps the clip matched to the element through layout passes and
            // window resizes. The element owns this handler and the handler owns
            // the geometry, which holds nothing back, so there is no cycle.
            element.SizeChanged([geometry](
                winrt::Windows::Foundation::IInspectable const&,
                winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
            {
                auto const size = args.NewSize();
                geometry.Size({ size.Width, size.Height });
            });
        }
        catch (...)
        {
            // Rounding is decorative. A composition failure must not take the
            // artwork itself down with it.
        }
    }
}
