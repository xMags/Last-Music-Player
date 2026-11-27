#include "core/lyrics/LrcText.h"

#include <QChar>
#include <QStringList>
#include <QStringView>

#include <algorithm>
#include <optional>

namespace lmp::core::lyrics {

namespace {

// Reads "[m:ss]" up to "[mmm:ss.xxx]" at the start of `tag` (without its
// brackets); empty for any other tag.
[[nodiscard]] std::optional<qint64> timeTag(QStringView tag)
{
    const qsizetype colon = tag.indexOf(QLatin1Char(':'));
    if (colon < 1) {
        return std::nullopt;
    }
    bool minutesOk = false;
    const qint64 minutes = tag.first(colon).toLongLong(&minutesOk);
    if (!minutesOk || minutes < 0) {
        return std::nullopt;
    }
    QStringView rest = tag.sliced(colon + 1);
    // Seconds, then an optional fraction after "." or ":".
    qsizetype split = rest.indexOf(QLatin1Char('.'));
    if (split < 0) {
        split = rest.indexOf(QLatin1Char(':'));
    }
    const QStringView secondsText = split < 0 ? rest : rest.first(split);
    const QStringView fractionText = split < 0 ? QStringView() : rest.sliced(split + 1);
    if (secondsText.size() != 2 || fractionText.size() > 3) {
        return std::nullopt;
    }
    bool secondsOk = false;
    const qint64 seconds = secondsText.toLongLong(&secondsOk);
    if (!secondsOk || seconds < 0 || seconds > 59) {
        return std::nullopt;
    }
    qint64 fractionMs = 0;
    if (!fractionText.isEmpty()) {
        bool fractionOk = false;
        const qint64 fraction = fractionText.toLongLong(&fractionOk);
        if (!fractionOk || fraction < 0) {
            return std::nullopt;
        }
        // "5" is 500 ms, "05" is 50 ms, "005" is 5 ms.
        fractionMs = fractionText.size() == 1 ? fraction * 100 : fractionText.size() == 2 ? fraction * 10 : fraction;
    }
    return (minutes * 60 + seconds) * 1000 + fractionMs;
}

[[nodiscard]] QString twoDigits(qint64 value)
{
    return QString::number(value).rightJustified(2, QLatin1Char('0'));
}

} // namespace

QString formatLrc(const QList<LyricLine>& lines)
{
    QString text;
    for (const LyricLine& line : lines) {
        const qint64 ms = std::max<qint64>(0, line.timeMs);
        const qint64 minutes = ms / 60000;
        const qint64 seconds = (ms / 1000) % 60;
        const qint64 hundredths = (ms % 1000) / 10;
        // A line break inside a line would start an untimed one.
        QString words = line.text;
        words.replace(QLatin1Char('\r'), QLatin1Char(' '));
        words.replace(QLatin1Char('\n'), QLatin1Char(' '));
        text += QLatin1Char('[') + twoDigits(minutes) + QLatin1Char(':') + twoDigits(seconds) + QLatin1Char('.')
            + twoDigits(hundredths) + QLatin1Char(']') + words + QLatin1Char('\n');
    }
    return text;
}

QList<LyricLine> parseLrc(const QString& text)
{
    QList<LyricLine> lines;
    for (QStringView row : QStringView(text).split(QLatin1Char('\n'))) {
        if (row.endsWith(QLatin1Char('\r'))) {
            row.chop(1);
        }
        QList<qint64> times;
        while (row.startsWith(QLatin1Char('['))) {
            const qsizetype close = row.indexOf(QLatin1Char(']'));
            if (close < 0) {
                break;
            }
            const std::optional<qint64> time = timeTag(row.sliced(1, close - 1));
            if (!time) {
                break;
            }
            times.append(*time);
            row = row.sliced(close + 1);
        }
        for (const qint64 time : times) {
            lines.append(LyricLine{time, row.trimmed().toString()});
        }
    }
    std::stable_sort(lines.begin(), lines.end(),
                     [](const LyricLine& left, const LyricLine& right) { return left.timeMs < right.timeMs; });
    return lines;
}

LyricsResult lyricsFromSavedText(const QString& text)
{
    LyricsResult result;
    if (text.trimmed().isEmpty()) {
        return result;
    }
    result.found = true;
    result.source = QStringLiteral("saved");
    result.synced = parseLrc(text);
    if (result.synced.isEmpty()) {
        result.plain = text.trimmed();
        return result;
    }
    QStringList words;
    for (const LyricLine& line : result.synced) {
        words.append(line.text);
    }
    result.plain = words.join(QLatin1Char('\n'));
    return result;
}

} // namespace lmp::core::lyrics
