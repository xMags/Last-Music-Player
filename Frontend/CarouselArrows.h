#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace LastMusicPlayer::Frontend
{
    // Overlays previous/next buttons on a horizontally scrolling shelf, the way
    // the web client does: the wheel still works, but a pointer user gets the
    // same round chevrons at the edge of the row rather than having to know that
    // shift-wheel or a trackpad gesture is the only way across.
    //
    // `host` is a Grid that already contains `shelf`; the buttons are added on
    // top of it, so they need a panel that overlaps rather than stacks. The
    // caller supplies it rather than this function creating one, because a
    // shelf's parent is not reliably reachable when there is still time to
    // change it: an element declared in markup has no logical parent yet during
    // construction, and one built in code has not been added to anything.
    //
    // `artCenterY` is how far down the row the artwork's middle sits, which is
    // where the buttons line up: centring on the whole card would put them
    // beside the title instead of the picture.
    //
    // Safe to call before the shelf has loaded. The buttons stay hidden until
    // there is something to scroll to, appear on hover, and each one hides
    // again at its own end of the range.
    //
    // Must be called on the UI thread. The wiring lives as long as the host.
    void AttachCarouselArrows(
        winrt::Microsoft::UI::Xaml::Controls::Grid const& host,
        winrt::Microsoft::UI::Xaml::FrameworkElement const& shelf,
        double artCenterY);
}
