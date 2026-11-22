#pragma once

#include <QString>

namespace lmp::core {

// Values supplied privately at build time. Every field is empty in a build
// made without the private config, and callers must treat an empty field as
// "this feature is not available".
struct PrivateConfig {
    QString accountApiOrigin;
    QString accountFrontendOrigin;
    QString accountMediaOrigin;
    QString discordClientId;

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
