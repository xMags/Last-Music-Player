#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "core/app/UserDataCleanup.h"

namespace lmp::ui {

class ToastQueue;

// "Delete all data" in Settings' About and data card (`App.dataSettings`; the
// card's library count is App.settingsAbout's): it asks for "CLEAN UP" to be
// typed before it runs the clean-up (UserDataCleanup) and says how it ended.
//
// GUI thread only.
class DataSettingsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.dataSettings")

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    // The last clean-up's result, empty before one ran.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged FINAL)

public:
    // The words that unlock the clean-up (the current app's).
    static constexpr QLatin1StringView ConfirmWord{"CLEAN UP"};

    // Both outlive this object.
    DataSettingsViewModel(core::UserDataCleanup& cleanup, ToastQueue& toasts, QObject* parent = nullptr);

    [[nodiscard]] bool busy() const { return m_cleanup.running(); }
    [[nodiscard]] QString status() const { return m_status; }

    // Whether `typed` unlocks the clean-up.
    Q_INVOKABLE bool confirms(const QString& typed) const;
    // Runs the clean-up when `typed` is the confirm word; false otherwise.
    Q_INVOKABLE bool deleteAll(const QString& typed);

signals:
    void busyChanged();
    void statusChanged();

private:
    void onFinished(core::UserDataCleanup::Outcome outcome, const QString& detail);

    core::UserDataCleanup& m_cleanup;
    ToastQueue& m_toasts;
    QString m_status;
};

} // namespace lmp::ui
