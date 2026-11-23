#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace lmp::platform {
class IDnsServiceBrowser;
struct DnsServiceRecord;
} // namespace lmp::platform

namespace lmp::core::cast {

// A cast device found on the local network.
struct CastDevice {
    QString id;   // the device's own id (TXT "id"), else its instance name
    QString name; // its friendly name (TXT "fn"), else the instance label
    QString host;
    quint16 port = 8009;

    friend bool operator==(const CastDevice&, const CastDevice&) = default;
};

inline constexpr char CastServiceName[] = "_googlecast._tcp.local";

// The device a resolved record describes; nothing without a host.
[[nodiscard]] std::optional<CastDevice> castDeviceFrom(const platform::DnsServiceRecord& record);

// Browses for up to `timeoutMs`, then resolves each instance (2 s each) and
// returns the devices in instance order, one per device id. Blocking: run
// off the GUI thread.
[[nodiscard]] QList<CastDevice> discoverCastDevices(platform::IDnsServiceBrowser& browser, int timeoutMs);

} // namespace lmp::core::cast
