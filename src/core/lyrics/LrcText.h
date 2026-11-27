#pragma once

#include "core/lyrics/LyricsService.h"

#include <QList>
#include <QString>

namespace lmp::core::lyrics {

// Lyrics as LRC text: one "[mm:ss.xx]line" per synced line, the format other
// players read from a ".lrc" file next to the song (and many from the lyrics
// tag). Pure functions.

// The synced lines as LRC text, ending in a line break; empty without lines.
// Minutes are not capped at 99 (a long recording keeps counting).
[[nodiscard]] QString formatLrc(const QList<LyricLine>& lines);

// Reads LRC text: every line with one or more leading time tags ("[m:ss]",
// "[mm:ss.x]", "[mm:ss.xx]" or "[mm:ss.xxx]"; ":" may stand for the dot)
// gives one synced line per tag. Other tags ("[ar:...]", "[offset:...]")
// and untimed lines are left out. The result is sorted by time.
[[nodiscard]] QList<LyricLine> parseLrc(const QString& text);

// Lyrics read from saved text: synced when it is LRC with at least one timed
// line (plain then holds the lines without their times), plain otherwise.
// Not found for empty or blank text.
[[nodiscard]] LyricsResult lyricsFromSavedText(const QString& text);

} // namespace lmp::core::lyrics
