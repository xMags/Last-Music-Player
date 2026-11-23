#include "ui/cast/CastViewModel.h"

#include "core/cast/CastController.h"

namespace lmp::ui {

namespace {

constexpr QLatin1StringView StopItem{"stop"};
constexpr QLatin1StringView DevicePrefix{"device:"};

} // namespace

CastViewModel::CastViewModel(core::cast::CastController& controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
{
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
    const bool casting = phase == Phase::Casting;
    const QString refusal = m_controller.refusalForCurrent();
    if (!casting && phase != Phase::Connecting && !refusal.isEmpty()) {
        items.append({.id = QStringLiteral("refusal"), .label = refusal, .iconName = QStringLiteral("cast"), .enabled = false});
        m_menu.setItems(items);
        return;
    }
    if (casting) {
        items.append({.id = StopItem, .label = tr("Stop casting"), .iconName = QStringLiteral("close")});
    }
    if (phase == Phase::Connecting) {
        items.append({.id = QStringLiteral("connecting"),
                      .label = tr("Connecting to %1…").arg(m_controller.deviceName()),
                      .iconName = QStringLiteral("cast"),
                      .enabled = false});
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
                      .separatorBefore = first && casting});
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
