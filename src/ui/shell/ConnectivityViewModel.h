#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace lmp::core {
namespace account {
class AccountSessionService;
}
namespace mode {
class ModeService;
}
} // namespace lmp::core

namespace lmp::ui {

// Whether the app started without reaching the service (App.connectivity): Account
// mode with a stored session the service could not confirm. The library
// shown is the copy saved at the last sync; changes made meanwhile are kept
// and sent once the session is confirmed again, which happens on its own
// when the network returns (AccountSyncScheduler).
//
// GUI thread only.
class ConnectivityViewModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.connectivity")

    Q_PROPERTY(bool offline READ offline NOTIFY offlineChanged FINAL)

public:
    // Both outlive this object.
    ConnectivityViewModel(core::account::AccountSessionService& session, core::mode::ModeService& mode,
                          QObject* parent = nullptr);

    [[nodiscard]] bool offline() const { return m_offline; }

signals:
    void offlineChanged();

private:
    void update();

    core::account::AccountSessionService& m_session;
    core::mode::ModeService& m_mode;
    bool m_offline = false;
};

} // namespace lmp::ui
