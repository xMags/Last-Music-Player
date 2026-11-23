#pragma once

#include <QString>

#include <optional>

#include "core/account/LibraryArtistContract.h"
#include "core/catalog/CatalogModels.h"
#include "ui/navigation/Route.h"

// Which page a catalogue card opens, and how a page's route names what it
// shows. One place, so Home, Browse, search and the pages agree.
//
// Routes and their parameters:
// - CatalogDetail: "type" (album, playlist, artist), "id" (catalogue id).
// - SourceAlbum: "provider", "catalogId", "sourceUrl", "name", "artist";
//   with "finding" = "1" the album of a song still to be found, whose
//   "title", "album", "albumCatalogId" and "durationMs" describe the song.
// - SourceArtist: "provider", "catalogId", "sourceUrl", "name", and a
//   catalogue artist id to try first in "catalogArtistId".
// - ExternalPlaylist: "id".
// - Chart: "region", "kind", "id", "type".
// - Shelf: "key" (a Home shelf key).
namespace lmp::ui::catalog_routes {

// The page a card opens; nothing for a song (it plays).
[[nodiscard]] std::optional<Route> routeForItem(const core::catalog::CatalogItem& item);
[[nodiscard]] Route routeForChart(const core::catalog::ChartRef& chart);
[[nodiscard]] std::optional<core::catalog::ChartRef> chartFromRoute(const Route& route);
[[nodiscard]] Route routeForAlbumIdentity(const core::account::LibraryArtistIdentity& identity, const QString& artist);
[[nodiscard]] core::account::LibraryArtistIdentity identityFromRoute(const Route& route);
// The page that finds a song's album first.
[[nodiscard]] Route routeFindingAlbum(const core::Track& track);
[[nodiscard]] Route routeForShelf(const QString& key);

} // namespace lmp::ui::catalog_routes
