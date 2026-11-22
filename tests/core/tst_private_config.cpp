#include <QLatin1Char>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/config/PrivateConfig.h"

class PrivateConfigTest : public QObject {
    Q_OBJECT

private slots:
    void accountServiceNeedsAllThreeOrigins();
    void buildValuesAreWellFormed();
};

void PrivateConfigTest::accountServiceNeedsAllThreeOrigins()
{
    lmp::core::PrivateConfig config;
    QVERIFY(!config.hasAccountService());

    config.accountApiOrigin = QStringLiteral("https://api.example.invalid");
    config.accountFrontendOrigin = QStringLiteral("https://app.example.invalid");
    QVERIFY(!config.hasAccountService());

    config.accountMediaOrigin = QStringLiteral("https://media.example.invalid");
    QVERIFY(config.hasAccountService());
}

void PrivateConfigTest::buildValuesAreWellFormed()
{
    // Passes with or without a private config: empty is valid, and the build
    // has already checked that a present origin is well formed.
    const lmp::core::PrivateConfig config = lmp::core::PrivateConfig::fromBuild();
    for (const QString& origin :
         {config.accountApiOrigin, config.accountFrontendOrigin, config.accountMediaOrigin}) {
        QVERIFY(origin.isEmpty() || origin.startsWith(QStringLiteral("http")));
        QVERIFY(!origin.endsWith(QLatin1Char('/')));
    }
}

QTEST_GUILESS_MAIN(PrivateConfigTest)
#include "tst_private_config.moc"
