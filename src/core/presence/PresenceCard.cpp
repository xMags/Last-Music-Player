#include "core/presence/PresenceCard.h"

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <QLatin1StringView>

#include <cmath>

namespace lmp::core::presence {

namespace {

constexpr QLatin1StringView kTitleThenArtist{"titleThenArtist"};
constexpr QLatin1StringView kTitleArtistThenAlbum{"titleArtistThenAlbum"};
constexpr QLatin1StringView kShow{"show"};
constexpr QLatin1StringView kHideAfterDelay{"hideAfterDelay"};
constexpr QLatin1StringView kHide{"hide"};

const QString kSeparator = QStringLiteral(" · ");

} // namespace

PresenceLines presenceLines(const QString& title, const QString& artist, const QString& album,
                            const PresenceOptions& options, const QString& castDevice)
{
    PresenceLines lines;
    const QString trimmedTitle = title.trimmed();
    const QString shownArtist = artist.trimmed();
    QString shownAlbum = album.trimmed();
    // A single's album repeats its title; saying it twice says nothing.
    if (shownAlbum == trimmedTitle) {
        shownAlbum.clear();
    }
    const QString shownTitle = trimmedTitle.isEmpty() ? QStringLiteral("Unknown title") : trimmedTitle;

    if (!options.showSong) {
        // Nothing that names the song: no title, artist, album or cover.
        lines.details = QString::fromUtf16(PrivateDetails);
        lines.state = QString::fromUtf16(PrivateState);
        lines.showArtwork = false;
    } else if (options.layout == PresenceLayout::TitleArtistThenAlbum) {
        lines.details = shownArtist.isEmpty() ? shownTitle : shownTitle + kSeparator + shownArtist;
        lines.state = shownAlbum.isEmpty() ? shownArtist : shownAlbum;
    } else {
        lines.details = shownTitle;
        lines.state = shownArtist;
    }

    const QString device = castDevice.trimmed();
    if (options.showCastDevice && !device.isEmpty()) {
        const QString on = QStringLiteral("on ") + device;
        lines.state = lines.state.isEmpty() ? on : lines.state + kSeparator + on;
    }
    if (lines.state.isEmpty()) {
        lines.state = QStringLiteral("Unknown artist");
    }
    return lines;
}

bool shouldPublish(const PresenceVisibility& visibility, qint64 pausedSince, qint64 now)
{
    if (visibility.snoozeUntil > now) {
        return false;
    }
    if (pausedSince <= 0) {
        return true;
    }
    const qint64 pausedFor = now - pausedSince;
    switch (visibility.whenPaused) {
    case PausedBehavior::Show:
        return true;
    case PausedBehavior::HideAfterDelay:
        return pausedFor < PausedHideDelaySeconds;
    case PausedBehavior::Hide:
        return pausedFor < PausedHideGraceSeconds;
    }
    return true;
}

std::optional<qint64> nextVisibilityChange(const PresenceVisibility& visibility, qint64 pausedSince, qint64 now)
{
    std::optional<qint64> next;
    const auto consider = [&next, now](qint64 at) {
        if (at > now && (!next || at < *next)) {
            next = at;
        }
    };
    consider(visibility.snoozeUntil);
    if (pausedSince > 0) {
        if (visibility.whenPaused == PausedBehavior::HideAfterDelay) {
            consider(pausedSince + PausedHideDelaySeconds);
        } else if (visibility.whenPaused == PausedBehavior::Hide) {
            consider(pausedSince + PausedHideGraceSeconds);
        }
    }
    return next;
}

PresenceOptions presenceOptionsFrom(const SettingsStore& settings)
{
    namespace keys = settings::keys;
    PresenceOptions options;
    options.showSong = settings.boolean(keys::DiscordShowSong, true);
    options.layout = layoutFromStored(settings.string(keys::DiscordLayout));
    options.showCastDevice = settings.boolean(keys::DiscordShowCastDevice, true);
    return options;
}

PresenceVisibility presenceVisibilityFrom(const SettingsStore& settings)
{
    namespace keys = settings::keys;
    PresenceVisibility visibility;
    visibility.whenPaused = pausedBehaviorFromStored(settings.string(keys::DiscordWhenPaused));
    // Unix seconds, kept as a JSON number; anything not a sane whole time
    // reads as no pause.
    const double until = settings.number(keys::DiscordSnoozeUntil, 0.0);
    visibility.snoozeUntil = std::isfinite(until) && until > 0.0 && until < 1.0e12 ? std::llround(until) : 0;
    return visibility;
}

QString storedLayout(PresenceLayout layout)
{
    return layout == PresenceLayout::TitleArtistThenAlbum ? QString(kTitleArtistThenAlbum)
                                                          : QString(kTitleThenArtist);
}

PresenceLayout layoutFromStored(const QString& stored)
{
    return stored == kTitleArtistThenAlbum ? PresenceLayout::TitleArtistThenAlbum : PresenceLayout::TitleThenArtist;
}

QString storedPausedBehavior(PausedBehavior behavior)
{
    switch (behavior) {
    case PausedBehavior::HideAfterDelay:
        return QString(kHideAfterDelay);
    case PausedBehavior::Hide:
        return QString(kHide);
    case PausedBehavior::Show:
        break;
    }
    return QString(kShow);
}

PausedBehavior pausedBehaviorFromStored(const QString& stored)
{
    if (stored == kHideAfterDelay) {
        return PausedBehavior::HideAfterDelay;
    }
    if (stored == kHide) {
        return PausedBehavior::Hide;
    }
    return PausedBehavior::Show;
}

} // namespace lmp::core::presence
