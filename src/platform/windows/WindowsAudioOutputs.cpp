#include "platform/windows/WindowsAudioOutputs.h"

#include <QMetaObject>

#include <mutex>

#pragma warning(push, 0)
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Devices.h>
#pragma warning(pop)

namespace lmp::platform::windows {

namespace wde = winrt::Windows::Devices::Enumeration;
namespace wf = winrt::Windows::Foundation;

namespace {

// Lets Windows callbacks reach the object without outliving it.
struct OutputsBridge {
    std::mutex mutex;
    WindowsAudioOutputs* owner = nullptr;
};

} // namespace

struct WindowsAudioOutputs::Native {
    std::shared_ptr<OutputsBridge> bridge = std::make_shared<OutputsBridge>();
    wde::DeviceWatcher watcher{nullptr};
    wde::DeviceWatcher::Added_revoker added;
    wde::DeviceWatcher::Removed_revoker removed;
    wde::DeviceWatcher::Updated_revoker updated;
};

WindowsAudioOutputs::WindowsAudioOutputs(QObject* parent)
    : IAudioOutputs(parent)
    , m_native(std::make_unique<Native>())
{
    m_native->bridge->owner = this;
    try {
        m_native->watcher = wde::DeviceInformation::CreateWatcher(winrt::Windows::Media::Devices::MediaDevice::GetAudioRenderSelector());
        std::weak_ptr<OutputsBridge> weak = m_native->bridge;
        const auto changed = [weak]() {
            const std::shared_ptr<OutputsBridge> bridge = weak.lock();
            if (!bridge) {
                return;
            }
            const std::lock_guard<std::mutex> lock(bridge->mutex);
            if (WindowsAudioOutputs* owner = bridge->owner) {
                QMetaObject::invokeMethod(owner, [owner]() { owner->read(); }, Qt::QueuedConnection);
            }
        };
        m_native->added = m_native->watcher.Added(winrt::auto_revoke,
                                                  [changed](const wde::DeviceWatcher&, const wde::DeviceInformation&) { changed(); });
        m_native->removed = m_native->watcher.Removed(
            winrt::auto_revoke, [changed](const wde::DeviceWatcher&, const wde::DeviceInformationUpdate&) { changed(); });
        // Needed for Added to keep coming after the first pass (a device
        // plugged in later), and a device's name or state may change.
        m_native->updated = m_native->watcher.Updated(
            winrt::auto_revoke, [changed](const wde::DeviceWatcher&, const wde::DeviceInformationUpdate&) { changed(); });
        m_native->watcher.Start();
    } catch (const winrt::hresult_error&) {
        // Without a watcher the list is read once.
    }
    read();
}

WindowsAudioOutputs::~WindowsAudioOutputs()
{
    {
        const std::lock_guard<std::mutex> lock(m_native->bridge->mutex);
        m_native->bridge->owner = nullptr;
    }
    m_native->added.revoke();
    m_native->removed.revoke();
    m_native->updated.revoke();
    try {
        if (m_native->watcher && m_native->watcher.Status() == wde::DeviceWatcherStatus::Started) {
            m_native->watcher.Stop();
        }
    } catch (const winrt::hresult_error&) {
    }
}

void WindowsAudioOutputs::read()
{
    const quint64 number = ++m_read;
    try {
        auto operation = wde::DeviceInformation::FindAllAsync(winrt::Windows::Media::Devices::MediaDevice::GetAudioRenderSelector());
        std::weak_ptr<OutputsBridge> weak = m_native->bridge;
        operation.Completed([weak, number](const wf::IAsyncOperation<wde::DeviceInformationCollection>& done,
                                           wf::AsyncStatus status) {
            QList<AudioOutput> outputs;
            if (status == wf::AsyncStatus::Completed) {
                try {
                    for (const wde::DeviceInformation& device : done.GetResults()) {
                        outputs.append({QString::fromWCharArray(device.Id().c_str()),
                                        QString::fromWCharArray(device.Name().c_str())});
                    }
                } catch (const winrt::hresult_error&) {
                }
            }
            const std::shared_ptr<OutputsBridge> bridge = weak.lock();
            if (!bridge) {
                return;
            }
            const std::lock_guard<std::mutex> lock(bridge->mutex);
            if (WindowsAudioOutputs* owner = bridge->owner) {
                QMetaObject::invokeMethod(
                    owner, [owner, outputs, number]() { owner->apply(outputs, number); }, Qt::QueuedConnection);
            }
        });
    } catch (const winrt::hresult_error&) {
    }
}

void WindowsAudioOutputs::apply(QList<AudioOutput> outputs, quint64 read)
{
    if (read != m_read) {
        return; // a newer read is on its way
    }
    m_outputs = std::move(outputs);
    emit outputsChanged();
}

} // namespace lmp::platform::windows
