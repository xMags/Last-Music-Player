#include "core/app/AppPaths.h"

#include "core/app/AppIdentity.h"

#include <QDir>
#include <QtGlobal>

#include <utility>

namespace lmp::core {

AppPaths::AppPaths(QString root)
    : m_root(QDir::cleanPath(std::move(root)))
{
}

bool AppPaths::developerDataRoot()
{
#ifndef QT_NO_DEBUG
    return !qEnvironmentVariable(DataRootVariable).isEmpty();
#else
    return false;
#endif
}

AppPaths AppPaths::forCurrentUser()
{
#ifndef QT_NO_DEBUG
    // A developer run can point the whole data folder somewhere else, so it
    // never touches the real one. Release builds ignore the variable.
    const QString overrideRoot = qEnvironmentVariable(DataRootVariable);
    if (!overrideRoot.isEmpty()) {
        return AppPaths(overrideRoot);
    }
#endif
    // Earlier versions read the variable directly rather than asking the
    // shell, so this does too: the two can differ under redirection.
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString base = localAppData.isEmpty() ? QDir::currentPath() : localAppData;
    return AppPaths(QDir(base).filePath(AppIdentity::dataFolderName()));
}

QString AppPaths::settingsFile() const
{
    return child(QStringLiteral("Settings.json"));
}

QString AppPaths::stateFile() const
{
    return child(QStringLiteral("LastMusicState.json"));
}

QString AppPaths::databaseFile() const
{
    return child(QStringLiteral("lastmusic.db"));
}

QString AppPaths::downloadsFile() const
{
    return child(QStringLiteral("Downloads.json"));
}

QString AppPaths::offlineFolder() const
{
    return child(QStringLiteral("Offline"));
}

QString AppPaths::thumbsFolder() const
{
    return child(QStringLiteral("thumbs"));
}

QString AppPaths::streamCacheFolder() const
{
    return child(QStringLiteral("stream-cache"));
}

QString AppPaths::child(const QString& name) const
{
    return QDir(m_root).filePath(name);
}

} // namespace lmp::core
