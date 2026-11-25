#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace lmp::core {
class PlayerController;
class SettingsStore;
} // namespace lmp::core

namespace lmp::ui {

class ShellLayoutViewModel;
class ToastQueue;

// The full-screen player (`App.fullscreen`): whether it is open, which side
// panel it shows (Lyrics, Up Next or none, kept in the FullscreenPanel
// setting) and whether the window is wide enough for that panel. The song
// and transport come from App.playerBar, the Up Next list from
// App.queueRail. It opens only with a song loaded
// and closes when the song goes away.
//
// Lives on the GUI thread; the settings write is one small file write per
// panel choice, as every setting is saved.
class FullscreenPlayerViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.fullscreen")

    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged FINAL)
    Q_PROPERTY(bool lyricsChosen READ lyricsChosen NOTIFY panelChanged FINAL)
    Q_PROPERTY(bool queueChosen READ queueChosen NOTIFY panelChanged FINAL)
    // The window is wider than SidePanelHiddenMaxWidth: the panel switch and
    // the side panel may show.
    Q_PROPERTY(bool panelFits READ panelFits NOTIFY panelChanged FINAL)
    // A panel is chosen and fits.
    Q_PROPERTY(bool sidePanelShown READ sidePanelShown NOTIFY panelChanged FINAL)
    // "Album · 2021", or what of it is known.
    Q_PROPERTY(QString albumLine READ albumLine NOTIFY trackChanged FINAL)

public:
    enum class Panel { None, Lyrics, Queue };

    // At this window width and below there is no side panel.
    static constexpr int SidePanelHiddenMaxWidth = 900;

    // Every service must outlive the view model.
    FullscreenPlayerViewModel(core::PlayerController& player, core::SettingsStore& settings,
                              ShellLayoutViewModel& layout, ToastQueue& toasts, QObject* parent = nullptr);

    [[nodiscard]] bool isOpen() const { return m_open; }
    [[nodiscard]] Panel panel() const { return m_panel; }
    [[nodiscard]] bool lyricsChosen() const { return m_panel == Panel::Lyrics; }
    [[nodiscard]] bool queueChosen() const { return m_panel == Panel::Queue; }
    [[nodiscard]] bool panelFits() const;
    [[nodiscard]] bool sidePanelShown() const { return m_panel != Panel::None && panelFits(); }
    [[nodiscard]] QString albumLine() const;

    // Opens the player, or says "Play something first." with no song.
    Q_INVOKABLE void openPlayer();
    Q_INVOKABLE void close();
    Q_INVOKABLE void toggle();
    // Shows that panel; picking the one shown hides the panel.
    Q_INVOKABLE void pickLyrics();
    Q_INVOKABLE void pickQueue();

    // The stored form of a panel choice; an unknown value reads as Lyrics.
    [[nodiscard]] static Panel panelFromSetting(const QString& value);
    [[nodiscard]] static QString settingForPanel(Panel panel);

signals:
    // The player is about to open: anything that must be read before it
    // takes focus (where focus was) is read now.
    void aboutToOpen();
    void openChanged();
    void panelChanged();
    void trackChanged();

private:
    void pick(Panel panel);
    void setOpen(bool open);

    core::PlayerController& m_player;
    core::SettingsStore& m_settings;
    ShellLayoutViewModel& m_layout;
    ToastQueue& m_toasts;
    Panel m_panel = Panel::Lyrics;
    bool m_open = false;
};

} // namespace lmp::ui
