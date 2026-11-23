#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "core/account/ProfileIdentity.h"
#include "ui/menus/MenuItemListModel.h"

namespace lmp::core {
class SettingsStore;
}

namespace lmp::core::account {
class AccountSessionService;
class AccountSignOutCoordinator;
} // namespace lmp::core::account

namespace lmp::core::mode {
class ModeService;
}

namespace lmp::ui {

class Navigator;

// The profile card at the foot of the sidebar (`App.profile`): who the app
// presents (core::account::chooseProfileIdentity: the account's name, plan
// and picture in Account mode, the typed name otherwise), and its menu:
// Settings, and Sign out while signed in. The picture is used only when it
// passes the account address rules (an https address or an inline image).
//
// GUI thread only.
class ProfileViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.profile")

    Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
    // The account's plan in capitals ("PREMIUM"); empty outside Account mode
    // or when the plan is not a short word.
    Q_PROPERTY(QString plan READ plan NOTIFY changed FINAL)
    // Up to two capital letters from the name.
    Q_PROPERTY(QString initials READ initials NOTIFY changed FINAL)
    // Empty when there is no picture or it failed the address rules.
    Q_PROPERTY(QUrl avatarUrl READ avatarUrl NOTIFY changed FINAL)
    Q_PROPERTY(lmp::ui::MenuItemListModel* menu READ menu CONSTANT FINAL)

public:
    struct Parts {
        core::SettingsStore& settings;
        const core::mode::ModeService& mode;
        const core::account::AccountSessionService& session;
        core::account::AccountSignOutCoordinator& signOut;
        Navigator& navigator;
    };

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit ProfileViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] QString name() const { return m_identity.name; }
    [[nodiscard]] QString plan() const;
    [[nodiscard]] QString initials() const;
    [[nodiscard]] QUrl avatarUrl() const;
    [[nodiscard]] MenuItemListModel* menu() { return &m_menu; }

    // Fills the menu for the current state; call before opening it.
    Q_INVOKABLE void prepareMenu();
    Q_INVOKABLE void triggerMenu(const QString& itemId);

    // The reference's initials: the first letter of each word, at most two,
    // in capitals; "L" for an empty name.
    [[nodiscard]] static QString initialsOf(const QString& name);

signals:
    void changed();

private:
    void refresh();
    [[nodiscard]] bool signedIn() const;

    core::SettingsStore& m_settings;
    const core::mode::ModeService& m_mode;
    const core::account::AccountSessionService& m_session;
    core::account::AccountSignOutCoordinator& m_signOut;
    Navigator& m_navigator;
    core::account::ProfileIdentity m_identity;
    MenuItemListModel m_menu;
};

} // namespace lmp::ui
