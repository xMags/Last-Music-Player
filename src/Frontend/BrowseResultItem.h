#pragma once

#include "BrowseResultItem.g.h"
#include "Backend/TrackInfo.h"

#include <utility>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    struct BrowseResultItem : BrowseResultItemT<BrowseResultItem>
    {
        BrowseResultItem() = default;

        winrt::hstring Kind() { return m_kind; }
        void Kind(winrt::hstring const& value) { m_kind = value; }
        winrt::hstring Title() { return m_title; }
        void Title(winrt::hstring const& value) { m_title = value; }
        winrt::hstring Subtitle() { return m_subtitle; }
        void Subtitle(winrt::hstring const& value) { m_subtitle = value; }
        winrt::hstring Meta() { return m_meta; }
        void Meta(winrt::hstring const& value) { m_meta = value; }
        winrt::hstring Key() { return m_key; }
        void Key(winrt::hstring const& value) { m_key = value; }
        winrt::hstring ArtworkUrl() { return m_artworkUrl; }
        void ArtworkUrl(winrt::hstring const& value) { m_artworkUrl = value; }
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage ArtworkSource() { return m_artworkSource; }
        void ArtworkSource(winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage const& value) { m_artworkSource = value; }
        winrt::Last_Music_Player::TrackInfo RepresentativeTrack() { return m_representativeTrack; }
        void RepresentativeTrack(winrt::Last_Music_Player::TrackInfo const& value) { m_representativeTrack = value; }

        winrt::Microsoft::UI::Xaml::CornerRadius ArtworkCornerRadius()
        {
            return m_kind == L"artist"
                ? winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(999.0)
                : winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(8.0);
        }

        winrt::Microsoft::UI::Xaml::TextAlignment GridTextAlignment()
        {
            return m_kind == L"artist"
                ? winrt::Microsoft::UI::Xaml::TextAlignment::Center
                : winrt::Microsoft::UI::Xaml::TextAlignment::Left;
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> const& MemberTracks() const noexcept
        {
            return m_memberTracks;
        }

        void MemberTracks(std::vector<winrt::Last_Music_Player::TrackInfo> tracks)
        {
            m_memberTracks = std::move(tracks);
        }

    private:
        winrt::hstring m_kind;
        winrt::hstring m_title;
        winrt::hstring m_subtitle;
        winrt::hstring m_meta;
        winrt::hstring m_key;
        winrt::hstring m_artworkUrl;
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage m_artworkSource{ nullptr };
        winrt::Last_Music_Player::TrackInfo m_representativeTrack{ nullptr };
        std::vector<winrt::Last_Music_Player::TrackInfo> m_memberTracks;
    };
}

namespace winrt::Last_Music_Player::factory_implementation
{
    struct BrowseResultItem : BrowseResultItemT<BrowseResultItem, implementation::BrowseResultItem>
    {
    };
}
