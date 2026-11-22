#pragma once

#include <QString>

namespace lmp::core {

// Names the operating system and the installer know this application by.
// Installed copies of earlier versions depend on every one of these, so they
// must never change.
struct AppIdentity {
    // Shown in the title bar and used to find the running window.
    [[nodiscard]] static QString displayName();

    // Folder under the per-user local application data directory.
    [[nodiscard]] static QString dataFolderName();

    // Must match the identifier the installer writes into its shortcuts.
    [[nodiscard]] static QString appUserModelId();

    [[nodiscard]] static QString singleInstanceMutexName();

    [[nodiscard]] static QString version();

    // Public values the account service knows this application by. They
    // name the client, are not secrets, and match earlier versions.

    // The client id sent with sign-in and the token exchange.
    [[nodiscard]] static QString accountClientId();

    // The X-Requested-With value sent on every account request.
    [[nodiscard]] static QString accountRequestedWith();

    // The path of the loopback address the browser returns to after sign-in.
    [[nodiscard]] static QString accountCallbackPath();
};

} // namespace lmp::core
