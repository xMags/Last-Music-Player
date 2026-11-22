#include "core/app/AppIdentity.h"

namespace lmp::core {

QString AppIdentity::displayName()
{
    return QStringLiteral("Last Music Player");
}

QString AppIdentity::dataFolderName()
{
    return QStringLiteral("Last Music Player");
}

QString AppIdentity::appUserModelId()
{
    return QStringLiteral("LastMusicPlayer.App");
}

QString AppIdentity::singleInstanceMutexName()
{
    return QStringLiteral("Local\\LastMusicPlayer.SingleInstance");
}

QString AppIdentity::version()
{
    return QStringLiteral(LMP_VERSION_STRING);
}

QString AppIdentity::accountClientId()
{
    return QStringLiteral("last-music-desktop");
}

QString AppIdentity::accountRequestedWith()
{
    return QStringLiteral("last-music-desktop");
}

QString AppIdentity::accountCallbackPath()
{
    return QStringLiteral("/callback");
}

} // namespace lmp::core
