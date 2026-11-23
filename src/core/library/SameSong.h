#pragma once

#include <QList>
#include <QSet>
#include <QString>

namespace lmp::core {

// Whether two songs are the same song from different sources, so a like keeps
// one copy per song (the reference client's same-song.ts; the account service
// applies the same rule when a like is saved, and every client must pass the
// shared cases). Same song: equal ids, or the same song name (read out of
// upload titles such as "Film: Song | Cast | Singers" or "Song - Artist",
// brackets dropped), at least one shared credited artist, and the same
// recording markers (remix, live, cover, ...). No fuzzy scoring: a missed
// match only means two hearts, a false one would hide a different song.

struct SongIdentity {
    QString id;
    QString title;
    QString artist;
};

struct SongKey {
    // The bare song name in comparable form; empty when there is none.
    QString name;
    // Credited artist words (the artist field and title credits), generic words removed.
    QSet<QString> artists;
    // Recording markers found anywhere in the title.
    QSet<QString> variants;
};

// Accents removed (NFKD, marks dropped), lower case, every run of characters
// that are not letters or digits made one space, trimmed.
[[nodiscard]] QString comparableText(const QString& value);
[[nodiscard]] SongKey songKey(const QString& title, const QString& artist);
[[nodiscard]] bool sameSongKeys(const SongKey& a, const SongKey& b);
[[nodiscard]] bool isSameSong(const SongIdentity& a, const SongIdentity& b);
// Every song in `pool` that is the same song as `song` (its own id included).
[[nodiscard]] QList<SongIdentity> sameSongMatches(const SongIdentity& song, const QList<SongIdentity>& pool);

} // namespace lmp::core
