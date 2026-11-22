#include <QObject>
#include <QString>
#include <QTest>

#include "core/app/AppIdentity.h"

// These values are a contract with installed copies of earlier versions.
class AppIdentityTest : public QObject {
    Q_OBJECT

private slots:
    void identityMatchesInstalledVersions();
    void versionIsSet();
};

void AppIdentityTest::identityMatchesInstalledVersions()
{
    QCOMPARE(lmp::core::AppIdentity::displayName(), QStringLiteral("Last Music Player"));
    QCOMPARE(lmp::core::AppIdentity::dataFolderName(), QStringLiteral("Last Music Player"));
    QCOMPARE(lmp::core::AppIdentity::appUserModelId(), QStringLiteral("LastMusicPlayer.App"));
    QCOMPARE(lmp::core::AppIdentity::singleInstanceMutexName(),
             QStringLiteral("Local\\LastMusicPlayer.SingleInstance"));
}

void AppIdentityTest::versionIsSet()
{
    QVERIFY(lmp::core::AppIdentity::version().startsWith(QStringLiteral("2.")));
}

QTEST_GUILESS_MAIN(AppIdentityTest)
#include "tst_app_identity.moc"
