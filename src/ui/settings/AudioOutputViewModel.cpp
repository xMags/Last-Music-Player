#include "ui/settings/AudioOutputViewModel.h"

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/IAudioOutputs.h"
#include "platform/IAudioPlayer.h"

namespace lmp::ui {

namespace keys = core::settings::keys;

AudioOutputViewModel::AudioOutputViewModel(platform::IAudioOutputs& outputs, platform::IAudioPlayer& player,
                                           core::SettingsStore& settings, QObject* parent)
    : QObject(parent)
    , m_outputs(outputs)
    , m_player(player)
    , m_settings(settings)
{
    connect(&m_outputs, &platform::IAudioOutputs::outputsChanged, this, &AudioOutputViewModel::follow);
    // The setting changed elsewhere (Reset settings clears it): the player
    // follows it, as it does a device coming or going.
    connect(&m_settings, &core::SettingsStore::changed, this, [this](const QString& key) {
        if (key.isEmpty() || key == keys::OutputDeviceId) {
            follow();
        }
    });
    // The stored choice at once; the player falls back to the default when
    // the system does not know it.
    m_applied = m_settings.string(keys::OutputDeviceId, QString());
    m_player.setOutputDevice(m_applied);
}

QStringList AudioOutputViewModel::choices() const
{
    QStringList list{tr("System default")};
    for (const platform::AudioOutput& output : m_outputs.outputs()) {
        list.append(output.name);
    }
    return list;
}

int AudioOutputViewModel::currentIndex() const
{
    const QString stored = m_settings.string(keys::OutputDeviceId, QString());
    const QList<platform::AudioOutput> outputs = m_outputs.outputs();
    for (qsizetype index = 0; index < outputs.size(); ++index) {
        if (outputs.at(index).id == stored) {
            return static_cast<int>(index) + 1;
        }
    }
    return 0;
}

void AudioOutputViewModel::choose(int index)
{
    const QList<platform::AudioOutput> outputs = m_outputs.outputs();
    if (index < 0 || index > outputs.size()) {
        return;
    }
    const QString id = index == 0 ? QString() : outputs.at(index - 1).id;
    m_settings.setString(keys::OutputDeviceId, id);
    m_applied = id;
    m_player.setOutputDevice(id);
    emit changed();
}

void AudioOutputViewModel::follow()
{
    const QString stored = m_settings.string(keys::OutputDeviceId, QString());
    QString wanted;
    for (const platform::AudioOutput& output : m_outputs.outputs()) {
        if (output.id == stored) {
            wanted = stored;
        }
    }
    if (wanted != m_applied) {
        m_applied = wanted;
        m_player.setOutputDevice(wanted);
    }
    emit changed();
}

} // namespace lmp::ui
