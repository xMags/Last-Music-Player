#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace lmp::core {
class SettingsStore;
} // namespace lmp::core

namespace lmp::ui {

// Settings > Sound, the equalizer card (`App.equalizer`), in the reference
// look: ten bands and a preamp from -12 to +12 dB in 0.5 dB steps, and the
// presets. It reads and writes the earlier versions' keys (EqBand0 to
// EqBand9, EqPreamp), but the card is shown disabled: the audio engine of
// this version cannot apply an equalizer (decision D12).
//
// GUI thread only.
class EqualizerViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.equalizer")

    // False in this version: the card is shown, inert.
    Q_PROPERTY(bool applies READ applies CONSTANT FINAL)
    Q_PROPERTY(QString unavailableReason READ unavailableReason CONSTANT FINAL)
    // [{label, gain, valueText}] for the ten bands.
    Q_PROPERTY(QVariantList bands READ bands NOTIFY valuesChanged FINAL)
    Q_PROPERTY(double preamp READ preamp NOTIFY valuesChanged FINAL)
    Q_PROPERTY(QString preampText READ preampText NOTIFY valuesChanged FINAL)
    // The presets, then "Custom".
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT FINAL)
    Q_PROPERTY(int presetIndex READ presetIndex NOTIFY valuesChanged FINAL)

public:
    static constexpr double GainLimitDb = 12.0;
    static constexpr double GainStepDb = 0.5;

    // `settings` outlives this object.
    explicit EqualizerViewModel(core::SettingsStore& settings, QObject* parent = nullptr);

    [[nodiscard]] bool applies() const { return false; }
    [[nodiscard]] QString unavailableReason() const;
    [[nodiscard]] QVariantList bands() const;
    [[nodiscard]] double preamp() const;
    [[nodiscard]] QString preampText() const;
    [[nodiscard]] QStringList presetNames() const;
    [[nodiscard]] int presetIndex() const;

    Q_INVOKABLE void setBand(int band, double gainDb);
    Q_INVOKABLE void setPreamp(double gainDb);
    // An index of presetNames; "Custom" changes nothing.
    Q_INVOKABLE void choosePreset(int index);
    Q_INVOKABLE void reset();

    // Clamped to the limit and rounded to the step.
    [[nodiscard]] static double normalizeGain(double gainDb);
    // "+3 dB", "-1.5 dB", "0 dB".
    [[nodiscard]] static QString formatDb(double gainDb);

signals:
    void valuesChanged();

private:
    [[nodiscard]] QList<double> gains() const;

    core::SettingsStore& m_settings;
};

} // namespace lmp::ui
