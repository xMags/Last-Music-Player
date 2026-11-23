#include <QSignalSpy>
#include <QTest>

#include "core/model/Track.h"
#include "ui/motion/MotionEffects.h"

using lmp::core::Track;
using lmp::ui::MotionEffects;

// When the queue arc flies, and where it lands.
class MotionEffectsTest : public QObject {
    Q_OBJECT

private slots:
    void aQueuedSongArcsFromTheLastPress()
    {
        qint64 now = 1000;
        MotionEffects effects([&now]() { return now; });
        QSignalSpy arcs(&effects, &MotionEffects::queueArc);
        Track track;
        track.title = QStringLiteral("song");
        track.artworkUrl = QStringLiteral("https://example.invalid/a.jpg");

        effects.notePress(QPointF(120, 340));
        now += 400;
        effects.trackQueued(track);
        QCOMPARE(arcs.count(), 1);
        QCOMPARE(arcs.first().at(0).toPointF(), QPointF(120, 340));
        QCOMPARE(arcs.first().at(1).toUrl(), QUrl(track.artworkUrl));
        QCOMPARE(arcs.first().at(3).toString(), QStringLiteral("S"));

        // The press is used once; a keyboard action has none.
        effects.trackQueued(track);
        QCOMPARE(arcs.count(), 1);
    }

    void anOldPressDoesNotFly()
    {
        qint64 now = 0;
        MotionEffects effects([&now]() { return now; });
        QSignalSpy arcs(&effects, &MotionEffects::queueArc);
        effects.notePress(QPointF(1, 1));
        now += MotionEffects::PressWindowMs + 1;
        effects.trackQueued(Track{});
        QCOMPARE(arcs.count(), 0);
    }

    void queueTargetsComeHighestPriorityFirstAndForgetDeadOnes()
    {
        MotionEffects effects;
        QObject button;
        auto tabs = std::make_unique<QObject>();
        effects.addQueueTarget(&button, 0.5, 0);
        effects.addQueueTarget(tabs.get(), 0.25, 1);
        QVariantList targets = effects.queueTargets();
        QCOMPARE(targets.size(), 2);
        QCOMPARE(targets.at(0).toMap().value(QStringLiteral("item")).value<QObject*>(), tabs.get());
        QCOMPARE(targets.at(0).toMap().value(QStringLiteral("fractionX")).toDouble(), 0.25);
        tabs.reset();
        targets = effects.queueTargets();
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets.at(0).toMap().value(QStringLiteral("item")).value<QObject*>(), &button);
    }

    void heartAndSaveRequestsPassTheirItem()
    {
        MotionEffects effects;
        QSignalSpy hearts(&effects, &MotionEffects::heartBurst);
        QSignalSpy saves(&effects, &MotionEffects::saveBurst);
        QObject heart;
        effects.heartLiked(&heart);
        effects.heartLiked(nullptr);
        effects.collectionSaved(&heart);
        QCOMPARE(hearts.count(), 1);
        QCOMPARE(saves.count(), 1);
    }
};

QTEST_GUILESS_MAIN(MotionEffectsTest)
#include "tst_motion_effects.moc"
