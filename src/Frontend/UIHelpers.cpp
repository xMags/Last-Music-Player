#include "pch.h"
#include "Frontend/UIHelpers.h"
#include <sstream>
#include <iomanip>

namespace LastMusicPlayer::Frontend
{
    winrt::Microsoft::UI::Xaml::Media::SolidColorBrush 
        UIHelpers::BrushFromHex(const std::wstring& hex)
    {
        // Expect format: "#RRGGBB" or "#AARRGGBB"
        unsigned int color = 0;
        std::wstringstream ss;
        ss << std::hex << hex.substr(1); // skip '#'
        ss >> color;

        uint8_t a = 0xFF, r, g, b;
        if (hex.size() == 9) // #AARRGGBB
        {
            a = static_cast<uint8_t>((color >> 24) & 0xFF);
            r = static_cast<uint8_t>((color >> 16) & 0xFF);
            g = static_cast<uint8_t>((color >> 8) & 0xFF);
            b = static_cast<uint8_t>(color & 0xFF);
        }
        else // #RRGGBB
        {
            r = static_cast<uint8_t>((color >> 16) & 0xFF);
            g = static_cast<uint8_t>((color >> 8) & 0xFF);
            b = static_cast<uint8_t>(color & 0xFF);
        }

        winrt::Windows::UI::Color c{ a, r, g, b };
        return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(c);
    }

    std::wstring UIHelpers::FormatTime(double seconds)
    {
        int mins = static_cast<int>(seconds) / 60;
        int secs = static_cast<int>(seconds) % 60;
        wchar_t buf[16];
        swprintf_s(buf, L"%d:%02d", mins, secs);
        return buf;
    }

    double UIHelpers::Clamp(double value, double min, double max)
    {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }
}
