#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace lmp::core {
class PlayerController;
class SettingsStore;
} // namespace lmp::core

namespace lmp::core::mode {
class ModeService;
}

namespace lmp::ui {

// The preference rows of Settings (`App.settings`): appearance, playback,
// the system media controls, the close button and the typed display name.
// Every row reads and writes its key in Settings.json in the format earlier
// versions use; what a key does is applied by whoever owns it (the theme
// binding, the media controls bridge, the player bar, the player), so a
// change made anywhere, including Reset, shows here and there at once.
//
// GUI thread only.
class SettingsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.settings")

    // "Light", "Dark" or "System": the stored strings.
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY preferencesChanged FINAL)
    // 0 to 4 in the order of accentSeeds.
    Q_PROPERTY(int accentIndex READ accentIndex WRITE setAccentIndex NOTIFY preferencesChanged FINAL)
    // The seed colour and name of each accent choice.
    Q_PROPERTY(QVariantList accentSeeds READ accentSeeds CONSTANT FINAL)
    Q_PROPERTY(QStringList accentNames READ accentNames CONSTANT FINAL)
    Q_PROPERTY(bool autoplay READ autoplay WRITE setAutoplay NOTIFY preferencesChanged FINAL)
    // Kept and saved like every other choice; the audio side has no gap
    // removal yet, as in earlier versions.
    Q_PROPERTY(bool gapless READ gapless WRITE setGapless NOTIFY preferencesChanged FINAL)
    Q_PROPERTY(bool showAlbumArt READ showAlbumArt WRITE setShowAlbumArt NOTIFY preferencesChanged FINAL)
    Q_PROPERTY(bool mediaControls READ mediaControls WRITE setMediaControls NOTIFY preferencesChanged FINAL)
    // 0 minimise to the tray, 1 quit, in the order of closeChoices.
    Q_PROPERTY(int closeBehavior READ closeBehavior WRITE setCloseBehavior NOTIFY preferencesChanged FINAL)
    Q_PROPERTY(QStringList closeChoices READ closeChoices CONSTANT FINAL)
    // The saved display name, trimmed; empty when none was typed.
    Q_PROPERTY(QString displayName READ displayName NOTIFY preferencesChanged FINAL)
    // The typed name is the profile name outside Account mode, so it can be
    // edited there.
    Q_PROPERTY(bool displayNameShown READ displayNameShown NOTIFY preferencesChanged FINAL)
    // "grid" or "list": how every song list is laid out (TrackView).
    Q_PROPERTY(QString trackView READ trackView WRITE setTrackView NOTIFY preferencesChanged FINAL)

public:
    // All three are owned by the composition root and outlive this.
    SettingsViewModel(core::SettingsStore& settings, core::PlayerController& player, const core::mode::ModeService& mode,
                      QObject* parent = nullptr);

    [[nodiscard]] QString theme() const;
    void setTheme(const QString& theme);
    [[nodiscard]] int accentIndex() const;
    void setAccentIndex(int index);
    [[nodiscard]] QVariantList accentSeeds() const;
    [[nodiscard]] QStringList accentNames() const;
    [[nodiscard]] bool autoplay() const;
    void setAutoplay(bool autoplay);
    [[nodiscard]] bool gapless() const;
    void setGapless(bool gapless);
    [[nodiscard]] bool showAlbumArt() const;
    void setShowAlbumArt(bool show);
    [[nodiscard]] bool mediaControls() const;
    void setMediaControls(bool enabled);
    [[nodiscard]] int closeBehavior() const;
    void setCloseBehavior(int behavior);
    [[nodiscard]] QStringList closeChoices() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] bool displayNameShown() const;
    [[nodiscard]] QString trackView() const;
    void setTrackView(const QString& view);

    // Whether saving `text` would change the saved name: leading and trailing
    // white space alone does not.
    Q_INVOKABLE bool isDisplayNameEdit(const QString& text) const;
    // Saves the name trimmed (an empty name is allowed and shows the default
    // name elsewhere) and returns what was saved.
    Q_INVOKABLE QString saveDisplayName(const QString& text);

    // Writes the defaults of the keys earlier versions reset
    // (core::resetPreferencesToDefaults).
    Q_INVOKABLE void resetDefaults();

    // White space trimmed from both ends, as earlier versions trimmed names.
    [[nodiscard]] static QString trimmedName(const QString& text);

signals:
    void preferencesChanged();

private:
    core::SettingsStore& m_settings;
    core::PlayerController& m_player;
    const core::mode::ModeService& m_mode;
};

} // namespace lmp::ui
