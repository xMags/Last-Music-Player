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
};

} // namespace lmp::core
