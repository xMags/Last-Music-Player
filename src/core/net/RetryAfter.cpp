#include "core/net/RetryAfter.h"

#include <QByteArray>
#include <QLocale>
#include <QString>
#include <QTimeZone>

#include <algorithm>

namespace lmp::core::net {

namespace {

[[nodiscard]] bool isAllDigits(const QByteArray& text)
{
    return std::all_of(text.cbegin(), text.cend(), [](char character) {
        return character >= '0' && character <= '9';
    });
}

[[nodiscard]] int clampSeconds(qint64 seconds)
{
    return static_cast<int>(std::clamp<qint64>(seconds, 0, MaxRetryAfterSeconds));
}

[[nodiscard]] std::optional<int> delaySeconds(const QByteArray& digits)
{
    // More than ten digits is past any representable delay; it still means
    // "wait as long as allowed".
    if (digits.size() > 10) {
        return MaxRetryAfterSeconds;
    }
    bool ok = false;
    const qint64 seconds = digits.toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return clampSeconds(seconds);
}

[[nodiscard]] std::optional<int> dateDelaySeconds(const QByteArray& text, const QDateTime& nowUtc)
{
    // Month and day names are English in HTTP, whatever the user's locale.
    const QDateTime parsed = QLocale::c().toDateTime(QString::fromLatin1(text),
                                                     QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
    if (!parsed.isValid()) {
        return std::nullopt;
    }
    const QDateTime when(parsed.date(), parsed.time(), QTimeZone::UTC);
    return clampSeconds(nowUtc.secsTo(when));
}

} // namespace

std::optional<int> parseRetryAfterSeconds(QByteArrayView value, const QDateTime& nowUtc)
{
    const QByteArray text = value.toByteArray().trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    if (isAllDigits(text)) {
        return delaySeconds(text);
    }
    return dateDelaySeconds(text, nowUtc);
}

} // namespace lmp::core::net
