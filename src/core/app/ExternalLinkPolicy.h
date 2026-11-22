#pragma once

#include <QString>
#include <QUrl>

namespace lmp::core {

// Which addresses the app may hand to the system to open.
//
// - An https address with a host and no user name or password.
// - A local file: address (no host) whose path, with "." and ".." resolved,
//   is `dataRoot` itself or lies inside it, without a query or fragment.
//   Paths are compared without regard to case on Windows, where the file
//   system ignores it, and exactly elsewhere. An empty `dataRoot` allows no
//   file at all.
//
// Everything else (http, other schemes, network shares, relative addresses)
// is refused.
[[nodiscard]] bool isAllowedExternalLink(const QUrl& url, const QString& dataRoot);

} // namespace lmp::core
