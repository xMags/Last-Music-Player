#include <QDir>
#include <QObject>
#include <QString>
#include <QTest>
#include <QUrl>

#include "core/app/ExternalLinkPolicy.h"

using lmp::core::isAllowedExternalLink;

namespace {

// An absolute folder on any system; nothing is created or opened.
QString dataRoot()
{
    return QDir::rootPath() + QStringLiteral("Users/test/AppData/Local/Last Music Player");
}

QUrl fileIn(const QString& relative)
{
    return QUrl::fromLocalFile(dataRoot() + relative);
}

} // namespace

class ExternalLinkPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void webAddresses_data();
    void webAddresses();
    void filesInsideTheDataFolder();
    void filesOutsideTheDataFolder();
    void withoutADataFolderNoFileIsAllowed();
};

void ExternalLinkPolicyTest::webAddresses_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<bool>("allowed");
    QTest::newRow("https page") << QStringLiteral("https://account.example.test/account") << true;
    QTest::newRow("https with query") << QStringLiteral("https://api.example.test/v1/sso/authorize?state=a%20b") << true;
    QTest::newRow("upper-case scheme") << QStringLiteral("HTTPS://account.example.test/") << true;
    QTest::newRow("http") << QStringLiteral("http://account.example.test/") << false;
    QTest::newRow("loopback http") << QStringLiteral("http://127.0.0.1:8787/") << false;
    QTest::newRow("no host") << QStringLiteral("https:///path") << false;
    QTest::newRow("user info") << QStringLiteral("https://user:secret@account.example.test/") << false;
    QTest::newRow("script") << QStringLiteral("javascript:alert(1)") << false;
    QTest::newRow("mail") << QStringLiteral("mailto:someone@example.test") << false;
    QTest::newRow("settings") << QStringLiteral("ms-settings:privacy") << false;
    QTest::newRow("relative") << QStringLiteral("/account") << false;
    QTest::newRow("empty") << QString() << false;
}

void ExternalLinkPolicyTest::webAddresses()
{
    QFETCH(QString, address);
    QFETCH(bool, allowed);
    QCOMPARE(isAllowedExternalLink(QUrl(address), dataRoot()), allowed);
}

void ExternalLinkPolicyTest::filesInsideTheDataFolder()
{
    QVERIFY(isAllowedExternalLink(QUrl::fromLocalFile(dataRoot()), dataRoot()));
    QVERIFY(isAllowedExternalLink(fileIn(QStringLiteral("/")), dataRoot()));
    QVERIFY(isAllowedExternalLink(fileIn(QStringLiteral("/Offline")), dataRoot()));
    QVERIFY(isAllowedExternalLink(fileIn(QStringLiteral("/thumbs/a/../b.jpg")), dataRoot()));
    QVERIFY(isAllowedExternalLink(QUrl::fromLocalFile(dataRoot() + QStringLiteral("/Offline")),
                                  QDir::toNativeSeparators(dataRoot() + QStringLiteral("/"))));
#ifdef Q_OS_WIN
    QVERIFY(isAllowedExternalLink(QUrl::fromLocalFile(dataRoot().toUpper() + QStringLiteral("/Offline")), dataRoot()));
#endif
}

void ExternalLinkPolicyTest::filesOutsideTheDataFolder()
{
    QVERIFY(!isAllowedExternalLink(fileIn(QStringLiteral("/../Other App")), dataRoot()));
    QVERIFY(!isAllowedExternalLink(fileIn(QStringLiteral("/../../../../../Windows/System32")), dataRoot()));
    QVERIFY(!isAllowedExternalLink(QUrl::fromLocalFile(dataRoot() + QStringLiteral("2")), dataRoot()));
    QVERIFY(!isAllowedExternalLink(QUrl::fromLocalFile(QDir::rootPath()), dataRoot()));
    QVERIFY(!isAllowedExternalLink(QUrl(QStringLiteral("file://server/share/Last Music Player")), dataRoot()));
    QUrl withQuery = fileIn(QStringLiteral("/Offline"));
    withQuery.setQuery(QStringLiteral("a=1"));
    QVERIFY(!isAllowedExternalLink(withQuery, dataRoot()));
    QUrl withFragment = fileIn(QStringLiteral("/Offline"));
    withFragment.setFragment(QStringLiteral("top"));
    QVERIFY(!isAllowedExternalLink(withFragment, dataRoot()));
}

void ExternalLinkPolicyTest::withoutADataFolderNoFileIsAllowed()
{
    QVERIFY(!isAllowedExternalLink(fileIn(QStringLiteral("/Offline")), QString()));
    QVERIFY(!isAllowedExternalLink(fileIn(QStringLiteral("/Offline")), QStringLiteral("relative/folder")));
    QVERIFY(isAllowedExternalLink(QUrl(QStringLiteral("https://account.example.test/")), QString()));
}

QTEST_GUILESS_MAIN(ExternalLinkPolicyTest)
#include "tst_external_link_policy.moc"
