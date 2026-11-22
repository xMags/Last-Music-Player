#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include "core/app/AppIdentity.h"

Q_IMPORT_QML_PLUGIN(LastMusicPlayerPlugin)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(lmp::core::AppIdentity::displayName());
    QCoreApplication::setApplicationVersion(lmp::core::AppIdentity::version());

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("LastMusicPlayer", "Main");

    // Used by automated checks: prove the window loads, then leave.
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    }

    return QGuiApplication::exec();
}
