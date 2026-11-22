#pragma once

// SQL for albums and playlists that more than one part of the database layer
// runs. Copied character for character from earlier versions.
namespace lmp::core::db::collection_sql {

inline constexpr char UpsertAlbumSql[] =
    "INSERT INTO Albums (AlbumKey, Title, Artist, ArtworkUrl, SourceKind, SourceUrl, SourceLabel, CreatedAt, UpdatedAt) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, strftime('%s','now'), strftime('%s','now')) "
    "ON CONFLICT(AlbumKey) DO UPDATE SET "
    "Title=excluded.Title, Artist=excluded.Artist, ArtworkUrl=excluded.ArtworkUrl, "
    "SourceKind=excluded.SourceKind, SourceUrl=excluded.SourceUrl, SourceLabel=excluded.SourceLabel, UpdatedAt=excluded.UpdatedAt;";

inline constexpr char AlbumIdByKeySql[] = "SELECT Id FROM Albums WHERE AlbumKey=?;";

inline constexpr char UpsertPlaylistSql[] =
    "INSERT INTO Playlists (PlaylistKey, Title, Description, ArtworkUrl, SourceKind, Provider, SourceUrl, SourceLabel, CreatedAt, UpdatedAt) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, strftime('%s','now'), strftime('%s','now')) "
    "ON CONFLICT(PlaylistKey) DO UPDATE SET "
    "Title=excluded.Title, Description=excluded.Description, ArtworkUrl=excluded.ArtworkUrl, "
    "SourceKind=excluded.SourceKind, Provider=excluded.Provider, SourceUrl=excluded.SourceUrl, SourceLabel=excluded.SourceLabel, UpdatedAt=excluded.UpdatedAt;";

inline constexpr char PlaylistIdByKeySql[] = "SELECT Id FROM Playlists WHERE PlaylistKey=?;";

} // namespace lmp::core::db::collection_sql
