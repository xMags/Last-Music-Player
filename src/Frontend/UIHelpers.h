#pragma once
#include <string>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace LastMusicPlayer::Frontend
{
    class UIHelpers
    {
    public:
        // Convert hex color string (e.g., "#FF0000") to a SolidColorBrush
        static winrt::Microsoft::UI::Xaml::Media::SolidColorBrush 
            BrushFromHex(const std::wstring& hex);

        // Format seconds to "m:ss" display string
        static std::wstring FormatTime(double seconds);

        // Clamp a value between min and max
        static double Clamp(double value, double min, double max);
    };
}
