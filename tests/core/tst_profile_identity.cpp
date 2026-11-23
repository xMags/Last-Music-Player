#include <QObject>
#include <QString>
#include <QTest>

#include "core/account/ProfileIdentity.h"

using lmp::core::account::AccountProfile;
using lmp::core::account::AccountSessionState;
using lmp::core::account::accountTierLabel;
using lmp::core::account::chooseProfileIdentity;
using lmp::core::account::defaultListenerName;
using lmp::core::account::ProfileIdentitySource;
using lmp::core::account::trimProfileName;
using lmp::core::mode::SourceMode;

// Which identity the app presents. The cases are the earlier version's
// (TestProfileIdentity), one for one, plus the plan label rules.
class ProfileIdentityTest : public QObject {
    Q_OBJECT

private slots:
    void validatedAccountTakesOver();
    void offlineSessionKeepsTheAccount();
    void otherModesKeepTheTypedName();
    void accountModeWithoutSessionKeepsTheTypedName();
    void signingInDoesNotPublishTheProfile();
    void missingPartsFallBackOneByOne();
    void blankEverywhereIsTheDefaultName();
    void trimsAsciiWhiteSpaceOnly();
    void tierLabel_data();
    void tierLabel();

private:
    [[nodiscard]] static AccountProfile fullProfile();
};

AccountProfile ProfileIdentityTest::fullProfile()
{
    AccountProfile profile;
    profile.id = QStringLiteral("owner-1");
    profile.username = QStringLiteral("listener");
    profile.displayName = QStringLiteral("  Sam Lee  ");
    profile.avatarUrl = QStringLiteral("https://cdn.example.test/avatar.png");
    profile.planLabel = QStringLiteral(" Premium ");
    return profile;
}

void ProfileIdentityTest::validatedAccountTakesOver()
{
    const auto identity = chooseProfileIdentity(SourceMode::Account, AccountSessionState::Validated, fullProfile(),
                                                QStringLiteral("Typed Name"));
    QCOMPARE(identity.source, ProfileIdentitySource::Account);
    QCOMPARE(identity.name, QStringLiteral("Sam Lee"));
    QCOMPARE(identity.avatarUrl, QStringLiteral("https://cdn.example.test/avatar.png"));
    QCOMPARE(identity.planLabel, QStringLiteral("Premium"));
}

void ProfileIdentityTest::offlineSessionKeepsTheAccount()
{
    const auto identity = chooseProfileIdentity(SourceMode::Account, AccountSessionState::Offline, fullProfile(),
                                                QStringLiteral("Typed Name"));
    QCOMPARE(identity.source, ProfileIdentitySource::Account);
    QCOMPARE(identity.name, QStringLiteral("Sam Lee"));
}

void ProfileIdentityTest::otherModesKeepTheTypedName()
{
    for (const SourceMode mode : {SourceMode::LocalOnly, SourceMode::ApiKey}) {
        const auto identity = chooseProfileIdentity(mode, AccountSessionState::Validated, fullProfile(),
                                                    QStringLiteral("  Typed Name  "));
        QCOMPARE(identity.source, ProfileIdentitySource::Manual);
        QCOMPARE(identity.name, QStringLiteral("Typed Name"));
        QVERIFY(identity.avatarUrl.isEmpty());
        QVERIFY(identity.planLabel.isEmpty());
    }
}

void ProfileIdentityTest::accountModeWithoutSessionKeepsTheTypedName()
{
    const auto identity = chooseProfileIdentity(SourceMode::Account, AccountSessionState::SignedOut,
                                                AccountProfile{}, QStringLiteral("Typed Name"));
    QCOMPARE(identity.source, ProfileIdentitySource::Manual);
    QCOMPARE(identity.name, QStringLiteral("Typed Name"));
}

void ProfileIdentityTest::signingInDoesNotPublishTheProfile()
{
    const auto identity = chooseProfileIdentity(SourceMode::Account, AccountSessionState::SigningIn, fullProfile(),
                                                QStringLiteral("Typed Name"));
    QCOMPARE(identity.source, ProfileIdentitySource::Manual);
}

void ProfileIdentityTest::missingPartsFallBackOneByOne()
{
    AccountProfile usernameOnly;
    usernameOnly.id = QStringLiteral("owner-2");
    usernameOnly.username = QStringLiteral("listener42");
    const auto byUsername = chooseProfileIdentity(SourceMode::Account, AccountSessionState::Validated, usernameOnly,
                                                  QStringLiteral("Typed Name"));
    QCOMPARE(byUsername.source, ProfileIdentitySource::Account);
    QCOMPARE(byUsername.name, QStringLiteral("listener42"));
    QVERIFY(byUsername.planLabel.isEmpty());

    AccountProfile nameless;
    nameless.id = QStringLiteral("owner-3");
    const auto byManual = chooseProfileIdentity(SourceMode::Account, AccountSessionState::Validated, nameless,
                                                QStringLiteral("Typed Name"));
    QCOMPARE(byManual.name, QStringLiteral("Typed Name"));
    QCOMPARE(byManual.source, ProfileIdentitySource::Account);
}

void ProfileIdentityTest::blankEverywhereIsTheDefaultName()
{
    AccountProfile nameless;
    nameless.id = QStringLiteral("owner-3");
    const auto unnamed = chooseProfileIdentity(SourceMode::Account, AccountSessionState::Validated, nameless,
                                               QStringLiteral("   "));
    QCOMPARE(unnamed.name, defaultListenerName());

    const auto blankManual = chooseProfileIdentity(SourceMode::LocalOnly, AccountSessionState::SignedOut,
                                                   AccountProfile{}, QStringLiteral(" \t "));
    QCOMPARE(blankManual.name, defaultListenerName());
    QCOMPARE(defaultListenerName(), QStringLiteral("Listener"));
}

void ProfileIdentityTest::trimsAsciiWhiteSpaceOnly()
{
    QCOMPARE(trimProfileName(QStringLiteral("\t\n Sam \r\v\f")), QStringLiteral("Sam"));
    const QString noBreak = QString(QChar(0x00A0)) + QStringLiteral("Sam");
    QCOMPARE(trimProfileName(noBreak), noBreak);
}

void ProfileIdentityTest::tierLabel_data()
{
    QTest::addColumn<QString>("plan");
    QTest::addColumn<QString>("label");
    QTest::newRow("word") << QStringLiteral("premium") << QStringLiteral("PREMIUM");
    QTest::newRow("padded") << QStringLiteral("  free ") << QStringLiteral("FREE");
    QTest::newRow("underscores") << QStringLiteral("family_plan") << QStringLiteral("FAMILY PLAN");
    QTest::newRow("dash runs") << QStringLiteral("pro--max") << QStringLiteral("PRO MAX");
    QTest::newRow("digits") << QStringLiteral("tier2") << QStringLiteral("TIER2");
    QTest::newRow("24 characters") << QStringLiteral("abcdefghijklmnopqrstuvwx") << QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWX");
    QTest::newRow("25 characters") << QStringLiteral("abcdefghijklmnopqrstuvwxy") << QString();
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("starts with a digit") << QStringLiteral("2pro") << QString();
    QTest::newRow("sentence") << QStringLiteral("Upgrade now!") << QString();
    QTest::newRow("markup") << QStringLiteral("<b>pro</b>") << QString();
    QTest::newRow("not ascii") << QStringLiteral("prémium") << QString();
    QTest::newRow("unicode padding") << QString(QChar(0x00A0)) + QStringLiteral("free") << QStringLiteral("FREE");
}

void ProfileIdentityTest::tierLabel()
{
    QFETCH(QString, plan);
    QFETCH(QString, label);
    QCOMPARE(accountTierLabel(plan), label);
}

QTEST_GUILESS_MAIN(ProfileIdentityTest)
#include "tst_profile_identity.moc"
