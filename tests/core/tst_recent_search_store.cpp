#include <QDir>
#include <QList>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "core/library/RecentSearchStore.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

using lmp::core::RecentSearchStore;
using lmp::core::SettingsStore;
namespace keys = lmp::core::settings::keys;

class RecentSearchStoreTest : public QObject {
    Q_OBJECT

private slots:
    void recentSearchStore();
    void recordKeepsOrderAndSkipsBlanks();
    void encodedFormIsExact();
    void decodeHandlesEscapesAndLimits();
    void settingRoundTrip();
};

void RecentSearchStoreTest::recentSearchStore()
{
    const QList<QString> history{QStringLiteral("Rock"), QStringLiteral("Jazz"), QStringLiteral("Ambient")};
    const QList<QString> updated = RecentSearchStore::record(history, QStringLiteral("  rock  "), 3);
    QCOMPARE(updated.size(), qsizetype{3});
    QVERIFY(updated[0] == QStringLiteral("rock") && updated[1] == QStringLiteral("Jazz")
            && updated[2] == QStringLiteral("Ambient"));

    const QList<QString> special{QStringLiteral("line one\nline two"), QStringLiteral("C:\\Music")};
    const QString encoded = RecentSearchStore::encode(special);
    const QList<QString> decoded = RecentSearchStore::decode(encoded);
    QCOMPARE(decoded, special);
    QCOMPARE(RecentSearchStore::decode(encoded, 1).size(), qsizetype{1});
    QVERIFY(RecentSearchStore::record(history, QStringLiteral("new"), 0).isEmpty());
}

void RecentSearchStoreTest::recordKeepsOrderAndSkipsBlanks()
{
    QCOMPARE(RecentSearchStore::Limit, qsizetype{6});

    const QList<QString> history{QStringLiteral(" a "), QStringLiteral("  "), QStringLiteral("B"),
                                 QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e"),
                                 QStringLiteral("f")};
    QCOMPARE(RecentSearchStore::record(history, QStringLiteral("b")),
             (QList<QString>{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("d"),
                             QStringLiteral("e"), QStringLiteral("f")}));
    // A blank query returns the history exactly as it was, untrimmed.
    QCOMPARE(RecentSearchStore::record(history, QStringLiteral(" \t ")), history);
    QVERIFY(RecentSearchStore::record(history, QString(), 0).isEmpty());
    QCOMPARE(RecentSearchStore::record({}, QStringLiteral("x"), 1), QList<QString>{QStringLiteral("x")});
}

void RecentSearchStoreTest::encodedFormIsExact()
{
    QCOMPARE(RecentSearchStore::encode({}), QString());
    QCOMPARE(RecentSearchStore::encode({QStringLiteral("first"), QStringLiteral("second\\line")}),
             QStringLiteral("first\nsecond\\\\line"));
    // A carriage return is stored as an escaped newline.
    QCOMPARE(RecentSearchStore::encode({QStringLiteral("a\r\nb")}), QStringLiteral("a\\n\\nb"));
}

void RecentSearchStoreTest::decodeHandlesEscapesAndLimits()
{
    QCOMPARE(RecentSearchStore::decode(QString()), QList<QString>());
    QCOMPARE(RecentSearchStore::decode(QStringLiteral("first\nsecond\\\\line")),
             (QList<QString>{QStringLiteral("first"), QStringLiteral("second\\line")}));
    // Blank entries are skipped and the rest trimmed; an unknown escape keeps
    // its character and a trailing backslash is kept as it is.
    QCOMPARE(RecentSearchStore::decode(QStringLiteral(" one \n\n  \n\\q\nend\\")),
             (QList<QString>{QStringLiteral("one"), QStringLiteral("q"), QStringLiteral("end\\")}));
    QCOMPARE(RecentSearchStore::decode(QStringLiteral("1\n2\n3\n4\n5\n6\n7\n8")).size(), qsizetype{6});
    QVERIFY(RecentSearchStore::decode(QStringLiteral("a\nb"), 0).isEmpty());
}

void RecentSearchStoreTest::settingRoundTrip()
{
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    SettingsStore settings(QDir(folder.path()).filePath(QStringLiteral("Settings.json")));

    QVERIFY(RecentSearchStore::load(settings).isEmpty());
    const QList<QString> history{QStringLiteral("first"), QStringLiteral("second\\line"),
                                 QStringLiteral("two\nlines")};
    RecentSearchStore::save(settings, history);
    QCOMPARE(settings.string(keys::RecentSearches), QStringLiteral("first\nsecond\\\\line\ntwo\\nlines"));

    SettingsStore reloaded(settings.filePath());
    QCOMPARE(RecentSearchStore::load(reloaded), history);

    RecentSearchStore::clear(reloaded);
    QVERIFY(reloaded.contains(keys::RecentSearches));
    QCOMPARE(reloaded.string(keys::RecentSearches), QString());
    QVERIFY(RecentSearchStore::load(reloaded).isEmpty());
}

QTEST_GUILESS_MAIN(RecentSearchStoreTest)
#include "tst_recent_search_store.moc"
