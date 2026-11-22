#include "platform/windows/WindowsAppIdentity.h"

#include <QLoggingCategory>

#include <string>

#include <windows.h>
#include <shobjidl.h>

namespace lmp::platform::windows {

namespace {

Q_LOGGING_CATEGORY(lcAppIdentity, "lmp.platform.appidentity")

} // namespace

bool WindowsAppIdentity::apply(const QString& appUserModelId)
{
    const std::wstring id = appUserModelId.toStdWString();
    const HRESULT result = SetCurrentProcessExplicitAppUserModelID(id.c_str());
    if (FAILED(result)) {
        qCWarning(lcAppIdentity) << "Could not set the AppUserModelID, HRESULT"
                                 << Qt::hex << static_cast<unsigned long>(result);
        return false;
    }
    return true;
}

} // namespace lmp::platform::windows
