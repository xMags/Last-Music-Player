#include <QObject>
#include <QString>
#include <QTest>

#include "core/net/RemoteUrlPolicy.h"

using lmp::core::net::isHttpUrl;
using lmp::core::net::isSafeRemoteUrl;
using lmp::core::net::RemoteUrlUse;

// The first nine cases are the earlier remote address safety test.
class RemoteUrlPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void legacyCases();
    void moreCases();
    void httpPrefix();
};

void RemoteUrlPolicyTest::legacyCases()
{
    QVERIFY(isSafeRemoteUrl(QStringLiteral("https://catalog.example.test/watch?id=stable"), RemoteUrlUse::Durable));
    QVERIFY(isSafeRemoteUrl(QStringLiteral("https://catalog.example.test/watch?monkey=value"), RemoteUrlUse::Durable));
    QVERIFY(isSafeRemoteUrl(QStringLiteral("http://127.0.0.1:4527/watch?id=stable"), RemoteUrlUse::Durable));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("http://catalog.example.test/watch?id=stable"), RemoteUrlUse::Durable));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("https://user:secret@catalog.example.test/watch?id=stable"),
                             RemoteUrlUse::EphemeralMedia));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("https://catalog.example.test/watch?access_token=secret"),
                             RemoteUrlUse::EphemeralMedia));
    QVERIFY(isSafeRemoteUrl(QStringLiteral("https://cdn.example.test/song?media_token=stream.1.signature"),
                            RemoteUrlUse::EphemeralMedia));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("https://cdn.example.test/song?media_token=stream.1.signature"),
                             RemoteUrlUse::Durable));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("https://catalog.example.test/watch#session=secret"),
                             RemoteUrlUse::Durable));
}

void RemoteUrlPolicyTest::moreCases()
{
    QVERIFY(!isSafeRemoteUrl(QString(), RemoteUrlUse::EphemeralMedia));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("not a url"), RemoteUrlUse::EphemeralMedia));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("/relative/path"), RemoteUrlUse::EphemeralMedia));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("ftp://files.example.test/a"), RemoteUrlUse::EphemeralMedia));
    QVERIFY(isSafeRemoteUrl(QStringLiteral("http://localhost:8080/a"), RemoteUrlUse::Durable));
    QVERIFY(isSafeRemoteUrl(QStringLiteral("http://[::1]:8080/a"), RemoteUrlUse::Durable));
    QVERIFY(isSafeRemoteUrl(QStringLiteral("HTTPS://Catalog.Example.Test/a"), RemoteUrlUse::Durable));
    // Names compare whole and ignore case.
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("https://a.example.test/x?API_KEY=1"), RemoteUrlUse::EphemeralMedia));
    QVERIFY(isSafeRemoteUrl(QStringLiteral("https://a.example.test/x?keyboard=1"), RemoteUrlUse::Durable));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("https://a.example.test/x?a=1&key=2"), RemoteUrlUse::Durable));
    QVERIFY(isSafeRemoteUrl(QStringLiteral("https://a.example.test/x?a=1&key=2"), RemoteUrlUse::EphemeralMedia));
    // Signed delivery prefixes.
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("https://a.example.test/x?X-Amz-Date=1"), RemoteUrlUse::Durable));
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("https://a.example.test/x?x-goog-signature=1"), RemoteUrlUse::Durable));
    QVERIFY(isSafeRemoteUrl(QStringLiteral("https://a.example.test/x?x-goog-signature=1"), RemoteUrlUse::EphemeralMedia));
    // A name with no value still counts.
    QVERIFY(!isSafeRemoteUrl(QStringLiteral("https://a.example.test/x?jwt"), RemoteUrlUse::EphemeralMedia));
}

void RemoteUrlPolicyTest::httpPrefix()
{
    QVERIFY(isHttpUrl(QStringLiteral("http://a")));
    QVERIFY(isHttpUrl(QStringLiteral("https://a")));
    QVERIFY(!isHttpUrl(QStringLiteral("HTTP://a")));
    QVERIFY(!isHttpUrl(QStringLiteral("C:\\Music\\a.mp3")));
    QVERIFY(!isHttpUrl(QString()));
}

QTEST_GUILESS_MAIN(RemoteUrlPolicyTest)
#include "tst_remote_url_policy.moc"
