#pragma once

#include "ui/menus/MenuItemListModel.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace lmp::core::cast {
class CastController;
} // namespace lmp::core::cast

namespace lmp::ui {

// The player bar's devices button and its menu (`App.cast`). Opening the
// menu scans the network; the menu lists what was found, "Stop casting"
// while casting, or why the song that plays cannot be cast.
//
// GUI thread only.
class CastViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.cast")

    Q_PROPERTY(bool active READ active NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged FINAL)
    Q_PROPERTY(lmp::ui::MenuItemListModel* menu READ menu CONSTANT FINAL)

public:
    // `controller` outlives this object.
    explicit CastViewModel(core::cast::CastController& controller, QObject* parent = nullptr);

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString deviceName() const;
    [[nodiscard]] MenuItemListModel* menu() { return &m_menu; }

    // Fills the menu and starts a scan when casting is possible.
    Q_INVOKABLE void openMenu();
    Q_INVOKABLE void choose(const QString& itemId);

signals:
    void stateChanged();

private:
    void rebuild();

    core::cast::CastController& m_controller;
    MenuItemListModel m_menu;
};

} // namespace lmp::ui
