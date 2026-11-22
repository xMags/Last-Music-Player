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

} // namespace lmp::core
