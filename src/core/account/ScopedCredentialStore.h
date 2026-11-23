#pragma once

#include <QString>

#include "platform/ICredentialStore.h"

namespace lmp::core::account {

// Passes every call to another credential store with a suffix added to the
// target name ("<target>#<suffix>"), or unchanged when the suffix is empty.
// A developer run on its own data folder (LMP_DATA_ROOT, Debug builds) uses
// it so that signing in, out or deleting all data there never touches the
// credentials of the installed app, which share the real target names.
//
// Thread-safe as far as the wrapped store is.
class ScopedCredentialStore final : public platform::ICredentialStore {
public:
    // `inner` outlives this object.
    ScopedCredentialStore(platform::ICredentialStore& inner, QString suffix);

    [[nodiscard]] Result<std::optional<QString>> read(const QString& target) override;
    [[nodiscard]] Result<void> write(const QString& target, const QString& secret) override;
    [[nodiscard]] Result<void> remove(const QString& target) override;
    [[nodiscard]] Result<platform::CredentialRemoval> removeIfMatches(const QString& target,
                                                                      const QString& expected) override;

    [[nodiscard]] QString scoped(const QString& target) const;

    // The suffix for a developer data folder: a short stable hash of its
    // path; empty for the real data folder.
    [[nodiscard]] static QString suffixForDataRoot(const QString& dataRoot, bool developerRoot);

private:
    platform::ICredentialStore& m_inner;
    QString m_suffix;
};

} // namespace lmp::core::account
