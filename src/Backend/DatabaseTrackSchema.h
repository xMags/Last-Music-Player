#pragma once

namespace LastMusicPlayer::Backend::DatabaseTrackSchema
{
    inline constexpr char AddRemoteCatalogIdSql[] =
        "ALTER TABLE Tracks ADD COLUMN RemoteCatalogId TEXT;";
    inline constexpr char AddAlbumCatalogIdSql[] =
        "ALTER TABLE Tracks ADD COLUMN AlbumCatalogId TEXT;";
    inline constexpr char AddArtistCatalogIdSql[] =
        "ALTER TABLE Tracks ADD COLUMN ArtistCatalogId TEXT;";
}
