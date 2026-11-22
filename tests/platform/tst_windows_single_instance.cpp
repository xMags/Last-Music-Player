#include <QObject>
#include <QString>
#include <QTest>
#include <QUuid>

#include "platform/windows/WindowsSingleInstance.h"

using lmp::platform::windows::WindowsSingleInstance;

class WindowsSingleInstanceTest : public QObject {
    Q_OBJECT

private slots:
    void secondClaimFailsWhileTheFirstHoldsIt();
    void claimIsReleasedOnDestruction();
    void activatingWithoutAWindowDoesNothing();

private:
    // A name no other process uses, so the test never meets a running app.
    static QString uniqueMutexName()
    {
        return QStringLiteral("Local\\LmpSingleInstanceTest.")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
};

void WindowsSingleInstanceTest::secondClaimFailsWhileTheFirstHoldsIt()
{
    const QString name = uniqueMutexName();
    WindowsSingleInstance first(name, QStringLiteral("unused"));
    WindowsSingleInstance second(name, QStringLiteral("unused"));
    QVERIFY(first.tryAcquire());
    QVERIFY(first.tryAcquire()); // asking again keeps the claim
    QVERIFY(!second.tryAcquire());
}

void WindowsSingleInstanceTest::claimIsReleasedOnDestruction()
{
    const QString name = uniqueMutexName();
    {
        WindowsSingleInstance first(name, QStringLiteral("unused"));
        QVERIFY(first.tryAcquire());
    }
    WindowsSingleInstance next(name, QStringLiteral("unused"));
    QVERIFY(next.tryAcquire());
}

void WindowsSingleInstanceTest::activatingWithoutAWindowDoesNothing()
{
    WindowsSingleInstance instance(uniqueMutexName(),
                                   QUuid::createUuid().toString(QUuid::WithoutBraces));
    instance.activateExistingWindow();
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(WindowsSingleInstanceTest)
#include "tst_windows_single_instance.moc"
