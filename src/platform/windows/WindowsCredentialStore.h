#pragma once

#include <QMutex>
#include <QString>

#include <optional>

#include "platform/ICredentialStore.h"

namespace lmp::platform::windows {

// Credential Manager: generic credentials persisted for the local machine
// under the current user, with the fixed user name "Last Music Player" and the
// secret's raw UTF-8 bytes (no terminator, no wrapper) as the blob. Installed
// earlier versions read and write the same entries, so none of this may
// change.
//
// Thread-safe: one mutex serialises every call, which is what makes
// removeIfMatches atomic for this process.
class WindowsCredentialStore final : public ICredentialStore {
public:
    WindowsCredentialStore() = default;

    WindowsCredentialStore(const WindowsCredentialStore&) = delete;
    WindowsCredentialStore& operator=(const WindowsCredentialStore&) = delete;

    [[nodiscard]] core::Result<std::optional<QString>> read(const QString& target) override;
    [[nodiscard]] core::Result<void> write(const QString& target, const QString& secret) override;
    [[nodiscard]] core::Result<void> remove(const QString& target) override;
    [[nodiscard]] core::Result<CredentialRemoval> removeIfMatches(const QString& target,
                                                                  const QString& expected) override;

private:
    [[nodiscard]] static core::Result<std::optional<QString>> readLocked(const QString& target);
    [[nodiscard]] static core::Result<void> removeLocked(const QString& target);

    QMutex m_mutex;
};

} // namespace lmp::platform::windows
