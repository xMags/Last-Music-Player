#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace lmp::core {
class SettingsStore;
} // namespace lmp::core

namespace lmp::platform {
class IAudioOutputs;
class IAudioPlayer;
} // namespace lmp::platform

namespace lmp::ui {

// Settings > Playback "Output device" (`App.audioOutput`), as the current
// app offers it: "System default" first, then the devices Windows lists.
// The choice is kept in OutputDeviceId and applied to the player at once.
// When the chosen device is gone the sound goes to the default; the choice
// stays, so the device is used again when it comes back.
//
// GUI thread only.
class AudioOutputViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.audioOutput")

    Q_PROPERTY(QStringList choices READ choices NOTIFY changed FINAL)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed FINAL)

public:
    // All three outlive this object.
    AudioOutputViewModel(platform::IAudioOutputs& outputs, platform::IAudioPlayer& player,
                         core::SettingsStore& settings, QObject* parent = nullptr);

    [[nodiscard]] QStringList choices() const;
    [[nodiscard]] int currentIndex() const;

    Q_INVOKABLE void choose(int index);

signals:
    void changed();

private:
    void follow();

    platform::IAudioOutputs& m_outputs;
    platform::IAudioPlayer& m_player;
    core::SettingsStore& m_settings;
    // What the player was last told, so a list change only re-applies when
    // the answer differs.
    QString m_applied;
};

} // namespace lmp::ui
