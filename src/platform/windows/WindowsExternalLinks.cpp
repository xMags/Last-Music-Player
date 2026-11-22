#include "platform/windows/WindowsExternalLinks.h"

#include "core/app/ExternalLinkPolicy.h"

#include <QDir>

#include <utility>

#include <windows.h>
#include <shellapi.h>

namespace lmp::platform::windows {

WindowsExternalLinks::WindowsExternalLinks(QString dataRoot)
    : m_dataRoot(std::move(dataRoot))
{
}

bool WindowsExternalLinks::open(const QUrl& url)
{
    if (!core::isAllowedExternalLink(url, m_dataRoot)) {
        return false;
    }
    // A folder is handed over as its path so the shell opens it in File
    // Explorer; a web address keeps its percent-encoding.
    const QString target = url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile())
                                             : url.toString(QUrl::FullyEncoded);
    const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", qUtf16Printable(target), nullptr, nullptr,
                                             SW_SHOWNORMAL);
    // ShellExecuteW reports success as a value above 32; the handle is only
    // an integer code kept for compatibility, which is why it is converted.
    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace lmp::platform::windows
