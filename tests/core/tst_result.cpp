#include <QObject>
#include <QString>
#include <QTest>

#include "core/async/Result.h"

using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::Result;

namespace {

Result<int> half(int value)
{
    if (value % 2 != 0) {
        return Failure::of(FailureKind::InvalidResponse, QStringLiteral("odd"));
    }
    return value / 2;
}

Result<void> check(bool good)
{
    if (!good) {
        return Failure::of(FailureKind::Storage, QStringLiteral("disk"), 0);
    }
    return Result<void>::success();
}

} // namespace

class ResultTest : public QObject {
    Q_OBJECT

private slots:
    void holdsAValue();
    void holdsAFailure();
    void valueOrFallsBack();
    void voidResult();
    void movesOutTheValue();
};

void ResultTest::holdsAValue()
{
    const Result<int> result = half(8);
    QVERIFY(result.isOk());
    QVERIFY(static_cast<bool>(result));
    QCOMPARE(result.value(), 4);
}

void ResultTest::holdsAFailure()
{
    const Result<int> result = half(3);
    QVERIFY(!result.isOk());
    QCOMPARE(result.failure().kind, FailureKind::InvalidResponse);
    QCOMPARE(result.failure().httpStatus, 0);
    QCOMPARE(result.failure().detail, QStringLiteral("odd"));

    const Result<QString> http = Failure::of(FailureKind::NotFound, QStringLiteral("gone"), 404);
    QCOMPARE(http.failure().httpStatus, 404);
}

void ResultTest::valueOrFallsBack()
{
    QCOMPARE(half(10).valueOr(-1), 5);
    QCOMPARE(half(7).valueOr(-1), -1);
}

void ResultTest::voidResult()
{
    QVERIFY(check(true).isOk());
    const Result<void> bad = check(false);
    QVERIFY(!bad.isOk());
    QCOMPARE(bad.failure().kind, FailureKind::Storage);
}

void ResultTest::movesOutTheValue()
{
    Result<QString> result = QStringLiteral("text");
    const QString moved = std::move(result).value();
    QCOMPARE(moved, QStringLiteral("text"));
}

QTEST_GUILESS_MAIN(ResultTest)
#include "tst_result.moc"
