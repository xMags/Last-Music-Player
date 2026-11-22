#pragma once

#include <QUrl>

namespace lmp::platform {

// Hands an address to the system: a web page opens in the default browser, a
// folder in the file manager. Only https pages and places inside the app's
// own data folder may be opened (core::isAllowedExternalLink); everything
// else is refused without asking the system.
//
// Used on the GUI thread.
class IExternalLinks {
public:
    virtual ~IExternalLinks() = default;

    // True when the system accepted the address. False when it was refused
    // by the rule above or the system could not open it.
    [[nodiscard]] virtual bool open(const QUrl& url) = 0;

protected:
    IExternalLinks() = default;
    IExternalLinks(const IExternalLinks&) = default;
    IExternalLinks& operator=(const IExternalLinks&) = default;
};

} // namespace lmp::platform
