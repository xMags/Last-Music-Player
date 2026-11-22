#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QTest>

#include <optional>

#include "core/async/Result.h"
#include "core/net/UserMessages.h"

using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::net::UserMessage;
using lmp::core::net::planRefusalText;
using lmp::core::net::userMessageForFailure;
using lmp::core::net::userMessageForSignInFailure;
using lmp::core::net::userMessageForStatus;
using lmp::core::net::userMessageText;

class UserMessagesTest : public QObject {
    Q_OBJECT

private slots:
    void textsAreTheFixedSentences_data();
    void textsAreTheFixedSentences();
    void statusesMapToPlainWording_data();
    void statusesMapToPlainWording();
    void failuresMapByTransportThenStatusThenKind_data();
    void failuresMapByTransportThenStatusThenKind();
    void signInFailuresReadAsSignInProblems();
    void accountStatusesHaveBoundedText();
    void planRefusalIsTheOnlyServerTextShown();
};

void UserMessagesTest::textsAreTheFixedSentences_data()
{
    QTest::addColumn<UserMessage>("message");
    QTest::addColumn<QString>("text");
    QTest::newRow("connection") << UserMessage::Connection
                                << QStringLiteral("Could not reach Last Music. Check your connection and try again.");
    QTest::newRow("timeout") << UserMessage::Timeout << QStringLiteral("The request took too long. Try again.");
    QTest::newRow("unavailable") << UserMessage::Unavailable
                                 << QStringLiteral("Last Music is unavailable right now. Try again shortly.");
    QTest::newRow("session expired") << UserMessage::SessionExpired
                                     << QStringLiteral("Your session has expired. Sign in again.");
    QTest::newRow("forbidden") << UserMessage::Forbidden << QStringLiteral("You don't have permission to do that.");
    QTest::newRow("not found") << UserMessage::NotFound << QStringLiteral("That is no longer available.");
    QTest::newRow("conflict") << UserMessage::Conflict
                              << QStringLiteral("That changed in the meantime. Refresh and try again.");
    QTest::newRow("rate limited") << UserMessage::RateLimited
                                  << QStringLiteral("Too many requests. Wait a moment and try again.");
    QTest::newRow("generic") << UserMessage::Generic << QStringLiteral("Something went wrong. Try again.");
    QTest::newRow("sign-in failed") << UserMessage::SignInFailed
                                    << QStringLiteral("Sign-in could not be completed. Try again.");
}

void UserMessagesTest::textsAreTheFixedSentences()
{
    QFETCH(UserMessage, message);
    QFETCH(QString, text);
    QCOMPARE(userMessageText(message), text);
}

void UserMessagesTest::statusesMapToPlainWording_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<UserMessage>("message");
    QTest::newRow("401") << 401 << UserMessage::SessionExpired;
    QTest::newRow("403") << 403 << UserMessage::Forbidden;
    QTest::newRow("404") << 404 << UserMessage::NotFound;
    QTest::newRow("410") << 410 << UserMessage::NotFound;
    QTest::newRow("408") << 408 << UserMessage::Timeout;
    QTest::newRow("409") << 409 << UserMessage::Conflict;
    QTest::newRow("429") << 429 << UserMessage::RateLimited;
    QTest::newRow("500") << 500 << UserMessage::Unavailable;
    QTest::newRow("502") << 502 << UserMessage::Unavailable;
    QTest::newRow("599") << 599 << UserMessage::Unavailable;
    QTest::newRow("400") << 400 << UserMessage::Generic;
    QTest::newRow("302") << 302 << UserMessage::Generic;
    QTest::newRow("0") << 0 << UserMessage::Generic;
}

void UserMessagesTest::statusesMapToPlainWording()
{
    QFETCH(int, status);
    QFETCH(UserMessage, message);
    QCOMPARE(userMessageForStatus(status), message);
}

void UserMessagesTest::failuresMapByTransportThenStatusThenKind_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("status");
    QTest::addColumn<UserMessage>("message");
    const auto row = [](const char* name, FailureKind kind, int status, UserMessage message) {
        QTest::newRow(name) << static_cast<int>(kind) << status << message;
    };
    row("network", FailureKind::Network, 0, UserMessage::Connection);
    row("network after a status", FailureKind::Network, 200, UserMessage::Connection);
    row("timeout", FailureKind::Timeout, 0, UserMessage::Timeout);
    row("unauthorized", FailureKind::Unauthorized, 401, UserMessage::SessionExpired);
    row("forbidden", FailureKind::Forbidden, 403, UserMessage::Forbidden);
    row("not found", FailureKind::NotFound, 404, UserMessage::NotFound);
    row("rate limited", FailureKind::RateLimited, 429, UserMessage::RateLimited);
    row("server", FailureKind::Server, 503, UserMessage::Unavailable);
    row("conflict status", FailureKind::Unknown, 409, UserMessage::Conflict);
    row("gone status", FailureKind::Unknown, 410, UserMessage::NotFound);
    row("bad request", FailureKind::Unknown, 400, UserMessage::Generic);
    row("redirect refused", FailureKind::InvalidResponse, 302, UserMessage::Generic);
    row("unauthorized kind only", FailureKind::Unauthorized, 0, UserMessage::SessionExpired);
    row("forbidden kind only", FailureKind::Forbidden, 0, UserMessage::Forbidden);
    row("not found kind only", FailureKind::NotFound, 0, UserMessage::NotFound);
    row("rate limited kind only", FailureKind::RateLimited, 0, UserMessage::RateLimited);
    row("server kind only", FailureKind::Server, 0, UserMessage::Unavailable);
    row("not available", FailureKind::NotAvailable, 0, UserMessage::Unavailable);
    row("invalid response", FailureKind::InvalidResponse, 0, UserMessage::Generic);
    row("cancelled", FailureKind::Cancelled, 0, UserMessage::Generic);
    row("storage", FailureKind::Storage, 0, UserMessage::Generic);
    row("unknown", FailureKind::Unknown, 0, UserMessage::Generic);
}

void UserMessagesTest::failuresMapByTransportThenStatusThenKind()
{
    QFETCH(int, kind);
    QFETCH(int, status);
    QFETCH(UserMessage, message);
    QCOMPARE(userMessageForFailure(Failure::of(static_cast<FailureKind>(kind), {}, status)), message);
}

void UserMessagesTest::signInFailuresReadAsSignInProblems()
{
    QVERIFY(!userMessageForSignInFailure(Failure::of(FailureKind::Cancelled)).has_value());
    QVERIFY(userMessageForSignInFailure(Failure::of(FailureKind::Network)) == UserMessage::Connection);
    for (const FailureKind kind : {FailureKind::Timeout, FailureKind::Unauthorized, FailureKind::Server,
                                   FailureKind::InvalidResponse, FailureKind::Storage, FailureKind::Unknown}) {
        QVERIFY(userMessageForSignInFailure(Failure::of(kind, {}, 400)) == UserMessage::SignInFailed);
    }
}

void UserMessagesTest::accountStatusesHaveBoundedText()
{
    for (const int status : {400, 401, 403, 404, 409, 429, 500}) {
        const QString text = userMessageText(userMessageForStatus(status));
        QVERIFY(!text.isEmpty());
        QVERIFY(text.size() < 128);
    }
}

void UserMessagesTest::planRefusalIsTheOnlyServerTextShown()
{
    const auto payload = [](const QString& code, const QString& error) {
        QJsonObject object{{QStringLiteral("error"), error}};
        if (!code.isEmpty()) {
            object.insert(QStringLiteral("code"), code);
        }
        return QJsonValue(object);
    };
    QVERIFY(planRefusalText(payload(QStringLiteral("plan_required"), QStringLiteral("Your plan does not include Lyrics.")))
            == QStringLiteral("Your plan does not include Lyrics."));
    QVERIFY(planRefusalText(payload(QStringLiteral("plan_limit_reached"),
                                    QStringLiteral("Your plan's limit for Playlists is 5.")))
            == QStringLiteral("Your plan's limit for Playlists is 5."));
    QVERIFY(!planRefusalText(payload(QStringLiteral("session_scope_forbidden"), QStringLiteral("internal-service-name")))
                 .has_value());
    QVERIFY(!planRefusalText(payload({}, QStringLiteral("internal-service-name"))).has_value());
    QVERIFY(!planRefusalText(payload(QStringLiteral("plan_required"), {})).has_value());
    QVERIFY(!planRefusalText(QJsonValue(QStringLiteral("plain text"))).has_value());
    QVERIFY(!planRefusalText(QJsonObject{{QStringLiteral("code"), QStringLiteral("plan_required")},
                                         {QStringLiteral("error"), 7}})
                 .has_value());
}

QTEST_GUILESS_MAIN(UserMessagesTest)
#include "tst_user_messages.moc"
