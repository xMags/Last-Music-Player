#pragma once

#include <QList>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace lmp::core {
struct Track;
} // namespace lmp::core

namespace lmp::ui {

// The small celebrations, drawn by the shell's MotionLayer:
// - the queue arc (queue-fly.ts): after "Play next" or "Add to queue", a
//   small cover arcs from where the pointer last pressed (the menu item or
//   button) to the queue; nothing flies after a keyboard action;
// - the heart burst (heart-bloom.ts): liking an empty heart;
// - the save motion (save-motion.ts): a collection was saved.
// The window's presses are noted by an event filter (installed on the main
// window), because buttons take the press before any QML handler sees it.
//
// GUI thread only.
class MotionEffects : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.effects")

public:
    // A press older than this did not start the queue action that follows.
    static constexpr qint64 PressWindowMs = 1500;

    // `now` gives milliseconds on a steady clock (tests pass their own).
    explicit MotionEffects(std::function<qint64()> now = {}, QObject* parent = nullptr);

    // Where the queue lands: an item and where across it (0.5 is its middle).
    // The layer uses the first visible one, highest priority first (the side
    // panel's Up Next tab before the player bar's queue button).
    Q_INVOKABLE void addQueueTarget(QObject* item, qreal fractionX = 0.5, int priority = 0);
    // [{item, fractionX}] of the targets still alive, in that order.
    Q_INVOKABLE QVariantList queueTargets() const;

    Q_INVOKABLE void heartLiked(QObject* heart);
    Q_INVOKABLE void collectionSaved(QObject* button);

    // A song went to the queue from a press (TrackActions::queued).
    void trackQueued(const core::Track& track);
    // A press in the window, in window coordinates.
    void notePress(const QPointF& windowPosition);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void queueArc(const QPointF& from, const QUrl& source, const QString& artworkId, const QString& label);
    void heartBurst(QObject* heart);
    void saveBurst(QObject* button);

private:
    struct Target {
        QPointer<QObject> item;
        qreal fractionX = 0.5;
        int priority = 0;
    };
    struct Press {
        qint64 at = 0;
        QPointF position;
    };

    std::function<qint64()> m_now;
    QList<Target> m_targets;
    std::optional<Press> m_press;
};

} // namespace lmp::ui
