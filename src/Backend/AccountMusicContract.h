#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    // The service-facing representation shared by account history, likes,
    // playlists, and library parsing. It intentionally contains no WinUI type,
    // which keeps the wire contract independently testable.
    struct PortableAccountTrack
    {
        std::wstring Id;
        std::wstring Title;
        std::wstring Artist;
        std::wstring Album;
        std::wstring Genre;
        std::wstring ArtworkUrl;
        std::wstring SourceKind;
        std::wstring Provider;
        std::wstring SourceUrl;
        std::wstring CatalogId;
        std::wstring AlbumCatalogId;
        std::wstring ArtistCatalogId;
        double DurationSec{};
    };

    struct AccountHistoryEventDto
    {
        std::wstring EventId;
        PortableAccountTrack Track;
        std::wstring PlayedAt;
        double PositionSec{};
    };

    struct AccountLibraryTrackDto
    {
        PortableAccountTrack Track;
        bool Liked{};
        std::int64_t PlayCount{};
        std::wstring AddedAt;
        double LegacyAddedSortKey{};
        std::wstring FirstPlayedAt;
        std::wstring LastPlayedAt;
        double LastPositionSec{};
    };

    struct AccountPlaylistDto
    {
        std::wstring Id;
        std::wstring Name;
        std::wstring Description;
        std::wstring SourceUrl;
        std::wstring UpdatedAt;
        std::vector<std::wstring> TrackIds;
    };

    struct ParsedAccountLibrary
    {
        bool Valid{};
        std::vector<AccountLibraryTrackDto> Tracks;
        std::vector<AccountPlaylistDto> Playlists;
    };

    enum class PortablePreferenceKey
    {
        Theme,
        Accent,
        GaplessPlayback,
        DataSaver,
        Storefront,
    };

    struct PortablePreferences
    {
        std::optional<std::wstring> Theme;
        std::optional<std::wstring> Accent;
        std::optional<bool> GaplessPlayback;
        std::optional<bool> DataSaver;
        std::optional<std::wstring> Storefront;
    };

    [[nodiscard]] bool IsValidPortableTrack(PortableAccountTrack const& track) noexcept;
    [[nodiscard]] winrt::hstring SerializePortableTrack(PortableAccountTrack const& track);
    [[nodiscard]] std::optional<PortableAccountTrack> ParsePortableTrack(
        winrt::hstring const& payload) noexcept;
    [[nodiscard]] winrt::hstring SerializeHistoryBatch(
        std::vector<AccountHistoryEventDto> const& events);
    [[nodiscard]] winrt::hstring WrapTrackRequest(winrt::hstring const& portableTrackJson);
    [[nodiscard]] winrt::hstring SerializePlaylistReplacement(
        winrt::hstring const& name,
        std::vector<PortableAccountTrack> const& tracks);
    [[nodiscard]] ParsedAccountLibrary ParseAccountLibrary(
        winrt::hstring const& payload) noexcept;
    [[nodiscard]] std::vector<std::wstring> ParseAcknowledgedHistoryEvents(
        winrt::hstring const& payload,
        std::vector<std::wstring> const& submittedEventIds) noexcept;

    [[nodiscard]] wchar_t const* PortablePreferenceName(PortablePreferenceKey key) noexcept;
    [[nodiscard]] std::optional<PortablePreferenceKey> ParsePortablePreferenceName(
        std::wstring const& name) noexcept;
    [[nodiscard]] PortablePreferences ParsePortablePreferences(
        winrt::hstring const& payload) noexcept;
    [[nodiscard]] winrt::hstring SerializePortablePreferences(
        PortablePreferences const& preferences);
    [[nodiscard]] std::unordered_map<PortablePreferenceKey, std::wstring>
        PortablePreferenceValues(PortablePreferences const& preferences);
    [[nodiscard]] PortablePreferences PortablePreferencesFromValues(
        std::unordered_map<PortablePreferenceKey, std::wstring> const& values) noexcept;
    [[nodiscard]] std::optional<std::wstring> ValidatePortablePreferenceValue(
        PortablePreferenceKey key,
        std::wstring const& jsonValue) noexcept;
}
