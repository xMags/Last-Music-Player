#pragma once

#include "ui/menus/MenuItemListModel.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace lmp::core::cast {
class CastController;
} // namespace lmp::core::cast

namespace lmp::core::connect {
class ConnectClient;
class ConnectSync;
} // namespace lmp::core::connect

namespace lmp::ui {

// The player bar's devices button and its menu (`App.cast`). Opening the
// menu scans the network; the menu lists what was found, "Stop casting"
// while casting, or why the song that plays cannot be cast.
//
// With the device link open, the account's devices come first: this PC,
// then the others, the playing one checked; choosing one moves playback
// there. While another device plays, only those are listed (casting starts
// from the device that plays).
//
// GUI thread only.
class CastViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.cast")

    Q_PROPERTY(bool active READ active NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged FINAL)
    // The account has other devices this PC can reach.
    Q_PROPERTY(bool otherDevices READ otherDevices NOTIFY stateChanged FINAL)
    Q_PROPERTY(lmp::ui::MenuItemListModel* menu READ menu CONSTANT FINAL)
    // The Connect panel (connect-ui.ts renderPanel): where music plays now
    // ({kind: "self" | "device" | "cast", name, detail, icon}), the other
    // places it can play ({id, icon, name, detail}; choose(id) moves it
    // there), the Cast receivers, and notes on what is still being looked
    // for or cannot be reached.
    Q_PROPERTY(QVariantMap current READ current NOTIFY panelChanged FINAL)
    Q_PROPERTY(QVariantList deviceRows READ deviceRows NOTIFY panelChanged FINAL)
    Q_PROPERTY(QVariantList castRows READ castRows NOTIFY panelChanged FINAL)
    Q_PROPERTY(QString castNote READ castNote NOTIFY panelChanged FINAL)
    Q_PROPERTY(QString connectNote READ connectNote NOTIFY panelChanged FINAL)

public:
    // `controller`, and the link's `client` and `sync` when given, outlive this object.
    explicit CastViewModel(core::cast::CastController& controller, core::connect::ConnectClient* client = nullptr,
                           core::connect::ConnectSync* sync = nullptr, QObject* parent = nullptr);

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString deviceName() const;
    [[nodiscard]] bool otherDevices() const;
    [[nodiscard]] MenuItemListModel* menu() { return &m_menu; }
    [[nodiscard]] QVariantMap current() const { return m_current; }
    [[nodiscard]] QVariantList deviceRows() const { return m_deviceRows; }
    [[nodiscard]] QVariantList castRows() const { return m_castRows; }
    [[nodiscard]] QString castNote() const { return m_castNote; }
    [[nodiscard]] QString connectNote() const { return m_connectNote; }
    // The open panel looks for receivers again (every 15 s while open).
    Q_INVOKABLE void scan();

    // Fills the menu and starts a scan when casting is possible.
    Q_INVOKABLE void openMenu();
    Q_INVOKABLE void choose(const QString& itemId);

signals:
    void stateChanged();
    void panelChanged();

private:
    void rebuild();
    void rebuildPanel();
    // The account's devices; true when any were listed.
    bool appendLinkedDevices(QList<MenuItemListModel::Item>& items) const;
    core::cast::CastController& m_controller;
    core::connect::ConnectClient* m_client = nullptr;
    core::connect::ConnectSync* m_sync = nullptr;
    MenuItemListModel m_menu;
    QVariantMap m_current;
    QVariantList m_deviceRows;
    QVariantList m_castRows;
    QString m_castNote;
    QString m_connectNote;
};

} // namespace lmp::ui
