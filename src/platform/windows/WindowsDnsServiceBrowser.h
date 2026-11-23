#pragma once

#include "platform/IDnsServiceBrowser.h"

namespace lmp::platform::windows {

// DNS-SD on the system's own resolver (DnsServiceBrowse and
// DnsServiceResolve), as the earlier versions used it. Each operation's
// state is shared with the system callback, so a late callback after a
// timeout never touches freed memory.
class WindowsDnsServiceBrowser final : public IDnsServiceBrowser {
public:
    WindowsDnsServiceBrowser() = default;

    [[nodiscard]] std::unique_ptr<IDnsBrowseOperation> beginBrowse(const QString& serviceName) override;
    [[nodiscard]] std::unique_ptr<IDnsResolveOperation> beginResolve(const QString& instanceName) override;
};

} // namespace lmp::platform::windows
