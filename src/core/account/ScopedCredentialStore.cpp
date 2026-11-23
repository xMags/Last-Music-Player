#include "core/account/ScopedCredentialStore.h"

#include <QCryptographicHash>
#include <QDir>

#include <utility>

namespace lmp::core::account {

ScopedCredentialStore::ScopedCredentialStore(platform::ICredentialStore& inner, QString suffix)
    : m_inner(inner)
    , m_suffix(std::move(suffix))
{
}

QString ScopedCredentialStore::scoped(const QString& target) const
{
    return m_suffix.isEmpty() ? target : target + QLatin1Char('#') + m_suffix;
}

QString ScopedCredentialStore::suffixForDataRoot(const QString& dataRoot, bool developerRoot)
{
    if (!developerRoot) {
        return {};
    }
    const QByteArray path = QDir::cleanPath(dataRoot).toLower().toUtf8();
    return QStringLiteral("dev-")
        + QString::fromLatin1(QCryptographicHash::hash(path, QCryptographicHash::Sha1).toHex().left(12));
}

Result<std::optional<QString>> ScopedCredentialStore::read(const QString& target)
{
    return m_inner.read(scoped(target));
}

Result<void> ScopedCredentialStore::write(const QString& target, const QString& secret)
{
    return m_inner.write(scoped(target), secret);
}

Result<void> ScopedCredentialStore::remove(const QString& target)
{
    return m_inner.remove(scoped(target));
}

Result<platform::CredentialRemoval> ScopedCredentialStore::removeIfMatches(const QString& target,
                                                                           const QString& expected)
{
    return m_inner.removeIfMatches(scoped(target), expected);
}

} // namespace lmp::core::account
