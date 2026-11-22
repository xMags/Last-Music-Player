#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "ui/navigation/Navigator.h"
#include "ui/shell/WindowChromeViewModel.h"

class QJSEngine;
class QQmlEngine;

namespace lmp::ui {

// The `App` singleton: the shell-level view models for QML. The composition
// root creates the one instance and keeps ownership; QML receives it through
// create(). The pointer kept for create() is the only static state allowed in
// the project.
class AppRoot : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::WindowChromeViewModel* windowChrome READ windowChrome CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::Navigator* navigator READ navigator CONSTANT FINAL)

public:
    // The view models are owned by the composition root and outlive this.
    AppRoot(WindowChromeViewModel* windowChrome, Navigator* navigator, QObject* parent = nullptr);
    ~AppRoot() override;

    AppRoot(const AppRoot&) = delete;
    AppRoot& operator=(const AppRoot&) = delete;

    [[nodiscard]] QString version() const;
    [[nodiscard]] WindowChromeViewModel* windowChrome() const { return m_windowChrome; }
    [[nodiscard]] Navigator* navigator() const { return m_navigator; }

    // Called by the QML engine the first time QML uses `App`. Returns the
    // instance the composition root registered, with C++ ownership.
    static AppRoot* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // The composition root registers the instance before loading any QML and
    // clears it before destroying it.
    static void registerInstance(AppRoot* instance);

private:
    WindowChromeViewModel* m_windowChrome;
    Navigator* m_navigator;
};

} // namespace lmp::ui
