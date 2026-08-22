#pragma once

#include "Backend/DatabaseEngine.h"

#include <filesystem>
#include <string>

namespace LastMusicPlayer::Backend::DatabaseDetail
{
    std::filesystem::path AppDataDirectory();
    std::string WideToUtf8(std::wstring const& wide);
    std::wstring Utf8ToWide(char const* utf8);
    void BindText(sqlite3_stmt* stmt, int index, std::wstring const& value);
    std::wstring ColumnText(sqlite3_stmt* stmt, int index);
    std::wstring ToLowerInvariant(std::wstring value);
    bool Exec(sqlite3* db, char const* sql);
    void TryExec(sqlite3* db, char const* sql);
    std::wstring TimestampKeyPart();

    struct Statement
    {
        sqlite3_stmt* value{};

        Statement(sqlite3* db, char const* sql);
        ~Statement();
        Statement(Statement const&) = delete;
        Statement& operator=(Statement const&) = delete;

        explicit operator bool() const;
    };

    bool TracksTableHasLegacyFilePathUnique(sqlite3* db);
    bool RebuildTracksTableWithoutFilePathUnique(sqlite3* db);
    bool NormalizeRemoteTrackSourceKeys(sqlite3* db);
    std::wstring StoredFilePathFor(TrackInfo const& track, std::wstring const& sourceKind, std::wstring const& sourceKey);
    winrt::hstring SourceLabelFor(std::wstring const& sourceKind);
    void ApplyArtworkUrl(TrackInfo& track, winrt::hstring const& artworkUrl);
    TrackInfo TrackFromStatement(sqlite3_stmt* stmt);

    extern char const kTrackColumns[];
    extern char const kJoinedTrackColumns[];

    std::string TrackOrderClause(std::wstring const& filter, std::wstring const& sort);
    std::string TrackFilterClause(std::wstring const& filter);
    std::string GroupFilterClause(std::wstring const& groupKind);

    // ORDER BY for a curated collection (a playlist or an album collection)
    // whose membership rows carry an explicit position. These queries join
    // through an aliased Tracks table, so they cannot reuse TrackOrderClause,
    // which emits unqualified column names for the flat EffectiveTracks view.
    //
    // An empty or unrecognised sort keeps the curated position, which is what
    // the collection was arranged as; the named sorts mirror TrackOrderClause.
    std::string CollectionOrderClause(std::wstring const& sort, char const* positionColumn);

    // LIKE predicate over the aliased title, artist and album columns, bound to
    // one parameter index that the caller supplies and binds. Returns an empty
    // string for empty search text so callers can append it unconditionally.
    std::string CollectionSearchClause(std::wstring const& searchText, int parameterIndex);
}