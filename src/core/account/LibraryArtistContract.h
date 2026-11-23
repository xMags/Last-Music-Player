#pragma once

#include "core/account/AccountMusicContract.h"

#include <QByteArray>
#include <QLatin1StringView>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <optional>

// The account service's artist list and follow calls, read and built as
// earlier versions did: a page is accepted whole or not at all.
namespace lmp::core::account {

inline constexpr QLatin1StringView LibraryArtistsCapability{"library-artists-v1"};
inline constexpr qsizetype MaxLibraryArtistSearchCharacters = 120;
inline constexpr int MaxLibraryArtistPageSize = 100;
inline constexpr int DefaultLibraryArtistPageSize = 50;

enum class LibraryArtistView { Default, Followed, All };

struct LibraryArtistListRequest {
    LibraryArtistView view = LibraryArtistView::Default;
    QString query;
    int limit = DefaultLibraryArtistPageSize;
    QString cursor;
};

// Where the artist's detail page is looked up.
struct LibraryArtistIdentity {
    QString catalogId;
    QString sourceUrl;
    QString name;
    QString provider;
};

struct LibraryArtist {
    // The service's public id: the only id the follow and song calls take.
    QString id;
    QString name;
    QString provider;
    QString providerCatalogId;
    QString providerSourceUrl;
    // An http(s) address or an account cover link; may be empty.
    QString artworkUrl;
    QString heroArtworkUrl;
    qint64 trackCount = 0;
    qint64 albumCount = 0;
    qint64 playCount = 0;
    bool followed = false;
    // The service is still matching the artist's songs.
    bool resolving = false;
    LibraryArtistIdentity detailIdentity;
};

struct LibraryArtistPage {
    bool valid = false;
    QList<LibraryArtist> artists;
    // Empty on the last page.
    QString nextCursor;
    std::optional<qint64> totalCount;
};

// "default", "followed", "all".
[[nodiscard]] QLatin1StringView libraryArtistViewName(LibraryArtistView view);

// 1 to 256 characters, none of them control characters.
[[nodiscard]] bool isValidLibraryArtistPublicId(const QString& id);

// "/v1/music/library/artists?view=<view>&limit=<n>[&q=<query>][&cursor=<c>]"
// with the query trimmed and every value percent-encoded; nothing when the
// trimmed query is longer than 120 characters, the limit is not 1 to 100, or
// the cursor is longer than 2048 characters.
[[nodiscard]] std::optional<QString> libraryArtistListPath(const LibraryArtistListRequest& request);

// A page ({"artists": [...], "nextCursor", "totalCount"}, also under
// "data"). Not valid when anything in it is not as the service promises:
// more than 100 artists, an artist without an id or name, a count that is not
// a whole number of at least 0, an address that may not be kept, a field of
// the wrong type.
[[nodiscard]] LibraryArtistPage parseLibraryArtistPage(const QByteArray& payload);

// The songs of an artist's library entry (the library answer's form).
[[nodiscard]] std::optional<QList<AccountLibraryTrack>> parseLibraryArtistTracks(const QByteArray& payload);

} // namespace lmp::core::account
