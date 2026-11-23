#pragma once

#include "core/async/Result.h"
#include "core/model/Track.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>

namespace lmp::core {

// The queue autoplay tops up: the player, or a fake in tests.
class AutoplayQueue {
public:
    virtual ~AutoplayQueue() = default;

    [[nodiscard]] virtual std::optional<Track> currentTrack() const = 0;
    [[nodiscard]] virtual bool autoplay() const = 0;
    [[nodiscard]] virtual bool repeatOff() const = 0;
    // Songs still to play after the current one.
    [[nodiscard]] virtual qsizetype upcomingCount() const = 0;
    // Every song in the queue, played or not.
    [[nodiscard]] virtual QList<Track> queuedTracks() const = 0;
    // Appends the songs at the end; `resume` goes on to the first of them
    // when the queue had run out.
    virtual void appendAutoplayTracks(const QList<Track>& tracks, bool resume) = 0;
    // Plays `track` alone, replacing the queue.
    virtual void playAlone(const Track& track) = 0;

protected:
    AutoplayQueue() = default;
    AutoplayQueue(const AutoplayQueue&) = default;
    AutoplayQueue& operator=(const AutoplayQueue&) = default;
};

// Where related songs come from (the service in Account mode).
struct AutoplaySource {
    // Songs like `seed` (by its link); a failure when it cannot ask.
    std::function<QFuture<Result<QList<Track>>>(const Track& seed)> related;
    // A plain search, the fallback when related songs give nothing.
    std::function<QFuture<Result<QList<Track>>>(const QString& query)> search;
    // Songs played lately, which autoplay does not bring back.
    std::function<QList<Track>()> history;
};

// Autoplay: while it is on and repeat is off, a queue with two songs or
// fewer to go is topped up with up to 20 related songs (a search for the
// artist, or the title, when there are none). Songs already queued, played
// lately, or added earlier in this run are left out. When the queue had run
// out, playback goes on with the first new song. A request started for an
// older queue, or before autoplay or repeat changed, is dropped (an epoch).
//
// "Start mix" plays a song alone; autoplay fills the queue after it, or,
// while autoplay is off, the related songs are added once.
//
// GUI thread only.
class AutoplayService : public QObject {
    Q_OBJECT

public:
    static constexpr qsizetype TopUpThreshold = 2;
    static constexpr qsizetype MaximumTracks = 20;

    // `queue` outlives the service.
    AutoplayService(AutoplayQueue& queue, AutoplaySource source, QObject* parent = nullptr);

    // A point where the queue may need songs (the player's autoplayExtensionWanted).
    void extensionWanted(bool queueEnded);
    // A new queue replaced the old one: forget the songs this run added.
    void contextReplaced();
    // Autoplay or repeat changed, or the mode did: drop requests in flight.
    void invalidate();
    void startMix(const Track& track);

    [[nodiscard]] bool loading() const { return m_inFlight.has_value(); }

signals:
    void loadingChanged();
    // "Start mix" found nothing to add.
    void mixFoundNothing();

private:
    [[nodiscard]] bool owns(std::uint64_t epoch) const;
    [[nodiscard]] QList<Track> fresh(const QList<Track>& candidates, const Track& seed) const;
    // Related songs for `seed`, else the search; `done` gets the fresh ones.
    void gather(const Track& seed, std::function<bool()> stillWanted,
                std::function<void(QList<Track>)> done);
    void finish(std::uint64_t epoch);

    AutoplayQueue& m_queue;
    AutoplaySource m_source;
    std::uint64_t m_epoch = 0;
    std::optional<std::uint64_t> m_inFlight;
    bool m_resumeWhenReady = false;
    // Songs this run added, by queue identity.
    QSet<QString> m_seen;
};

} // namespace lmp::core
