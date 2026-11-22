#include "ui/shell/AppRoot.h"

#include <QJSEngine>
#include <QQmlEngine>

#include "core/app/AppIdentity.h"

namespace lmp::ui {

namespace {

// Set by the composition root for the lifetime of its AppRoot. QML's
// singleton factory has no other way to reach an object made outside QML.
AppRoot* g_instance = nullptr;

} // namespace

AppRoot::AppRoot(QObject* parent)
    : QObject(parent)
{
}

AppRoot::~AppRoot()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QString AppRoot::version() const
{
    return core::AppIdentity::version();
}

AppRoot* AppRoot::create(QQmlEngine* /*qmlEngine*/, QJSEngine* /*jsEngine*/)
{
    Q_ASSERT_X(g_instance != nullptr, "AppRoot::create",
               "the composition root must register the App instance before QML loads");
    if (g_instance != nullptr) {
        QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    }
    return g_instance;
}

void AppRoot::registerInstance(AppRoot* instance)
{
    g_instance = instance;
}

} // namespace lmp::ui
