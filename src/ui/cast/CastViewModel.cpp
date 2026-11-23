#include "ui/cast/CastViewModel.h"

#include "core/cast/CastController.h"
#include "core/connect/ConnectClient.h"
#include "core/connect/ConnectSync.h"

namespace lmp::ui {

namespace {

constexpr QLatin1StringView StopItem{"stop"};
constexpr QLatin1StringView DevicePrefix{"device:"};
constexpr QLatin1StringView LinkedPrefix{"linked:"};

[[nodiscard]] QString iconFor(lmp::core::connect::DeviceKind kind)
{
    switch (kind) {
    case lmp::core::connect::DeviceKind::Windows:
        return QStringLiteral("logo-desktop");
    case lmp::core::connect::DeviceKind::Android:
        return QStringLiteral("logo-phone");
    case lmp::core::connect::DeviceKind::Web:
        return QStringLiteral("logo-browser");
    }
    return QStringLiteral("devices");
}

} // namespace

CastViewModel::CastViewModel(core::cast::CastController& controller, core::connect::ConnectClient* client,
                             core::connect::ConnectSync* sync, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_client(client)
    , m_sync(sync)
{
    if (m_client != nullptr) {
        const auto refresh = [this]() {
            rebuild();
            emit stateChanged();
        };
        connect(m_client, &core::connect::ConnectClient::devicesChanged, this, refresh);
        connect(m_client, &core::connect::ConnectClient::sessionChanged, this, refresh);
        connect(m_client, &core::connect::ConnectClient::statusChanged, this, refresh);
    }
    connect(&m_controller, &core::cast::CastController::phaseChanged, this, [this]() {
        rebuild();
        emit stateChanged();
    });
    connect(&m_controller, &core::cast::CastController::devicesChanged, this, &CastViewModel::rebuild);
}

bool CastViewModel::active() const
{
    return m_controller.phase() == core::cast::CastController::Phase::Casting;
}

bool CastViewModel::busy() const
{
    return m_controller.phase() == core::cast::CastController::Phase::Connecting;
}

QString CastViewModel::deviceName() const
{
    return m_controller.deviceName();
}

bool CastViewModel::otherDevices() const
{
    if (m_client == nullptr || m_client->status() != core::connect::ConnectClient::Status::Open) {
        return false;
    }
    for (const core::connect::Device& device : m_client->devices()) {
        if (device.deviceId != m_client->deviceId()) {
            return true;
        }
    }
    return m_client->isRemote();
}

bool CastViewModel::appendLinkedDevices(QList<MenuItemListModel::Item>& items) const
{
    if (!otherDevices()) {
        return false;
    }
    const QString self = m_client->deviceId();
    const QString active = m_client->session().activeDeviceId;
    const bool remote = m_client->isRemote();
    items.append({.id = LinkedPrefix + self,
                  .label = tr("This PC"),
                  .iconName = !remote && active == self ? QStringLiteral("check") : QStringLiteral("logo-desktop"),
                  .enabled = remote});
    for (const core::connect::Device& device : m_client->devices()) {
        if (device.deviceId == self) {
            continue;
        }
        const bool playing = device.deviceId == active;
        items.append({.id = LinkedPrefix + device.deviceId,
                      .label = device.canPlay ? device.name : tr("%1 (open it to play there)").arg(device.name),
                      .iconName = playing ? QStringLiteral("check") : iconFor(device.kind),
                      .enabled = !playing});
    }
    return true;
}

void CastViewModel::openMenu()
{
    if (active() || m_controller.refusalForCurrent().isEmpty()) {
        m_controller.scan();
    }
    rebuild();
}

void CastViewModel::rebuild()
{
    using Phase = core::cast::CastController::Phase;
    const Phase phase = m_controller.phase();
    QList<MenuItemListModel::Item> items;
    const bool linked = appendLinkedDevices(items);
    if (m_client != nullptr && m_client->isRemote()) {
        m_menu.setItems(items);
        return;
    }
    const bool casting = phase == Phase::Casting;
    const QString refusal = m_controller.refusalForCurrent();
    if (!casting && phase != Phase::Connecting && !refusal.isEmpty()) {
        items.append({.id = QStringLiteral("refusal"),
                      .label = refusal,
                      .iconName = QStringLiteral("cast"),
                      .enabled = false,
                      .separatorBefore = linked});
        m_menu.setItems(items);
        return;
    }
    if (casting) {
        items.append({.id = StopItem, .label = tr("Stop casting"), .iconName = QStringLiteral("close"), .separatorBefore = linked});
    }
    if (phase == Phase::Connecting) {
        items.append({.id = QStringLiteral("connecting"),
                      .label = tr("Connecting to %1…").arg(m_controller.deviceName()),
                      .iconName = QStringLiteral("cast"),
                      .enabled = false,
                      .separatorBefore = linked && !casting});
    }
    const QList<core::cast::CastDevice>& devices = m_controller.devices();
    bool first = true;
    for (qsizetype index = 0; index < devices.size(); ++index) {
        const core::cast::CastDevice& device = devices.at(index);
        const bool current = casting && device.name == m_controller.deviceName();
        items.append({.id = DevicePrefix + QString::number(index),
                      .label = device.name,
                      .iconName = current ? QStringLiteral("check") : QStringLiteral("cast"),
                      .enabled = phase != Phase::Connecting && !current,
                      .separatorBefore = first && (casting || linked)});
        first = false;
    }
    if (phase == Phase::Scanning) {
        items.append({.id = QStringLiteral("scanning"),
                      .label = tr("Scanning for devices…"),
                      .enabled = false,
                      .separatorBefore = !items.isEmpty()});
    } else if (devices.isEmpty() && phase != Phase::Connecting) {
        items.append({.id = QStringLiteral("none"),
                      .label = tr("No cast devices found"),
                      .enabled = false,
                      .separatorBefore = !items.isEmpty()});
    }
    m_menu.setItems(items);
}

void CastViewModel::choose(const QString& itemId)
{
    if (itemId.startsWith(LinkedPrefix)) {
        if (m_sync != nullptr) {
            m_sync->playOn(itemId.mid(LinkedPrefix.size()));
        }
        return;
    }
    if (itemId == StopItem) {
        m_controller.stopCasting();
        return;
    }
    if (!itemId.startsWith(DevicePrefix)) {
        return;
    }
    bool ok = false;
    const qsizetype index = itemId.mid(DevicePrefix.size()).toLongLong(&ok);
    const QList<core::cast::CastDevice>& devices = m_controller.devices();
    if (!ok || index < 0 || index >= devices.size()) {
        return;
    }
    m_controller.connectTo(devices.at(index));
}

} // namespace lmp::ui
