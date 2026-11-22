#include "ui/shell/PlaybackKeyGuard.h"

#include <QAccessible>

#include <array>

namespace lmp::ui {

namespace {

// Qt Quick classes that handle Space or the arrow keys themselves. Matched
// by class name because most are not public API; a QML type built on one
// (IconButton on Button, for example) matches too.
constexpr std::array<const char*, 9> KeyOwningClasses{
    "QQuickTextInput",      // text fields
    "QQuickTextEdit",       // text areas
    "QQuickAbstractButton", // buttons, switches, check boxes
    "QQuickSlider",
    "QQuickRangeSlider",
    "QQuickComboBox",
    "QQuickSpinBox",
    "QQuickDial",
    "QQuickPopupItem", // the content of a dialog, menu or other popup
};

[[nodiscard]] bool isKeyOwningClass(const QQuickItem& item)
{
    for (const char* className : KeyOwningClasses) {
        if (item.inherits(className)) {
            return true;
        }
    }
    return false;
}

// A plain item that acts as a button (Accessible.role: Accessible.Button),
// such as a queue row, takes Space to activate itself.
[[nodiscard]] bool actsAsButton(QQuickItem& item)
{
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&item);
    return accessible != nullptr && accessible->role() == QAccessible::Button;
}

} // namespace

PlaybackKeyGuard::PlaybackKeyGuard(QObject* parent)
    : QObject(parent)
{
}

void PlaybackKeyGuard::setFocusItem(QQuickItem* item)
{
    if (item == m_focusItem) {
        return;
    }
    m_focusItem = item;
    emit focusItemChanged();
    const bool blocked = ownsPlaybackKeys(item);
    if (blocked != m_blocked) {
        m_blocked = blocked;
        emit blockedChanged();
    }
}

bool PlaybackKeyGuard::ownsPlaybackKeys(QQuickItem* item)
{
    for (QQuickItem* current = item; current != nullptr; current = current->parentItem()) {
        if (isKeyOwningClass(*current) || actsAsButton(*current)) {
            return true;
        }
    }
    return false;
}

} // namespace lmp::ui
