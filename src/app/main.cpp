#include <QCoreApplication>
#include <QFont>
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
#include "platform/windows/WindowsSingleInstance.h"

Q_IMPORT_QML_PLUGIN(LastMusicPlayerPlugin)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(lmp::core::AppIdentity::displayName());
    QCoreApplication::setApplicationVersion(lmp::core::AppIdentity::version());

    // Claimed before any service exists: a second copy must not open the
    // database (which resets its account context), the download list or the
    // settings of the copy that is already running. Held until exit.
    lmp::platform::windows::WindowsSingleInstance singleInstance(lmp::core::AppIdentity::singleInstanceMutexName(),
                                                                 lmp::core::AppIdentity::displayName());
    if (!singleInstance.tryAcquire()) {
        // Another copy is running: bring it to the front and leave.
        singleInstance.activateExistingWindow();
        return 0;
    }

    lmp::app::CompositionRoot root;

    // Text drawn by Windows' own rasteriser (DirectWrite, ClearType), hinted
    // to the pixel grid as in other Windows apps. The alternatives each fell
    // short at 125% and 150%: distance fields and outline (curve) rendering
    // look soft, FreeType drew thin, pale strokes, and grey DirectWrite
    // glyphs (a styled Text) came out heavy and crowded in bold. Large text
    // is drawn from its outlines instead (AppText.qml). Must be set before
    // any window or text item exists.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    // Text without a theme font is hinted as the theme's fonts are
    // (TextStyles.cpp).
    QFont defaultFont = QGuiApplication::font();
    defaultFont.setHintingPreference(QFont::PreferVerticalHinting);
    QGuiApplication::setFont(defaultFont);

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
        // With the display off no frames run, so layout work waits for the
        // next one; a first grab renders that frame and the saved one, a
        // second later, shows the settled page.
        QTimer::singleShot(11000, &app, [&app, &engine, file]() {
            const QList<QObject*> roots = engine.rootObjects();
            auto* window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.constFirst());
            if (window == nullptr) {
                QCoreApplication::quit();
                return;
            }
            static_cast<void>(window->grabWindow());
            QTimer::singleShot(1000, &app, [window, file]() {
                if (!window->grabWindow().save(file)) {
                    qWarning("Could not save the grab to %s", qPrintable(file));
                }
                QCoreApplication::quit();
            });
        });
    }
    // For reviewing a page further down: scrolls it by that many pixels once
    // the page is open (after --open-view), before --grab saves it.
    if (const qsizetype at = QCoreApplication::arguments().indexOf(QStringLiteral("--scroll"));
        at >= 0 && at + 1 < QCoreApplication::arguments().size()) {
        const qreal offset = QCoreApplication::arguments().at(at + 1).toDouble();
        QTimer::singleShot(8000, &app, [&engine, offset]() {
            const QList<QObject*> roots = engine.rootObjects();
            QObject* scroller = roots.isEmpty() ? nullptr : roots.constFirst()->findChild<QObject*>(QStringLiteral("pageScroller"));
            if (scroller != nullptr) {
                scroller->setProperty("contentY", offset);
            }
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
