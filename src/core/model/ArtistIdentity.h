#pragma once

#include <QString>
#include <QStringView>

namespace lmp::core {

// What identifies an artist across the library, a server catalogue and the
// followed list, strongest first: a catalogue id (with the source it belongs
// to), then a durable address, then the normalised name.
struct ArtistIdentity {
    QString catalogId;
    QString sourceUrl;
    QString name;
    QString provider;
};

// Lower case, white space runs collapsed to one space, none at either end.
[[nodiscard]] QString normalizeArtistName(QStringView value);

// "catalog|<normalised provider>|<catalogId>", else "url|<sourceUrl>", else
// "name|<normalised name>"; empty when all three are empty. Stored as the
// FollowedArtists key, so its shape must not change.
[[nodiscard]] QString artistIdentityKey(const ArtistIdentity& artist);

// Whether both stand for the same artist. A catalogue id on either side
// decides alone (both must have it, equal, and the providers must agree when
// both name one); otherwise an address on either side decides alone; the
// normalised name is the last resort.
[[nodiscard]] bool sameArtistIdentity(const ArtistIdentity& left, const ArtistIdentity& right);

} // namespace lmp::core
