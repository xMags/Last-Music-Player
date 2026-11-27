#pragma once

#include "core/async/Result.h"
#include "core/lyrics/LyricsService.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace lmp::core::downloads {

// What a downloaded song carries for every player: its names, its cover and
// its lyrics. Empty fields are left as the file has them.
struct SongFileExtras {
    QString title;
    QString artist;
    QString album;
    // JPEG or PNG bytes; anything else is not written.
    QByteArray cover;
    // The words, inside the file's lyrics tag.
    QString plainLyrics;
    // Timed lines, also saved as a ".lrc" file next to the song.
    QList<lyrics::LyricLine> syncedLyrics;
};

// The ".lrc" file of a song: its path with the extension changed.
[[nodiscard]] QString lrcPathFor(const QString& audioPath);

// Writes `extras` into the song at `audioPath` (MP4, MP3, FLAC, Ogg, Opus or
// WAV), and its timed lyrics into the ".lrc" file. The tags are written into
// a copy that then replaces the song, so the song is never left half
// written; the replace fails while another program holds the file open
// without sharing it, and the song stays as it was. NotAvailable: the file
// is of a kind that holds no tags; NotFound: it is gone.
//
// Blocking file work: call it off the GUI thread. Thread-safe for different
// files; two calls for one file at once are not.
[[nodiscard]] Result<void> writeSongFileExtras(const QString& audioPath, const SongFileExtras& extras);

// The lyrics saved with a song: its ".lrc" file when that has timed lines,
// else the file's lyrics tag. Empty when there are none. Blocking, like
// writeSongFileExtras.
[[nodiscard]] std::optional<lyrics::LyricsResult> readSavedLyrics(const QString& audioPath);

} // namespace lmp::core::downloads
