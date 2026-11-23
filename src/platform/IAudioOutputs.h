#pragma once

#include <QList>
#include <QObject>
#include <QString>

namespace lmp::platform {

// One device sound can be sent to.
struct AudioOutput {
    // The system's id, kept in the OutputDeviceId setting.
    QString id;
    QString name;
};

// The machine's audio output devices, as the operating system lists them.
// The list is read in the background; `outputsChanged` follows the first
// read and every device that comes or goes.
//
// GUI thread only (signals arrive there).
class IAudioOutputs : public QObject {
    Q_OBJECT

public:
    ~IAudioOutputs() override = default;
    IAudioOutputs(const IAudioOutputs&) = delete;
    IAudioOutputs& operator=(const IAudioOutputs&) = delete;

    [[nodiscard]] virtual QList<AudioOutput> outputs() const = 0;

signals:
    void outputsChanged();

protected:
    explicit IAudioOutputs(QObject* parent = nullptr) : QObject(parent) {}
};

} // namespace lmp::platform
