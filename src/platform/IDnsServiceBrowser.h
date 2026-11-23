#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace lmp::platform {

// One resolved DNS-SD service instance on the local network.
struct DnsServiceRecord {
    // The full instance name, e.g. "Living Room-1a2b._googlecast._tcp.local".
    QString instanceName;
    // An IPv4 address when the answer had one, else the host name.
    QString host;
    quint16 port = 0;
    // TXT record keys in lower case.
    QHash<QString, QString> properties;
};

// A browse that collects instance names until it completes or is
// cancelled. Blocking: use off the GUI thread.
class IDnsBrowseOperation {
public:
    virtual ~IDnsBrowseOperation() = default;
    // True when the system finished the browse within the time.
    [[nodiscard]] virtual bool wait(int timeoutMs) = 0;
    // Stops the browse and waits until the system has let go of it.
    virtual void cancelAndWait() noexcept = 0;
    [[nodiscard]] virtual QStringList instances() const = 0;

protected:
    IDnsBrowseOperation() = default;
};

// A resolve of one instance name. Blocking: use off the GUI thread.
class IDnsResolveOperation {
public:
    virtual ~IDnsResolveOperation() = default;
    [[nodiscard]] virtual bool wait(int timeoutMs) = 0;
    virtual void cancelAndWait() noexcept = 0;
    [[nodiscard]] virtual std::optional<DnsServiceRecord> record() const = 0;

protected:
    IDnsResolveOperation() = default;
};

// Multicast DNS service discovery. Each call returns nothing when the
// system refused to start the query. Thread-safe.
class IDnsServiceBrowser {
public:
    virtual ~IDnsServiceBrowser() = default;
    IDnsServiceBrowser(const IDnsServiceBrowser&) = delete;
    IDnsServiceBrowser& operator=(const IDnsServiceBrowser&) = delete;

    // `serviceName` like "_googlecast._tcp.local".
    [[nodiscard]] virtual std::unique_ptr<IDnsBrowseOperation> beginBrowse(const QString& serviceName) = 0;
    [[nodiscard]] virtual std::unique_ptr<IDnsResolveOperation> beginResolve(const QString& instanceName) = 0;

protected:
    IDnsServiceBrowser() = default;
};

} // namespace lmp::platform
