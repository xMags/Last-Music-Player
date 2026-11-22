#include <QLatin1StringView>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "core/account/AccountUrlPolicy.h"
#include "core/config/PrivateConfig.h"

using lmp::core::PrivateConfig;
using lmp::core::account::AccountMediaScope;
using lmp::core::account::isSafeAccountProfileUrl;
using lmp::core::account::isTrustedAccountMediaUrl;
using lmp::core::account::isTrustedAccountOrigin;
using lmp::core::account::isUrlFromTrustedAccountOrigin;
using lmp::core::account::LoopbackHttp;
using lmp::core::account::loopbackHttpForThisBuild;
using lmp::core::account::mediaTokenNeedsRefresh;
using lmp::core::account::normalizeTrustedAccountOrigin;

namespace {

// A fixed clock: the rules take the time as an argument.
constexpr qint64 kNowMs = 1'700'000'000'000;
constexpr qint64 kFiveMinutesMs = 5 * 60 * 1000;

QString token(QLatin1StringView scope, qint64 offsetMs)
{
    return QString(scope) + QLatin1Char('.') + QString::number(kNowMs + offsetMs) + QStringLiteral(".signature");
}

QString streamUrl(const QString& prefix, const QString& mediaToken)
{
    return prefix + QStringLiteral("/v1/stream/direct%3A1?media_token=") + mediaToken;
}

bool trustedStream(const QString& url, const QString& origin = QStringLiteral("https://media.example.test"))
{
    return isTrustedAccountMediaUrl(url, origin, AccountMediaScope::Stream, kNowMs, LoopbackHttp::Refused);
}

} // namespace

class AccountUrlPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void originRules();
    void loopbackOriginsFollowTheBuild();
    void originNormalisation();
    void configuredOriginsAreTrusted();
    void sameOriginRules();
    void profileAddressRules();
    void profileAddressRefusesEverySensitiveName();
    void mediaTokenRefreshRules();
    void mediaTokenExpiryParsing();
    void accountMediaBoundary();
    void accountMediaPathRules();
};

void AccountUrlPolicyTest::originRules()
{
    QVERIFY(isTrustedAccountOrigin(QStringLiteral("https://account.example.test")));
    QVERIFY(isTrustedAccountOrigin(QStringLiteral("https://account.example.test/")));
    QVERIFY(isTrustedAccountOrigin(QStringLiteral("https://account.example.test:8443")));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("http://account.example.test")));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("https://account.example.test/path")));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("https://account.example.test/?token=secret")));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("https://account.example.test?")));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("https://account.example.test#part")));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("https://user:secret@account.example.test")));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("https://user@account.example.test")));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("ftp://account.example.test")));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("account.example.test")));
    QVERIFY(!isTrustedAccountOrigin(QString()));
}

void AccountUrlPolicyTest::loopbackOriginsFollowTheBuild()
{
    const QString loopback = QStringLiteral("http://127.0.0.1:8787");
#ifdef QT_NO_DEBUG
    QCOMPARE(loopbackHttpForThisBuild(), LoopbackHttp::Refused);
    QVERIFY(!isTrustedAccountOrigin(loopback));
#else
    QCOMPARE(loopbackHttpForThisBuild(), LoopbackHttp::Allowed);
    QVERIFY(isTrustedAccountOrigin(loopback));
#endif
    QVERIFY(isTrustedAccountOrigin(loopback, LoopbackHttp::Allowed));
    QVERIFY(!isTrustedAccountOrigin(loopback, LoopbackHttp::Refused));
    QVERIFY(isTrustedAccountOrigin(QStringLiteral("http://localhost:8787"), LoopbackHttp::Allowed));
    QVERIFY(isTrustedAccountOrigin(QStringLiteral("http://LOCALHOST"), LoopbackHttp::Allowed));
    QVERIFY(isTrustedAccountOrigin(QStringLiteral("http://[::1]:8787"), LoopbackHttp::Allowed));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("http://127.0.0.2:8787"), LoopbackHttp::Allowed));
    QVERIFY(!isTrustedAccountOrigin(QStringLiteral("http://account.example.test"), LoopbackHttp::Allowed));
}

void AccountUrlPolicyTest::originNormalisation()
{
    QCOMPARE(normalizeTrustedAccountOrigin(QStringLiteral("https://account.example.test")),
             QStringLiteral("https://account.example.test"));
    QCOMPARE(normalizeTrustedAccountOrigin(QStringLiteral("https://account.example.test/")),
             QStringLiteral("https://account.example.test"));
    QCOMPARE(normalizeTrustedAccountOrigin(QStringLiteral("https://account.example.test:8443/")),
             QStringLiteral("https://account.example.test:8443"));
    // The text is kept as written apart from the trailing slash.
    QCOMPARE(normalizeTrustedAccountOrigin(QStringLiteral("HTTPS://Account.Example.test")),
             QStringLiteral("HTTPS://Account.Example.test"));
    QVERIFY(normalizeTrustedAccountOrigin(QStringLiteral("https://account.example.test/path")).isEmpty());
    QVERIFY(normalizeTrustedAccountOrigin(QStringLiteral("http://account.example.test")).isEmpty());
    QVERIFY(normalizeTrustedAccountOrigin(QString()).isEmpty());
}

void AccountUrlPolicyTest::configuredOriginsAreTrusted()
{
    // Whatever this build was configured with must pass the runtime check.
    // A build without the private config has nothing to check.
    const PrivateConfig config = PrivateConfig::fromBuild();
    for (const QString& origin : {config.accountApiOrigin, config.accountFrontendOrigin, config.accountMediaOrigin}) {
        if (!origin.isEmpty()) {
            QVERIFY(isTrustedAccountOrigin(origin));
            QVERIFY(!normalizeTrustedAccountOrigin(origin).isEmpty());
        }
    }
}

void AccountUrlPolicyTest::sameOriginRules()
{
    const QString origin = QStringLiteral("https://media.example.test");
    const auto fromOrigin = [&origin](const QString& url) {
        return isUrlFromTrustedAccountOrigin(url, origin, LoopbackHttp::Refused);
    };
    QVERIFY(fromOrigin(QStringLiteral("https://media.example.test/v1/stream/a")));
    QVERIFY(fromOrigin(QStringLiteral("https://media.example.test:443/v1/stream/a")));
    QVERIFY(fromOrigin(QStringLiteral("HTTPS://MEDIA.example.test/v1/stream/a")));
    QVERIFY(!fromOrigin(QStringLiteral("https://other.example.test/v1/stream/a")));
    QVERIFY(!fromOrigin(QStringLiteral("https://media.example.test.attacker.test/v1/stream/a")));
    QVERIFY(!fromOrigin(QStringLiteral("https://media.example.test:444/v1/stream/a")));
    QVERIFY(!fromOrigin(QStringLiteral("http://media.example.test:443/v1/stream/a")));
    QVERIFY(!fromOrigin(QStringLiteral("https://user:secret@media.example.test/v1/stream/a")));
    QVERIFY(!fromOrigin(QStringLiteral("/v1/stream/a")));
    QVERIFY(!fromOrigin(QString()));
    QVERIFY(isUrlFromTrustedAccountOrigin(QStringLiteral("https://media.example.test:8443/a"),
                                          QStringLiteral("https://media.example.test:8443/"), LoopbackHttp::Refused));
    QVERIFY(!isUrlFromTrustedAccountOrigin(QStringLiteral("https://media.example.test/a"),
                                           QStringLiteral("https://media.example.test/path"), LoopbackHttp::Refused));
    QVERIFY(isUrlFromTrustedAccountOrigin(QStringLiteral("http://127.0.0.1:8787/a"),
                                          QStringLiteral("http://127.0.0.1:8787"), LoopbackHttp::Allowed));
    QVERIFY(!isUrlFromTrustedAccountOrigin(QStringLiteral("http://127.0.0.1:8788/a"),
                                           QStringLiteral("http://127.0.0.1:8787"), LoopbackHttp::Allowed));
}

void AccountUrlPolicyTest::profileAddressRules()
{
    const auto safe = [](const QString& url) { return isSafeAccountProfileUrl(url, LoopbackHttp::Refused); };
    // Inline image data is not an address.
    QVERIFY(!safe(QStringLiteral(
        "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")));
    QVERIFY(safe(QStringLiteral("https://cdn.example.test/avatar.png?size=128")));
    QVERIFY(!safe(QStringLiteral("file:///C:/private.png")));
    QVERIFY(!safe(QStringLiteral("https://cdn.example.test/avatar.png?access_token=secret")));
    QVERIFY(!safe(QStringLiteral("https://cdn.example.test/avatar.png#fragment")));

    QVERIFY(safe(QStringLiteral("https://cdn.example.test/avatar.png")));
    QVERIFY(safe(QStringLiteral("https://cdn.example.test/avatar.png?monkey=1&tokens=2")));
    QVERIFY(!safe(QStringLiteral("http://cdn.example.test/avatar.png")));
    QVERIFY(!safe(QStringLiteral("https://user:secret@cdn.example.test/avatar.png")));
    QVERIFY(!safe(QStringLiteral("https://cdn.example.test/avatar.png?size=1&TOKEN=2")));
    QVERIFY(!safe(QStringLiteral("https://cdn.example.test/avatar.png?%74oken=2")));
    QVERIFY(!safe(QStringLiteral("https://cdn.example.test/avatar.png?X-Amz-Date=2")));
    QVERIFY(!safe(QStringLiteral("/avatar.png")));
    QVERIFY(!safe(QString()));

    QVERIFY(!isSafeAccountProfileUrl(QStringLiteral("http://127.0.0.1:8787/a.png"), LoopbackHttp::Refused));
    QVERIFY(isSafeAccountProfileUrl(QStringLiteral("http://127.0.0.1:8787/a.png"), LoopbackHttp::Allowed));

    const QString prefix = QStringLiteral("https://cdn.example.test/");
    QVERIFY(safe(prefix + QString(2048 - prefix.size(), QLatin1Char('a'))));
    QVERIFY(!safe(prefix + QString(2049 - prefix.size(), QLatin1Char('a'))));
}

void AccountUrlPolicyTest::profileAddressRefusesEverySensitiveName()
{
    const QStringList names{
        QStringLiteral("access_token"), QStringLiteral("api_key"),      QStringLiteral("apikey"),
        QStringLiteral("authorization"), QStringLiteral("bearer"),      QStringLiteral("password"),
        QStringLiteral("refresh_token"), QStringLiteral("session"),     QStringLiteral("jwt"),
        QStringLiteral("token"),         QStringLiteral("media_token"), QStringLiteral("signature"),
        QStringLiteral("sig"),           QStringLiteral("credential"),  QStringLiteral("expires"),
        QStringLiteral("auth"),          QStringLiteral("key"),         QStringLiteral("x-amz-anything"),
        QStringLiteral("x-goog-anything"),
    };
    for (const QString& name : names) {
        const QString url = QStringLiteral("https://cdn.example.test/a.png?size=1&") + name + QStringLiteral("=v");
        QVERIFY2(!isSafeAccountProfileUrl(url, LoopbackHttp::Refused), qPrintable(name));
        // A name without a value counts too.
        QVERIFY2(!isSafeAccountProfileUrl(QStringLiteral("https://cdn.example.test/a.png?") + name,
                                          LoopbackHttp::Refused),
                 qPrintable(name));
    }
}

void AccountUrlPolicyTest::mediaTokenRefreshRules()
{
    const QString base = QStringLiteral("https://provider.example.test");
    const QString future = streamUrl(base, token(QLatin1StringView("stream"), kFiveMinutesMs));
    const QString expired = streamUrl(base, token(QLatin1StringView("stream"), -60 * 1000));
    const QString emptySignature =
        streamUrl(base, QStringLiteral("stream.") + QString::number(kNowMs + kFiveMinutesMs) + QLatin1Char('.'));
    const auto needsRefresh = [](const QString& url, AccountMediaScope scope) {
        return mediaTokenNeedsRefresh(url, scope, kNowMs);
    };

    QVERIFY(!needsRefresh(future, AccountMediaScope::Stream));
    QVERIFY(needsRefresh(expired, AccountMediaScope::Stream));
    QVERIFY(needsRefresh(base + QStringLiteral("/v1/stream/direct%3A1"), AccountMediaScope::Stream));
    QVERIFY(needsRefresh(streamUrl(base, QStringLiteral("broken")), AccountMediaScope::Stream));
    QVERIFY(needsRefresh(emptySignature, AccountMediaScope::Stream));
    QVERIFY(!needsRefresh(QStringLiteral("https://images.example.test/cover.jpg"), AccountMediaScope::Artwork));

    // A token for the other scope, or with the scope in other letter case.
    QVERIFY(needsRefresh(streamUrl(base, token(QLatin1StringView("artwork"), kFiveMinutesMs)),
                         AccountMediaScope::Stream));
    QVERIFY(needsRefresh(streamUrl(base, token(QLatin1StringView("Stream"), kFiveMinutesMs)),
                         AccountMediaScope::Stream));
    // The 30 second margin.
    QVERIFY(needsRefresh(streamUrl(base, token(QLatin1StringView("stream"), 30'000)), AccountMediaScope::Stream));
    QVERIFY(!needsRefresh(streamUrl(base, token(QLatin1StringView("stream"), 30'001)), AccountMediaScope::Stream));
    // The first media_token wins, its name is folded, and the fragment is not read.
    QVERIFY(!needsRefresh(base + QStringLiteral("/v1/stream/a?MEDIA_TOKEN=")
                              + token(QLatin1StringView("stream"), kFiveMinutesMs) + QStringLiteral("&media_token=x"),
                          AccountMediaScope::Stream));
    QVERIFY(needsRefresh(base + QStringLiteral("/v1/stream/a?x=1#media_token=")
                             + token(QLatin1StringView("stream"), kFiveMinutesMs),
                         AccountMediaScope::Stream));
    QVERIFY(!needsRefresh(QString(), AccountMediaScope::Stream));
    QVERIFY(!needsRefresh(base + QStringLiteral("/v1/ARTWORK?media_token=")
                              + token(QLatin1StringView("artwork"), kFiveMinutesMs),
                          AccountMediaScope::Artwork));
}

void AccountUrlPolicyTest::mediaTokenExpiryParsing()
{
    const QString base = QStringLiteral("https://provider.example.test/v1/stream/a?media_token=stream.");
    const QString future = QString::number(kNowMs + kFiveMinutesMs);
    const auto needsRefresh = [&base](const QString& expiry) {
        return mediaTokenNeedsRefresh(base + expiry + QStringLiteral(".sig"), AccountMediaScope::Stream, kNowMs);
    };
    QVERIFY(!needsRefresh(future));
    // Read like std::stoll: sign and leading white space accepted, trailing
    // text ignored, no digits or an overflow refused.
    QVERIFY(!needsRefresh(QLatin1Char('+') + future));
    QVERIFY(!needsRefresh(QLatin1Char(' ') + future));
    QVERIFY(!needsRefresh(future + QStringLiteral("abc")));
    QVERIFY(needsRefresh(QLatin1Char('-') + future));
    QVERIFY(needsRefresh(QStringLiteral("abc")));
    QVERIFY(needsRefresh(QString()));
    QVERIFY(needsRefresh(QStringLiteral("+")));
    QVERIFY(!needsRefresh(QStringLiteral("9223372036854775807")));
    QVERIFY(needsRefresh(QStringLiteral("9223372036854775808")));
    QVERIFY(needsRefresh(QStringLiteral("-9223372036854775808")));
    QVERIFY(needsRefresh(QStringLiteral("-9223372036854775809")));
}

void AccountUrlPolicyTest::accountMediaBoundary()
{
    const QString media = QStringLiteral("https://media.example.test");
    const QString streamToken = token(QLatin1StringView("stream"), kFiveMinutesMs);
    const QString artworkToken = token(QLatin1StringView("artwork"), kFiveMinutesMs);

    QVERIFY(trustedStream(streamUrl(media, streamToken)));
    QVERIFY(trustedStream(streamUrl(QStringLiteral("https://media.example.test:443"), streamToken)));
    QVERIFY(!trustedStream(streamUrl(QStringLiteral("https://other.example.test"), streamToken)));
    QVERIFY(!trustedStream(streamUrl(QStringLiteral("https://media.example.test.attacker.test"), streamToken)));
    QVERIFY(!trustedStream(streamUrl(QStringLiteral("https://media.example.test:444"), streamToken)));
    QVERIFY(!trustedStream(media + QStringLiteral("/v1/not-stream/direct%3A1?media_token=") + streamToken));
    QVERIFY(!trustedStream(streamUrl(media, artworkToken)));
    QVERIFY(!trustedStream(media + QStringLiteral("/v1/stream/direct%3A1")));
    QVERIFY(!trustedStream(streamUrl(media, token(QLatin1StringView("stream"), -60 * 1000))));
    QVERIFY(!trustedStream(
        streamUrl(media, QStringLiteral("stream.") + QString::number(kNowMs + kFiveMinutesMs) + QLatin1Char('.'))));
    QVERIFY(!trustedStream(streamUrl(QStringLiteral("https://user:secret@media.example.test"), streamToken)));
    QVERIFY(!trustedStream(streamUrl(QStringLiteral("http://media.example.test"), streamToken),
                           QStringLiteral("http://media.example.test")));
    QVERIFY(!trustedStream(streamUrl(media, streamToken), QStringLiteral("https://media.example.test/path")));

    const auto trustedArtwork = [&media](const QString& url) {
        return isTrustedAccountMediaUrl(url, media, AccountMediaScope::Artwork, kNowMs, LoopbackHttp::Refused);
    };
    QVERIFY(trustedArtwork(media + QStringLiteral("/v1/artwork?media_token=") + artworkToken));
    QVERIFY(!trustedArtwork(media + QStringLiteral("/v1/artwork-untrusted?media_token=") + artworkToken));

    const QString loopbackUrl = streamUrl(QStringLiteral("http://127.0.0.1:8787"), streamToken);
    const QString loopbackOrigin = QStringLiteral("http://127.0.0.1:8787");
    QVERIFY(isTrustedAccountMediaUrl(loopbackUrl, loopbackOrigin, AccountMediaScope::Stream, kNowMs,
                                     LoopbackHttp::Allowed));
    QVERIFY(!isTrustedAccountMediaUrl(loopbackUrl, loopbackOrigin, AccountMediaScope::Stream, kNowMs,
                                      LoopbackHttp::Refused));
    QCOMPARE(isTrustedAccountMediaUrl(loopbackUrl, loopbackOrigin, AccountMediaScope::Stream, kNowMs),
             loopbackHttpForThisBuild() == LoopbackHttp::Allowed);
}

void AccountUrlPolicyTest::accountMediaPathRules()
{
    const QString media = QStringLiteral("https://media.example.test");
    const QString streamToken = token(QLatin1StringView("stream"), kFiveMinutesMs);
    QVERIFY(!trustedStream(streamUrl(media, streamToken), QString()));
    // Credentials never ride along, even on a trusted address.
    QVERIFY(!trustedStream(streamUrl(media, streamToken) + QStringLiteral("&access_token=secret")));
    // The stream path needs something after it; letter case is folded.
    QVERIFY(!trustedStream(media + QStringLiteral("/v1/stream/?media_token=") + streamToken));
    QVERIFY(trustedStream(media + QStringLiteral("/V1/Stream/a?media_token=") + streamToken));
    // Dot segments are resolved before the path is compared.
    QVERIFY(!trustedStream(media + QStringLiteral("/v1/stream/../other?media_token=") + streamToken));
    QVERIFY(!isTrustedAccountMediaUrl(media + QStringLiteral("/v1/artwork/../x?media_token=")
                                          + token(QLatin1StringView("artwork"), kFiveMinutesMs),
                                      media, AccountMediaScope::Artwork, kNowMs, LoopbackHttp::Refused));
}

QTEST_GUILESS_MAIN(AccountUrlPolicyTest)
#include "tst_account_url_policy.moc"
