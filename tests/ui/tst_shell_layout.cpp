#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/settings/SettingsStore.h"
#include "ui/shell/ShellLayoutViewModel.h"

using lmp::ui::ShellLayoutViewModel;

// Each threshold is checked one pixel on either side.
class ShellLayoutTest : public QObject {
    Q_OBJECT

private slots:
    void connectTakesTheRailAndGivesItBack();
    void sidebarWidthSteps();
    void railWidthAndDocking();
    void overlayWidthIsCappedAt88Percent();
    void railStartsClosedInANarrowWindow();
    void crossingTheDockingWidthOpensOrClosesTheRail();
    void toggleIsRemembered();
    void railIsKeptBetweenRuns();
};

void ShellLayoutTest::sidebarWidthSteps()
{
    ShellLayoutViewModel layout;
    layout.setWindowWidth(1500);
    QCOMPARE(layout.sidebarWidth(), 300);
    layout.setWindowWidth(1499);
    QCOMPARE(layout.sidebarWidth(), 240);
    layout.setWindowWidth(900);
    QCOMPARE(layout.sidebarWidth(), 240);
    QVERIFY(!layout.sidebarCollapsed());
    layout.setWindowWidth(899);
    QCOMPARE(layout.sidebarWidth(), 64);
    QVERIFY(layout.sidebarCollapsed());
}

void ShellLayoutTest::railWidthAndDocking()
{
    ShellLayoutViewModel layout;
    layout.setWindowWidth(1500);
    QCOMPARE(layout.railWidth(), 380);
    layout.setWindowWidth(1499);
    QCOMPARE(layout.railWidth(), 300);
    layout.setWindowWidth(1180);
    QVERIFY(layout.railDocked());
    layout.setWindowWidth(1179);
    QVERIFY(!layout.railDocked());
}

void ShellLayoutTest::overlayWidthIsCappedAt88Percent()
{
    ShellLayoutViewModel layout;
    layout.setWindowWidth(1000);
    QCOMPARE(layout.railOverlayWidth(), 380.0);
    layout.setWindowWidth(400);
    QCOMPARE(layout.railOverlayWidth(), 352.0);
}

void ShellLayoutTest::railStartsClosedInANarrowWindow()
{
    ShellLayoutViewModel narrow;
    narrow.setWindowWidth(1179);
    QVERIFY(!narrow.railOpen());
    ShellLayoutViewModel wide;
    wide.setWindowWidth(1180);
    QVERIFY(wide.railOpen());
}

void ShellLayoutTest::crossingTheDockingWidthOpensOrClosesTheRail()
{
    ShellLayoutViewModel layout;
    layout.setWindowWidth(1600);
    layout.setRailOpen(false);
    layout.setWindowWidth(1200); // still docked: the user's choice stays
    QVERIFY(!layout.railOpen());
    layout.setWindowWidth(1100); // narrowing past 1180 closes it
    QVERIFY(!layout.railOpen());
    layout.toggleRail();
    QVERIFY(layout.railOpen());
    layout.setWindowWidth(1000);
    QVERIFY(layout.railOpen());
    layout.setWindowWidth(1180); // widening past it opens it
    QVERIFY(layout.railOpen());
    layout.setWindowWidth(1179);
    QVERIFY(!layout.railOpen());
}

void ShellLayoutTest::toggleIsRemembered()
{
    ShellLayoutViewModel layout;
    QSignalSpy changed(&layout, &ShellLayoutViewModel::changed);
    layout.setWindowWidth(1400);
    layout.toggleRail();
    QVERIFY(!layout.railOpen());
    layout.setWindowWidth(1450);
    QVERIFY(!layout.railOpen());
    QCOMPARE(changed.count(), 3);
}

void ShellLayoutTest::connectTakesTheRailAndGivesItBack()
{
    lmp::ui::ShellLayoutViewModel layout;
    layout.setWindowWidth(1300);
    QVERIFY(layout.railOpen());
    layout.toggleConnect();
    QVERIFY(layout.connectOpen());
    // Queue gives the column back to the queue.
    layout.toggleRail();
    QVERIFY(!layout.connectOpen());
    QVERIFY(layout.railOpen());

    // Opened with the rail closed, closing Connect closes the rail again.
    layout.setRailOpen(false);
    layout.toggleConnect();
    QVERIFY(layout.railOpen());
    layout.toggleConnect();
    QVERIFY(!layout.connectOpen());
    QVERIFY(!layout.railOpen());
    // Closing the rail closes the panel.
    layout.toggleConnect();
    layout.setRailOpen(false);
    QVERIFY(!layout.connectOpen());
}

void ShellLayoutTest::railIsKeptBetweenRuns()
{
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    lmp::core::SettingsStore settings(folder.filePath(QStringLiteral("Settings.json")));
    {
        ShellLayoutViewModel layout(&settings);
        layout.setWindowWidth(1400);
        QVERIFY(layout.railOpen());
        QCOMPARE(layout.railTab(), 0);
        layout.toggleRail();
        layout.setRailTab(1);
    }
    {
        // The next run opens as the user left it.
        ShellLayoutViewModel layout(&settings);
        layout.setWindowWidth(1400);
        QVERIFY(!layout.railOpen());
        QCOMPARE(layout.railTab(), 1);
        // Widening past the docking width still opens it (not a choice to keep).
        layout.setWindowWidth(1100);
        layout.setWindowWidth(1400);
        QVERIFY(layout.railOpen());
    }
    {
        // Opened again by the user while docked, it starts open again.
        ShellLayoutViewModel layout(&settings);
        layout.setWindowWidth(1400);
        QVERIFY(!layout.railOpen());
        layout.toggleRail();
        QVERIFY(layout.railOpen());
        // Closing the overlay in a narrow window is not a choice for the next run.
        layout.setWindowWidth(1000);
        layout.setRailOpen(true);
        layout.setRailOpen(false);
    }
    ShellLayoutViewModel layout(&settings);
    layout.setWindowWidth(1400);
    QVERIFY(layout.railOpen());
}

QTEST_GUILESS_MAIN(ShellLayoutTest)
#include "tst_shell_layout.moc"
