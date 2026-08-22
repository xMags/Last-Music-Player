#pragma once
#include "TrackViewModel.g.h"

namespace winrt::Last_Music_Player::implementation
{
    struct TrackViewModel : TrackViewModelT<TrackViewModel>
    {
        TrackViewModel() = default;

        hstring Title() { return m_title; }
        void Title(hstring const& value) { m_title = value; }

        hstring Artist() { return m_artist; }
        void Artist(hstring const& value) { m_artist = value; }

        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage AlbumArt() { return m_albumArt; }
        void AlbumArt(winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage const& value) { m_albumArt = value; }

        winrt::Windows::Storage::StorageFile File() { return m_file; }
        void File(winrt::Windows::Storage::StorageFile const& value) { m_file = value; }

    private:
        hstring m_title;
        hstring m_artist;
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage m_albumArt{ nullptr };
        winrt::Windows::Storage::StorageFile m_file{ nullptr };
    };
}
namespace winrt::Last_Music_Player::factory_implementation
{
    struct TrackViewModel : TrackViewModelT<TrackViewModel, implementation::TrackViewModel>
    {
    };
}
