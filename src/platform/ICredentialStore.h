#pragma once

#include "core/async/Result.h"

#include <QString>

#include <optional>

namespace lmp::platform {

// What a conditional removal found.
enum class CredentialRemoval {
    Removed,
    // There was no credential to remove.
    NotFound,
    // The stored secret is different (or the expected value was empty), so it
    // was kept.
    Mismatch,
};

// Named secrets kept by the operating system for the current user. A secret
// is non-empty text, stored as its UTF-8 bytes. Implementations are
// thread-safe, and removeIfMatches is atomic against the other calls made
// through the same object.
class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;

    // Nothing when no credential has this name. A stored value that is empty
    // or not valid UTF-8 is a FailureKind::InvalidResponse failure; any other
    // refusal by the system is FailureKind::Storage.
    [[nodiscard]] virtual core::Result<std::optional<QString>> read(const QString& target) = 0;

    // Creates or replaces the credential. An empty secret, or one that cannot
    // be encoded or is too large for the system, is refused (Storage).
    [[nodiscard]] virtual core::Result<void> write(const QString& target, const QString& secret) = 0;

    // Succeeds when the credential is gone afterwards, including when it was
    // never there.
    [[nodiscard]] virtual core::Result<void> remove(const QString& target) = 0;

    // Removes the credential only when it holds exactly `expected`. A stored
    // value that cannot be read, or a failed removal, is a failure; the
    // credential is then left as it was.
    [[nodiscard]] virtual core::Result<CredentialRemoval> removeIfMatches(const QString& target,
                                                                          const QString& expected) = 0;

protected:
    ICredentialStore() = default;
    ICredentialStore(const ICredentialStore&) = default;
    ICredentialStore& operator=(const ICredentialStore&) = default;
};

} // namespace lmp::platform
