#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include "app/CompositionRoot.h"
#include "core/app/AppIdentity.h"

Q_IMPORT_QML_PLUGIN(LastMusicPlayerPlugin)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(lmp::core::AppIdentity::displayName());
    QCoreApplication::setApplicationVersion(lmp::core::AppIdentity::version());

    lmp::app::CompositionRoot root;
    if (!root.claimSingleInstance()) {
        // Another copy is running and has been brought to the front.
        return 0;
    }

    // Declared after the root so it is destroyed first: QML never outlives
    // the services behind it.
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    root.attachEngine(engine);
    engine.loadFromModule("LastMusicPlayer", "Main");
    if (!root.showMainWindow(engine)) {
        return 1;
    }

#ifndef NDEBUG
    // For reviewing the welcome flow without a new account.
    if (QCoreApplication::arguments().contains(QStringLiteral("--show-welcome"))) {
        root.showWelcomeForReview();
    }
#endif

    // Used by automated checks: prove the window loads, then leave.
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    }

    return QGuiApplication::exec();
}
