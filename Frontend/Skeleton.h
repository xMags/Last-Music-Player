#pragma once

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace LastMusicPlayer::Frontend
{
    // Which content a placeholder stands in for. Each shape copies the geometry
    // of the real template it replaces, so the surface lays out once and does
    // not jump when the data arrives.
    enum class SkeletonShape
    {
        // Vertical rows matching the track list item template: a 58px row with
        // a 42px artwork block, title and artist bars, then source and duration.
        TrackList,

        // A horizontal band of 156px tiles, matching CatalogTileTemplate and the
        // row height the catalog shelves use. For surfaces whose heading is real
        // markup that stays put while the content loads.
        TileRow,

        // A heading bar above a TileRow, repeated. For the catalog, whose shelf
        // titles arrive with the data and so are missing during the load too.
        Shelf,

        // A wrapping grid of 168px cards, matching the library album, artist,
        // genre and playlist grids.
        TileGrid,
    };

    // Owns the placeholder for one surface.
    //
    // Two rules keep placeholders from making things worse than the blank panel
    // they replace:
    //
    // Most loads here are local SQLite queries that finish in tens of
    // milliseconds. A placeholder shown and hidden inside that window is a
    // visible glitch, so nothing appears until a load has run long enough to be
    // worth covering. It then comes down the instant the load ends, so a
    // surface never shows a placeholder and its finished state at once.
    //
    // A placeholder also only ever stands in for an empty surface. Several of
    // these load paths run again to append later pages, and replacing rows the
    // user is already reading with grey blocks would be a step backwards.
    //
    // Not thread-safe and not meant to be: every member touches XAML and must be
    // called on the UI thread.
    class SkeletonPresenter
    {
    public:
        SkeletonPresenter() = default;

        // Stops the timers. A running DispatcherQueueTimer is kept alive by the
        // queue and its tick captures this presenter by raw pointer, so leaving
        // one armed past destruction would fire against freed memory.
        ~SkeletonPresenter();

        SkeletonPresenter(SkeletonPresenter const&) = delete;
        SkeletonPresenter& operator=(SkeletonPresenter const&) = delete;

        // Binds this presenter to the panel that hosts the placeholder. The
        // host is expected to be collapsed in markup and is owned by the caller;
        // the presenter only ever adds its own child and toggles visibility.
        // Safe to call again to rebind, which discards any built tree.
        //
        // `count` is read in the unit that suits the shape: rows for TrackList,
        // tiles for TileRow, cards for TileGrid, and shelves for Shelf.
        void Attach(
            winrt::Microsoft::UI::Xaml::Controls::Panel const& host,
            SkeletonShape shape,
            int count);

        // Call when a load starts. Does nothing unless the surface is empty,
        // and even then shows nothing until the delay threshold passes.
        void BeginLoading(bool surfaceIsEmpty);

        // Call when a load finishes, succeeds, fails or is abandoned. Safe to
        // call when no load is in flight.
        void EndLoading();

    private:
        void Show();
        void Hide();
        void CancelTimers();
        winrt::Microsoft::UI::Xaml::UIElement EnsureContent();

        winrt::Microsoft::UI::Xaml::Controls::Panel m_host{ nullptr };
        SkeletonShape m_shape{ SkeletonShape::TrackList };
        int m_count{};

        bool m_loading{};
        bool m_visible{};

        // Runs the delay before showing; restarted per load.
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_showTimer{ nullptr };

        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard m_pulse{ nullptr };
    };

    // Ends a load when it leaves scope.
    //
    // Several of these load paths are coroutines that return early on a stale
    // epoch, a cancelled navigation, or a caught transport error. Pairing
    // EndLoading with each of those by hand is exactly the kind of thing that
    // gets missed when a new early return is added later, and the symptom is a
    // placeholder left on screen forever. A coroutine destroys its locals on
    // every suspension-free exit, so scoping it here covers them all.
    class SkeletonLoadScope
    {
    public:
        SkeletonLoadScope(SkeletonPresenter& presenter, bool surfaceIsEmpty)
            : m_presenter(&presenter)
        {
            m_presenter->BeginLoading(surfaceIsEmpty);
        }

        ~SkeletonLoadScope()
        {
            if (m_presenter)
            {
                m_presenter->EndLoading();
            }
        }

        SkeletonLoadScope(SkeletonLoadScope const&) = delete;
        SkeletonLoadScope& operator=(SkeletonLoadScope const&) = delete;

    private:
        SkeletonPresenter* m_presenter;
    };
}
