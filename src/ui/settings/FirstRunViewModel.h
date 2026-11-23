#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "core/mode/SourceChoice.h"

namespace lmp::core {
class SettingsStore;
}

namespace lmp::core::mode {
class ModeService;
}

namespace lmp::ui {

class Navigator;

// The one-time "Choose your music source" page (`App.firstRun`), shown
// before the shell on a first run (core::mode::shouldOfferSourceChoice). It
// ends when a remote source is chosen (API key mode opens Settings, where its
// form is) or when the user keeps Offline; SourceChoiceSeen then records it.
//
// GUI thread only.
class FirstRunViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.firstRun")

    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged FINAL)

public:
    // All three are owned by the composition root and outlive this.
    FirstRunViewModel(core::SettingsStore& settings, const core::mode::ModeService& mode, Navigator& navigator,
                      QObject* parent = nullptr);

    // Decides at launch whether the page shows; stores SourceChoiceSeen as
    // false when it does.
    void decide(const core::mode::SourceChoiceInputs& inputs);

    [[nodiscard]] bool shown() const { return m_shown; }

    // Keeps the current source and goes on to the app.
    Q_INVOKABLE void finish();

signals:
    void shownChanged();

private:
    void onModeChanged();

    core::SettingsStore& m_settings;
    const core::mode::ModeService& m_mode;
    Navigator& m_navigator;
    bool m_shown = false;
};

} // namespace lmp::ui
