#pragma once

#include <QString>
#include <QStringList>

namespace lmp::core {

// Values supplied privately at build time. Every field is empty in a build
// made without the private config, and callers must treat an empty field as
// "this feature is not available".
struct PrivateConfig {
    QString accountApiOrigin;
    QString accountFrontendOrigin;
    QString accountMediaOrigin;
    QString discordClientId;
    // Source tag of catalogue items that carry the catalogue's own numeric ids.
    QString sourceTagCatalog;
    // Source tags whose songs and albums the service resolves directly; the
    // first is assumed when an item has none.
    QStringList sourceTagsStream;
    // Words (lower case) naming where music comes from; never shown as an
    // artist, curator or album name.
    QStringList sourceLabels;
    // The prefix of an external playlist id.
    QString externalPlaylistIdPrefix;

    // The values compiled into this binary, with withDefaults() applied.
    [[nodiscard]] static PrivateConfig fromBuild();

    // The media origin falls back to the API origin when it is empty, as in
    // earlier versions, where the service streamed from its API origin
    // unless a build named another one.
    [[nodiscard]] PrivateConfig withDefaults() const;

    // True when the account API origin is present. The frontend origin only
    // adds the account management link, so a build without it still signs
    // in, as earlier versions did.
    [[nodiscard]] bool hasAccountService() const;
};

} // namespace lmp::core
