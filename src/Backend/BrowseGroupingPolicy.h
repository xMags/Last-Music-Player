#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    enum class BrowseResultKind
    {
        Song,
        Album,
        Playlist,
        Artist
    };

    struct BrowseResultInput
    {
        BrowseResultKind Kind{ BrowseResultKind::Song };
        std::wstring Title;
        std::wstring Artist;
        std::wstring Album;
        std::wstring Year;
        std::wstring Duration;
        std::wstring Key;
        std::size_t Count{};
    };

    struct BrowseResultEntity
    {
        BrowseResultKind Kind{ BrowseResultKind::Song };
        std::wstring Title;
        std::wstring Subtitle;
        std::wstring Meta;
        std::wstring Key;
        std::size_t RepresentativeIndex{};
        std::vector<std::size_t> MemberIndices;
    };

    struct BrowseResultGroup
    {
        BrowseResultKind Kind{ BrowseResultKind::Song };
        std::vector<BrowseResultEntity> Items;
    };

    struct GroupedBrowseResults
    {
        std::vector<BrowseResultGroup> Groups;
        std::size_t DisplayedEntityCount{};
    };

    [[nodiscard]] GroupedBrowseResults BuildGroupedBrowseResults(
        std::vector<BrowseResultInput> const& inputs,
        std::wstring const& query,
        bool preserveUrlLookupSong);

    enum class BrowseViewPreference
    {
        Grid,
        List
    };

    [[nodiscard]] BrowseViewPreference ParseBrowseViewPreference(std::wstring const& value) noexcept;
    [[nodiscard]] std::wstring BrowseViewPreferenceTag(BrowseViewPreference value);
}
