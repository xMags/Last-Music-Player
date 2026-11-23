#pragma once

#include "core/account/LibraryArtistContract.h"
#include "core/async/Result.h"
#include "core/catalog/CatalogModels.h"

#include <QByteArray>
#include <QString>
#include <QUrlQuery>

#include <optional>

// Readers for detail pages: a catalogue album, playlist or artist; a
// provider artist, one of its sections, or a provider album; an external
// playlist. Items that cannot be used are skipped; a page fails only when it
// is not JSON or lacks the item it is about. Pure functions, no I/O.
namespace lmp::core::catalog {

// The capability names a provider lists when it serves artist and album
// pages by identity.
inline constexpr QLatin1StringView ArtistDetailCapability{"artist-detail-v1"};
inline constexpr QLatin1StringView AlbumDetailCapability{"album-detail-v1"};

// The one identity a lookup sends, in this order: the catalogue id, else the
// source address, else the name. Sending only the chosen one keeps a failed
// id from turning into an unrelated result of the same name. An album looked
// up by name also sends `artist`, which narrows a title several releases
// share. Nothing when the chosen value is too long, holds control
// characters, or is an address that may not be kept.
[[nodiscard]] std::optional<QUrlQuery> detailIdentityQuery(const account::LibraryArtistIdentity& identity,
                                                           const QString& artist = {});

// True when the provider answer lists `capability`, directly or under one of
// its providers.
[[nodiscard]] bool providerSupportsCapability(const QByteArray& payload, QLatin1StringView capability);

// /v1/music/catalog/<type>s/<id>: the item under its type's key, its songs,
// and for an artist its sections (latest release, top songs, albums, singles
// and EPs marked as such, similar artists), each kept when it has items.
[[nodiscard]] Result<Detail> parseCatalogDetail(const QByteArray& payload, ResourceType type, const SourceTags& tags);

// A provider artist page: the artist (name falling back to the identity's),
// its biography as plain text, and the sections top songs, albums, singles
// and EPs, similar artists.
[[nodiscard]] Result<Detail> parseProviderArtistDetail(const QByteArray& payload,
                                                       const account::LibraryArtistIdentity& identity);

// One provider artist section in full. Fails when the answer names another
// type than the section holds.
[[nodiscard]] Result<Shelf> parseProviderArtistSection(const QByteArray& payload, ArtistSection section,
                                                       const QString& title);

// A provider album: its release and songs, opened again by the identity it
// answers with.
[[nodiscard]] Result<Detail> parseProviderAlbumDetail(const QByteArray& payload,
                                                      const account::LibraryArtistIdentity& identity);

// An external playlist and its songs. Fails when the answer is about
// another playlist.
[[nodiscard]] Result<Detail> parseExternalPlaylistDetail(const QByteArray& payload, const QString& id,
                                                         const SourceTags& tags);

// The album a song belongs to, as the service picks it: the catalogue's own
// album (by its numeric id) or a provider release (by its identity, with
// its artist to narrow a name lookup).
struct AlbumTarget {
    // Set for the catalogue's own album.
    QString catalogAlbumId;
    account::LibraryArtistIdentity identity;
    QString artist;
};

// The /v1/music/catalog/songs/album answer; nothing when it names no usable
// album (a catalogue album needs a numeric id; names are cut to 240
// characters, the source tag to 64).
[[nodiscard]] std::optional<AlbumTarget> parseSongAlbum(const QByteArray& payload, const SourceTags& tags);

} // namespace lmp::core::catalog
