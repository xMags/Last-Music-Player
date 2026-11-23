#pragma once

#include "core/async/Result.h"
#include "core/model/Track.h"

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QString>

#include <deque>
#include <functional>

namespace lmp::core::lyrics {

struct LyricLine {
    qint64 timeMs = 0;
    QString text;
};

// What the service knows about a song's lyrics (GET /v1/music/lyrics).
struct LyricsResult {
    bool found = false;
    bool instrumental = false;
    QString source;
    QString plain;
    // Sorted by time.
    QList<LyricLine> synced;
};

// What a lyrics request names. The song's link goes as its server reference
// when it has one; only a web link is ever sent, never a path on this PC.
struct LyricsQuery {
    QString artist;
    QString title;
    QString album;
    qint64 durationMs = 0;
    QString sourceUrl;
    QString sourceReference;
};

// "artist=...&title=..." for /v1/music/lyrics, every value percent-encoded.
[[nodiscard]] QString lyricsPathAndQuery(const LyricsQuery& query);

// The query for a song.
[[nodiscard]] LyricsQuery lyricsQueryFor(const Track& track);

// Pure: reads the lyrics answer. Empty or malformed input gives an empty
// result; synced lines are sorted by time.
[[nodiscard]] LyricsResult parseLyrics(const QByteArray& payload);

// Pure: the line whose time is the greatest not after `positionMs`; -1
// before the first line or without lines. `lines` is sorted by time.
[[nodiscard]] int activeLineIndex(const QList<LyricLine>& lines, qint64 positionMs);

// Fetches and keeps lyrics. The last 32 answers are kept, keyed by the
// remote scope (whose account or key asked) and the song: its web link, else
// its artist, title and whole seconds. A failed request is not kept, so a
// retry asks again. Not thread-safe: used on the GUI thread.
class LyricsService {
public:
    static constexpr std::size_t CacheCapacity = 32;

    // Sends the request for the current scope; a failure when there is none.
    using Fetch = std::function<QFuture<Result<QByteArray>>(const LyricsQuery&)>;
    // The current remote scope; empty when nothing remote is available.
    using Scope = std::function<QString()>;

    LyricsService(Fetch fetch, Scope scope);

    // Whether there is a scope to ask in (an account or a key).
    [[nodiscard]] bool available() const;
    [[nodiscard]] QFuture<Result<LyricsResult>> lyricsFor(const Track& track);
    void clear();

    // The cache key of a song, without the scope.
    [[nodiscard]] static QString songKey(const Track& track);

private:
    void store(const QString& key, const QByteArray& payload);

    Fetch m_fetch;
    Scope m_scope;
    QHash<QString, QByteArray> m_cache;
    std::deque<QString> m_order;
};

} // namespace lmp::core::lyrics
