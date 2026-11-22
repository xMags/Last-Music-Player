#include "core/app/ExternalLinkPolicy.h"

#include <QDir>
#include <QLatin1Char>
#include <QLatin1StringView>

namespace lmp::core {

namespace {

[[nodiscard]] constexpr Qt::CaseSensitivity fileSystemCase()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

[[nodiscard]] bool isAllowedWebAddress(const QUrl& url)
{
    return !url.host().isEmpty() && url.userInfo().isEmpty();
}

[[nodiscard]] bool isInsideDataRoot(const QUrl& url, const QString& dataRoot)
{
    // A host means a network share (file://server/share), never the data
    // folder on this machine.
    if (dataRoot.isEmpty() || !url.host().isEmpty() || url.hasQuery() || url.hasFragment()) {
        return false;
    }
    const QString path = url.toLocalFile();
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        return false;
    }
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(dataRoot));
    const QString candidate = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (root.isEmpty() || !QDir::isAbsolutePath(root)) {
        return false;
    }
    if (candidate.compare(root, fileSystemCase()) == 0) {
        return true;
    }
    // The separator keeps a sibling such as "<root>2" outside.
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return candidate.startsWith(prefix, fileSystemCase());
}

} // namespace

bool isAllowedExternalLink(const QUrl& url, const QString& dataRoot)
{
    if (!url.isValid() || url.isRelative()) {
        return false;
    }
    const QString scheme = url.scheme();
    if (scheme == QLatin1StringView("https")) {
        return isAllowedWebAddress(url);
    }
    if (scheme == QLatin1StringView("file")) {
        return isInsideDataRoot(url, dataRoot);
    }
    return false;
}

} // namespace lmp::core
