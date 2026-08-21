#pragma once
#include "TrackInfo.g.h"
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace winrt::Last_Music_Player::implementation
{
    struct TrackInfo : TrackInfoT<TrackInfo>
    {
        TrackInfo() = default;

        hstring Title() { return m_title; }
        void Title(hstring const& value) { m_title = value; }

        hstring Artist() { return m_artist; }
        void Artist(hstring const& value) { m_artist = value; }

        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage AlbumArt() { return m_albumArt; }
        void AlbumArt(winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage const& value) { m_albumArt = value; }

        hstring ArtworkUrl() { return m_artworkUrl; }
        void ArtworkUrl(hstring const& value) { m_artworkUrl = value; }

        hstring ArtworkMode() { return m_artworkMode; }
        void ArtworkMode(hstring const& value) { m_artworkMode = value; }

        hstring ArtworkTitle() { return m_artworkTitle; }
        void ArtworkTitle(hstring const& value) { m_artworkTitle = value; }

        hstring ArtworkCaption() { return m_artworkCaption; }
        void ArtworkCaption(hstring const& value) { m_artworkCaption = value; }

        hstring ArtworkGlyph() { return m_artworkGlyph; }
        void ArtworkGlyph(hstring const& value) { m_artworkGlyph = value; }

        double ArtworkGlyphOpacity() { return m_artworkGlyphOpacity; }
        void ArtworkGlyphOpacity(double value) { m_artworkGlyphOpacity = value; }

        double ArtworkGlyphSize() { return m_artworkGlyphSize; }
        void ArtworkGlyphSize(double value) { m_artworkGlyphSize = value; }

        winrt::Microsoft::UI::Xaml::Media::Brush ArtworkBackground() { return m_artworkBackground; }
        void ArtworkBackground(winrt::Microsoft::UI::Xaml::Media::Brush const& value) { m_artworkBackground = value; }

        double ImageArtworkOpacity() { return m_imageArtworkOpacity; }
        void ImageArtworkOpacity(double value) { m_imageArtworkOpacity = value; }

        double GeneratedArtworkOpacity() { return m_generatedArtworkOpacity; }
        void GeneratedArtworkOpacity(double value) { m_generatedArtworkOpacity = value; }

        winrt::Windows::Storage::StorageFile File() { return m_file; }
        void File(winrt::Windows::Storage::StorageFile const& value) { m_file = value; }

        hstring FilePath() { return m_filePath; }
        void FilePath(hstring const& value) { m_filePath = value; }

        double DurationSeconds() { return m_durationSeconds; }
        void DurationSeconds(double value) { m_durationSeconds = value; }

        hstring Album() { return m_album; }
        void Album(hstring const& value) { m_album = value; }

        hstring Genre() { return m_genre; }
        void Genre(hstring const& value) { m_genre = value; }

        hstring DateAdded() { return m_dateAdded; }
        void DateAdded(hstring const& value) { m_dateAdded = value; }

        double DateAddedSortKey() { return m_dateAddedSortKey; }
        void DateAddedSortKey(double value) { m_dateAddedSortKey = value; }

        hstring Duration() { return m_duration; }
        void Duration(hstring const& value) { m_duration = value; }

        int32_t Index() { return m_index; }
        void Index(int32_t value) { m_index = value; }

        int64_t CatalogId() { return m_catalogId; }
        void CatalogId(int64_t value) { m_catalogId = value; }
        hstring RemoteId() { return m_remoteId; }
        void RemoteId(hstring const& value) { m_remoteId = value; }

        // Remote catalog identifiers, empty for anything that did not come from
        // the catalog. Distinct from CatalogId, which is a local row id.
        hstring RemoteCatalogId() { return m_remoteCatalogId; }
        void RemoteCatalogId(hstring const& value) { m_remoteCatalogId = value; }

        hstring AlbumCatalogId() { return m_albumCatalogId; }
        void AlbumCatalogId(hstring const& value) { m_albumCatalogId = value; }

        hstring ArtistCatalogId() { return m_artistCatalogId; }
        void ArtistCatalogId(hstring const& value) { m_artistCatalogId = value; }

        hstring SourceKind() { return m_sourceKind; }
        void SourceKind(hstring const& value) { m_sourceKind = value; }

        hstring Provider() { return m_provider; }
        void Provider(hstring const& value) { m_provider = value; }

        hstring SourceUrl() { return m_sourceUrl; }
        void SourceUrl(hstring const& value) { m_sourceUrl = value; }

        hstring SourceLabel() { return m_sourceLabel; }
        void SourceLabel(hstring const& value);

        hstring SourceBadgeText() { return m_sourceBadgeText; }
        winrt::Microsoft::UI::Xaml::Visibility SourceBadgeVisibility() { return m_sourceBadgeVisibility; }

        bool IsLiked() { return m_isLiked; }
        void IsLiked(bool value) { m_isLiked = value; }

        hstring LikeActionText() { return m_isLiked ? L"Unlike" : L"Like"; }

        int32_t TrackCount() { return m_trackCount; }
        void TrackCount(int32_t value) { m_trackCount = value; }

    private:
        hstring m_title;
        hstring m_artist;
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage m_albumArt{ nullptr };
        hstring m_artworkUrl;
        hstring m_artworkMode{ L"track-placeholder" };
        hstring m_artworkTitle;
        hstring m_artworkCaption;
        hstring m_artworkGlyph{ L"\xE8D6" };
        // Zero keeps the centre glyph invisible on every surface that does not
        // opt into one, which is all of them bar the system playlist covers.
        double m_artworkGlyphOpacity{ 0.0 };
        double m_artworkGlyphSize{ 36.0 };
        // Null leaves the item on the palette gradient the artwork border picks.
        winrt::Microsoft::UI::Xaml::Media::Brush m_artworkBackground{ nullptr };
        double m_imageArtworkOpacity{ 0.0 };
        double m_generatedArtworkOpacity{ 1.0 };
        winrt::Windows::Storage::StorageFile m_file{ nullptr };
        hstring m_filePath;
        double m_durationSeconds{ 0.0 };
        hstring m_album;
        hstring m_genre;
        hstring m_dateAdded;
        double m_dateAddedSortKey{ 0.0 };
        hstring m_duration;
        int32_t m_index{ 0 };
        int64_t m_catalogId{ 0 };
        hstring m_remoteId;
        hstring m_remoteCatalogId;
        hstring m_albumCatalogId;
        hstring m_artistCatalogId;
        hstring m_sourceKind;
        hstring m_provider;
        hstring m_sourceUrl;
        hstring m_sourceLabel;
        hstring m_sourceBadgeText;
        winrt::Microsoft::UI::Xaml::Visibility m_sourceBadgeVisibility{
            winrt::Microsoft::UI::Xaml::Visibility::Collapsed };
        bool m_isLiked{ false };
        int32_t m_trackCount{ 0 };
    };
}

namespace winrt::Last_Music_Player::factory_implementation
{
    struct TrackInfo : TrackInfoT<TrackInfo, implementation::TrackInfo>
    {
    };
}

namespace LastMusicPlayer::Backend
{
    using TrackInfo = winrt::Last_Music_Player::TrackInfo;
}
