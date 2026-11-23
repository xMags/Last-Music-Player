#pragma once

#include "core/account/LibraryArtistContract.h"
#include "core/model/Track.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace lmp::core::share {

// What a share link holds: one song, a list of songs (a snapshot of an
// album, playlist or chart), or an artist by its identity.
enum class ShareKind { Song, List, Artist };

struct ShareRequest {
    ShareKind kind = ShareKind::Song;
    // Song.
    Track track;
    // List: its title, an optional subtitle and picture, and its songs.
    QString title;
    QString subtitle;
    QString artworkUrl;
    QList<Track> tracks;
    // Artist (its name is identity.name); artworkUrl is its picture.
    account::LibraryArtistIdentity artist;
};

// The link the service made (or found: sharing the same item again gives
// the same link).
struct ShareLink {
    QString code;
    QString url;
    QString title;
};

// A share link's item as a signed-in app sees it.
struct SharedItem {
    QString code;
    ShareKind kind = ShareKind::Song;
    QString title;
    QString subtitle;
    QString artworkUrl;
    QList<Track> tracks;
    // Set for an artist.
    std::optional<account::LibraryArtistIdentity> artist;
};

// The JSON body of POST /v1/music/shares, or nothing when the request
// cannot be shared (a song the account cannot hold, an empty list, an
// artist without a provider and an id or link).
[[nodiscard]] std::optional<QByteArray> shareRequestBody(const ShareRequest& request);

// The service's answer; nothing unless it holds an https link.
[[nodiscard]] std::optional<ShareLink> parseShareLink(const QByteArray& payload);

// GET /v1/music/shares/<code>; nothing for an unknown kind.
[[nodiscard]] std::optional<SharedItem> parseSharedItem(const QByteArray& payload, const QString& code);

// A share code: ten letters or digits.
[[nodiscard]] bool isShareCode(const QString& code);

// The code of a share link (<frontendOrigin>/s/<code>), or nothing for any
// other text. Only links to the app's own site count.
[[nodiscard]] std::optional<QString> shareCodeFromLink(const QString& text, const QString& frontendOrigin);

// The text shared with a link: "Title by Artist" for a song, else the
// title and, when there is one, " · " and the subtitle.
[[nodiscard]] QString shareText(const ShareRequest& request);

} // namespace lmp::core::share
