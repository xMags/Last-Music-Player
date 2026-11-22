#pragma once

#include <QString>
#include <QUrl>

#include "platform/IExternalLinks.h"

namespace lmp::platform::windows {

// Opens allowed addresses with the shell, the way the system opens a link the
// user clicks: web pages in the default browser, folders in File Explorer.
class WindowsExternalLinks final : public IExternalLinks {
public:
    // `dataRoot` is the app's data folder (AppPaths::root()); file addresses
    // are allowed only inside it.
    explicit WindowsExternalLinks(QString dataRoot);

    [[nodiscard]] bool open(const QUrl& url) override;

private:
    QString m_dataRoot;
};

} // namespace lmp::platform::windows
