#include <QObject>
#include <QTest>

#include "core/mode/SourceChoice.h"

using lmp::core::mode::shouldOfferSourceChoice;
using lmp::core::mode::SourceChoiceInputs;
using lmp::core::mode::SourceMode;

// The first-run source page shows once, and never to someone upgrading.
class SourceChoiceTest : public QObject {
    Q_OBJECT

private slots:
    void decides_data();
    void decides();
};

namespace {

SourceChoiceInputs firstRun()
{
    return SourceChoiceInputs{};
}

} // namespace

void SourceChoiceTest::decides_data()
{
    QTest::addColumn<SourceChoiceInputs>("inputs");
    QTest::addColumn<bool>("shown");

    QTest::newRow("clean data folder") << firstRun() << true;

    SourceChoiceInputs upgrading = firstRun();
    upgrading.modeStoredBefore = true;
    QTest::newRow("earlier version chose a mode") << upgrading << false;

    SourceChoiceInputs songs = firstRun();
    songs.libraryHasSongs = true;
    QTest::newRow("library has songs") << songs << false;

    SourceChoiceInputs session = firstRun();
    session.sessionStored = true;
    QTest::newRow("account session stored") << session << false;

    SourceChoiceInputs provider = firstRun();
    provider.mode = SourceMode::ApiKey;
    QTest::newRow("first-run default is API key") << provider << false;

    SourceChoiceInputs seen = firstRun();
    seen.choiceSeenStored = true;
    seen.choiceSeen = true;
    QTest::newRow("already chosen") << seen << false;

    // Shown on an earlier launch and closed without a choice; that launch
    // stored the mode.
    SourceChoiceInputs leftOpen = firstRun();
    leftOpen.choiceSeenStored = true;
    leftOpen.modeStoredBefore = true;
    QTest::newRow("left open last time") << leftOpen << true;

    SourceChoiceInputs leftOpenThenConnected = leftOpen;
    leftOpenThenConnected.mode = SourceMode::Account;
    QTest::newRow("left open, now in Account mode") << leftOpenThenConnected << false;

    SourceChoiceInputs seenTrueOnly = firstRun();
    seenTrueOnly.choiceSeen = true;
    QTest::newRow("seen without stored flag") << seenTrueOnly << false;
}

void SourceChoiceTest::decides()
{
    QFETCH(SourceChoiceInputs, inputs);
    QFETCH(bool, shown);
    QCOMPARE(shouldOfferSourceChoice(inputs), shown);
}

QTEST_GUILESS_MAIN(SourceChoiceTest)
#include "tst_source_choice.moc"
