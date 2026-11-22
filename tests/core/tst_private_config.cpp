#include <QLatin1Char>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/config/PrivateConfig.h"

class PrivateConfigTest : public QObject {
    Q_OBJECT

private slots:
    void accountServiceNeedsTheApiOrigin();
    void mediaOriginFallsBackToTheApiOrigin();
    void buildValuesAreWellFormed();
};

void PrivateConfigTest::accountServiceNeedsTheApiOrigin()
{
    lmp::core::PrivateConfig config;
    QVERIFY(!config.hasAccountService());

    config.accountFrontendOrigin = QStringLiteral("https://app.example.invalid");
    config.accountMediaOrigin = QStringLiteral("https://media.example.invalid");
    QVERIFY(!config.hasAccountService());

    lmp::core::PrivateConfig apiOnly;
    apiOnly.accountApiOrigin = QStringLiteral("https://api.example.invalid");
    QVERIFY(apiOnly.hasAccountService());
}

void PrivateConfigTest::mediaOriginFallsBackToTheApiOrigin()
{
    lmp::core::PrivateConfig apiOnly;
    apiOnly.accountApiOrigin = QStringLiteral("https://api.example.invalid");
    const lmp::core::PrivateConfig filled = apiOnly.withDefaults();
    QCOMPARE(filled.accountMediaOrigin, QStringLiteral("https://api.example.invalid"));
    QVERIFY(filled.accountFrontendOrigin.isEmpty());

    lmp::core::PrivateConfig full = apiOnly;
    full.accountMediaOrigin = QStringLiteral("https://media.example.invalid");
    QCOMPARE(full.withDefaults().accountMediaOrigin, QStringLiteral("https://media.example.invalid"));

    QVERIFY(lmp::core::PrivateConfig{}.withDefaults().accountMediaOrigin.isEmpty());
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
