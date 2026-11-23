#pragma once

#include "platform/IFolderPicker.h"

namespace lmp::platform::windows {

// The Windows folder picker (IFileOpenDialog in folder mode), starting in the
// given folder or the Music library, as earlier versions' picker did.
class WindowsFolderPicker final : public IFolderPicker {
public:
    WindowsFolderPicker() = default;

    [[nodiscard]] std::optional<QString> pickFolder(quintptr ownerWindow, const QString& startFolder) override;
};

} // namespace lmp::platform::windows
