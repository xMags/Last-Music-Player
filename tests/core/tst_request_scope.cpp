#include <QObject>
#include <QTest>

#include "core/async/RequestScope.h"

using lmp::core::RequestScope;

class RequestScopeTest : public QObject {
    Q_OBJECT

private slots:
    void latestTokenIsCurrent();
    void invalidateMakesEveryTokenStale();
    void freshScopeHasNoCurrentRequest();
};

void RequestScopeTest::latestTokenIsCurrent()
{
    RequestScope scope;
    const RequestScope::Token first = scope.next();
    QVERIFY(scope.isCurrent(first));
    const RequestScope::Token second = scope.next();
    QVERIFY(second != first);
    QVERIFY(!scope.isCurrent(first));
    QVERIFY(scope.isCurrent(second));
}

void RequestScopeTest::invalidateMakesEveryTokenStale()
{
    RequestScope scope;
    const RequestScope::Token token = scope.next();
    scope.invalidate();
    QVERIFY(!scope.isCurrent(token));
    // A request started afterwards is current again.
    const RequestScope::Token later = scope.next();
    QVERIFY(scope.isCurrent(later));
}

void RequestScopeTest::freshScopeHasNoCurrentRequest()
{
    RequestScope scope;
    RequestScope other;
    // Tokens from a different scope's first request are not this scope's.
    const RequestScope::Token foreign = other.next();
    QVERIFY(!scope.isCurrent(foreign));
}

QTEST_GUILESS_MAIN(RequestScopeTest)
#include "tst_request_scope.moc"
