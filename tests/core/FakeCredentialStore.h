#pragma once

#include <QHash>
#include <QString>

#include <optional>

#include "core/async/Result.h"
#include "platform/ICredentialStore.h"

namespace lmp::test {

// Keeps credentials in memory and can be told to fail, following the
// contract of ICredentialStore.
class FakeCredentialStore final : public platform::ICredentialStore {
public:
    core::Result<std::optional<QString>> read(const QString& target) override
    {
        ++readCount;
        if (failRead) {
            return core::Failure::of(core::FailureKind::Storage, QStringLiteral("read refused"));
        }
        if (corruptNextRead) {
            corruptNextRead = false;
            return std::optional<QString>(QStringLiteral("wrong"));
        }
        const auto found = values.constFind(target);
        if (found == values.constEnd()) {
            return std::optional<QString>{};
        }
        return std::optional<QString>(found.value());
    }

    core::Result<void> write(const QString& target, const QString& secret) override
    {
        lastWriteTarget = target;
        ++writeCount;
        if (failWrite || secret.isEmpty()) {
            return core::Failure::of(core::FailureKind::Storage, QStringLiteral("write refused"));
        }
        values.insert(target, secret);
        return {};
    }

    core::Result<void> remove(const QString& target) override
    {
        lastRemoveTarget = target;
        if (failRemove) {
            return core::Failure::of(core::FailureKind::Storage, QStringLiteral("remove refused"));
        }
        values.remove(target);
        return {};
    }

    core::Result<platform::CredentialRemoval> removeIfMatches(const QString& target, const QString& expected) override
    {
        ++conditionalRemoveCount;
        const auto found = values.constFind(target);
        if (found == values.constEnd()) {
            return platform::CredentialRemoval::NotFound;
        }
        if (expected.isEmpty() || found.value() != expected) {
            return platform::CredentialRemoval::Mismatch;
        }
        const core::Result<void> removed = remove(target);
        if (!removed) {
            return removed.failure();
        }
        return platform::CredentialRemoval::Removed;
    }

    QHash<QString, QString> values;
    QString lastWriteTarget;
    QString lastRemoveTarget;
    int readCount = 0;
    int writeCount = 0;
    int conditionalRemoveCount = 0;
    bool failRead = false;
    bool failWrite = false;
    bool failRemove = false;
    bool corruptNextRead = false;
};

} // namespace lmp::test
