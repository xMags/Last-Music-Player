#pragma once

// SQL for the catalogue id columns of Tracks. Copied character for character
// from earlier versions: existing databases depend on it.

namespace lmp::core::db::track_schema {
    inline constexpr char AddRemoteCatalogIdSql[] =
        "ALTER TABLE Tracks ADD COLUMN RemoteCatalogId TEXT;";
    inline constexpr char AddAlbumCatalogIdSql[] =
        "ALTER TABLE Tracks ADD COLUMN AlbumCatalogId TEXT;";
    inline constexpr char AddArtistCatalogIdSql[] =
        "ALTER TABLE Tracks ADD COLUMN ArtistCatalogId TEXT;";
} // namespace lmp::core::db::track_schema
