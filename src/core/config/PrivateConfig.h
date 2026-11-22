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

    // The values compiled into this binary.
    [[nodiscard]] static PrivateConfig fromBuild();

    // True when all three account origins are present.
    [[nodiscard]] bool hasAccountService() const;
};

} // namespace lmp::core
