#include "platform/windows/WindowsCredentialStore.h"

#include "core/text/StrictUtf8.h"

#include <QByteArray>
#include <QMutexLocker>

#include <memory>
#include <string>
#include <utility>

#include <windows.h>
#include <wincred.h>

namespace lmp::platform::windows {

namespace {

using core::Failure;
using core::FailureKind;

// Shared with installed earlier versions; part of the stored credential.
constexpr wchar_t CredentialUserName[] = L"Last Music Player";

struct CredentialDeleter {
    void operator()(CREDENTIALW* credential) const noexcept { CredFree(credential); }
};
using CredentialPointer = std::unique_ptr<CREDENTIALW, CredentialDeleter>;

[[nodiscard]] Failure systemFailure(const char* call, DWORD error)
{
    return Failure::of(FailureKind::Storage,
                       QStringLiteral("%1 failed with error %2").arg(QLatin1StringView(call)).arg(error));
}

} // namespace

core::Result<std::optional<QString>> WindowsCredentialStore::read(const QString& target)
{
    const QMutexLocker locker(&m_mutex);
    return readLocked(target);
}

core::Result<void> WindowsCredentialStore::write(const QString& target, const QString& secret)
{
    if (target.isEmpty()) {
        return Failure::of(FailureKind::Storage, QStringLiteral("credential name is empty"));
    }
    if (secret.isEmpty()) {
        return Failure::of(FailureKind::Storage, QStringLiteral("secret is empty"));
    }
    std::optional<QByteArray> utf8 = core::text::encodeUtf8Strict(secret);
    if (!utf8 || utf8->isEmpty()) {
        return Failure::of(FailureKind::Storage, QStringLiteral("secret is not valid text"));
    }
    if (utf8->size() > static_cast<qsizetype>(CRED_MAX_CREDENTIAL_BLOB_SIZE)) {
        return Failure::of(FailureKind::Storage, QStringLiteral("secret is too large"));
    }

    std::wstring targetName = target.toStdWString();
    std::wstring userName(CredentialUserName);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = targetName.data();
    credential.CredentialBlobSize = static_cast<DWORD>(utf8->size());
    // The blob is a byte buffer; the API types it as LPBYTE while QByteArray
    // hands out char.
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(utf8->data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = userName.data();

    const QMutexLocker locker(&m_mutex);
    if (CredWriteW(&credential, 0) == FALSE) {
        return systemFailure("CredWriteW", GetLastError());
    }
    return core::Result<void>::success();
}

core::Result<void> WindowsCredentialStore::remove(const QString& target)
{
    const QMutexLocker locker(&m_mutex);
    return removeLocked(target);
}

core::Result<CredentialRemoval> WindowsCredentialStore::removeIfMatches(const QString& target,
                                                                        const QString& expected)
{
    if (expected.isEmpty()) {
        return CredentialRemoval::Mismatch;
    }
    const QMutexLocker locker(&m_mutex);
    core::Result<std::optional<QString>> current = readLocked(target);
    if (!current) {
        return current.failure();
    }
    if (!current.value()) {
        return CredentialRemoval::NotFound;
    }
    if (*current.value() != expected) {
        return CredentialRemoval::Mismatch;
    }
    core::Result<void> removed = removeLocked(target);
    if (!removed) {
        return removed.failure();
    }
    return CredentialRemoval::Removed;
}

core::Result<std::optional<QString>> WindowsCredentialStore::readLocked(const QString& target)
{
    if (target.isEmpty()) {
        return Failure::of(FailureKind::Storage, QStringLiteral("credential name is empty"));
    }
    const std::wstring targetName = target.toStdWString();
    PCREDENTIALW raw = nullptr;
    if (CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &raw) == FALSE) {
        const DWORD error = GetLastError();
        if (error == ERROR_NOT_FOUND) {
            return std::optional<QString>{};
        }
        return systemFailure("CredReadW", error);
    }
    const CredentialPointer credential(raw);
    if (credential->CredentialBlob == nullptr || credential->CredentialBlobSize == 0) {
        return Failure::of(FailureKind::InvalidResponse, QStringLiteral("stored secret is empty"));
    }
    // The blob is a byte buffer; QByteArrayView reads it as char.
    const QByteArrayView blob(reinterpret_cast<const char*>(credential->CredentialBlob),
                              static_cast<qsizetype>(credential->CredentialBlobSize));
    std::optional<QString> secret = core::text::decodeUtf8Strict(blob);
    if (!secret) {
        return Failure::of(FailureKind::InvalidResponse, QStringLiteral("stored secret is not valid UTF-8"));
    }
    return std::optional<QString>(std::move(*secret));
}

core::Result<void> WindowsCredentialStore::removeLocked(const QString& target)
{
    if (target.isEmpty()) {
        return Failure::of(FailureKind::Storage, QStringLiteral("credential name is empty"));
    }
    const std::wstring targetName = target.toStdWString();
    if (CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0) != FALSE) {
        return core::Result<void>::success();
    }
    const DWORD error = GetLastError();
    if (error == ERROR_NOT_FOUND) {
        return core::Result<void>::success();
    }
    return systemFailure("CredDeleteW", error);
}

} // namespace lmp::platform::windows
