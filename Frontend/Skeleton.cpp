#include "pch.h"
#include "Frontend/Skeleton.h"

#include <winrt/Windows.Foundation.h>

#include <array>
#include <chrono>

namespace LastMusicPlayer::Frontend
{
    namespace
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        namespace animation = winrt::Microsoft::UI::Xaml::Media::Animation;

        // Long enough that a local query answering in tens of milliseconds
        // never puts anything on screen, short enough that a real wait is
        // covered before it starts to feel like a dead panel.
        constexpr std::chrono::milliseconds kShowDelay{ 150 };

        // There is deliberately no minimum visible time. Holding a placeholder
        // after its load finished means the surface briefly shows the
        // placeholder and the finished state at once, and callers reveal empty
        // states the moment they have an answer. The show delay above already
        // keeps fast loads from flashing anything at all.

        // Bar widths are varied per row so a block of placeholders does not read
        // as a mechanical grid. The values are inset amounts, not widths, so
        // they work against whatever the surface is stretched to.
        constexpr std::array<double, 8> kTitleInsets{ 0, 62, 24, 96, 40, 78, 12, 54 };
        constexpr std::array<double, 8> kSubtitleInsets{ 110, 148, 132, 96, 160, 118, 140, 104 };

        Style BlockStyle()
        {
            // A root-dictionary key, so this is not a theme dictionary lookup.
            // The style's own setter carries the ThemeResource reference, which
            // resolves per element and follows theme changes.
            return Application::Current().Resources()
                .Lookup(winrt::box_value(winrt::hstring{ L"SkeletonBlock" }))
                .try_as<Style>();
        }

        CornerRadius LargeRadius()
        {
            // A CornerRadius resource arrives boxed as IReference, so it has to
            // be unwrapped rather than cast. Falls back to the token's own value
            // so a missing or retyped resource degrades to a square-ish block
            // instead of throwing out of a layout pass.
            try
            {
                auto boxed = Application::Current().Resources()
                    .Lookup(winrt::box_value(winrt::hstring{ L"Radius2xl" }));
                if (auto radius = boxed.try_as<winrt::Windows::Foundation::IReference<CornerRadius>>())
                {
                    return radius.Value();
                }
            }
            catch (...)
            {
            }
            return CornerRadiusHelper::FromUniformRadius(16);
        }

        Border Block(Style const& style, double height, Thickness const& margin)
        {
            Border block;
            if (style)
            {
                block.Style(style);
            }
            block.Height(height);
            block.Margin(margin);
            return block;
        }

        // The tile shared by TileRow and TileGrid: a square of artwork with a
        // couple of text bars under it.
        StackPanel Tile(
            Style const& style,
            double width,
            double artSize,
            Thickness const& margin,
            std::size_t index)
        {
            StackPanel tile;
            tile.Width(width);
            tile.Margin(margin);
            tile.VerticalAlignment(VerticalAlignment::Top);

            // Width is set rather than inherited: a library card is 168 wide but
            // its artwork is 160, so stretching to the parent would overstate it.
            auto art = Block(style, artSize, ThicknessHelper::FromUniformLength(0));
            art.Width(artSize);
            art.HorizontalAlignment(HorizontalAlignment::Left);
            art.CornerRadius(LargeRadius());
            tile.Children().Append(art);

            tile.Children().Append(Block(
                style, 14, ThicknessHelper::FromLengths(0, 12, 0, 0)));
            tile.Children().Append(Block(
                style, 14, ThicknessHelper::FromLengths(0, 8, kTitleInsets[index % kTitleInsets.size()] / 3.0, 0)));
            tile.Children().Append(Block(
                style, 12, ThicknessHelper::FromLengths(0, 10, 40 + kTitleInsets[index % kTitleInsets.size()] / 3.0, 0)));
            return tile;
        }

        // Fixed-width tiles laid side by side would ask for more width than the
        // page has and widen the whole view. A ScrollViewer with both scroll
        // modes disabled measures its child freely and clips to the viewport,
        // which is exactly what the real GridView does.
        ScrollViewer Clipped(UIElement const& content, double height)
        {
            ScrollViewer clip;
            clip.Height(height);
            clip.HorizontalScrollMode(ScrollMode::Disabled);
            clip.HorizontalScrollBarVisibility(ScrollBarVisibility::Hidden);
            clip.VerticalScrollMode(ScrollMode::Disabled);
            clip.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
            clip.Content(content);
            return clip;
        }

        UIElement BuildTrackList(Style const& style, int count)
        {
            StackPanel rows;
            for (int index = 0; index < count; ++index)
            {
                Grid row;
                row.Height(58);
                row.ColumnSpacing(12);
                row.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));

                for (double width : { 44.0, 0.0, 70.0, 52.0 })
                {
                    ColumnDefinition column;
                    column.Width(width > 0.0
                        ? GridLengthHelper::FromPixels(width)
                        : GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                    row.ColumnDefinitions().Append(column);
                }

                auto art = Block(style, 42, ThicknessHelper::FromUniformLength(0));
                art.Width(42);
                art.VerticalAlignment(VerticalAlignment::Center);
                row.Children().Append(art);

                StackPanel text;
                text.VerticalAlignment(VerticalAlignment::Center);
                text.Spacing(6);
                auto const inset = kTitleInsets[static_cast<std::size_t>(index) % kTitleInsets.size()];
                text.Children().Append(Block(
                    style, 13, ThicknessHelper::FromLengths(0, 0, inset, 0)));
                text.Children().Append(Block(
                    style,
                    11,
                    ThicknessHelper::FromLengths(
                        0,
                        0,
                        kSubtitleInsets[static_cast<std::size_t>(index) % kSubtitleInsets.size()],
                        0)));
                Grid::SetColumn(text, 1);
                row.Children().Append(text);

                auto source = Block(style, 11, ThicknessHelper::FromUniformLength(0));
                source.Width(44);
                source.HorizontalAlignment(HorizontalAlignment::Right);
                source.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(source, 2);
                row.Children().Append(source);

                auto duration = Block(style, 11, ThicknessHelper::FromUniformLength(0));
                duration.Width(32);
                duration.HorizontalAlignment(HorizontalAlignment::Right);
                duration.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(duration, 3);
                row.Children().Append(duration);

                rows.Children().Append(row);
            }
            return rows;
        }

        UIElement BuildTileRow(Style const& style, int count)
        {
            StackPanel tiles;
            tiles.Orientation(Orientation::Horizontal);
            for (int index = 0; index < count; ++index)
            {
                tiles.Children().Append(Tile(
                    style,
                    156,
                    156,
                    ThicknessHelper::FromLengths(0, 0, 20, 0),
                    static_cast<std::size_t>(index)));
            }
            return Clipped(tiles, 260);
        }

        UIElement BuildShelves(Style const& style, int count)
        {
            // Heading widths differ per shelf so the placeholder does not look
            // like a repeated stamp.
            constexpr std::array<double, 3> headingWidths{ 180, 140, 210 };
            constexpr int tilesPerShelf = 6;

            StackPanel shelves;
            shelves.Spacing(32);
            for (int index = 0; index < count; ++index)
            {
                StackPanel shelf;

                auto heading = Block(style, 26, ThicknessHelper::FromLengths(0, 0, 0, 16));
                heading.Width(headingWidths[static_cast<std::size_t>(index) % headingWidths.size()]);
                heading.HorizontalAlignment(HorizontalAlignment::Left);
                shelf.Children().Append(heading);

                // Offset the tile variation per shelf so two shelves do not end
                // up with an identical run of bar widths.
                StackPanel tiles;
                tiles.Orientation(Orientation::Horizontal);
                for (int tile = 0; tile < tilesPerShelf; ++tile)
                {
                    tiles.Children().Append(Tile(
                        style,
                        156,
                        156,
                        ThicknessHelper::FromLengths(0, 0, 20, 0),
                        static_cast<std::size_t>(index * tilesPerShelf + tile)));
                }
                shelf.Children().Append(Clipped(tiles, 260));

                shelves.Children().Append(shelf);
            }
            return shelves;
        }

        UIElement BuildTileGrid(Style const& style, int count)
        {
            // Laid out as fixed rows rather than a wrapping panel: WinUI has no
            // wrap panel without pulling in the toolkit, and a placeholder does
            // not need to reflow the way the real grid does.
            constexpr int perRow = 5;
            constexpr double cardWidth = 168;
            constexpr double artSize = 160;
            constexpr double rowHeight = 220;

            StackPanel grid;
            auto index = 0;
            while (index < count)
            {
                StackPanel row;
                row.Orientation(Orientation::Horizontal);
                for (int column = 0; column < perRow && index < count; ++column, ++index)
                {
                    auto card = Tile(
                        style,
                        cardWidth,
                        artSize,
                        ThicknessHelper::FromLengths(0, 0, 16, 0),
                        static_cast<std::size_t>(index));
                    row.Children().Append(card);
                }
                grid.Children().Append(Clipped(row, rowHeight));
            }
            return grid;
        }
    }

    SkeletonPresenter::~SkeletonPresenter()
    {
        CancelTimers();
    }

    void SkeletonPresenter::Attach(
        winrt::Microsoft::UI::Xaml::Controls::Panel const& host,
        SkeletonShape shape,
        int count)
    {
        CancelTimers();
        if (m_host)
        {
            m_host.Children().Clear();
        }

        m_host = host;
        m_shape = shape;
        m_count = count > 0 ? count : 1;
        m_loading = false;
        m_visible = false;
        m_pulse = nullptr;

        if (m_host)
        {
            m_host.Visibility(Visibility::Collapsed);
        }
    }

    void SkeletonPresenter::BeginLoading(bool surfaceIsEmpty)
    {
        if (!m_host)
        {
            return;
        }

        // A surface with rows on it is being paged, not populated. Leave what
        // the user is reading alone.
        if (!surfaceIsEmpty)
        {
            EndLoading();
            return;
        }

        if (m_loading)
        {
            return;
        }
        m_loading = true;

        if (m_visible)
        {
            // Still on screen from a previous load; nothing to schedule.
            return;
        }

        auto queue = m_host.DispatcherQueue();
        if (!queue)
        {
            return;
        }

        if (!m_showTimer)
        {
            m_showTimer = queue.CreateTimer();
            m_showTimer.IsRepeating(false);
            m_showTimer.Tick([this](auto&&, auto&&)
            {
                if (m_loading)
                {
                    Show();
                }
            });
        }
        m_showTimer.Interval(kShowDelay);
        m_showTimer.Start();
    }

    void SkeletonPresenter::EndLoading()
    {
        m_loading = false;
        if (m_showTimer && m_showTimer.IsRunning())
        {
            m_showTimer.Stop();
        }

        if (m_visible)
        {
            Hide();
        }
    }

    winrt::Microsoft::UI::Xaml::UIElement SkeletonPresenter::EnsureContent()
    {
        if (m_host.Children().Size() > 0)
        {
            return m_host.Children().GetAt(0);
        }

        auto style = BlockStyle();
        UIElement content{ nullptr };
        switch (m_shape)
        {
        case SkeletonShape::TileRow:
            content = BuildTileRow(style, m_count);
            break;
        case SkeletonShape::Shelf:
            content = BuildShelves(style, m_count);
            break;
        case SkeletonShape::TileGrid:
            content = BuildTileGrid(style, m_count);
            break;
        case SkeletonShape::TrackList:
        default:
            content = BuildTrackList(style, m_count);
            break;
        }
        m_host.Children().Append(content);
        return content;
    }

    void SkeletonPresenter::Show()
    {
        if (!m_host || m_visible)
        {
            return;
        }

        auto content = EnsureContent();
        if (!content)
        {
            return;
        }

        m_visible = true;
        m_host.Visibility(Visibility::Visible);

        if (!m_pulse)
        {
            // Shallow on purpose: dipping far enough to fade the blocks back
            // toward the page colour costs more legibility than the motion
            // buys. Opacity is an independent animation, so this runs on the
            // composition thread instead of competing with the load it covers.
            animation::DoubleAnimation fade;
            fade.From(1.0);
            fade.To(0.65);
            fade.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(1100)));
            fade.AutoReverse(true);
            fade.RepeatBehavior(animation::RepeatBehaviorHelper::Forever());
            // Targeted by object, not by name: this tree is built here rather
            // than in markup, so there is no namescope for a name to resolve in.
            animation::Storyboard::SetTarget(fade, content);
            animation::Storyboard::SetTargetProperty(fade, L"Opacity");

            animation::Storyboard pulse;
            pulse.Children().Append(fade);
            m_pulse = pulse;
        }
        m_pulse.Begin();
    }

    void SkeletonPresenter::Hide()
    {
        if (!m_host)
        {
            return;
        }

        if (m_pulse)
        {
            // Stopped rather than left running: a collapsed element still
            // animates, and this one is on screen only during a load.
            m_pulse.Stop();
        }
        if (m_host.Children().Size() > 0)
        {
            m_host.Children().GetAt(0).Opacity(1.0);
        }
        m_host.Visibility(Visibility::Collapsed);
        m_visible = false;
    }

    void SkeletonPresenter::CancelTimers()
    {
        if (m_showTimer && m_showTimer.IsRunning())
        {
            m_showTimer.Stop();
        }
    }
}
