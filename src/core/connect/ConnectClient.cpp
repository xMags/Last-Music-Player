#include "core/connect/ConnectClient.h"

#include "core/connect/IConnectSocket.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <algorithm>
#include <chrono>
#include <utility>

namespace lmp::core::connect {

namespace {

Q_LOGGING_CATEGORY(lcConnect, "lmp.connect")

} // namespace

ConnectClient::ConnectClient(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_dependencies(std::move(dependencies))
    , m_deviceId(isValidDeviceId(m_dependencies.deviceId) ? m_dependencies.deviceId : newDeviceId())
{
    m_reconnectTimer = m_dependencies.clock.createTimer([this]() {
        if (m_wanted && !m_socket && !m_ticketPending) {
            open();
        }
    });
    m_watchdog = m_dependencies.clock.createTimer([this]() { giveUpSilentSocket(); });
}

ConnectClient::~ConnectClient()
{
    m_tickets.invalidate();
    if (m_socket) {
        m_socket->disconnect(this);
    }
}

void ConnectClient::start()
{
    m_wanted = true;
    if (m_socket || m_ticketPending || m_reconnectTimer->isActive()) {
        return;
    }
    open();
}

void ConnectClient::stop()
{
    m_wanted = false;
    m_reconnectTimer->stop();
    m_watchdog->stop();
    m_tickets.invalidate();
    m_ticketPending = false;
    m_attempt = 0;
    if (m_socket && m_status == Status::Open && isActive()) {
        (void)send(releaseMessage());
    }
    retireSocket(1000, QStringLiteral("signed out"));
    resetSessionView();
}

bool ConnectClient::isActive() const
{
    return !m_session.activeDeviceId.isEmpty() && m_session.activeDeviceId == m_deviceId;
}

bool ConnectClient::isRemote() const
{
    return m_status == Status::Open && !m_session.activeDeviceId.isEmpty() && m_session.activeDeviceId != m_deviceId;
}

std::optional<Device> ConnectClient::activeDevice() const
{
    if (m_session.activeDeviceId.isEmpty()) {
        return std::nullopt;
    }
    for (const Device& device : m_devices) {
        if (device.deviceId == m_session.activeDeviceId) {
            return device;
        }
    }
    return std::nullopt;
}

bool ConnectClient::sendState(const Playback& state)
{
    return send(stateMessage(m_epoch, state));
}

bool ConnectClient::sendCommand(const Command& command)
{
    return send(commandMessage(command));
}

bool ConnectClient::transfer(const QString& toDeviceId, bool play)
{
    return send(transferMessage(toDeviceId, play));
}

bool ConnectClient::release()
{
    return send(releaseMessage());
}

void ConnectClient::open()
{
    setStatus(Status::Connecting);
    const RequestScope::Token token = m_tickets.next();
    m_ticketPending = true;
    m_dependencies.requestTicket()
        .then(this, [this, token](const Result<QByteArray>& answer) { handleTicket(token, answer); })
        .onCanceled(this, [this, token]() {
            handleTicket(token, Failure::of(FailureKind::Cancelled, QStringLiteral("The ticket request was cancelled.")));
        });
}

void ConnectClient::handleTicket(RequestScope::Token token, const Result<QByteArray>& answer)
{
    if (!m_tickets.isCurrent(token)) {
        return;
    }
    m_ticketPending = false;
    if (!m_wanted) {
        setStatus(Status::Off);
        return;
    }
    std::optional<QUrl> url;
    if (answer) {
        const QJsonObject object = QJsonDocument::fromJson(answer.value()).object();
        url = socketUrl(m_dependencies.apiOrigin(), object.value(QLatin1StringView("path")).toString(),
                        object.value(QLatin1StringView("ticket")).toString());
    }
    if (!url) {
        qCInfo(lcConnect) << "No device link ticket; trying again later";
        // A server that asked for a wait (Retry-After) gets at least that.
        const int retryAfter = answer ? 0 : std::max(0, answer.failure().retryAfterSeconds.value_or(0));
        scheduleReconnect(std::chrono::seconds(retryAfter));
        return;
    }
    std::unique_ptr<IConnectSocket> socket = m_dependencies.makeSocket();
    IConnectSocket* raw = socket.get();
    connect(raw, &IConnectSocket::opened, this, [this, raw]() { handleOpened(raw); });
    connect(raw, &IConnectSocket::textReceived, this, [this, raw](const QByteArray& text) { handleText(raw, text); });
    connect(raw, &IConnectSocket::closed, this, [this, raw](int code) { handleClosed(raw, code); });
    m_socket = std::move(socket);
    // Only a connection that opens counts towards the stable-connection reset.
    m_openedAt = 0;
    m_socket->open(*url);
    // A socket that never opens is given up too.
    m_watchdog->start(DeadSocket);
}

void ConnectClient::handleOpened(IConnectSocket* socket)
{
    if (socket != m_socket.get()) {
        return;
    }
    m_openedAt = m_dependencies.clock.nowMilliseconds();
    (void)send(helloMessage(m_deviceId, m_dependencies.deviceName));
    m_watchdog->start(DeadSocket);
}

void ConnectClient::handleText(IConnectSocket* socket, const QByteArray& text)
{
    if (socket != m_socket.get()) {
        return;
    }
    m_watchdog->start(DeadSocket);
    const ServerMessage message = parseServerMessage(text);
    using Type = ServerMessage::Type;
    switch (message.type) {
    case Type::Welcome:
        m_epoch = message.epoch.value_or(0);
        m_devices = message.devices;
        qCInfo(lcConnect) << "Device link open;" << m_devices.size() << "devices listed";
        setStatus(Status::Open);
        emit devicesChanged();
        applySession(message.session);
        return;
    case Type::Devices:
        m_devices = message.devices;
        emit devicesChanged();
        return;
    case Type::Session:
        if (message.epoch) {
            m_epoch = *message.epoch;
        }
        applySession(message.session);
        return;
    case Type::Command:
        if (isActive() && message.command) {
            emit commandReceived(*message.command);
        }
        return;
    case Type::Takeover:
        if (message.epoch) {
            m_epoch = *message.epoch;
        }
        if (message.state) {
            emit takeoverReceived(*message.state, message.play);
        }
        return;
    case Type::Stop:
        if (message.epoch) {
            m_epoch = *message.epoch;
        }
        emit stopReceived();
        return;
    case Type::Error:
        emit errorReceived(message.errorCode, message.errorMessage);
        return;
    case Type::Ignored:
        return;
    }
}

void ConnectClient::handleClosed(IConnectSocket* socket, int code)
{
    if (socket != m_socket.get()) {
        return;
    }
    socketClosed(code);
}

void ConnectClient::socketClosed(int code)
{
    const std::uint64_t openedAt = m_openedAt;
    retireSocket(code, QString());
    m_watchdog->stop();
    resetSessionView();
    if (code == CloseReplaced || code == CloseSignedOut) {
        // Another connection has this device id, or the session ended: do
        // not fight it. start() links up again (after the next sign-in).
        qCInfo(lcConnect) << "Device link closed for good, code" << code;
        m_wanted = false;
        m_attempt = 0;
        return;
    }
    if (openedAt > 0 && m_dependencies.clock.nowMilliseconds() - openedAt > static_cast<std::uint64_t>(StableConnection.count())) {
        m_attempt = 0;
    }
    scheduleReconnect(code == CloseFlooding ? MaxBackoff : std::chrono::milliseconds{});
}

void ConnectClient::scheduleReconnect(std::chrono::milliseconds minimum)
{
    setStatus(Status::Off);
    if (!m_wanted) {
        return;
    }
    const std::chrono::milliseconds delay = reconnectDelay(m_attempt, m_dependencies.random(), minimum);
    ++m_attempt;
    m_reconnectTimer->start(delay);
}

void ConnectClient::giveUpSilentSocket()
{
    if (!m_socket) {
        return;
    }
    qCInfo(lcConnect) << "Device link silent; reconnecting";
    socketClosed(CloseSilent);
}

void ConnectClient::retireSocket(int code, const QString& reason)
{
    if (!m_socket) {
        return;
    }
    // Detached first: its own late close is not this client's business.
    std::unique_ptr<IConnectSocket> socket = std::move(m_socket);
    socket->disconnect(this);
    socket->close(code < 4000 && code != 1000 ? 1000 : code, reason);
    // Deleted later: this may run inside one of its signals.
    socket.release()->deleteLater();
    m_openedAt = 0;
}

void ConnectClient::resetSessionView()
{
    const bool hadSession = !m_session.activeDeviceId.isEmpty() || m_session.state.has_value();
    const bool hadDevices = !m_devices.isEmpty();
    setStatus(Status::Off);
    m_devices.clear();
    m_session = Session{};
    m_sessionReceivedAt = m_dependencies.clock.nowMilliseconds();
    if (hadDevices) {
        emit devicesChanged();
    }
    if (hadSession) {
        emit sessionChanged();
    }
}

void ConnectClient::setStatus(Status status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    emit statusChanged();
}

void ConnectClient::applySession(const Session& session)
{
    m_session = session;
    m_sessionReceivedAt = m_dependencies.clock.nowMilliseconds();
    emit sessionChanged();
}

bool ConnectClient::send(const QByteArray& text)
{
    if (!m_socket || m_openedAt == 0) {
        return false;
    }
    return m_socket->sendText(text);
}

} // namespace lmp::core::connect
