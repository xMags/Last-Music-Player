#pragma once

#include "platform/IAudioOutputs.h"

#include <memory>

namespace lmp::platform::windows {

// The audio render devices Windows lists (Windows.Devices.Enumeration with
// the media render selector). A device watcher reports devices that come or
// go; each change reads the list again. Windows calls back on its own
// threads; results are passed to this object's thread before they are used.
class WindowsAudioOutputs final : public IAudioOutputs {
    Q_OBJECT

public:
    explicit WindowsAudioOutputs(QObject* parent = nullptr);
    ~WindowsAudioOutputs() override;

    [[nodiscard]] QList<AudioOutput> outputs() const override { return m_outputs; }

private:
    struct Native;

    void read();
    void apply(QList<AudioOutput> outputs);

    std::unique_ptr<Native> m_native;
    QList<AudioOutput> m_outputs;
};

} // namespace lmp::platform::windows
