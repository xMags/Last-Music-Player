#include "pch.h"
#include "Backend/TrackInfo.h"
#include "TrackInfo.g.cpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <string_view>

namespace winrt::Last_Music_Player::implementation
{
    // Property implementations are inline in the header, except the ones below
    // that derive state from another property.

    void TrackInfo::SourceLabel(hstring const& value)
    {
        m_sourceLabel = value;

        // Labels that mean "this PC", including the generated playlists that are
        // built from local tracks. Everything else names a remote origin (an
        // account, a sync, a provider), which is what the card badge is for; a
        // purely local library therefore shows no badges.
        static constexpr std::array<std::wstring_view, 7> localLabels{
            L"Local", L"On this PC", L"Manual", L"Album", L"Playlist",
            L"System", L"Auto mix"
        };

        std::wstring_view label{ m_sourceLabel.c_str(), m_sourceLabel.size() };
        bool isRemote = !label.empty()
            && std::find(localLabels.begin(), localLabels.end(), label) == localLabels.end();

        m_sourceBadgeVisibility = isRemote
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

        if (!isRemote)
        {
            m_sourceBadgeText = {};
            return;
        }

        std::wstring upper{ label };
        std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        m_sourceBadgeText = hstring{ upper };
    }
}
