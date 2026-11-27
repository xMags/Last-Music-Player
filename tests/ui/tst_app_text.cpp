#include <QByteArray>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(LastMusicPlayerPlugin)

namespace {

// Text.NativeRendering and Text.CurveRendering.
constexpr int NativeRendering = 1;
constexpr int CurveRendering = 2;

} // namespace

// AppText draws small text with the system rasteriser, and text from 15px
// up, or semibold and heavier from 13px up, from its outlines.
class AppTextTest : public QObject {
    Q_OBJECT

private slots:
    void renderTypeFollowsTheSize_data()
    {
        QTest::addColumn<QString>("fontSetting");
        QTest::addColumn<int>("renderType");
        QTest::newRow("description 12px") << QStringLiteral("font.pixelSize: 12") << NativeRendering;
        QTest::newRow("body 14px") << QStringLiteral("font.pixelSize: 14") << NativeRendering;
        QTest::newRow("bold label 11px") << QStringLiteral("font.pixelSize: 11; font.weight: Font.Bold") << NativeRendering;
        QTest::newRow("semibold 12px") << QStringLiteral("font.pixelSize: 12; font.weight: Font.DemiBold") << NativeRendering;
        QTest::newRow("semibold 13px") << QStringLiteral("font.pixelSize: 13; font.weight: Font.DemiBold") << CurveRendering;
        QTest::newRow("song title 14px") << QStringLiteral("font.pixelSize: 14; font.weight: Font.DemiBold") << CurveRendering;
        QTest::newRow("medium 14px") << QStringLiteral("font.pixelSize: 14; font.weight: Font.Medium") << NativeRendering;
        QTest::newRow("regular 15px") << QStringLiteral("font.pixelSize: 15") << CurveRendering;
        QTest::newRow("greeting 44px") << QStringLiteral("font.pixelSize: 44") << CurveRendering;
        QTest::newRow("in points, 16px") << QStringLiteral("font.pointSize: 12") << CurveRendering;
        QTest::newRow("in points, 12px") << QStringLiteral("font.pointSize: 9") << NativeRendering;
    }

    void renderTypeFollowsTheSize()
    {
        QFETCH(QString, fontSetting);
        QFETCH(int, renderType);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(QStringLiteral("import QtQuick\nimport LastMusicPlayer\nAppText { text: \"Magneto\"; %1 }").arg(fontSetting).toUtf8(),
                          QUrl());
        std::unique_ptr<QObject> text(component.create());
        QVERIFY2(text, qPrintable(component.errorString()));
        QCOMPARE(text->property("renderType").toInt(), renderType);
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication app(argc, argv);
    AppTextTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_app_text.moc"
