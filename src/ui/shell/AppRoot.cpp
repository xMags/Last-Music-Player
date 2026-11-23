#include "ui/shell/AppRoot.h"

#include <QJSEngine>
#include <QQmlEngine>

#include "core/app/AppIdentity.h"
#include "core/mode/ModeService.h"
#include "core/mode/SourceMode.h"

namespace lmp::ui {

namespace {

// Set by the composition root for the lifetime of its AppRoot. QML's
// singleton factory has no other way to reach an object made outside QML.
AppRoot* g_instance = nullptr;

} // namespace

AppRoot::AppRoot(Parts parts, QObject* parent)
    : QObject(parent)
    , m_parts(parts)
{
    if (m_parts.mode != nullptr) {
        connect(m_parts.mode, &core::mode::ModeService::changed, this, &AppRoot::sourceChanged);
    }
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

QString AppRoot::sourceMode() const
{
    const core::mode::SourceMode mode = m_parts.mode != nullptr ? m_parts.mode->mode() : core::mode::SourceMode::LocalOnly;
    return QString(core::mode::sourceModeName(mode));
}

core::mode::Capabilities AppRoot::capabilities() const
{
    return m_parts.mode != nullptr ? m_parts.mode->capabilities() : core::mode::Capabilities{};
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
