#pragma once

#include <QObject>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

#include "platform/IWindowChrome.h"

namespace lmp::ui {

// The window frame for QML (`App.windowChrome`): whether the custom title
// strip is in use, the window's state for the caption buttons, and the
// actions behind them. Without a chrome (another platform, or a failure to
// take over the frame) `custom` is false and the system frame stays.
class WindowChromeViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.windowChrome")

    Q_PROPERTY(bool custom READ custom NOTIFY changed FINAL)
    Q_PROPERTY(bool maximized READ maximized NOTIFY changed FINAL)
    Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
    Q_PROPERTY(bool backdropActive READ backdropActive NOTIFY changed FINAL)
    // A backdrop is chosen but this run's window cannot show it (it started
    // with an opaque surface); the next start shows it.
    Q_PROPERTY(bool backdropNeedsRestart READ backdropNeedsRestart NOTIFY changed FINAL)
    Q_PROPERTY(bool maximizeHovered READ maximizeHovered NOTIFY changed FINAL)
    Q_PROPERTY(bool maximizePressed READ maximizePressed NOTIFY changed FINAL)
    // The user is dragging the window to another place (not resizing it).
    Q_PROPERTY(bool moving READ moving NOTIFY changed FINAL)

public:
    explicit WindowChromeViewModel(platform::IWindowChrome* chrome, QObject* parent = nullptr);
    ~WindowChromeViewModel() override;

    WindowChromeViewModel(const WindowChromeViewModel&) = delete;
    WindowChromeViewModel& operator=(const WindowChromeViewModel&) = delete;

    // The composition root calls this once the window exists.
    void setAttached(bool attached);

    [[nodiscard]] bool custom() const { return m_attached; }
    [[nodiscard]] bool maximized() const;
    [[nodiscard]] bool active() const;
    [[nodiscard]] bool backdropActive() const;
    [[nodiscard]] bool backdropNeedsRestart() const { return m_backdropNeedsRestart; }
    [[nodiscard]] bool maximizeHovered() const;
    [[nodiscard]] bool maximizePressed() const;
    [[nodiscard]] bool moving() const;

    // Applies the dark system frame in dark theme.
    Q_INVOKABLE void applyTheme(bool dark);
    // Whether this run's window draws on a surface Windows can see through
    // (main.cpp decides at start). Without it a backdrop is not asked for.
    void setBackdropSupported(bool supported);
    // Asks for the material behind the sidebar and title strip; None keeps
    // the frame opaque. backdropActive reports what Windows granted.
    void setBackdrop(platform::WindowBackdrop backdrop);
    Q_INVOKABLE void setDragStripHeight(qreal height);
    // Rectangles in window coordinates, as mapToItem(null, ...) gives them.
    Q_INVOKABLE void setCaptionButtons(const QRectF& minimize, const QRectF& maximize,
                                       const QRectF& close);
    Q_INVOKABLE void minimize();
    Q_INVOKABLE void toggleMaximize();
    Q_INVOKABLE void close();

signals:
    void changed();

private:
    platform::IWindowChrome* m_chrome; // not owned; outlives this object
    bool m_attached = false;
    bool m_backdropSupported = false;
    bool m_backdropNeedsRestart = false;
};

} // namespace lmp::ui
