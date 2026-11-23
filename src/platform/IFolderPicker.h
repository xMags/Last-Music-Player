#pragma once

#include <QString>
#include <QtGlobal>

#include <optional>

namespace lmp::platform {

// The system's folder picker. Blocks until the user chooses or cancels; the
// dialog is modal to the given window.
//
// GUI thread only.
class IFolderPicker {
public:
    virtual ~IFolderPicker() = default;

    // The chosen folder, or nothing when the user cancelled. `ownerWindow` is
    // the native window handle (0 for none); `startFolder` may be empty, and
    // then the picker opens in the user's Music folder.
    [[nodiscard]] virtual std::optional<QString> pickFolder(quintptr ownerWindow, const QString& startFolder) = 0;

protected:
    IFolderPicker() = default;
    IFolderPicker(const IFolderPicker&) = default;
    IFolderPicker& operator=(const IFolderPicker&) = default;
};

} // namespace lmp::platform
