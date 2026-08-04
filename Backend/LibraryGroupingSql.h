#pragma once

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
}
