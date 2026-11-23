#include "ui/settings/EqualizerViewModel.h"

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>

namespace lmp::ui {

namespace keys = core::settings::keys;

namespace {

struct Preset {
    const char* name;
    double preamp;
    std::array<double, keys::EqBandCount> gains;
};

// The reference client's presets.
constexpr std::array<Preset, 8> Presets{{
    {"Flat", 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Bass Boost", -2, {6, 5, 4, 2, 1, 0, 0, 0, 0, 0}},
    {"Treble Boost", -2, {0, 0, 0, 0, 1, 2, 3, 4, 5, 6}},
    {"Vocal", -1, {-2, -1, 0, 2, 4, 4, 3, 1, 0, -1}},
    {"Rock", -2, {5, 3, 1, 0, -1, 0, 2, 3, 4, 5}},
    {"Pop", -1, {-1, 1, 3, 4, 4, 3, 1, -1, -1, -1}},
    {"Electronic", -2, {5, 4, 2, 0, -2, 1, 0, 2, 4, 5}},
    {"Classical", -1, {4, 3, 2, 1, -1, -1, 0, 2, 3, 3}},
}};

constexpr std::array<const char*, keys::EqBandCount> BandLabels{"31", "62", "125", "250", "500",
                                                               "1K", "2K", "4K", "8K", "16K"};

} // namespace

EqualizerViewModel::EqualizerViewModel(core::SettingsStore& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    connect(&m_settings, &core::SettingsStore::changed, this, [this](const QString& key) {
        if (key.isEmpty() || key == keys::EqPreamp || key.startsWith(QLatin1StringView("EqBand"))) {
            emit valuesChanged();
        }
    });
}

QString EqualizerViewModel::unavailableReason() const
{
    return tr("Not available in this version: playback uses the Windows media player, which has no equalizer.");
}

double EqualizerViewModel::normalizeGain(double gainDb)
{
    if (!std::isfinite(gainDb)) {
        return 0.0;
    }
    const double clamped = std::clamp(gainDb, -GainLimitDb, GainLimitDb);
    return std::round(clamped / GainStepDb) * GainStepDb;
}

QString EqualizerViewModel::formatDb(double gainDb)
{
    const double value = normalizeGain(gainDb);
    QString number = QString::number(value, 'f', 1);
    if (number.endsWith(QLatin1StringView(".0"))) {
        number.chop(2);
    }
    if (number == QLatin1StringView("-0")) {
        number = QStringLiteral("0");
    }
    return (value > 0.0 ? QStringLiteral("+") : QString()) + number + QStringLiteral(" dB");
}

QList<double> EqualizerViewModel::gains() const
{
    QList<double> result;
    result.reserve(keys::EqBandCount);
    for (int band = 0; band < keys::EqBandCount; ++band) {
        result.append(normalizeGain(m_settings.number(keys::eqBand(band), 0.0)));
    }
    return result;
}

QVariantList EqualizerViewModel::bands() const
{
    const QList<double> values = gains();
    QVariantList result;
    for (int band = 0; band < keys::EqBandCount; ++band) {
        result.append(QVariantMap{{QStringLiteral("label"), QString::fromLatin1(BandLabels.at(static_cast<std::size_t>(band)))},
                                  {QStringLiteral("gain"), values.at(band)},
                                  {QStringLiteral("valueText"), formatDb(values.at(band))}});
    }
    return result;
}

double EqualizerViewModel::preamp() const
{
    return normalizeGain(m_settings.number(keys::EqPreamp, 0.0));
}

QString EqualizerViewModel::preampText() const
{
    return formatDb(preamp());
}

QStringList EqualizerViewModel::presetNames() const
{
    QStringList names;
    for (const Preset& preset : Presets) {
        names.append(tr(preset.name));
    }
    names.append(tr("Custom"));
    return names;
}

int EqualizerViewModel::presetIndex() const
{
    const QList<double> values = gains();
    const double pre = preamp();
    for (std::size_t index = 0; index < Presets.size(); ++index) {
        const Preset& preset = Presets.at(index);
        if (preset.preamp != pre) {
            continue;
        }
        bool same = true;
        for (int band = 0; band < keys::EqBandCount && same; ++band) {
            same = preset.gains.at(static_cast<std::size_t>(band)) == values.at(band);
        }
        if (same) {
            return static_cast<int>(index);
        }
    }
    return static_cast<int>(Presets.size()); // Custom
}

void EqualizerViewModel::setBand(int band, double gainDb)
{
    if (band < 0 || band >= keys::EqBandCount) {
        return;
    }
    m_settings.setNumber(keys::eqBand(band), normalizeGain(gainDb));
}

void EqualizerViewModel::setPreamp(double gainDb)
{
    m_settings.setNumber(keys::EqPreamp, normalizeGain(gainDb));
}

void EqualizerViewModel::choosePreset(int index)
{
    if (index < 0 || index >= static_cast<int>(Presets.size())) {
        return;
    }
    const Preset& preset = Presets.at(static_cast<std::size_t>(index));
    for (int band = 0; band < keys::EqBandCount; ++band) {
        m_settings.setNumber(keys::eqBand(band), preset.gains.at(static_cast<std::size_t>(band)));
    }
    m_settings.setNumber(keys::EqPreamp, preset.preamp);
}

void EqualizerViewModel::reset()
{
    choosePreset(0);
}

} // namespace lmp::ui
