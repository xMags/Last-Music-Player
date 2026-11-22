#include <QObject>
#include <QTest>

#include "core/account/AutoSyncPolicy.h"

#include <chrono>

using lmp::core::accountRetryDelay;
using lmp::core::AutoSyncConditions;
using lmp::core::AutoSyncDecision;
using lmp::core::AutoSyncFocusDebounce;
using lmp::core::AutoSyncPollInterval;
using lmp::core::AutoSyncTrigger;
using lmp::core::evaluateAutoSync;

namespace {

// Every condition met, and long enough since the last attempt that even the
// debounced trigger qualifies.
AutoSyncConditions ready()
{
    AutoSyncConditions conditions;
    conditions.accountMode = true;
    conditions.sessionValidated = true;
    conditions.sinceLastAttempt = AutoSyncFocusDebounce;
    return conditions;
}

} // namespace

class AutoSyncPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void autoSyncPolicy();
    void defaultsAndRetryEdges();
};

void AutoSyncPolicyTest::autoSyncPolicy()
{
    QVERIFY(evaluateAutoSync(ready(), AutoSyncTrigger::Timer) == AutoSyncDecision::Run);
    QVERIFY(evaluateAutoSync(ready(), AutoSyncTrigger::WindowFocus) == AutoSyncDecision::Run);

    // Local-only and API key mode both arrive as "not Account mode".
    AutoSyncConditions wrongMode = ready();
    wrongMode.accountMode = false;
    QVERIFY(evaluateAutoSync(wrongMode, AutoSyncTrigger::Timer) == AutoSyncDecision::NotAccountMode);

    // Signed out, signing in and offline all arrive as "not validated".
    AutoSyncConditions notLive = ready();
    notLive.sessionValidated = false;
    QVERIFY(evaluateAutoSync(notLive, AutoSyncTrigger::Timer) == AutoSyncDecision::NotSignedIn);

    // The focus debounce boundary; the timer is not subject to it.
    AutoSyncConditions justSynced = ready();
    justSynced.sinceLastAttempt = AutoSyncFocusDebounce - std::chrono::seconds(1);
    QVERIFY(evaluateAutoSync(justSynced, AutoSyncTrigger::WindowFocus) == AutoSyncDecision::TooSoon);
    QVERIFY(evaluateAutoSync(justSynced, AutoSyncTrigger::Timer) == AutoSyncDecision::Run);

    AutoSyncConditions neverSynced = ready();
    neverSynced.sinceLastAttempt = std::chrono::steady_clock::duration::max();
    QVERIFY(evaluateAutoSync(neverSynced, AutoSyncTrigger::WindowFocus) == AutoSyncDecision::Run);

    QVERIFY(accountRetryDelay(1) == std::chrono::seconds(5));
    QVERIFY(accountRetryDelay(5) == std::chrono::minutes(30));
    QVERIFY(accountRetryDelay(20) == std::chrono::minutes(30));
    QVERIFY(accountRetryDelay(2, 90) == std::chrono::seconds(90));
}

void AutoSyncPolicyTest::defaultsAndRetryEdges()
{
    QVERIFY(AutoSyncFocusDebounce == std::chrono::seconds(30));
    QVERIFY(AutoSyncPollInterval == std::chrono::minutes(5));

    // Mode is checked before the session, and default conditions never run.
    AutoSyncConditions neither;
    neither.sessionValidated = false;
    QVERIFY(evaluateAutoSync(neither, AutoSyncTrigger::Timer) == AutoSyncDecision::NotAccountMode);
    QVERIFY(evaluateAutoSync(AutoSyncConditions{}, AutoSyncTrigger::WindowFocus) == AutoSyncDecision::NotAccountMode);

    QVERIFY(accountRetryDelay(0) == std::chrono::seconds(5));
    QVERIFY(accountRetryDelay(-3) == std::chrono::seconds(5));
    QVERIFY(accountRetryDelay(2) == std::chrono::seconds(30));
    QVERIFY(accountRetryDelay(3) == std::chrono::minutes(2));
    QVERIFY(accountRetryDelay(4) == std::chrono::minutes(10));
    // A shorter Retry-After never shortens the schedule.
    QVERIFY(accountRetryDelay(4, 60) == std::chrono::minutes(10));
}

QTEST_GUILESS_MAIN(AutoSyncPolicyTest)
#include "tst_auto_sync_policy.moc"
