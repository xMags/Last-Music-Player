#include "pch.h"
#include "Frontend/GenreArtworkTile.h"
#include "GenreArtworkTile.g.cpp"

#include "Backend/GenreArtworkRecipe.h"

#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

namespace winrt::Last_Music_Player::implementation
{
    namespace
    {
        namespace MUX = winrt::Microsoft::UI::Xaml;
        namespace MUXC = winrt::Microsoft::UI::Xaml::Controls;
        namespace MUXM = winrt::Microsoft::UI::Xaml::Media;
        namespace MUXS = winrt::Microsoft::UI::Xaml::Shapes;

        winrt::Windows::UI::Color Color(std::uint32_t argb) noexcept
        {
            return {
                static_cast<std::uint8_t>((argb >> 24) & 0xFF),
                static_cast<std::uint8_t>((argb >> 16) & 0xFF),
                static_cast<std::uint8_t>((argb >> 8) & 0xFF),
                static_cast<std::uint8_t>(argb & 0xFF)
            };
        }

        MUXM::SolidColorBrush WhiteBrush()
        {
            return MUXM::SolidColorBrush{ Color(0xEBFFFFFF) };
        }

        void Position(MUX::FrameworkElement const& element, LastMusicPlayer::Backend::GenreArtworkPrimitive const& primitive)
        {
            element.Width(primitive.Width);
            element.Height(primitive.Height);
            MUXC::Canvas::SetLeft(element, primitive.X);
            MUXC::Canvas::SetTop(element, primitive.Y);
            if (primitive.Rotation != 0.0)
            {
                MUXM::RotateTransform rotation;
                rotation.Angle(primitive.Rotation);
                element.RenderTransformOrigin({ 0.5f, 0.5f });
                element.RenderTransform(rotation);
            }
        }

        MUX::UIElement BuildPrimitive(LastMusicPlayer::Backend::GenreArtworkPrimitive const& primitive)
        {
            using LastMusicPlayer::Backend::GenreArtworkPrimitiveKind;
            if (primitive.Kind == GenreArtworkPrimitiveKind::Dot)
            {
                MUXS::Ellipse ellipse;
                Position(ellipse, primitive);
                ellipse.Fill(WhiteBrush());
                return ellipse;
            }
            if (primitive.Kind == GenreArtworkPrimitiveKind::Ring
                || primitive.Kind == GenreArtworkPrimitiveKind::Oval)
            {
                MUXS::Ellipse ellipse;
                Position(ellipse, primitive);
                ellipse.Stroke(WhiteBrush());
                ellipse.StrokeThickness(primitive.StrokeWidth);
                if (primitive.Dashed)
                {
                    ellipse.StrokeDashArray().Append(2.0);
                    ellipse.StrokeDashArray().Append(2.0);
                }
                return ellipse;
            }
            if (primitive.Kind == GenreArtworkPrimitiveKind::Triangle)
            {
                MUXS::Polygon polygon;
                polygon.Fill(WhiteBrush());
                polygon.Points().Append({ 48.0f, 14.0f });
                polygon.Points().Append({ 90.0f, 84.0f });
                polygon.Points().Append({ 6.0f, 84.0f });
                return polygon;
            }

            MUXC::Border border;
            Position(border, primitive);
            border.CornerRadius(MUX::CornerRadiusHelper::FromUniformRadius(primitive.CornerRadius));
            if (primitive.Kind == GenreArtworkPrimitiveKind::Arch)
            {
                border.BorderBrush(WhiteBrush());
                border.BorderThickness({ primitive.StrokeWidth, primitive.StrokeWidth, primitive.StrokeWidth, 0.0 });
                border.CornerRadius({ primitive.CornerRadius, primitive.CornerRadius, 0.0, 0.0 });
            }
            else if (primitive.Kind == GenreArtworkPrimitiveKind::OutlineBox
                || primitive.Kind == GenreArtworkPrimitiveKind::OutlineRectangle)
            {
                border.BorderBrush(WhiteBrush());
                border.BorderThickness(MUX::ThicknessHelper::FromUniformLength(primitive.StrokeWidth));
            }
            else
            {
                border.Background(WhiteBrush());
            }
            return border;
        }

        MUXC::Canvas BuildMark(LastMusicPlayer::Backend::GenreArtworkRecipe const& recipe)
        {
            MUXC::Canvas mark;
            mark.Width(96.0);
            mark.Height(96.0);
            for (auto const& primitive : recipe.Primitives)
            {
                mark.Children().Append(BuildPrimitive(primitive));
            }
            return mark;
        }

        MUXM::LinearGradientBrush LinearGradient(
            winrt::Windows::UI::Color const& start,
            winrt::Windows::UI::Color const& end,
            winrt::Windows::Foundation::Point const& startPoint,
            winrt::Windows::Foundation::Point const& endPoint,
            double endOffset = 1.0)
        {
            MUXM::LinearGradientBrush brush;
            brush.StartPoint(startPoint);
            brush.EndPoint(endPoint);
            MUXM::GradientStop first;
            first.Color(start);
            first.Offset(0.0);
            brush.GradientStops().Append(first);
            MUXM::GradientStop second;
            second.Color(end);
            second.Offset(endOffset);
            brush.GradientStops().Append(second);
            return brush;
        }
    }

    GenreArtworkTile::GenreArtworkTile()
    {
        IsTabStop(false);
        IsHitTestVisible(false);
        HorizontalContentAlignment(MUX::HorizontalAlignment::Stretch);
        VerticalContentAlignment(MUX::VerticalAlignment::Stretch);
        Render();
    }

    void GenreArtworkTile::GenreName(winrt::hstring const& value)
    {
        if (m_genreName == value)
        {
            return;
        }
        m_genreName = value;
        Render();
    }

    void GenreArtworkTile::Render()
    {
        auto const& recipe = LastMusicPlayer::Backend::ResolveGenreArtworkRecipe(
            std::wstring_view{ m_genreName.c_str(), m_genreName.size() });

        MUXC::Border shell;
        shell.CornerRadius(MUX::CornerRadiusHelper::FromUniformRadius(16.0));
        shell.Background(LinearGradient(
            Color(recipe.GradientStart), Color(recipe.GradientEnd), { 0.0f, 0.0f }, { 1.0f, 1.0f }));

        MUXC::Grid layers;

        MUXC::Border highlight;
        MUXM::RadialGradientBrush highlightBrush;
        highlightBrush.Center({ 0.12f, 0.06f });
        highlightBrush.GradientOrigin({ 0.12f, 0.06f });
        highlightBrush.RadiusX(1.2);
        highlightBrush.RadiusY(0.9);
        MUXM::GradientStop highlightStart;
        highlightStart.Color(Color(0x57FFFFFF));
        highlightStart.Offset(0.0);
        highlightBrush.GradientStops().Append(highlightStart);
        MUXM::GradientStop highlightEnd;
        highlightEnd.Color(Color(0x00FFFFFF));
        highlightEnd.Offset(0.62);
        highlightBrush.GradientStops().Append(highlightEnd);
        highlight.Background(highlightBrush);
        layers.Children().Append(highlight);

        MUXC::Image hatch;
        MUXM::Imaging::SvgImageSource hatchSource;
        hatchSource.UriSource(winrt::Windows::Foundation::Uri{ L"ms-appx:///Assets/LibraryStripeOverlay.svg" });
        hatch.Source(hatchSource);
        hatch.Stretch(MUXM::Stretch::Fill);
        hatch.Opacity(1.0);
        layers.Children().Append(hatch);

        MUXC::Border shade;
        shade.Background(LinearGradient(
            Color(0x33000000), Color(0x00000000), { 0.0f, 1.0f }, { 0.0f, 0.0f }, 0.46));
        layers.Children().Append(shade);

        auto ghost = BuildMark(recipe);
        ghost.Opacity(0.16);
        ghost.HorizontalAlignment(MUX::HorizontalAlignment::Right);
        ghost.VerticalAlignment(MUX::VerticalAlignment::Bottom);
        ghost.RenderTransformOrigin({ 1.0f, 1.0f });
        MUXM::ScaleTransform ghostScale;
        ghostScale.ScaleX(2.1);
        ghostScale.ScaleY(2.1);
        ghost.RenderTransform(ghostScale);
        ghost.Translation({ 52.0f, 46.0f, 0.0f });
        layers.Children().Append(ghost);

        auto mark = BuildMark(recipe);
        mark.HorizontalAlignment(MUX::HorizontalAlignment::Center);
        mark.VerticalAlignment(MUX::VerticalAlignment::Center);
        mark.Translation({ 0.0f, -8.0f, 16.0f });
        MUXM::ThemeShadow markShadow;
        mark.Shadow(markShadow);
        layers.Children().Append(mark);

        MUXC::Border innerEdge;
        innerEdge.BorderBrush(MUXM::SolidColorBrush{ Color(0x38FFFFFF) });
        innerEdge.BorderThickness(MUX::ThicknessHelper::FromUniformLength(1.0));
        innerEdge.CornerRadius(MUX::CornerRadiusHelper::FromUniformRadius(16.0));
        layers.Children().Append(innerEdge);

        MUXC::StackPanel label;
        label.Orientation(MUXC::Orientation::Horizontal);
        label.Spacing(8.0);
        label.HorizontalAlignment(MUX::HorizontalAlignment::Left);
        label.VerticalAlignment(MUX::VerticalAlignment::Bottom);
        label.Margin({ 18.0, 0.0, 0.0, 16.0 });
        MUXS::Ellipse labelDot;
        labelDot.Width(5.0);
        labelDot.Height(5.0);
        labelDot.Fill(MUXM::SolidColorBrush{ Color(0xD9FFFFFF) });
        labelDot.VerticalAlignment(MUX::VerticalAlignment::Center);
        MUXC::TextBlock labelText;
        labelText.Text(L"genre");
        labelText.FontFamily(MUXM::FontFamily{ L"Cascadia Mono, Consolas" });
        labelText.FontSize(11.5);
        labelText.CharacterSpacing(140);
        labelText.Foreground(MUXM::SolidColorBrush{ Color(0xEBFFFFFF) });
        label.Children().Append(labelDot);
        label.Children().Append(labelText);
        layers.Children().Append(label);

        shell.Child(layers);
        Content(shell);
    }
}
