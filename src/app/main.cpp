#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QImage>
#include <QQuickWindow>
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

    // Text drawn by the system's font rasteriser, hinted to the pixel grid,
    // as in other Windows apps. Qt Quick's default (distance fields) looks
    // soft at normal sizes, more so at fractional display scaling. Must be
    // set before any window or text item exists.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

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
    // For reviewing a page without clicking to it.
    if (const qsizetype at = QCoreApplication::arguments().indexOf(QStringLiteral("--open-view"));
        at >= 0 && at + 1 < QCoreApplication::arguments().size()) {
        // Once the window has drawn, as a click would.
        QTimer::singleShot(5000, &app, [&root, view = QCoreApplication::arguments().at(at + 1)]() {
            root.showViewForReview(view);
        });
    }
    // For reviewing a screen as drawn: saves the window's own rendering to
    // the file named after --grab once the page has settled, then leaves.
    if (const qsizetype at = QCoreApplication::arguments().indexOf(QStringLiteral("--grab"));
        at >= 0 && at + 1 < QCoreApplication::arguments().size()) {
        const QString file = QCoreApplication::arguments().at(at + 1);
        QTimer::singleShot(9000, &app, [&engine, file]() {
            const QList<QObject*> roots = engine.rootObjects();
            auto* window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.constFirst());
            if (window != nullptr && !window->grabWindow().save(file)) {
                qWarning("Could not save the grab to %s", qPrintable(file));
            }
            QCoreApplication::quit();
        });
    }
    // For reviewing a search: the search box has no accessible role yet.
    if (const qsizetype at = QCoreApplication::arguments().indexOf(QStringLiteral("--search"));
        at >= 0 && at + 1 < QCoreApplication::arguments().size()) {
        root.showSearchForReview(QCoreApplication::arguments().at(at + 1));
    }
#endif

    // Used by automated checks: prove the window loads, then leave.
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    }

    return QGuiApplication::exec();
}
