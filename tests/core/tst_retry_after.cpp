#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QTest>
#include <QTime>
#include <QTimeZone>

#include <optional>

#include "core/net/RetryAfter.h"

using lmp::core::net::MaxRetryAfterSeconds;
using lmp::core::net::parseRetryAfterSeconds;

namespace {

QDateTime fixedNow()
{
    return QDateTime(QDate(2026, 11, 23), QTime(12, 0, 0), QTimeZone::UTC);
}

} // namespace

class RetryAfterTest : public QObject {
    Q_OBJECT

private slots:
    void parses_data();
    void parses();
};

void RetryAfterTest::parses_data()
{
    QTest::addColumn<QByteArray>("header");
    QTest::addColumn<int>("expected"); // -1 for nothing

    QTest::newRow("absent") << QByteArray() << -1;
    QTest::newRow("blank") << QByteArray("   ") << -1;
    QTest::newRow("seconds") << QByteArray("120") << 120;
    QTest::newRow("zero") << QByteArray("0") << 0;
    QTest::newRow("padded") << QByteArray(" 30 ") << 30;
    QTest::newRow("one day") << QByteArray("86400") << MaxRetryAfterSeconds;
    QTest::newRow("over a day") << QByteArray("70001") << 70001;
    QTest::newRow("clamped") << QByteArray("90000") << MaxRetryAfterSeconds;
    QTest::newRow("huge") << QByteArray("99999999999999999999") << MaxRetryAfterSeconds;
    QTest::newRow("negative") << QByteArray("-5") << -1;
    QTest::newRow("fraction") << QByteArray("1.5") << -1;
    QTest::newRow("text") << QByteArray("soon") << -1;
    QTest::newRow("date in a minute") << QByteArray("Mon, 23 Nov 2026 12:01:00 GMT") << 60;
    QTest::newRow("date now") << QByteArray("Mon, 23 Nov 2026 12:00:00 GMT") << 0;
    QTest::newRow("date in the past") << QByteArray("Sun, 22 Nov 2026 12:00:00 GMT") << 0;
    QTest::newRow("date in two days") << QByteArray("Wed, 25 Nov 2026 12:00:00 GMT") << MaxRetryAfterSeconds;
    QTest::newRow("date without zone") << QByteArray("Mon, 23 Nov 2026 12:01:00") << -1;
    QTest::newRow("malformed date") << QByteArray("Mon, 32 Nov 2026 12:01:00 GMT") << -1;
}

void RetryAfterTest::parses()
{
    QFETCH(QByteArray, header);
    QFETCH(int, expected);
    const std::optional<int> parsed = parseRetryAfterSeconds(header, fixedNow());
    QCOMPARE(parsed.has_value(), expected >= 0);
    if (parsed) {
        QCOMPARE(*parsed, expected);
    }
}

QTEST_GUILESS_MAIN(RetryAfterTest)
#include "tst_retry_after.moc"
