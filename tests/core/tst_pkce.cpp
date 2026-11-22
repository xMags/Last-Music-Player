#include <QChar>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/account/Pkce.h"

using lmp::core::account::createPkceChallenge;
using lmp::core::account::createPkceTransaction;
using lmp::core::account::PkceTransaction;

namespace {

bool isBase64UrlOnly(const QString& value)
{
    for (const QChar character : value) {
        const bool allowed = (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
            || (character >= QLatin1Char('a') && character <= QLatin1Char('z'))
            || (character >= QLatin1Char('0') && character <= QLatin1Char('9')) || character == QLatin1Char('-')
            || character == QLatin1Char('_');
        if (!allowed) {
            return false;
        }
    }
    return true;
}

} // namespace

class PkceTest : public QObject {
    Q_OBJECT

private slots:
    void rfc7636Example();
    void transactionShape();
    void transactionsAreFresh();
    void verifierLengthRule();
    void verifierMustBeAscii();
};

void PkceTest::rfc7636Example()
{
    const auto challenge = createPkceChallenge(QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"));
    QVERIFY(challenge.isOk());
    QCOMPARE(challenge.value(), QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
}

void PkceTest::transactionShape()
{
    const PkceTransaction transaction = createPkceTransaction();
    QCOMPARE(transaction.verifier.size(), 43);
    QCOMPARE(transaction.state.size(), 32);
    QCOMPARE(transaction.challenge.size(), 43);
    QVERIFY(isBase64UrlOnly(transaction.verifier));
    QVERIFY(isBase64UrlOnly(transaction.state));
    QVERIFY(isBase64UrlOnly(transaction.challenge));
    const auto challenge = createPkceChallenge(transaction.verifier);
    QVERIFY(challenge.isOk());
    QCOMPARE(transaction.challenge, challenge.value());
}

void PkceTest::transactionsAreFresh()
{
    const PkceTransaction first = createPkceTransaction();
    const PkceTransaction second = createPkceTransaction();
    QVERIFY(first.verifier != second.verifier);
    QVERIFY(first.state != second.state);
    QVERIFY(first.verifier != first.state.left(first.verifier.size()));
}

void PkceTest::verifierLengthRule()
{
    QVERIFY(!createPkceChallenge(QString()).isOk());
    QVERIFY(!createPkceChallenge(QString(42, QLatin1Char('a'))).isOk());
    QVERIFY(createPkceChallenge(QString(43, QLatin1Char('a'))).isOk());
    QVERIFY(createPkceChallenge(QString(128, QLatin1Char('a'))).isOk());
    QVERIFY(!createPkceChallenge(QString(129, QLatin1Char('a'))).isOk());
}

void PkceTest::verifierMustBeAscii()
{
    // Any ASCII character is hashed as it is, as before.
    QVERIFY(createPkceChallenge(QString(42, QLatin1Char('a')) + QLatin1Char('~')).isOk());
    QVERIFY(createPkceChallenge(QString(42, QLatin1Char('a')) + QLatin1Char(' ')).isOk());
    QVERIFY(!createPkceChallenge(QString(42, QLatin1Char('a')) + QChar(0x00e9)).isOk());
    QVERIFY(!createPkceChallenge(QString(42, QLatin1Char('a')) + QChar(0x0080)).isOk());
}

QTEST_GUILESS_MAIN(PkceTest)
#include "tst_pkce.moc"
