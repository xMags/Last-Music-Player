#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "../core/FakeAudioPlayer.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/IAudioOutputs.h"
#include "ui/settings/AudioOutputViewModel.h"

namespace {

class FakeOutputs final : public lmp::platform::IAudioOutputs {
public:
    [[nodiscard]] QList<lmp::platform::AudioOutput> outputs() const override { return list; }
    void set(QList<lmp::platform::AudioOutput> outputs)
    {
        list = std::move(outputs);
        emit outputsChanged();
    }
    QList<lmp::platform::AudioOutput> list;
};

} // namespace

// Settings "Output device": the choice, the stored setting, and the default
// while the chosen device is gone.
class AudioOutputViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void choiceIsKeptAndApplied();
    void goneDeviceFallsBackAndReturns();
    void resettingTheSettingMovesThePlayer();
};

void AudioOutputViewModelTest::choiceIsKeptAndApplied()
{
    QTemporaryDir folder;
    lmp::core::SettingsStore settings(folder.filePath(QStringLiteral("Settings.json")));
    FakeOutputs outputs;
    FakeAudioPlayer player;
    lmp::ui::AudioOutputViewModel model(outputs, player, settings);
    QCOMPARE(player.outputDevices, QList<QString>{QString()});
    outputs.set({{QStringLiteral("dev-a"), QStringLiteral("Speakers")}, {QStringLiteral("dev-b"), QStringLiteral("Headphones")}});
    QCOMPARE(model.choices(), (QStringList{QStringLiteral("System default"), QStringLiteral("Speakers"), QStringLiteral("Headphones")}));
    QCOMPARE(model.currentIndex(), 0);
    model.choose(2);
    QCOMPARE(settings.string(lmp::core::settings::keys::OutputDeviceId, QString()), QStringLiteral("dev-b"));
    QCOMPARE(player.outputDevices.constLast(), QStringLiteral("dev-b"));
    QCOMPARE(model.currentIndex(), 2);
    model.choose(0);
    QCOMPARE(player.outputDevices.constLast(), QString());
    model.choose(9);
    QCOMPARE(model.currentIndex(), 0);
}

void AudioOutputViewModelTest::goneDeviceFallsBackAndReturns()
{
    QTemporaryDir folder;
    lmp::core::SettingsStore settings(folder.filePath(QStringLiteral("Settings.json")));
    settings.setString(lmp::core::settings::keys::OutputDeviceId, QStringLiteral("dev-b"));
    FakeOutputs outputs;
    FakeAudioPlayer player;
    lmp::ui::AudioOutputViewModel model(outputs, player, settings);
    QCOMPARE(player.outputDevices.constLast(), QStringLiteral("dev-b"));
    // The list arrives without it: the default plays, the choice stays.
    outputs.set({{QStringLiteral("dev-a"), QStringLiteral("Speakers")}});
    QCOMPARE(player.outputDevices.constLast(), QString());
    QCOMPARE(settings.string(lmp::core::settings::keys::OutputDeviceId, QString()), QStringLiteral("dev-b"));
    // It comes back.
    outputs.set({{QStringLiteral("dev-a"), QStringLiteral("Speakers")}, {QStringLiteral("dev-b"), QStringLiteral("Headphones")}});
    QCOMPARE(player.outputDevices.constLast(), QStringLiteral("dev-b"));
    QCOMPARE(model.currentIndex(), 2);
}

void AudioOutputViewModelTest::resettingTheSettingMovesThePlayer()
{
    QTemporaryDir folder;
    lmp::core::SettingsStore settings(folder.filePath(QStringLiteral("Settings.json")));
    FakeOutputs outputs;
    FakeAudioPlayer player;
    lmp::ui::AudioOutputViewModel model(outputs, player, settings);
    outputs.set({{QStringLiteral("dev-a"), QStringLiteral("Speakers")}, {QStringLiteral("dev-b"), QStringLiteral("Headphones")}});
    model.choose(2);
    QCOMPARE(player.outputDevices.constLast(), QStringLiteral("dev-b"));
    // Reset settings (or anything else) clears the choice: the default plays.
    QVERIFY(settings.reset());
    QCOMPARE(player.outputDevices.constLast(), QString());
    QCOMPARE(model.currentIndex(), 0);
}

QTEST_GUILESS_MAIN(AudioOutputViewModelTest)
#include "tst_audio_output_view_model.moc"
