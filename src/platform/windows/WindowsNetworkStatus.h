#pragma once

#include <QObject>

#include <memory>

#include "platform/INetworkStatus.h"

namespace lmp::platform::windows {

// The internet connection profile Windows reports (Windows.Networking.
// Connectivity). Windows announces changes on a worker thread; they are
// passed to the thread this object lives on (the GUI thread) before the
// handler runs. Reading the profile is a short system call.
class WindowsNetworkStatus final : public QObject, public INetworkStatus {
    Q_OBJECT

public:
    explicit WindowsNetworkStatus(QObject* parent = nullptr);
    ~WindowsNetworkStatus() override;

    WindowsNetworkStatus(const WindowsNetworkStatus&) = delete;
    WindowsNetworkStatus& operator=(const WindowsNetworkStatus&) = delete;

    [[nodiscard]] bool online() const override;
    [[nodiscard]] bool metered() const override;
    void setChangeHandler(ChangeHandler handler) override;

private:
    void notify();

    struct Subscription;
    std::unique_ptr<Subscription> m_subscription;
    ChangeHandler m_handler;
};

} // namespace lmp::platform::windows
