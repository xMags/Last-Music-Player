#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "ui/settings/EqualizerViewModel.h"

using lmp::ui::EqualizerViewModel;
namespace keys = lmp::core::settings::keys;

// The equalizer card: the earlier versions' keys, the reference presets,
// and the card is inert.
class EqualizerViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void readsTheStoredKeys()
    {
        QTemporaryDir folder;
        lmp::core::SettingsStore settings(folder.filePath(QStringLiteral("Settings.json")));
        settings.setNumber(keys::eqBand(0), 3.0);
        settings.setNumber(keys::eqBand(9), -20.0); // clamped
        settings.setNumber(keys::EqPreamp, -1.26);  // rounded to the step
        EqualizerViewModel model(settings);
        QVERIFY(!model.applies());
        QVERIFY(!model.unavailableReason().isEmpty());
        const QVariantList bands = model.bands();
        QCOMPARE(bands.size(), 10);
        QCOMPARE(bands.at(0).toMap().value(QStringLiteral("gain")).toDouble(), 3.0);
        QCOMPARE(bands.at(0).toMap().value(QStringLiteral("valueText")).toString(), QStringLiteral("+3 dB"));
        QCOMPARE(bands.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("31"));
        QCOMPARE(bands.at(9).toMap().value(QStringLiteral("gain")).toDouble(), -12.0);
        QCOMPARE(model.preamp(), -1.5);
        QCOMPARE(model.preampText(), QStringLiteral("-1.5 dB"));
        QCOMPARE(model.presetIndex(), model.presetNames().size() - 1); // Custom
    }

    void presetsWriteTheKeysAndAreRecognised()
    {
        QTemporaryDir folder;
        lmp::core::SettingsStore settings(folder.filePath(QStringLiteral("Settings.json")));
        EqualizerViewModel model(settings);
        QCOMPARE(model.presetIndex(), 0); // all zero is Flat
        QSignalSpy changed(&model, &EqualizerViewModel::valuesChanged);
        const int bass = static_cast<int>(model.presetNames().indexOf(QStringLiteral("Bass Boost")));
        model.choosePreset(bass);
        QVERIFY(changed.count() > 0);
        QCOMPARE(settings.number(keys::eqBand(0)), 6.0);
        QCOMPARE(settings.number(keys::eqBand(4)), 1.0);
        QCOMPARE(settings.number(keys::EqPreamp), -2.0);
        QCOMPARE(model.presetIndex(), bass);
        model.setBand(4, 1.2);
        QCOMPARE(settings.number(keys::eqBand(4)), 1.0);
        model.setBand(4, 2.0);
        QCOMPARE(model.presetIndex(), model.presetNames().size() - 1);
        model.reset();
        QCOMPARE(model.presetIndex(), 0);
        // "Custom" and out-of-range picks change nothing.
        model.choosePreset(static_cast<int>(model.presetNames().size()) - 1);
        model.choosePreset(-1);
        model.setBand(10, 5.0);
        QCOMPARE(model.presetIndex(), 0);
    }

    void formatsLikeTheReference()
    {
        QCOMPARE(EqualizerViewModel::formatDb(0.0), QStringLiteral("0 dB"));
        QCOMPARE(EqualizerViewModel::formatDb(-0.2), QStringLiteral("0 dB"));
        QCOMPARE(EqualizerViewModel::formatDb(4.5), QStringLiteral("+4.5 dB"));
        QCOMPARE(EqualizerViewModel::formatDb(-12.0), QStringLiteral("-12 dB"));
    }
};

QTEST_GUILESS_MAIN(EqualizerViewModelTest)
#include "tst_equalizer_view_model.moc"
