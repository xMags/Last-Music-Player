#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace LastMusicPlayer::Frontend
{
    // Overlays previous/next buttons on a horizontally scrolling shelf, the way
    // the web client does.
    //
    // Every shelf these are attached to is declared with its horizontal scroll
    // mode disabled, which makes these buttons the only way across. That is
    // deliberate: a shelf with no vertical axis of its own has a vertical wheel
    // steered onto its horizontal one by the framework, so the row would run off
    // sideways whenever the pointer happened to be over it while the user was
    // scrolling down the page. Intercepting the wheel instead was tried and does
    // not hold: whether the shelf still scrolls depends on where the event is
    // handled relative to the ScrollViewer, which differs across the shapes a
    // shelf can take. Disabling the axis stops every input path at once, which
    // costs touch panning on these rows.
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
