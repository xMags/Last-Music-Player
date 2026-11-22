#include <QObject>
#include <QTest>

#include "core/library/UserDataOperationGate.h"

#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <thread>
#include <utility>

using lmp::core::UserDataOperationGate;

class UserDataOperationGateTest : public QObject {
    Q_OBJECT

private slots:
    void userDataOperationGate();
    void leasesReleaseOnMoveAndReset();
};

void UserDataOperationGateTest::userDataOperationGate()
{
    UserDataOperationGate gate;
    std::optional<UserDataOperationGate::Lease> admission = gate.tryEnter();
    QVERIFY(admission.has_value());
    QVERIFY(gate.isAccepting());
    const std::uint64_t initialGeneration = gate.generation();
    QVERIFY(gate.isCurrent(initialGeneration));

    QVERIFY(gate.closeAdmissions());
    const std::uint64_t closedGeneration = gate.generation();
    QVERIFY(closedGeneration != initialGeneration);
    QVERIFY(!gate.isCurrent(initialGeneration));
    QVERIFY(!gate.closeAdmissions());
    QCOMPARE(gate.generation(), closedGeneration);
    QVERIFY(!gate.isAccepting());
    QVERIFY(!gate.tryEnter().has_value());
    QVERIFY(!gate.waitForIdle(std::chrono::milliseconds(0)));

    std::promise<void> releaseSignal;
    std::future<void> releaseFuture = releaseSignal.get_future();
    UserDataOperationGate::Lease lease = std::move(*admission);
    std::thread releaseThread{[lease = std::move(lease), releaseFuture = std::move(releaseFuture)]() mutable {
        releaseFuture.wait();
        lease = UserDataOperationGate::Lease{};
    }};

    releaseSignal.set_value();
    const bool becameIdle = gate.waitForIdle(std::chrono::seconds(2));
    releaseThread.join();
    QVERIFY(becameIdle);

    gate.reopen();
    QVERIFY(gate.isAccepting());
    QVERIFY(gate.isCurrent(closedGeneration));
    QVERIFY(!gate.isCurrent(initialGeneration));
    QVERIFY(gate.tryEnter().has_value());
}

void UserDataOperationGateTest::leasesReleaseOnMoveAndReset()
{
    UserDataOperationGate gate;
    QVERIFY(gate.waitForIdle(std::chrono::milliseconds(0)));

    std::optional<UserDataOperationGate::Lease> first = gate.tryEnter();
    std::optional<UserDataOperationGate::Lease> second = gate.tryEnter();
    QVERIFY(first.has_value() && second.has_value());
    QVERIFY(static_cast<bool>(*first));
    QVERIFY(!static_cast<bool>(UserDataOperationGate::Lease{}));

    // Assigning over a held lease releases it; the moved-from lease is empty.
    *first = std::move(*second);
    QVERIFY(!static_cast<bool>(*second));
    QVERIFY(!gate.waitForIdle(std::chrono::milliseconds(0)));

    first.reset();
    QVERIFY(gate.waitForIdle(std::chrono::milliseconds(0)));
    second.reset();
    QVERIFY(gate.waitForIdle(std::chrono::milliseconds(0)));

    // A lease may outlive its gate.
    std::optional<UserDataOperationGate::Lease> orphan;
    {
        UserDataOperationGate shortLived;
        orphan = shortLived.tryEnter();
    }
    QVERIFY(orphan.has_value() && static_cast<bool>(*orphan));
    orphan.reset();
}

QTEST_GUILESS_MAIN(UserDataOperationGateTest)
#include "tst_user_data_operation_gate.moc"
