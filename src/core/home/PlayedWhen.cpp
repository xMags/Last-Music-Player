#include "core/home/PlayedWhen.h"

#include <QCoreApplication>

namespace lmp::core::home {

namespace {

constexpr qint64 MinuteMs = 60'000;
constexpr qint64 DayMs = 24 * 60 * MinuteMs;

QString text(const char* source)
{
    return QCoreApplication::translate("PlayedWhen", source);
}

} // namespace

QString relativePlayedText(const QString& isoTime, const QDateTime& now)
{
    const QString value = isoTime.trimmed();
    if (value.isEmpty()) {
        return {};
    }
    QDateTime time = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!time.isValid()) {
        time = QDateTime::fromString(value, Qt::ISODate);
    }
    if (!time.isValid()) {
        return text("Recently played");
    }
    const qint64 elapsed = time.msecsTo(now);
    if (elapsed < 0) {
        return elapsed > -DayMs ? text("Just now") : text("Recently played");
    }
    const qint64 minutes = elapsed / MinuteMs;
    if (minutes < 1) {
        return text("Just now");
    }
    if (minutes < 60) {
        return text("%1 min ago").arg(minutes);
    }
    const qint64 hours = minutes / 60;
    if (hours < 24) {
        return text("%1 hr ago").arg(hours);
    }
    const qint64 days = hours / 24;
    return days == 1 ? text("Yesterday") : text("%1 days ago").arg(days);
}

} // namespace lmp::core::home
