#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "ui/navigation/Navigator.h"
#include "ui/shell/SidebarListModels.h"

namespace lmp::ui {

// The left sidebar (`App.sidebar`): navigation items with the active one and
// the playlists section (the profile card is App.profile). The active item
// is the last main view opened; catalogue pages leave it as it was.
class SidebarViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.sidebar")

    Q_PROPERTY(lmp::ui::SidebarNavListModel* navItems READ navItems CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::SidebarPlaylistListModel* playlists READ playlists CONSTANT FINAL)
    Q_PROPERTY(int activeKind READ activeKind NOTIFY activeKindChanged FINAL)


public:
    // The navigator is owned by the composition root and outlives this.
    explicit SidebarViewModel(Navigator* navigator, QObject* parent = nullptr);

    [[nodiscard]] SidebarNavListModel* navItems() { return &m_navItems; }
    [[nodiscard]] SidebarPlaylistListModel* playlists() { return &m_playlists; }
    [[nodiscard]] int activeKind() const { return m_activeKind; }


    Q_INVOKABLE void openView(int kind);


signals:
    void activeKindChanged();

    // The "+" beside Playlists; creating playlists arrives with the library.
    void newPlaylistRequested();

private:
    void followNavigator();

    Navigator* m_navigator;
    SidebarNavListModel m_navItems;
    SidebarPlaylistListModel m_playlists;
    int m_activeKind;
};

} // namespace lmp::ui
