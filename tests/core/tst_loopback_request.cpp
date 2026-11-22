#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/account/LoopbackRequest.h"

using lmp::core::account::kMaxLoopbackCodeBytes;
using lmp::core::account::kMaxLoopbackRequestBytes;
using lmp::core::account::LoopbackRequestOutcome;
using lmp::core::account::LoopbackRequestResult;
using lmp::core::account::parseLoopbackRequest;

namespace {

const QByteArray kAuthority = QByteArrayLiteral("127.0.0.1:49152");

QByteArray request(const QByteArray& target, const QByteArray& headers = "Host: 127.0.0.1:49152\r\n",
                   const QByteArray& start = "GET ", const QByteArray& version = " HTTP/1.1")
{
    return start + target + version + "\r\n" + headers + "\r\n";
}

LoopbackRequestResult parse(const QByteArray& text, const QByteArray& state = "s")
{
    return parseLoopbackRequest(text, kAuthority, "/callback", state);
}

LoopbackRequestOutcome outcome(const QByteArray& text, const QByteArray& state = "s")
{
    return parse(text, state).outcome;
}

} // namespace

class LoopbackRequestTest : public QObject {
    Q_OBJECT

private slots:
    void legacyCases();
    void requestLineRules();
    void hostRules();
    void targetRules();
    void queryRules();
    void stateAndErrorRules();
    void codeRules();
    void sizeRules();
};

void LoopbackRequestTest::legacyCases()
{
    const LoopbackRequestResult valid = parseLoopbackRequest(
        "GET /callback?code=code-value&state=state-value HTTP/1.1\r\nHost: 127.0.0.1:49152\r\nConnection: "
        "close\r\n\r\n",
        "127.0.0.1:49152", "/callback", "state-value");
    QCOMPARE(valid.outcome, LoopbackRequestOutcome::Code);
    QCOMPARE(valid.code, QStringLiteral("code-value"));

    const LoopbackRequestResult ipv6 =
        parseLoopbackRequest("GET /callback?state=state-value&code=ipv6-code HTTP/1.1\r\nHost: [::1]:49153\r\n\r\n",
                             "[::1]:49153", "/callback", "state-value");
    QCOMPARE(ipv6.outcome, LoopbackRequestOutcome::Code);

    QCOMPARE(outcome("GET /callback?code=x&state=s HTTP/1.1\r\nHost: localhost:49152\r\n\r\n"),
             LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s&state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback/extra?code=x&state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?error=access_denied&state=s")), LoopbackRequestOutcome::Cancelled);
}

void LoopbackRequestTest::requestLineRules()
{
    QCOMPARE(outcome(request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\n", "GET ", " HTTP/1.0")),
             LoopbackRequestOutcome::Code);
    QCOMPARE(outcome(request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\n", "POST ")),
             LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\n", "get ")),
             LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\n", "HEAD ")),
             LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\n", "GET ", " HTTP/2.0")),
             LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\n", "GET ", "")),
             LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome("GET /callback?code=x&state=s HTTP/1.1"), LoopbackRequestOutcome::Invalid);
}

void LoopbackRequestTest::hostRules()
{
    QCOMPARE(outcome(request("/callback?code=x&state=s", "")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\nHOST: 127.0.0.1:49152\r\n")),
             LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s", "hOsT: \t127.0.0.1:49152 \t\r\n")),
             LoopbackRequestOutcome::Code);
    QCOMPARE(outcome(request("/callback?code=x&state=s", "Host: 127.0.0.1:49153\r\n")),
             LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\nNoColon\r\n")),
             LoopbackRequestOutcome::Invalid);
    // A header line must end in CRLF; the blank line may be missing, as before.
    QCOMPARE(outcome("GET /callback?code=x&state=s HTTP/1.1\r\nHost: 127.0.0.1:49152"),
             LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome("GET /callback?code=x&state=s HTTP/1.1\r\nHost: 127.0.0.1:49152\r\n"),
             LoopbackRequestOutcome::Code);
    // The authority is compared with A to Z folded.
    QCOMPARE(parseLoopbackRequest(request("/callback?code=x&state=s", "Host: [::1]:49152\r\n"), "[::1]:49152",
                                  "/callback", "s")
                 .outcome,
             LoopbackRequestOutcome::Code);
}

void LoopbackRequestTest::targetRules()
{
    QCOMPARE(outcome(request("/callback?code=x&state=s#part")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/Callback?code=x&state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/%63allback?code=x&state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback/?code=x&state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("http://127.0.0.1:49152/callback?code=x&state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback")), LoopbackRequestOutcome::Invalid);
}

void LoopbackRequestTest::queryRules()
{
    QCOMPARE(outcome(request("/callback?")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&&state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s&")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s&other=%zz")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s&other=%4")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=s&other")), LoopbackRequestOutcome::Code);
    // Names are compared after decoding, so an encoded repeat is still a repeat.
    QCOMPARE(outcome(request("/callback?code=x&state=s&%73tate=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&code=y&state=s")), LoopbackRequestOutcome::Invalid);
    const LoopbackRequestResult decoded = parse(request("/callback?c%6Fde=a%2Bb+c&st%61te=s%20t"), "s t");
    QCOMPARE(decoded.outcome, LoopbackRequestOutcome::Code);
    QCOMPARE(decoded.code, QStringLiteral("a+b c"));
}

void LoopbackRequestTest::stateAndErrorRules()
{
    QCOMPARE(outcome(request("/callback?code=x")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=S")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=x&state=")), LoopbackRequestOutcome::Invalid);
    // An error without the right state is not trusted to stop the sign-in.
    QCOMPARE(outcome(request("/callback?error=access_denied")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?error=access_denied&state=t")), LoopbackRequestOutcome::Invalid);
    // An error wins over a code, and an empty error still counts.
    QCOMPARE(outcome(request("/callback?code=x&error=e&state=s")), LoopbackRequestOutcome::Cancelled);
    QCOMPARE(outcome(request("/callback?error&state=s")), LoopbackRequestOutcome::Cancelled);
    QVERIFY(parse(request("/callback?error=e&state=s")).code.isEmpty());
}

void LoopbackRequestTest::codeRules()
{
    QCOMPARE(outcome(request("/callback?state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=&state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code&state=s")), LoopbackRequestOutcome::Invalid);
    QCOMPARE(outcome(request("/callback?code=%C3%A9&state=s")), LoopbackRequestOutcome::Invalid);
    const QByteArray longest(kMaxLoopbackCodeBytes, 'c');
    const LoopbackRequestResult atLimit = parse(request("/callback?code=" + longest + "&state=s"));
    QCOMPARE(atLimit.outcome, LoopbackRequestOutcome::Code);
    QCOMPARE(atLimit.code.size(), kMaxLoopbackCodeBytes);
    QCOMPARE(outcome(request("/callback?code=" + longest + "c&state=s")), LoopbackRequestOutcome::Invalid);
}

void LoopbackRequestTest::sizeRules()
{
    QCOMPARE(outcome(QByteArray()), LoopbackRequestOutcome::Invalid);
    const QByteArray valid = request("/callback?code=x&state=s");
    const QByteArray padding = "X-Padding: " + QByteArray(kMaxLoopbackRequestBytes - valid.size() - 13, 'p') + "\r\n";
    const QByteArray atLimit = request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\n" + padding);
    QCOMPARE(atLimit.size(), kMaxLoopbackRequestBytes);
    QCOMPARE(outcome(atLimit), LoopbackRequestOutcome::Code);
    const QByteArray overLimit =
        request("/callback?code=x&state=s", "Host: 127.0.0.1:49152\r\n" + padding.left(11) + "p" + padding.mid(11));
    QCOMPARE(overLimit.size(), kMaxLoopbackRequestBytes + 1);
    QCOMPARE(outcome(overLimit), LoopbackRequestOutcome::Invalid);
}

QTEST_GUILESS_MAIN(LoopbackRequestTest)
#include "tst_loopback_request.moc"
