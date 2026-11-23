#include <QTest>

#include "ui/motion/PageCascade.h"

using lmp::ui::PageCascade;

// Who takes part in the page cascade.
class PageCascadeTest : public QObject {
    Q_OBJECT

private slots:
    void nothingCascadesBeforeAPageOpens()
    {
        PageCascade cascade([]() { return qint64(0); });
        QCOMPARE(cascade.nextIndex(), -1);
    }

    void theFirstFewGetPlacesInOrder()
    {
        qint64 now = 100;
        PageCascade cascade([&now]() { return now; });
        cascade.restart();
        for (int index = 0; index < PageCascade::Limit; ++index) {
            QCOMPARE(cascade.nextIndex(), index);
        }
        QCOMPARE(cascade.nextIndex(), -1);
        // A new page starts the line again.
        cascade.restart();
        QCOMPARE(cascade.nextIndex(), 0);
    }

    void itemsMadeLaterDoNotCascade()
    {
        qint64 now = 0;
        PageCascade cascade([&now]() { return now; });
        cascade.restart();
        now = PageCascade::WindowMs;
        QCOMPARE(cascade.nextIndex(), 0);
        now = PageCascade::WindowMs + 1;
        QCOMPARE(cascade.nextIndex(), -1);
    }
};

QTEST_GUILESS_MAIN(PageCascadeTest)
#include "tst_page_cascade.moc"
