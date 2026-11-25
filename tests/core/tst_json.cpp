#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTest>

#include <limits>

#include <optional>

#include "core/net/Json.h"

using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::Result;
namespace json = lmp::core::json;

namespace {

QJsonObject sample()
{
    const Result<QJsonObject> parsed = json::parseObject(R"({
        "text": "secret-value",
        "empty": "",
        "nothing": null,
        "count": 42,
        "negative": -2147483648,
        "tooBigForInt": 3000000000,
        "fraction": 2.5,
        "huge": 1e300,
        "numberText": "42",
        "flag": true,
        "child": {"a": 1},
        "list": [1, 2]
    })");
    return parsed ? parsed.value() : QJsonObject();
}

// The failure must be InvalidResponse, name the key, and never show a value.
void verifyFailure(const Failure& failure, const QString& key)
{
    QCOMPARE(failure.kind, FailureKind::InvalidResponse);
    QVERIFY2(failure.detail.contains(key), qPrintable(failure.detail));
    QVERIFY(!failure.detail.contains(QStringLiteral("secret-value")));
}

} // namespace

class JsonTest : public QObject {
    Q_OBJECT

private slots:
    void wholeNumbersStayInRange();
    void parseObjectAcceptsOnlyObjects();
    void parseArrayAcceptsOnlyArrays();
    void strings();
    void integers();
    void wideIntegers();
    void doubles();
    void booleans();
    void objectsAndArrays();
};

void JsonTest::parseObjectAcceptsOnlyObjects()
{
    QVERIFY(json::parseObject(R"({"a":1})"));
    QVERIFY(!sample().isEmpty());

    for (const QByteArray& bad : {QByteArray(), QByteArray("{"), QByteArray("[1]"), QByteArray("\"text\""),
                                  QByteArray("{\"a\":\"\xFF\"}")}) {
        const Result<QJsonObject> parsed = json::parseObject(bad);
        QVERIFY(!parsed);
        QCOMPARE(parsed.failure().kind, FailureKind::InvalidResponse);
    }
}

void JsonTest::parseArrayAcceptsOnlyArrays()
{
    const Result<QJsonArray> parsed = json::parseArray("[1, 2, 3]");
    QVERIFY(parsed);
    QCOMPARE(parsed.value().size(), 3);
    QVERIFY(!json::parseArray(R"({"a":1})"));
    QVERIFY(!json::parseArray("[1,"));
}

void JsonTest::strings()
{
    const QJsonObject object = sample();
    QCOMPARE(json::requireString(object, QLatin1StringView("text")).value(), QStringLiteral("secret-value"));
    QCOMPARE(json::requireString(object, QLatin1StringView("empty")).value(), QString());

    for (const char* key : {"missing", "nothing", "count"}) {
        const Result<QString> result = json::requireString(object, QLatin1StringView(key));
        QVERIFY(!result);
        verifyFailure(result.failure(), QString::fromLatin1(key));
    }

    QCOMPARE(json::optionalString(object, QLatin1StringView("text")).value(), QStringLiteral("secret-value"));
    QVERIFY(!json::optionalString(object, QLatin1StringView("missing")).value().has_value());
    QVERIFY(!json::optionalString(object, QLatin1StringView("nothing")).value().has_value());
    const Result<std::optional<QString>> wrong = json::optionalString(object, QLatin1StringView("flag"));
    QVERIFY(!wrong);
    verifyFailure(wrong.failure(), QStringLiteral("flag"));
}

void JsonTest::integers()
{
    const QJsonObject object = sample();
    QCOMPARE(json::requireInt(object, QLatin1StringView("count")).value(), 42);
    QCOMPARE(json::requireInt(object, QLatin1StringView("negative")).value(), -2147483647 - 1);

    for (const char* key : {"missing", "nothing", "tooBigForInt", "fraction", "huge", "numberText", "flag"}) {
        const Result<int> result = json::requireInt(object, QLatin1StringView(key));
        QVERIFY2(!result, key);
        verifyFailure(result.failure(), QString::fromLatin1(key));
    }

    QCOMPARE(json::optionalInt(object, QLatin1StringView("count")).value(), std::optional<int>(42));
    QVERIFY(!json::optionalInt(object, QLatin1StringView("missing")).value().has_value());
    QVERIFY(!json::optionalInt(object, QLatin1StringView("nothing")).value().has_value());
    QVERIFY(!json::optionalInt(object, QLatin1StringView("fraction")));
    QVERIFY(!json::optionalInt(object, QLatin1StringView("numberText")));
}

void JsonTest::wideIntegers()
{
    const QJsonObject object = sample();
    QCOMPARE(json::requireInt64(object, QLatin1StringView("tooBigForInt")).value(), Q_INT64_C(3000000000));
    QVERIFY(!json::requireInt64(object, QLatin1StringView("fraction")));
    QVERIFY(!json::requireInt64(object, QLatin1StringView("huge")));
    QVERIFY(!json::requireInt64(object, QLatin1StringView("missing")));
    QVERIFY(!json::optionalInt64(object, QLatin1StringView("missing")).value().has_value());
    QVERIFY(!json::optionalInt64(object, QLatin1StringView("numberText")));
}

void JsonTest::doubles()
{
    const QJsonObject object = sample();
    QCOMPARE(json::requireDouble(object, QLatin1StringView("fraction")).value(), 2.5);
    QCOMPARE(json::requireDouble(object, QLatin1StringView("count")).value(), 42.0);
    QVERIFY(!json::requireDouble(object, QLatin1StringView("numberText")));
    QVERIFY(!json::requireDouble(object, QLatin1StringView("nothing")));
    QCOMPARE(json::optionalDouble(object, QLatin1StringView("fraction")).value(), std::optional<double>(2.5));
    QVERIFY(!json::optionalDouble(object, QLatin1StringView("missing")).value().has_value());
    QVERIFY(!json::optionalDouble(object, QLatin1StringView("flag")));
}

void JsonTest::booleans()
{
    const QJsonObject object = sample();
    QCOMPARE(json::requireBool(object, QLatin1StringView("flag")).value(), true);
    QVERIFY(!json::requireBool(object, QLatin1StringView("count")));
    QVERIFY(!json::requireBool(object, QLatin1StringView("text")));
    QVERIFY(!json::requireBool(object, QLatin1StringView("missing")));
    QCOMPARE(json::optionalBool(object, QLatin1StringView("flag")).value(), std::optional<bool>(true));
    QVERIFY(!json::optionalBool(object, QLatin1StringView("nothing")).value().has_value());
    QVERIFY(!json::optionalBool(object, QLatin1StringView("count")));
}

void JsonTest::objectsAndArrays()
{
    const QJsonObject object = sample();
    QCOMPARE(json::requireObject(object, QLatin1StringView("child")).value().value(QLatin1StringView("a")).toInt(), 1);
    QVERIFY(!json::requireObject(object, QLatin1StringView("list")));
    QVERIFY(!json::requireObject(object, QLatin1StringView("missing")));
    QVERIFY(!json::optionalObject(object, QLatin1StringView("missing")).value().has_value());
    QVERIFY(!json::optionalObject(object, QLatin1StringView("text")));

    QCOMPARE(json::requireArray(object, QLatin1StringView("list")).value().size(), 2);
    QVERIFY(!json::requireArray(object, QLatin1StringView("child")));
    QVERIFY(!json::requireArray(object, QLatin1StringView("nothing")));
    QVERIFY(!json::optionalArray(object, QLatin1StringView("nothing")).value().has_value());
    const Result<std::optional<QJsonArray>> wrong = json::optionalArray(object, QLatin1StringView("count"));
    QVERIFY(!wrong);
    verifyFailure(wrong.failure(), QStringLiteral("count"));
}

void JsonTest::wholeNumbersStayInRange()
{
    using lmp::core::json::wholeNumber;
    QCOMPARE(wholeNumber(42.9, 0, 100), qint64(42));
    QCOMPARE(wholeNumber(-5.0, 0, 100), qint64(0));
    QCOMPARE(wholeNumber(1e300, 0, 100), qint64(100));
    QCOMPARE(wholeNumber(std::numeric_limits<double>::quiet_NaN(), 0, 100, 7), qint64(7));
    QCOMPARE(wholeNumber(std::numeric_limits<double>::infinity(), 0, 100, 7), qint64(7));
    QCOMPARE(wholeNumber(1e300, 0, std::numeric_limits<qint64>::max()), std::numeric_limits<qint64>::max());
}

QTEST_GUILESS_MAIN(JsonTest)
#include "tst_json.moc"
