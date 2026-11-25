#pragma once

#include "platform/IAudioOutputs.h"

#include <QtGlobal>

#include <memory>

namespace lmp::platform::windows {

// The audio render devices Windows lists (Windows.Devices.Enumeration with
// the media render selector). A device watcher reports devices that come or
// go or change; each change reads the list again, and only the newest read
// is used (reads can finish out of order). The watcher hears Added, Removed
// and Updated: without an Updated handler Windows stops reporting devices
// added after its first pass. Windows calls back on its own threads; results
// are passed to this object's thread before they are used.
class WindowsAudioOutputs final : public IAudioOutputs {
    Q_OBJECT

public:
    explicit WindowsAudioOutputs(QObject* parent = nullptr);
    ~WindowsAudioOutputs() override;

    [[nodiscard]] QList<AudioOutput> outputs() const override { return m_outputs; }

private:
    struct Native;

    void read();
    void apply(QList<AudioOutput> outputs, quint64 read);

    std::unique_ptr<Native> m_native;
    QList<AudioOutput> m_outputs;
    // The newest read asked for; an older one's answer is dropped.
    quint64 m_read = 0;
};

} // namespace lmp::platform::windows
