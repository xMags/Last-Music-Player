#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>

namespace lmp::platform::windows {

// The running copy's side of a second launch: the second copy posts the
// activation message (WindowsSingleInstance::activateExistingWindow) to the
// main window instead of showing it from outside, which would leave Qt
// believing the window is still hidden (and Close then does nothing). Here
// the message becomes activationRequested(), which the app answers by
// restoring its window the way the tray icon does.
//
// Install on the application (QCoreApplication::installNativeEventFilter).
// GUI thread only.
class WindowsActivationRequests final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit WindowsActivationRequests(QObject* parent = nullptr);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void activationRequested();

private:
    unsigned int m_message = 0;
};

} // namespace lmp::platform::windows
