#include "ui/app/AppInfo.h"

#include "core/app/AppIdentity.h"

namespace lmp::ui {

AppInfo::AppInfo(QObject* parent)
    : QObject(parent)
{
}

QString AppInfo::displayName() const
{
    return core::AppIdentity::displayName();
}

QString AppInfo::version() const
{
    return core::AppIdentity::version();
}

} // namespace lmp::ui
