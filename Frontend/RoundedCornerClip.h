#pragma once

#include <winrt/Microsoft.UI.Xaml.h>

namespace LastMusicPlayer::Frontend
{
    // Round off what an element actually paints.
    //
    // Border and Grid round their own background against CornerRadius, but they
    // do not clip what sits inside them. An Image hosted in a rounded frame
    // therefore paints square corners over that background the moment its
    // bitmap arrives, which is why artwork looks square while the placeholder
    // behind it looks rounded. A composition clip on the element's visual is
    // what actually cuts the corners, and it applies to the whole visual
    // subtree beneath that element.
    //
    // The clip follows the element's size, so it stays correct across layout
    // changes and window resizes.
    //
    // Safe to call repeatedly. Containers inside a DataTemplate are recycled and
    // raise Loaded again for a different track, and each call refreshes the
    // clip already on the element instead of stacking another one.

    // Takes the radius from the nearest ancestor within a few levels that
    // declares one, so the clip matches the frame the element sits in without
    // every call site having to repeat the design token.
    void ApplyRoundedCornerClip(winrt::Microsoft::UI::Xaml::FrameworkElement const& element);

    // For callers that know the radius they want. A radius of zero removes any
    // clip previously applied.
    void ApplyRoundedCornerClip(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
        float radius);
}
