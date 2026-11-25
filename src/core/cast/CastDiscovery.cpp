#include "core/cast/CastDiscovery.h"

#include "platform/IDnsServiceBrowser.h"

#include <QDeadlineTimer>
#include <QSet>

#include <algorithm>

namespace lmp::core::cast {

namespace {

constexpr int ResolveTimeoutMs = 2000;
constexpr quint16 DefaultCastPort = 8009;

} // namespace

std::optional<CastDevice> castDeviceFrom(const platform::DnsServiceRecord& record)
{
    if (record.host.isEmpty()) {
        return std::nullopt;
    }
    CastDevice device;
    device.host = record.host;
    device.port = record.port != 0 ? record.port : DefaultCastPort;
    QString label = record.instanceName;
    const qsizetype dot = label.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        label.truncate(dot);
    }
    const QString friendlyName = record.properties.value(QStringLiteral("fn"));
    const QString deviceId = record.properties.value(QStringLiteral("id"));
    device.name = friendlyName.isEmpty() ? label : friendlyName;
    device.id = !deviceId.isEmpty() ? deviceId : (!record.instanceName.isEmpty() ? record.instanceName : record.host);
    return device;
}

QList<CastDevice> discoverCastDevices(platform::IDnsServiceBrowser& browser, int timeoutMs,
                                     const DiscoveryLimits& limits)
{
    const QDeadlineTimer deadline(std::max(limits.totalMs, 0));
    const auto stopped = [&limits]() { return limits.stop != nullptr && limits.stop->load(); };
    const std::unique_ptr<platform::IDnsBrowseOperation> browse =
        browser.beginBrowse(QString::fromLatin1(CastServiceName));
    if (!browse) {
        return {};
    }
    // The browse rarely completes by itself; the time is the scan window.
    (void)browse->wait(timeoutMs);
    browse->cancelAndWait();

    QList<CastDevice> devices;
    QSet<QString> seen;
    const QStringList instances = browse->instances().mid(0, std::max(limits.maxInstances, 0));
    for (const QString& instance : instances) {
        const qint64 left = deadline.remainingTime();
        if (stopped() || left == 0) {
            break;
        }
        const std::unique_ptr<platform::IDnsResolveOperation> resolve = browser.beginResolve(instance);
        if (!resolve) {
            continue;
        }
        const int wait = left < 0 ? ResolveTimeoutMs : static_cast<int>(std::min<qint64>(ResolveTimeoutMs, left));
        const bool completed = resolve->wait(wait);
        resolve->cancelAndWait();
        if (!completed) {
            continue;
        }
        const std::optional<platform::DnsServiceRecord> record = resolve->record();
        const std::optional<CastDevice> device = record ? castDeviceFrom(*record) : std::nullopt;
        if (!device || device->id.isEmpty()) {
            continue;
        }
        if (!seen.contains(device->id)) {
            seen.insert(device->id);
            devices.append(*device);
        }
    }
    return devices;
}

} // namespace lmp::core::cast
