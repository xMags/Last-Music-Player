#pragma once

#include "core/model/Track.h"
#include "core/playback/QueueEngine.h"

#include <QList>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace lmp::core {

// The songs Previous goes back through: each step forward remembers the song
// that was playing and where the context stood, up to Capacity steps (the
// oldest is forgotten first). Going back restores that place in the context,
// or puts the song back at its front when the context has changed since,
// and returns a song taken from the user queue to its front.
//
// Not thread-safe; owned by the player controller.
class PlaybackBackHistory final {
public:
    static constexpr qsizetype Capacity = 50;

    struct Restored {
        QueueEngine queue;
        Track track;
    };

    // Remembers `playing` (nothing when it is empty) as it stood in `queue`.
    // `takenFromUserQueue` is the song the step took from the user queue, if
    // any; going back puts it at the front of the user queue again.
    void remember(const std::optional<Track>& playing, const QueueEngine& queue,
                  const std::optional<Track>& takenFromUserQueue = std::nullopt);

    // Takes the newest entry and applies it to `queue`; empty when there is none.
    [[nodiscard]] std::optional<Restored> restorePrevious(const QueueEngine& queue);

    void clear() noexcept { m_entries.clear(); }
    [[nodiscard]] qsizetype size() const noexcept { return m_entries.size(); }

private:
    struct Entry {
        Track track;
        // The queue identity of the current context song at the time, or
        // empty when no context song was current.
        QString contextIdentity;
        std::optional<Track> replayTrack;
    };

    QList<Entry> m_entries;
};

} // namespace lmp::core
