#include <QSignalSpy>
#include <QTest>

#include "ui/motion/CoverFlight.h"

using lmp::ui::CoverFlight;

// When a card's press leads to a cover flight.
class CoverFlightTest : public QObject {
    Q_OBJECT

private slots:
    void aFreshPressFliesOnceToTheHeader()
    {
        qint64 now = 1000;
        CoverFlight flight([&now]() { return now; });
        QSignalSpy wanted(&flight, &CoverFlight::flightWanted);
        QObject header;
        flight.notePress(QRectF(10, 20, 160, 160), QUrl(QStringLiteral("https://example.invalid/a.jpg")),
                         QStringLiteral("id"), QStringLiteral("A"), false);
        now += 600;
        flight.headerShown(&header);
        QCOMPARE(wanted.count(), 1);
        QCOMPARE(wanted.first().at(0).toRectF(), QRectF(10, 20, 160, 160));
        QCOMPARE(wanted.first().at(5).value<QObject*>(), &header);
        // The press is used up.
        flight.headerShown(&header);
        QCOMPARE(wanted.count(), 1);
    }

    void anOldOrTinyPressDoesNotFly()
    {
        qint64 now = 0;
        CoverFlight flight([&now]() { return now; });
        QSignalSpy wanted(&flight, &CoverFlight::flightWanted);
        QObject header;
        flight.notePress(QRectF(0, 0, 160, 160), QUrl(QStringLiteral("https://example.invalid/a.jpg")), {}, {}, false);
        now += CoverFlight::PressWindowMs + 1;
        flight.headerShown(&header);
        QCOMPARE(wanted.count(), 0);

        flight.notePress(QRectF(0, 0, 4, 4), QUrl(QStringLiteral("https://example.invalid/a.jpg")), {}, {}, false);
        flight.headerShown(&header);
        QCOMPARE(wanted.count(), 0);

        flight.notePress(QRectF(0, 0, 160, 160), QUrl(QStringLiteral("https://example.invalid/a.jpg")), {}, {}, false);
        flight.forget();
        flight.headerShown(&header);
        flight.headerShown(nullptr);
        QCOMPARE(wanted.count(), 0);
    }

    void landingFollowsItsItem()
    {
        CoverFlight flight;
        QSignalSpy changed(&flight, &CoverFlight::landingChanged);
        auto item = std::make_unique<QObject>();
        flight.setLanding(item.get());
        QCOMPARE(flight.landing(), item.get());
        flight.setLanding(item.get());
        QCOMPARE(changed.count(), 1);
        // A header that went away is no longer landing.
        item.reset();
        QCOMPARE(flight.landing(), nullptr);
    }
};

QTEST_GUILESS_MAIN(CoverFlightTest)
#include "tst_cover_flight.moc"
