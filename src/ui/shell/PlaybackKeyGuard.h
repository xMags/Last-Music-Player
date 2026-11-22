#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// Tells the window-wide playback keys (Space, Ctrl+Right, Ctrl+Left) when to
// stay out of the way: while the focused item, or one it sits in, uses those
// keys itself. That is a text field, a button or anything announced as one
// (Space activates it), a slider (arrows), a drop-down, or a popup such as a
// dialog or menu.
//
// QML: `PlaybackKeyGuard { focusItem: Window.activeFocusItem }`, then
// `enabled: !guard.blocked` on each Shortcut. GUI thread only.
class PlaybackKeyGuard : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickItem* focusItem READ focusItem WRITE setFocusItem NOTIFY focusItemChanged FINAL)
    Q_PROPERTY(bool blocked READ blocked NOTIFY blockedChanged FINAL)

public:
    explicit PlaybackKeyGuard(QObject* parent = nullptr);

    [[nodiscard]] QQuickItem* focusItem() const { return m_focusItem; }
    void setFocusItem(QQuickItem* item);
    [[nodiscard]] bool blocked() const { return m_blocked; }

    // Whether `item` or one of the items it sits in uses the playback keys.
    [[nodiscard]] static bool ownsPlaybackKeys(QQuickItem* item);

signals:
    void focusItemChanged();
    void blockedChanged();

private:
    QPointer<QQuickItem> m_focusItem;
    bool m_blocked = false;
};

} // namespace lmp::ui
