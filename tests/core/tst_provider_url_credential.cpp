#include <QObject>
#include <QString>
#include <QTest>

#include "core/net/ProviderUrlCredential.h"

using lmp::core::net::removeLegacyProviderUrlCredential;

class ProviderUrlCredentialTest : public QObject {
    Q_OBJECT

private slots:
    void removesTheFieldFromStreamAddresses();
    void removesTheFieldFromArtworkAddresses();
    void leavesOtherAddressesAlone();
    void keepsFragmentAndDropsEmptyFields();
};

void ProviderUrlCredentialTest::removesTheFieldFromStreamAddresses()
{
    QCOMPARE(removeLegacyProviderUrlCredential(
                 QStringLiteral("https://relay.example.test/v1/stream/direct%3A1?access_token=old&url=x&media_token=t")),
             QStringLiteral("https://relay.example.test/v1/stream/direct%3A1?url=x&media_token=t"));
    // Field names and the scheme compare without regard to case.
    QCOMPARE(removeLegacyProviderUrlCredential(QStringLiteral("HTTP://relay.example.test/V1/Stream/a?Access_Token=old")),
             QStringLiteral("HTTP://relay.example.test/V1/Stream/a"));
}

void ProviderUrlCredentialTest::removesTheFieldFromArtworkAddresses()
{
    QCOMPARE(removeLegacyProviderUrlCredential(
                 QStringLiteral("https://relay.example.test/v1/artwork?url=opaque&access_token=old&access_token=again")),
             QStringLiteral("https://relay.example.test/v1/artwork?url=opaque"));
}

void ProviderUrlCredentialTest::leavesOtherAddressesAlone()
{
    // Not a server path.
    QCOMPARE(removeLegacyProviderUrlCredential(QStringLiteral("https://media.example.test/a.mp3?access_token=x")),
             QStringLiteral("https://media.example.test/a.mp3?access_token=x"));
    // Not http(s).
    QCOMPARE(removeLegacyProviderUrlCredential(QStringLiteral("C:/Music/v1/stream/a.mp3?access_token=x")),
             QStringLiteral("C:/Music/v1/stream/a.mp3?access_token=x"));
    QCOMPARE(removeLegacyProviderUrlCredential(QString()), QString());
    // No query at all.
    QCOMPARE(removeLegacyProviderUrlCredential(QStringLiteral("https://relay.example.test/v1/stream/a")),
             QStringLiteral("https://relay.example.test/v1/stream/a"));
}

void ProviderUrlCredentialTest::keepsFragmentAndDropsEmptyFields()
{
    QCOMPARE(removeLegacyProviderUrlCredential(QStringLiteral("https://relay.example.test/v1/stream/a?&access_token=x&&b=1#part")),
             QStringLiteral("https://relay.example.test/v1/stream/a?b=1#part"));
    QCOMPARE(removeLegacyProviderUrlCredential(QStringLiteral("https://relay.example.test/v1/stream/a?access_token=x#part")),
             QStringLiteral("https://relay.example.test/v1/stream/a#part"));
}

QTEST_GUILESS_MAIN(ProviderUrlCredentialTest)
#include "tst_provider_url_credential.moc"
