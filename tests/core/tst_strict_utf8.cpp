#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/text/StrictUtf8.h"

using lmp::core::text::decodeUtf8Strict;
using lmp::core::text::encodeUtf8Strict;

// The cases of the earlier codec test, plus the byte order mark.
class StrictUtf8Test : public QObject {
    Q_OBJECT

private slots:
    void asciiRoundTrip();
    void bengaliRoundTrip();
    void emojiRoundTrip();
    void emptyIsNotAFailure();
    void embeddedNulIsKept();
    void malformedUtf8IsRejected();
    void truncatedSequenceIsRejected();
    void unpairedSurrogateIsRejected();
    void byteOrderMarkIsKeptAsText();
};

void StrictUtf8Test::asciiRoundTrip()
{
    const auto utf8 = encodeUtf8Strict(QStringLiteral("Last Music"));
    QVERIFY(utf8.has_value());
    QCOMPARE(*utf8, QByteArray("Last Music"));
    QCOMPARE(decodeUtf8Strict(*utf8), std::optional<QString>(QStringLiteral("Last Music")));
}

void StrictUtf8Test::bengaliRoundTrip()
{
    const QString bengali = QString::fromUtf8("\xE0\xA6\xB6\xE0\xA7\x87\xE0\xA6\xB7 \xE0\xA6\x97\xE0\xA6\xBE\xE0\xA6\xA8");
    const auto utf8 = encodeUtf8Strict(bengali);
    QVERIFY(utf8.has_value());
    QCOMPARE(decodeUtf8Strict(*utf8), std::optional<QString>(bengali));
}

void StrictUtf8Test::emojiRoundTrip()
{
    const QString emoji = QStringLiteral("Now playing ") + QString::fromUcs4(U"\U0001F3B5");
    const auto utf8 = encodeUtf8Strict(emoji);
    QVERIFY(utf8.has_value());
    QCOMPARE(utf8->size(), 16);
    QCOMPARE(decodeUtf8Strict(*utf8), std::optional<QString>(emoji));
}

void StrictUtf8Test::emptyIsNotAFailure()
{
    const auto utf8 = encodeUtf8Strict(QString());
    QVERIFY(utf8.has_value());
    QVERIFY(utf8->isEmpty());
    const auto text = decodeUtf8Strict(QByteArray());
    QVERIFY(text.has_value());
    QVERIFY(text->isEmpty());
}

void StrictUtf8Test::embeddedNulIsKept()
{
    QString withNul = QStringLiteral("A");
    withNul.append(QChar(0));
    withNul.append(QLatin1Char('B'));
    const auto utf8 = encodeUtf8Strict(withNul);
    QVERIFY(utf8.has_value());
    QCOMPARE(utf8->size(), 3);
    QCOMPARE(utf8->at(1), '\0');
    const auto back = decodeUtf8Strict(*utf8);
    QVERIFY(back.has_value());
    QCOMPARE(*back, withNul);
}

void StrictUtf8Test::malformedUtf8IsRejected()
{
    QVERIFY(!decodeUtf8Strict(QByteArray("\xF0\x28\x8C\x28", 4)).has_value());
    QVERIFY(!decodeUtf8Strict(QByteArray("ok\xFF", 3)).has_value());
}

void StrictUtf8Test::truncatedSequenceIsRejected()
{
    // The first two bytes of a three-byte sequence.
    QVERIFY(!decodeUtf8Strict(QByteArray("ab\xE0\xA6", 4)).has_value());
}

void StrictUtf8Test::unpairedSurrogateIsRejected()
{
    const QString lone(QChar(0xD800));
    QVERIFY(!encodeUtf8Strict(lone).has_value());
    const QString trailing = QStringLiteral("x") + QChar(0xDC00);
    QVERIFY(!encodeUtf8Strict(trailing).has_value());
}

void StrictUtf8Test::byteOrderMarkIsKeptAsText()
{
    const auto text = decodeUtf8Strict(QByteArray("\xEF\xBB\xBF{}", 5));
    QVERIFY(text.has_value());
    QCOMPARE(text->size(), 3);
    QCOMPARE(text->at(0), QChar(0xFEFF));
}

QTEST_GUILESS_MAIN(StrictUtf8Test)
#include "tst_strict_utf8.moc"
