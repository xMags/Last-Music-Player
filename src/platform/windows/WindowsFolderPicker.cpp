#include "platform/windows/WindowsFolderPicker.h"

#include <QDir>

#include <windows.h>
#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <winrt/base.h>

namespace lmp::platform::windows {

namespace {

winrt::com_ptr<IShellItem> shellItemFor(const QString& folder)
{
    winrt::com_ptr<IShellItem> item;
    if (!folder.isEmpty() && QDir(folder).exists()) {
        const QString native = QDir::toNativeSeparators(folder);
        if (SUCCEEDED(SHCreateItemFromParsingName(reinterpret_cast<const wchar_t*>(native.utf16()), nullptr,
                                                  IID_PPV_ARGS(item.put())))) {
            return item;
        }
    }
    // The Music library, where earlier versions' picker started.
    item = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_Music, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(item.put())))) {
        return nullptr;
    }
    return item;
}

} // namespace

std::optional<QString> WindowsFolderPicker::pickFolder(quintptr ownerWindow, const QString& startFolder)
{
    winrt::com_ptr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put())))) {
        return std::nullopt;
    }
    FILEOPENDIALOGOPTIONS options = 0;
    if (FAILED(dialog->GetOptions(&options))
        || FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST))) {
        return std::nullopt;
    }
    if (const winrt::com_ptr<IShellItem> start = shellItemFor(startFolder)) {
        (void)dialog->SetFolder(start.get());
    }
    // A cancelled dialog answers with an error too.
    if (FAILED(dialog->Show(reinterpret_cast<HWND>(ownerWindow)))) {
        return std::nullopt;
    }
    winrt::com_ptr<IShellItem> result;
    if (FAILED(dialog->GetResult(result.put()))) {
        return std::nullopt;
    }
    PWSTR path = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) || path == nullptr) {
        return std::nullopt;
    }
    QString chosen = QString::fromWCharArray(path);
    CoTaskMemFree(path);
    return chosen;
}

} // namespace lmp::platform::windows
