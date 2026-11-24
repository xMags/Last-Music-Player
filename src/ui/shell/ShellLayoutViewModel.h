#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// The shell's width steps (`App.layout`): sidebar and rail widths for the
// window width, and whether the queue rail is docked and open. Pure logic;
// the shell reports its width.
//
//   width >= 1500: sidebar 300, rail 380
//   width >= 1180: the rail is a column of the layout (docked)
//   width <  1180: the rail opens over the content, min(380, 88% of width)
//   width <=  899: the sidebar collapses to a 64px icon rail
//
// The rail starts open; it is closed at start-up in a narrow window, and
// crossing 1180 opens it when widening and closes it when narrowing.
class ShellLayoutViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.layout")

    Q_PROPERTY(qreal windowWidth READ windowWidth WRITE setWindowWidth NOTIFY changed FINAL)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth NOTIFY changed FINAL)
    Q_PROPERTY(bool sidebarCollapsed READ sidebarCollapsed NOTIFY changed FINAL)
    Q_PROPERTY(int railWidth READ railWidth NOTIFY changed FINAL)
    Q_PROPERTY(qreal railOverlayWidth READ railOverlayWidth NOTIFY changed FINAL)
    Q_PROPERTY(bool railDocked READ railDocked NOTIFY changed FINAL)
    Q_PROPERTY(bool railOpen READ railOpen WRITE setRailOpen NOTIFY changed FINAL)
    // The Connect panel (connect-ui.ts) holds the rail in place of its tabs.
    Q_PROPERTY(bool connectOpen READ connectOpen NOTIFY changed FINAL)

public:
    static constexpr int kWideMinWidth = 1500;
    static constexpr int kDockedRailMinWidth = 1180;
    static constexpr int kCollapsedMaxWidth = 899;

    explicit ShellLayoutViewModel(QObject* parent = nullptr);

    [[nodiscard]] qreal windowWidth() const { return m_width; }
    void setWindowWidth(qreal width);

    [[nodiscard]] int sidebarWidth() const;
    [[nodiscard]] bool sidebarCollapsed() const { return m_width <= kCollapsedMaxWidth; }
    [[nodiscard]] int railWidth() const { return m_width >= kWideMinWidth ? 380 : 300; }
    [[nodiscard]] qreal railOverlayWidth() const;
    [[nodiscard]] bool railDocked() const { return m_width >= kDockedRailMinWidth; }
    [[nodiscard]] bool railOpen() const { return m_railOpen; }
    void setRailOpen(bool open);

    Q_INVOKABLE void toggleRail();

    [[nodiscard]] bool connectOpen() const { return m_connectOpen; }
    // The Devices button: opens the panel in the rail (opening the rail
    // too), or closes it. Closing gives the rail back, or closes it again
    // when the panel opened it.
    Q_INVOKABLE void toggleConnect();
    Q_INVOKABLE void closeConnect();

signals:
    void changed();

private:
    qreal m_width = 0;
    bool m_widthKnown = false;
    bool m_railOpen = true;
    bool m_connectOpen = false;
    // The rail was closed when Connect opened in it.
    bool m_railOpenedForConnect = false;
};

} // namespace lmp::ui
