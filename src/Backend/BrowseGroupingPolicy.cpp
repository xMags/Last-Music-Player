#include "pch.h"
#include "Backend/BrowseGroupingPolicy.h"

#include <algorithm>
#include <cwctype>
#include <unordered_map>
#include <unordered_set>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        std::wstring Fold(std::wstring value)
        {
            auto const first = std::find_if_not(value.begin(), value.end(), [](wchar_t character)
            {
                return std::iswspace(character) != 0;
            });
            auto const last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character)
            {
                return std::iswspace(character) != 0;
            }).base();
            value = first < last ? std::wstring(first, last) : std::wstring{};
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        bool Contains(std::wstring const& field, std::wstring const& foldedQuery)
        {
            return !field.empty() && Fold(field).find(foldedQuery) != std::wstring::npos;
        }

        bool MatchesSong(BrowseResultInput const& input, std::wstring const& query)
        {
            return Contains(input.Title, query)
                || Contains(input.Artist, query)
                || Contains(input.Album, query);
        }

        bool MatchesAlbum(BrowseResultInput const& input, std::wstring const& query)
        {
            return Contains(input.Album, query)
                || Contains(input.Artist, query)
                || Contains(input.Year, query);
        }

        std::wstring JoinedSubtitle(std::wstring const& first, std::wstring const& second)
        {
            if (first.empty())
            {
                return second;
            }
            if (second.empty())
            {
                return first;
            }
            return first + L" · " + second;
        }

        std::wstring CountLabel(std::size_t count, wchar_t const* singular, wchar_t const* plural)
        {
            return std::to_wstring(count) + L" " + (count == 1 ? singular : plural);
        }

        BrowseResultGroup Songs(
            std::vector<BrowseResultInput> const& inputs,
            std::wstring const& query,
            bool preserveUrlLookupSong)
        {
            BrowseResultGroup group{ BrowseResultKind::Song };
            for (std::size_t index = 0; index < inputs.size(); ++index)
            {
                auto const& input = inputs[index];
                if (input.Kind != BrowseResultKind::Song
                    || (!preserveUrlLookupSong && !MatchesSong(input, query)))
                {
                    continue;
                }
                group.Items.push_back({
                    BrowseResultKind::Song,
                    input.Title,
                    JoinedSubtitle(input.Artist, input.Album),
                    input.Duration,
                    input.Key,
                    index,
                    { index }
                });
            }
            return group;
        }

        BrowseResultGroup Albums(
            std::vector<BrowseResultInput> const& inputs,
            std::wstring const& query)
        {
            BrowseResultGroup group{ BrowseResultKind::Album };
            std::unordered_map<std::wstring, std::size_t> positions;
            for (std::size_t index = 0; index < inputs.size(); ++index)
            {
                auto const& input = inputs[index];
                if (input.Kind != BrowseResultKind::Song
                    || input.Album.empty()
                    || !MatchesAlbum(input, query))
                {
                    continue;
                }

                auto const identity = Fold(input.Album) + L"\n" + Fold(input.Artist);
                auto const found = positions.find(identity);
                if (found != positions.end())
                {
                    group.Items[found->second].MemberIndices.push_back(index);
                    continue;
                }

                auto subtitle = JoinedSubtitle(input.Artist, input.Year);
                positions.emplace(identity, group.Items.size());
                group.Items.push_back({
                    BrowseResultKind::Album,
                    input.Album,
                    std::move(subtitle),
                    {},
                    input.Album,
                    index,
                    { index }
                });
            }
            return group;
        }

        BrowseResultGroup Playlists(
            std::vector<BrowseResultInput> const& inputs,
            std::wstring const& query)
        {
            BrowseResultGroup group{ BrowseResultKind::Playlist };
            for (std::size_t index = 0; index < inputs.size(); ++index)
            {
                auto const& input = inputs[index];
                if (input.Kind != BrowseResultKind::Playlist || !Contains(input.Title, query))
                {
                    continue;
                }
                group.Items.push_back({
                    BrowseResultKind::Playlist,
                    input.Title,
                    CountLabel(input.Count, L"track", L"tracks"),
                    {},
                    input.Key,
                    index,
                    { index }
                });
            }
            return group;
        }

        BrowseResultGroup Artists(
            std::vector<BrowseResultInput> const& inputs,
            std::wstring const& query)
        {
            BrowseResultGroup group{ BrowseResultKind::Artist };
            std::unordered_map<std::wstring, std::size_t> positions;
            std::vector<std::unordered_set<std::wstring>> albums;
            for (std::size_t index = 0; index < inputs.size(); ++index)
            {
                auto const& input = inputs[index];
                if (input.Kind != BrowseResultKind::Song
                    || input.Artist.empty()
                    || !Contains(input.Artist, query))
                {
                    continue;
                }

                auto const identity = Fold(input.Artist);
                auto const found = positions.find(identity);
                if (found != positions.end())
                {
                    auto const position = found->second;
                    group.Items[position].MemberIndices.push_back(index);
                    auto const album = Fold(input.Album);
                    if (!album.empty())
                    {
                        albums[position].insert(album);
                    }
                    continue;
                }

                positions.emplace(identity, group.Items.size());
                group.Items.push_back({
                    BrowseResultKind::Artist,
                    input.Artist,
                    {},
                    {},
                    input.Artist,
                    index,
                    { index }
                });
                albums.emplace_back();
                auto const album = Fold(input.Album);
                if (!album.empty())
                {
                    albums.back().insert(album);
                }
            }

            for (std::size_t index = 0; index < group.Items.size(); ++index)
            {
                group.Items[index].Subtitle = CountLabel(albums[index].size(), L"album", L"albums");
            }
            return group;
        }
    }

    GroupedBrowseResults BuildGroupedBrowseResults(
        std::vector<BrowseResultInput> const& inputs,
        std::wstring const& query,
        bool preserveUrlLookupSong)
    {
        GroupedBrowseResults result;
        auto const foldedQuery = Fold(query);
        if (foldedQuery.empty())
        {
            return result;
        }

        std::vector<BrowseResultGroup> groups;
        groups.push_back(Songs(inputs, foldedQuery, preserveUrlLookupSong));
        groups.push_back(Albums(inputs, foldedQuery));
        groups.push_back(Playlists(inputs, foldedQuery));
        groups.push_back(Artists(inputs, foldedQuery));
        for (auto& group : groups)
        {
            if (group.Items.empty())
            {
                continue;
            }
            result.DisplayedEntityCount += group.Items.size();
            result.Groups.push_back(std::move(group));
        }
        return result;
    }

    BrowseViewPreference ParseBrowseViewPreference(std::wstring const& value) noexcept
    {
        return Fold(value) == L"list" ? BrowseViewPreference::List : BrowseViewPreference::Grid;
    }

    std::wstring BrowseViewPreferenceTag(BrowseViewPreference value)
    {
        return value == BrowseViewPreference::List ? L"list" : L"grid";
    }
}
