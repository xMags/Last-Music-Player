#pragma once

#include <QDateTime>
#include <QString>

namespace lmp::core::home {

// When a Listen Again song was played, as its card's last line (home.ts
// relativeDate): "Just now", "12 min ago", "3 hr ago", "Yesterday",
// "4 days ago"; "Recently played" for a time it cannot read, or one more
// than a day ahead of `now` (a play from a device whose clock runs a little
// ahead is still "Just now"). Empty for no time. `isoTime` is an ISO 8601
// date and time.
[[nodiscard]] QString relativePlayedText(const QString& isoTime, const QDateTime& now);

} // namespace lmp::core::home
