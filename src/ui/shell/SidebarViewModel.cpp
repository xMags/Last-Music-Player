#include "ui/shell/SidebarViewModel.h"

#include "ui/navigation/Route.h"

namespace lmp::ui {

namespace {

QList<SidebarNavListModel::Item> mainViews()
{
    return {
        {static_cast<int>(Route::Kind::Home), Navigator::tr("Home"), QStringLiteral("home")},
        {static_cast<int>(Route::Kind::Browse), Navigator::tr("Browse"), QStringLiteral("browse")},
        {static_cast<int>(Route::Kind::Library), Navigator::tr("Library"), QStringLiteral("library")},
        {static_cast<int>(Route::Kind::Settings), Navigator::tr("Settings"),
         QStringLiteral("settings")},
    };
}

} // namespace

SidebarViewModel::SidebarViewModel(Navigator* navigator, QObject* parent)
    : QObject(parent)
    , m_navigator(navigator)
    , m_navItems(mainViews())
    , m_activeKind(static_cast<int>(Route::Kind::Home))
{
    connect(m_navigator, &Navigator::currentChanged, this, &SidebarViewModel::followNavigator);
    followNavigator();
}


void SidebarViewModel::openView(int kind)
{
    m_navigator->openView(kind);
}


void SidebarViewModel::followNavigator()
{
    const Route route = m_navigator->current();
    if (!route.isMainView()) {
        return;
    }
    const int kind = static_cast<int>(route.kind());
    if (kind != m_activeKind) {
        m_activeKind = kind;
        emit activeKindChanged();
    }
}

} // namespace lmp::ui
