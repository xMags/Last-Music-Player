#include "core/lyrics/LyricsService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace lmp::core::lyrics {

namespace {

[[nodiscard]] bool isWebLink(const QString& value)
{
    const QUrl url(value.trimmed());
    return url.isValid() && (url.scheme() == QLatin1StringView("https") || url.scheme() == QLatin1StringView("http"))
        && !url.host().isEmpty();
}

[[nodiscard]] QString encoded(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

[[nodiscard]] QString text(const QJsonObject& object, QLatin1StringView name)
{
    const QJsonValue value = object.value(name);
    return value.isString() ? value.toString() : QString();
}

} // namespace

LyricsQuery lyricsQueryFor(const Track& track)
{
    LyricsQuery query;
    query.artist = track.artist.trimmed();
    query.title = track.title.trimmed();
    query.album = track.album.trimmed();
    if (std::isfinite(track.durationSeconds) && track.durationSeconds > 0) {
        query.durationMs = std::llround(track.durationSeconds * 1000.0);
    }
    if (isWebLink(track.sourceUrl)) {
        query.sourceUrl = track.sourceUrl.trimmed();
        query.sourceReference = track.sourceReference.trimmed();
    }
    return query;
}

QString lyricsPathAndQuery(const LyricsQuery& query)
{
    QString path = QStringLiteral("/v1/music/lyrics?artist=") + encoded(query.artist) + QStringLiteral("&title=")
        + encoded(query.title) + QStringLiteral("&album=") + encoded(query.album)
        + QStringLiteral("&durationMs=") + QString::number(std::max<qint64>(query.durationMs, 0));
    // The song's reference stands in for its link, so the request names no source.
    if (!query.sourceReference.isEmpty() && !query.sourceUrl.isEmpty()) {
        path += QStringLiteral("&sourceRef=") + encoded(query.sourceReference);
    } else if (!query.sourceUrl.isEmpty()) {
        path += QStringLiteral("&sourceUrl=") + encoded(query.sourceUrl);
    }
    return path;
}

LyricsResult parseLyrics(const QByteArray& payload)
{
    LyricsResult result;
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return result;
    }
    const QJsonObject root = document.object();
    result.found = root.value(QLatin1StringView("found")).toBool(false);
    result.instrumental = root.value(QLatin1StringView("instrumental")).toBool(false);
    result.source = text(root, QLatin1StringView("source"));
    result.plain = text(root, QLatin1StringView("plain"));
    for (const QJsonValue& entry : root.value(QLatin1StringView("synced")).toArray()) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject line = entry.toObject();
        const QJsonValue time = line.value(QLatin1StringView("timeMs"));
        const double timeMs = time.isDouble() ? time.toDouble() : 0.0;
        result.synced.append({.timeMs = std::isfinite(timeMs) ? static_cast<qint64>(timeMs) : 0,
                              .text = text(line, QLatin1StringView("text"))});
    }
    std::stable_sort(result.synced.begin(), result.synced.end(),
                     [](const LyricLine& a, const LyricLine& b) { return a.timeMs < b.timeMs; });
    return result;
}

int activeLineIndex(const QList<LyricLine>& lines, qint64 positionMs)
{
    if (lines.isEmpty() || positionMs < lines.constFirst().timeMs) {
        return -1;
    }
    const auto after = std::upper_bound(lines.cbegin(), lines.cend(), positionMs,
                                        [](qint64 position, const LyricLine& line) { return position < line.timeMs; });
    return static_cast<int>(std::distance(lines.cbegin(), after) - 1);
}

LyricsService::LyricsService(Fetch fetch, Scope scope)
    : m_fetch(std::move(fetch))
    , m_scope(std::move(scope))
{
}

QString LyricsService::songKey(const Track& track)
{
    const LyricsQuery query = lyricsQueryFor(track);
    if (!query.sourceUrl.isEmpty()) {
        return QStringLiteral("u|") + query.sourceUrl;
    }
    return query.artist.toLower() + QLatin1Char('|') + query.title.toLower() + QLatin1Char('|')
        + QString::number(query.durationMs / 1000);
}

bool LyricsService::available() const
{
    return m_fetch && m_scope && !m_scope().isEmpty();
}

QFuture<Result<LyricsResult>> LyricsService::lyricsFor(const Track& track)
{
    const QString scope = m_scope ? m_scope() : QString();
    if (scope.isEmpty() || !m_fetch) {
        return QtFuture::makeReadyValueFuture(
            Result<LyricsResult>(Failure::of(FailureKind::Unknown, QStringLiteral("Lyrics are not available."))));
    }
    const QString key = scope + QLatin1Char('|') + songKey(track);
    if (const auto cached = m_cache.constFind(key); cached != m_cache.cend()) {
        return QtFuture::makeReadyValueFuture(Result<LyricsResult>(parseLyrics(*cached)));
    }
    return m_fetch(lyricsQueryFor(track)).then([this, key](const Result<QByteArray>& answer) -> Result<LyricsResult> {
        if (!answer) {
            return answer.failure();
        }
        if (!QJsonDocument::fromJson(answer.value()).isObject()) {
            return Failure::of(FailureKind::InvalidResponse, QStringLiteral("Lyrics could not be read."));
        }
        // A scope change while the request ran clears the cache; this answer
        // still belongs to the song asked for, so it is shown but kept only
        // under the scope that asked.
        store(key, answer.value());
        return parseLyrics(answer.value());
    });
}

void LyricsService::clear()
{
    m_cache.clear();
    m_order.clear();
}

void LyricsService::store(const QString& key, const QByteArray& payload)
{
    if (!m_cache.contains(key)) {
        m_order.push_back(key);
        if (m_order.size() > CacheCapacity) {
            m_cache.remove(m_order.front());
            m_order.pop_front();
        }
    }
    m_cache.insert(key, payload);
}

} // namespace lmp::core::lyrics
