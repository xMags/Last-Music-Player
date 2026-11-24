#pragma once

#include <QString>
#include <QtGlobal>

#include <optional>

namespace lmp::core {
class SettingsStore;
}

namespace lmp::core::presence {

// How the Discord card is laid out (the DiscordLayout setting).
enum class PresenceLayout {
    TitleThenArtist,      // the title, then the artist
    TitleArtistThenAlbum, // "Title · Artist", then the album
};

// What the card does while playback is paused (DiscordWhenPaused).
enum class PausedBehavior {
    Show,           // keep the song on the card
    HideAfterDelay, // clear it once paused for PausedHideDelaySeconds
    Hide,           // clear it once paused for PausedHideGraceSeconds
};

// "Hide after 5 minutes".
inline constexpr qint64 PausedHideDelaySeconds = 5 * 60;
// Even "hide right away" waits this long: the moment between one song and
// the next reads as a pause, and clearing for it would flicker the card.
inline constexpr qint64 PausedHideGraceSeconds = 3;
// "Pause for 1 hour".
inline constexpr qint64 SharingPauseSeconds = 60 * 60;

// The private card: music is playing, nothing says which.
inline constexpr char16_t PrivateDetails[] = u"Listening to music";
inline constexpr char16_t PrivateState[] = u"Song kept private";

// How the card is drawn. The defaults are the card before these choices.
struct PresenceOptions {
    bool showSong = true;
    PresenceLayout layout = PresenceLayout::TitleThenArtist;
    bool showCastDevice = true;
};

// When the card is shown at all.
struct PresenceVisibility {
    PausedBehavior whenPaused = PausedBehavior::Show;
    // Unix seconds until which nothing is shared; 0 when not paused.
    qint64 snoozeUntil = 0;
};

// The card's two lines, and whether the song's cover may go with them.
struct PresenceLines {
    QString details;
    QString state;
    bool showArtwork = true;
};

// The lines for a song. An album that repeats the title (a single) is left
// out. `castDevice` names the device the music plays on, empty when it plays
// here; it is added as "· on <device>" when the options allow it.
[[nodiscard]] PresenceLines presenceLines(const QString& title, const QString& artist, const QString& album,
                                          const PresenceOptions& options, const QString& castDevice);

// Whether a card should be on Discord now. `pausedSince` is when playback
// last stopped playing a song, or 0 while it plays.
[[nodiscard]] bool shouldPublish(const PresenceVisibility& visibility, qint64 pausedSince, qint64 now);

// When shouldPublish next changes its answer without anything else changing
// (a pause running out its delay, sharing coming back), or nothing.
[[nodiscard]] std::optional<qint64> nextVisibilityChange(const PresenceVisibility& visibility, qint64 pausedSince,
                                                         qint64 now);

// The choices as Settings.json keeps them: DiscordShowSong,
// DiscordLayout ("titleThenArtist" or "titleArtistThenAlbum"),
// DiscordShowCastDevice, DiscordWhenPaused ("show", "hideAfterDelay" or
// "hide") and DiscordSnoozeUntil. A missing or unknown value reads as the
// default.
[[nodiscard]] PresenceOptions presenceOptionsFrom(const SettingsStore& settings);
[[nodiscard]] PresenceVisibility presenceVisibilityFrom(const SettingsStore& settings);

[[nodiscard]] QString storedLayout(PresenceLayout layout);
[[nodiscard]] PresenceLayout layoutFromStored(const QString& stored);
[[nodiscard]] QString storedPausedBehavior(PausedBehavior behavior);
[[nodiscard]] PausedBehavior pausedBehaviorFromStored(const QString& stored);

} // namespace lmp::core::presence
