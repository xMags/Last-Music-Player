#pragma once

#include "core/async/RequestScope.h"
#include "core/async/Result.h"
#include "core/connect/ConnectProtocol.h"
#include "core/playback/PlaybackClock.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace lmp::core::connect {

class IConnectSocket;

// This app's live link to the account's other devices: asks the account
// service for a ticket, opens the socket, says hello, and keeps what the
// server last said (the devices and the session). Speaks the protocol only;
// ConnectSync ties it to the player.
//
// After any close it reconnects with a fresh ticket, waiting reconnectDelay()
// (the attempt count starts over after a connection that lasted
// StableConnection); it does not reconnect after CloseReplaced or
// CloseSignedOut until start() is called again, and waits at least
// MaxBackoff after CloseFlooding. A socket that hears nothing for DeadSocket
// is given up and replaced.
//
// GUI thread. Ticket answers arrive through continuations bound to this
// object and are dropped once stop() or a newer attempt superseded them.
class ConnectClient final : public QObject {
    Q_OBJECT

public:
    enum class Status { Off, Connecting, Open };
    Q_ENUM(Status)

    struct Dependencies {
        // POST /v1/music/connect/ticket with the signed-in session; the answer body.
        std::function<QFuture<Result<QByteArray>>()> requestTicket;
        // The account service's API origin, which the socket goes to.
        std::function<QString()> apiOrigin;
        std::function<std::unique_ptr<IConnectSocket>()> makeSocket;
        // Must outlive the client.
        PlaybackClock& clock;
        // Numbers in [0, 1) for the back-off jitter.
        std::function<double()> random;
        // A valid id (isValidDeviceId) kept across restarts, and this PC's name.
        QString deviceId;
        QString deviceName;
    };

    explicit ConnectClient(Dependencies dependencies, QObject* parent = nullptr);
    ~ConnectClient() override;

    // Links up for the signed-in account; nothing when already linked or linking.
    void start();
    // Drops the link (sign-out, leaving Account mode). An active device gives
    // the session up first.
    void stop();

    [[nodiscard]] Status status() const noexcept { return m_status; }
    [[nodiscard]] const QString& deviceId() const noexcept { return m_deviceId; }
    [[nodiscard]] const QList<Device>& devices() const noexcept { return m_devices; }
    [[nodiscard]] const Session& session() const noexcept { return m_session; }
    // The clock's time when the session arrived.
    [[nodiscard]] std::uint64_t sessionReceivedAt() const noexcept { return m_sessionReceivedAt; }
    [[nodiscard]] qint64 epoch() const noexcept { return m_epoch; }
    // This device plays for the account.
    [[nodiscard]] bool isActive() const;
    // Another device plays and the link is open: this one is its remote.
    [[nodiscard]] bool isRemote() const;
    // The playing device as listed, or nothing.
    [[nodiscard]] std::optional<Device> activeDevice() const;

    // Each returns false when the link is not open (nothing is sent).
    bool sendState(const Playback& state);
    bool sendCommand(const Command& command);
    bool transfer(const QString& toDeviceId, bool play);
    bool release();

signals:
    void statusChanged();
    void devicesChanged();
    // The active device or its state changed (also when the link went down).
    void sessionChanged();
    // A remote's command for this device, which is the active one.
    void commandReceived(const lmp::core::connect::Command& command);
    // This device is now the active one: load `state`, and play it if `play`.
    void takeoverReceived(const lmp::core::connect::Playback& state, bool play);
    // Another device took over: pause at once.
    void stopReceived();
    // The server refused the last request.
    void errorReceived(const QString& code, const QString& message);

private:
    void open();
    void handleTicket(RequestScope::Token token, const Result<QByteArray>& answer);
    void handleOpened(IConnectSocket* socket);
    void handleText(IConnectSocket* socket, const QByteArray& text);
    void handleClosed(IConnectSocket* socket, int code);
    void socketClosed(int code);
    void scheduleReconnect(std::chrono::milliseconds minimum);
    void giveUpSilentSocket();
    void retireSocket(int code, const QString& reason);
    void resetSessionView();
    void setStatus(Status status);
    void applySession(const Session& session);
    bool send(const QByteArray& text);

    Dependencies m_dependencies;
    QString m_deviceId;
    std::unique_ptr<IConnectSocket> m_socket;
    std::unique_ptr<PlaybackTimer> m_reconnectTimer;
    std::unique_ptr<PlaybackTimer> m_watchdog;
    RequestScope m_tickets;
    Status m_status = Status::Off;
    bool m_wanted = false;
    bool m_ticketPending = false;
    int m_attempt = 0;
    std::uint64_t m_openedAt = 0; // 0 until the current socket opened
    qint64 m_epoch = 0;
    QList<Device> m_devices;
    Session m_session;
    std::uint64_t m_sessionReceivedAt = 0;
};

} // namespace lmp::core::connect
