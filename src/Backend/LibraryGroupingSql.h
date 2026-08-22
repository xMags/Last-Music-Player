#pragma once

#include <cwctype>
#include <string>
#include <string_view>

namespace LastMusicPlayer::Backend::LibraryGroupingSql
{
    enum class GroupKind
    {
        Album,
        Artist,
        Genre
    };

    inline std::string Qualified(
        std::string_view tableReference,
        std::string_view column)
    {
        std::string result{ tableReference };
        if (!result.empty())
        {
            result.push_back('.');
        }
        result.append(column);
        return result;
    }

    inline std::string GroupTitleExpression(
        GroupKind kind,
        std::string_view tableReference = "EffectiveTracks")
    {
        std::string_view column;
        std::string_view unknownLabel;
        std::string_view unknownKey;
        switch (kind)
        {
        case GroupKind::Album:
            column = "Album";
            unknownLabel = "Unknown Album";
            unknownKey = "unknown album";
            break;
        case GroupKind::Artist:
            column = "Artist";
            unknownLabel = "Unknown Artist";
            unknownKey = "unknown artist";
            break;
        case GroupKind::Genre:
            column = "Genre";
            unknownLabel = "Unknown Genre";
            unknownKey = "unknown genre";
            break;
        }

        auto field = Qualified(tableReference, column);
        auto sourceKind = Qualified(tableReference, "SourceKind");
        auto trimmed = "TRIM(" + field + ")";
        return "CASE WHEN COALESCE(" + trimmed + ",'')='' OR LOWER(" + trimmed + ")='"
            + std::string{ unknownKey }
            + "' THEN CASE WHEN " + sourceKind + "='local' THEN '"
            + std::string{ unknownLabel }
            + "' ELSE '' END ELSE " + trimmed + " END";
    }

    inline std::string EligibleTrackPredicate(
        std::string_view tableReference = "EffectiveTracks")
    {
        auto sourceKind = Qualified(tableReference, "SourceKind");
        auto provider = Qualified(tableReference, "Provider");
        auto playCount = Qualified(tableReference, "PlayCount");
        auto lastPlayedOrder = Qualified(tableReference, "LastPlayedOrder");
        auto isLiked = Qualified(tableReference, "IsLiked");
        auto remoteId = Qualified(tableReference, "RemoteId");

        return "(" + sourceKind + "='local' OR (" + provider + "='account' AND ("
            "COALESCE(" + playCount + ",0)>0 OR "
            "COALESCE(" + lastPlayedOrder + ",0)>0 OR "
            "COALESCE(" + isLiked + ",0)=1 OR "
            "EXISTS (SELECT 1 FROM AccountPlaylistTracks apt "
            "JOIN ActiveAccountContext c ON c.SingletonId=1 AND c.RemoteMode='Account' "
            "AND c.AccountId=apt.AccountId WHERE apt.RemoteId=" + remoteId + "))))";
    }

    inline std::string GroupMatchPredicate(
        GroupKind kind,
        std::string_view parameter,
        std::string_view tableReference = "EffectiveTracks")
    {
        return EligibleTrackPredicate(tableReference)
            + " AND (" + GroupTitleExpression(kind, tableReference)
            + ") COLLATE NOCASE = " + std::string{ parameter } + " COLLATE NOCASE";
    }

    inline std::string EligibleTracksCte(GroupKind kind)
    {
        return "WITH EligibleLibraryTracks AS (SELECT e.*, "
            + GroupTitleExpression(kind, "e")
            + " AS GroupTitle FROM EffectiveTracks e WHERE e.IsActive=1 AND "
            + EligibleTrackPredicate("e") + ") ";
    }

    // Ordering and find clauses for the curated collections, playlists and
    // album collections, whose membership rows carry an explicit position.
    // These queries join through an aliased Tracks table, so they cannot reuse
    // the flat EffectiveTracks ordering, which emits unqualified columns.
    inline std::wstring FoldSortName(std::wstring value)
    {
        for (auto& character : value)
        {
            character = static_cast<wchar_t>(std::towlower(character));
        }
        return value;
    }

    // An empty or unrecognised sort keeps the curated position, which is what
    // the collection was arranged as; the named sorts mirror the flat ones.
    inline std::string CollectionOrderClause(
        std::wstring const& sort,
        std::string_view positionColumn)
    {
        auto const normalized = FoldSortName(sort);
        if (normalized == L"title")
        {
            return "ORDER BY t.Title COLLATE NOCASE ASC";
        }
        if (normalized == L"artist")
        {
            return "ORDER BY t.Artist COLLATE NOCASE ASC, t.Title COLLATE NOCASE ASC";
        }
        if (normalized == L"duration")
        {
            return "ORDER BY t.DurationSeconds DESC, t.Title COLLATE NOCASE ASC";
        }
        if (normalized == L"dateadded")
        {
            return "ORDER BY t.DateAddedSortKey DESC, t.Title COLLATE NOCASE ASC";
        }
        return "ORDER BY " + std::string{ positionColumn } + " ASC, t.Title COLLATE NOCASE ASC";
    }

    // LIKE predicate over the aliased title, artist and album columns, bound to
    // one parameter index that the caller supplies and binds. Returns an empty
    // string for empty search text so callers can append it unconditionally.
    inline std::string CollectionSearchClause(
        std::wstring const& searchText,
        int parameterIndex)
    {
        if (searchText.empty())
        {
            return {};
        }

        auto const parameter = "?" + std::to_string(parameterIndex);
        std::string clause = " AND (COALESCE(t.Title,'') LIKE " + parameter + " ESCAPE '!'";
        clause += " OR COALESCE(t.Artist,'') LIKE " + parameter + " ESCAPE '!'";
        clause += " OR COALESCE(t.Album,'') LIKE " + parameter + " ESCAPE '!')";
        return clause;
    }
}
