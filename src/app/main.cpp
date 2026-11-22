#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include "core/app/AppIdentity.h"
#include "platform/windows/WindowsReducedMotion.h"
#include "ui/theme/FontLoader.h"
#include "ui/theme/Motion.h"

Q_IMPORT_QML_PLUGIN(LastMusicPlayerPlugin)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(lmp::core::AppIdentity::displayName());
    QCoreApplication::setApplicationVersion(lmp::core::AppIdentity::version());

    // Before the theme resolves its font families.
    lmp::ui::FontLoader::loadBundledFonts();

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    // Declared after the engine so it is destroyed first and can never call
    // into a Motion object the engine has already deleted.
    lmp::platform::windows::WindowsReducedMotion reducedMotion;
    QPointer<lmp::ui::Motion> motion =
        engine.singletonInstance<lmp::ui::Motion*>("LastMusicPlayer", "Motion");
    if (motion) {
        motion->setReduced(reducedMotion.reduced());
        reducedMotion.setChangeHandler([motion](bool reduced) {
            if (motion) {
                motion->setReduced(reduced);
            }
        });
    }

    engine.loadFromModule("LastMusicPlayer", "Main");

    // Used by automated checks: prove the window loads, then leave.
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    }

    return QGuiApplication::exec();
}
