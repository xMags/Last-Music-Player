// The output device of the Windows audio player: the system default, or a
// device looked up by its id in the background and applied on the player's
// thread. An id Windows no longer knows falls back to the default.

#include "platform/windows/WindowsAudioPlayerNative.h"

#include <QMetaObject>

#pragma warning(push, 0)
#include <winrt/Windows.Devices.Enumeration.h>
#pragma warning(pop)

namespace lmp::platform::windows {

namespace wde = winrt::Windows::Devices::Enumeration;
namespace wf = winrt::Windows::Foundation;
namespace wmp = winrt::Windows::Media::Playback;

void WindowsAudioPlayer::setOutputDevice(const QString& deviceId)
{
    if (!m_native->player) {
        return;
    }
    const quint64 request = ++m_deviceRequest;
    const auto useDefault = [this]() {
        const std::optional<QString> failure = media::runPlayerCall("AudioDevice", [this]() {
            m_native->player.AudioDevice(nullptr);
            m_native->player.AudioDeviceType(wmp::MediaPlayerAudioDeviceType::Multimedia);
        });
        static_cast<void>(failure); // the default always exists; nothing else to try
    };
    if (deviceId.trimmed().isEmpty()) {
        useDefault();
        return;
    }
    try {
        auto lookup = wde::DeviceInformation::CreateFromIdAsync(winrt::hstring(deviceId.toStdWString()));
        std::weak_ptr<AudioPlayerEventBridge> weak = m_native->bridge;
        lookup.Completed([weak, request](const wf::IAsyncOperation<wde::DeviceInformation>& done, wf::AsyncStatus status) {
            wde::DeviceInformation device{nullptr};
            if (status == wf::AsyncStatus::Completed) {
                try {
                    device = done.GetResults();
                } catch (const winrt::hresult_error&) {
                }
            }
            const std::shared_ptr<AudioPlayerEventBridge> bridge = weak.lock();
            if (!bridge) {
                return;
            }
            const std::lock_guard<std::mutex> lock(bridge->mutex);
            WindowsAudioPlayer* owner = bridge->owner;
            if (owner == nullptr) {
                return;
            }
            QMetaObject::invokeMethod(
                owner,
                [owner, device, request]() {
                    if (request != owner->m_deviceRequest || !owner->m_native->player) {
                        return;
                    }
                    const std::optional<QString> failure = media::runPlayerCall("AudioDevice", [owner, device]() {
                        if (device && device.IsEnabled()) {
                            owner->m_native->player.AudioDevice(device);
                        } else {
                            owner->m_native->player.AudioDevice(nullptr);
                            owner->m_native->player.AudioDeviceType(wmp::MediaPlayerAudioDeviceType::Multimedia);
                        }
                    });
                    static_cast<void>(failure);
                },
                Qt::QueuedConnection);
        });
    } catch (const winrt::hresult_error&) {
        useDefault();
    }
}

} // namespace lmp::platform::windows
