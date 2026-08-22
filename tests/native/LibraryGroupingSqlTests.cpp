#include "pch.h"

#include "Backend/LibraryGroupingSql.h"
#include "ThirdParty/sqlite/sqlite3.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace grouping = LastMusicPlayer::Backend::LibraryGroupingSql;

namespace
{
    void Expect(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void Execute(sqlite3* database, char const* sql)
    {
        char* error{};
        auto result = sqlite3_exec(database, sql, nullptr, nullptr, &error);
        if (result == SQLITE_OK)
        {
            return;
        }

        std::string message = error ? error : "SQLite execution failed";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }

    class TestDatabase final
    {
    public:
        TestDatabase()
        {
            if (sqlite3_open(":memory:", &m_database) != SQLITE_OK)
            {
                throw std::runtime_error("Could not open the test database");
            }

            Execute(m_database,
                "CREATE TABLE EffectiveTracks ("
                "Id INTEGER PRIMARY KEY, SourceKind TEXT, Provider TEXT, RemoteId TEXT, "
                "Album TEXT, Artist TEXT, Genre TEXT, PlayCount INTEGER, "
                "LastPlayedOrder INTEGER, IsLiked INTEGER, IsActive INTEGER, ArtworkUrl TEXT);"
                "CREATE TABLE ActiveAccountContext ("
                "SingletonId INTEGER PRIMARY KEY, RemoteMode TEXT, AccountId TEXT);"
                "CREATE TABLE AccountPlaylistTracks ("
                "AccountId TEXT, PlaylistId TEXT, RemoteId TEXT, TrackOrder INTEGER);"
                "INSERT INTO ActiveAccountContext VALUES (1, 'Account', 'account-a');"
                "INSERT INTO EffectiveTracks VALUES "
                "(1, 'local', 'local', NULL, 'Shared Album', 'Local Artist', 'Rock', 0, 0, 0, 1, ''),"
                "(2, 'local', 'local', NULL, 'shared album', 'local artist', 'rock', 0, 0, 0, 1, ''),"
                "(3, 'local', 'local', NULL, '', '', '', 0, 0, 0, 1, ''),"
                "(4, 'remote', 'account', 'r-played', 'Played Album', 'Played Artist', 'Pop', 1, 0, 0, 1, ''),"
                "(5, 'remote', 'account', 'r-history', 'History Album', 'History Artist', 'Jazz', 0, 1, 0, 1, ''),"
                "(6, 'remote', 'account', 'r-liked', 'Liked Album', 'Liked Artist', 'Soul', 0, 0, 1, 1, ''),"
                "(7, 'remote', 'account', 'r-playlist', 'Playlist Album', 'Playlist Artist', 'Folk', 0, 0, 0, 1, ''),"
                "(8, 'remote', 'account', 'r-inert', 'Inert Album', 'Inert Artist', 'Ambient', 0, 0, 0, 1, ''),"
                "(9, 'remote', 'account', 'r-blank', '', '', '', 1, 0, 0, 1, ''),"
                "(10, 'remote', 'account', 'r-unknown', 'Unknown Album', 'Unknown Artist', 'Unknown Genre', 1, 0, 0, 1, ''),"
                "(11, 'remote', 'api-key', 'r-api', 'API Album', 'API Artist', 'Dance', 1, 1, 1, 1, ''),"
                "(12, 'local', 'local', NULL, 'Inactive Album', 'Inactive Artist', 'Metal', 0, 0, 0, 0, '');"
                "INSERT INTO AccountPlaylistTracks VALUES ('account-a', 'playlist-a', 'r-playlist', 0);");
        }

        ~TestDatabase()
        {
            sqlite3_close(m_database);
        }

        TestDatabase(TestDatabase const&) = delete;
        TestDatabase& operator=(TestDatabase const&) = delete;

        sqlite3* Get() const noexcept
        {
            return m_database;
        }

    private:
        sqlite3* m_database{};
    };

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::vector<std::pair<std::string, int>> GroupCounts(
        sqlite3* database,
        grouping::GroupKind kind)
    {
        auto sql = grouping::EligibleTracksCte(kind)
            + "SELECT GroupTitle, COUNT(*) FROM EligibleLibraryTracks "
            "WHERE GroupTitle<>'' GROUP BY GroupTitle COLLATE NOCASE "
            "ORDER BY COUNT(*) DESC, GroupTitle COLLATE NOCASE ASC;";

        sqlite3_stmt* statement{};
        if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        {
            throw std::runtime_error(sqlite3_errmsg(database));
        }

        std::vector<std::pair<std::string, int>> groups;
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            auto title = reinterpret_cast<char const*>(sqlite3_column_text(statement, 0));
            groups.emplace_back(title ? title : "", sqlite3_column_int(statement, 1));
        }
        sqlite3_finalize(statement);
        return groups;
    }

    std::vector<int> GroupTrackIds(
        sqlite3* database,
        grouping::GroupKind kind,
        char const* groupTitle)
    {
        auto sql = "SELECT Id FROM EffectiveTracks WHERE IsActive=1 AND "
            + grouping::GroupMatchPredicate(kind, "?1")
            + " ORDER BY Id;";

        sqlite3_stmt* statement{};
        if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        {
            throw std::runtime_error(sqlite3_errmsg(database));
        }
        sqlite3_bind_text(statement, 1, groupTitle, -1, SQLITE_TRANSIENT);

        std::vector<int> ids;
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            ids.push_back(sqlite3_column_int(statement, 0));
        }
        sqlite3_finalize(statement);
        return ids;
    }

    std::map<std::string, int> NormalizedCounts(
        std::vector<std::pair<std::string, int>> const& groups)
    {
        std::map<std::string, int> result;
        for (auto const& [title, count] : groups)
        {
            result.emplace(Lower(title), count);
        }
        return result;
    }

    void TestAccountUnionAndLocalUnknownGroups(sqlite3* database)
    {
        auto albums = GroupCounts(database, grouping::GroupKind::Album);
        auto counts = NormalizedCounts(albums);

        Expect(albums.size() == 6, "albums should include local tracks and the account cloud union only");
        Expect(Lower(albums.front().first) == "shared album" && albums.front().second == 2,
            "groups should sort by track count before title");
        Expect(counts["shared album"] == 2, "case variants should share one album group");
        Expect(counts["played album"] == 1, "played account tracks should be grouped");
        Expect(counts["history album"] == 1, "account history tracks should be grouped");
        Expect(counts["liked album"] == 1, "liked account tracks should be grouped");
        Expect(counts["playlist album"] == 1, "account playlist tracks should be grouped");
        Expect(counts["unknown album"] == 1, "an untagged local file should keep the Unknown Album group");
        Expect(!counts.contains("inert album"), "unreferenced account catalog tracks should stay out");
        Expect(!counts.contains("api album"), "API-key search tracks should not become account library groups");
        Expect(!counts.contains("inactive album"), "inactive local files should stay out");
    }

    void TestCloudUnknownLabelsAreDropped(sqlite3* database)
    {
        for (auto kind : { grouping::GroupKind::Album, grouping::GroupKind::Artist, grouping::GroupKind::Genre })
        {
            auto counts = NormalizedCounts(GroupCounts(database, kind));
            auto unknown = kind == grouping::GroupKind::Album
                ? "unknown album"
                : kind == grouping::GroupKind::Artist
                    ? "unknown artist"
                    : "unknown genre";
            Expect(counts[unknown] == 1,
                "an Unknown group should contain the local untagged file only");
        }
    }

    void TestGroupCardsAndDetailsUseTheSameMembership(sqlite3* database)
    {
        auto shared = GroupTrackIds(database, grouping::GroupKind::Album, "SHARED ALBUM");
        Expect(shared == std::vector<int>({ 1, 2 }),
            "detail matching should use the card's case-insensitive album key");

        auto unknownAlbum = GroupTrackIds(database, grouping::GroupKind::Album, "Unknown Album");
        Expect(unknownAlbum == std::vector<int>({ 3 }),
            "the Unknown Album detail should not pull in cloud placeholders");

        auto playlistAlbum = GroupTrackIds(database, grouping::GroupKind::Album, "Playlist Album");
        Expect(playlistAlbum == std::vector<int>({ 7 }),
            "a playlist-only account track should appear in its derived detail");

        auto unknownArtist = GroupTrackIds(database, grouping::GroupKind::Artist, "Unknown Artist");
        auto unknownGenre = GroupTrackIds(database, grouping::GroupKind::Genre, "Unknown Genre");
        Expect(unknownArtist == std::vector<int>({ 3 }),
            "the Unknown Artist detail should contain local untagged files only");
        Expect(unknownGenre == std::vector<int>({ 3 }),
            "the Unknown Genre detail should contain local untagged files only");
    }

    // The curated collection branches used to ignore both the caller's sort and
    // its find text and always hand back the stored order. These run the real
    // clauses against a playlist so a regression shows up as wrong rows, not as
    // a differing SQL string.
    class CollectionDatabase final
    {
    public:
        CollectionDatabase()
        {
            if (sqlite3_open(":memory:", &m_database) != SQLITE_OK)
            {
                throw std::runtime_error("failed to open the collection test database");
            }
            Execute(m_database,
                "CREATE TABLE Tracks("
                "Id INTEGER PRIMARY KEY, Title TEXT, Artist TEXT, Album TEXT,"
                "DurationSeconds REAL, DateAddedSortKey REAL, IsActive INTEGER);"
                "CREATE TABLE Playlists(Id INTEGER PRIMARY KEY, PlaylistKey TEXT);"
                "CREATE TABLE PlaylistTracks(PlaylistId INTEGER, TrackId INTEGER, TrackOrder INTEGER);"

                "INSERT INTO Tracks VALUES"
                "(1,'Zephyr','Ada','Night Drive',240,300,1),"
                "(2,'Anchor','Ada','Night Drive',100,100,1),"
                "(3,'Marigold','Bo','Sunroom',180,200,1),"
                "(4,'Dormant','Cy','Sunroom',300,400,0);"

                "INSERT INTO Playlists VALUES(1,'mix');"
                "INSERT INTO PlaylistTracks VALUES(1,3,0),(1,1,1),(1,2,2),(1,4,3);");
        }

        ~CollectionDatabase() { sqlite3_close(m_database); }
        CollectionDatabase(CollectionDatabase const&) = delete;
        CollectionDatabase& operator=(CollectionDatabase const&) = delete;

        [[nodiscard]] sqlite3* Get() const { return m_database; }

    private:
        sqlite3* m_database{};
    };

    std::vector<std::string> PlaylistTitles(
        sqlite3* database,
        std::wstring const& sort,
        std::wstring const& search)
    {
        auto const searchClause = grouping::CollectionSearchClause(search, 3);
        auto sql = std::string(
            "SELECT t.Title FROM PlaylistTracks pt "
            "JOIN Playlists p ON p.Id=pt.PlaylistId "
            "JOIN Tracks t ON t.Id=pt.TrackId "
            "WHERE p.PlaylistKey=?1 AND (?2=0 OR t.IsActive=1)")
            + searchClause + " "
            + grouping::CollectionOrderClause(sort, "pt.TrackOrder") + ";";

        sqlite3_stmt* statement{};
        if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        {
            throw std::runtime_error(sqlite3_errmsg(database));
        }
        sqlite3_bind_text(statement, 1, "mix", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 2, 1);
        if (!searchClause.empty())
        {
            auto pattern = "%" + std::string(search.begin(), search.end()) + "%";
            sqlite3_bind_text(statement, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
        }

        std::vector<std::string> titles;
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            auto title = reinterpret_cast<char const*>(sqlite3_column_text(statement, 0));
            titles.emplace_back(title ? title : "");
        }
        sqlite3_finalize(statement);
        return titles;
    }

    void TestCollectionOrderAndSearch(sqlite3* database)
    {
        Expect(PlaylistTitles(database, L"", L"")
            == std::vector<std::string>({ "Marigold", "Zephyr", "Anchor" }),
            "an empty sort should keep the curated playlist order");
        Expect(PlaylistTitles(database, L"Custom", L"")
            == std::vector<std::string>({ "Marigold", "Zephyr", "Anchor" }),
            "an unrecognised sort should fall back to the curated order");
        Expect(PlaylistTitles(database, L"Title", L"")
            == std::vector<std::string>({ "Anchor", "Marigold", "Zephyr" }),
            "Title should sort the collection alphabetically");
        Expect(PlaylistTitles(database, L"Artist", L"")
            == std::vector<std::string>({ "Anchor", "Zephyr", "Marigold" }),
            "Artist should sort by artist and then title");
        Expect(PlaylistTitles(database, L"Duration", L"")
            == std::vector<std::string>({ "Zephyr", "Marigold", "Anchor" }),
            "Duration should put the longest track first");
        Expect(PlaylistTitles(database, L"DateAdded", L"")
            == std::vector<std::string>({ "Zephyr", "Marigold", "Anchor" }),
            "DateAdded should put the newest track first");

        Expect(PlaylistTitles(database, L"Title", L"night")
            == std::vector<std::string>({ "Anchor", "Zephyr" }),
            "find should match the album column, case-insensitively");
        Expect(PlaylistTitles(database, L"Title", L"ada")
            == std::vector<std::string>({ "Anchor", "Zephyr" }),
            "find should match the artist column");
        Expect(PlaylistTitles(database, L"Title", L"marig")
            == std::vector<std::string>({ "Marigold" }),
            "find should match a partial title");
        Expect(PlaylistTitles(database, L"Title", L"dormant").empty(),
            "an inactive member should stay out of the results");

        Expect(grouping::CollectionSearchClause(L"", 3).empty(),
            "empty find text should append no clause at all");
    }
}

int main()
{
    try
    {
        TestDatabase database;
        TestAccountUnionAndLocalUnknownGroups(database.Get());
        TestCloudUnknownLabelsAreDropped(database.Get());
        TestGroupCardsAndDetailsUseTheSameMembership(database.Get());

        CollectionDatabase collections;
        TestCollectionOrderAndSearch(collections.Get());
        std::cout << "LibraryGroupingSqlTests passed" << std::endl;
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << "LibraryGroupingSqlTests failed: " << error.what() << std::endl;
        return 1;
    }
}
