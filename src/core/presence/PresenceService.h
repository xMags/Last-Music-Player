#pragma once

#include "core/presence/PresenceProtocol.h"

#include <QObject>
#include <QString>
#include <QThread>

class QLocalSocket;
class QTimer;

namespace lmp::core::presence {

class PresenceWorker;

// Discord rich presence over its local IPC pipes ("discord-ipc-0" to "-9"),
// as the earlier versions spoke it: handshake with the app's client id, wait
// for READY, then SET_ACTIVITY at most every 4 s (a newer song waits for the
// gap), ping answered with pong, and reconnects from 1 s doubling to 30 s
// with a little jitter. All pipe work runs on its own thread; this object is
// the GUI thread's side. Without a client id it does nothing.
//
// Shutdown clears nothing remotely: Discord drops the activity when the pipe
// closes.
class PresenceService : public QObject {
    Q_OBJECT

public:
    explicit PresenceService(QString clientId, QObject* parent = nullptr);
    ~PresenceService() override;
    PresenceService(const PresenceService&) = delete;
    PresenceService& operator=(const PresenceService&) = delete;

    [[nodiscard]] bool available() const { return !m_clientId.isEmpty(); }

    // Connects while enabled; disabling closes the pipe.
    void setEnabled(bool enabled);
    // The song to show (an empty title clears the activity).
    void show(const PresencePayload& payload);
    void clear();
    // Stops the thread; called by the destructor too.
    void shutdown();

private:
    QString m_clientId;
    QThread m_thread;
    PresenceWorker* m_worker = nullptr;
};

// The pipe side, on the service's thread. Public for the service only.
class PresenceWorker : public QObject {
    Q_OBJECT

public:
    explicit PresenceWorker(QString clientId);
    ~PresenceWorker() override;

    void setEnabled(bool enabled);
    void setPayload(const PresencePayload& payload);
    void stop();

private:
    void connectPipe();
    void onReadyRead();
    void onDisconnected();
    void flush();
    void scheduleReconnect();
    void write(Opcode opcode, const QByteArray& payload);
    [[nodiscard]] QString nextNonce();

    QString m_clientId;
    QLocalSocket* m_socket = nullptr;
    QTimer* m_sendTimer = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_handshakeTimer = nullptr;
    FrameDecoder m_decoder;
    bool m_enabled = false;
    bool m_ready = false;
    bool m_stopping = false;
    std::optional<PresencePayload> m_payload;
    QString m_sentFingerprint;
    qint64 m_lastSendMs = -1;
    std::chrono::milliseconds m_reconnectDelay = ReconnectStart;
    quint64 m_nonce = 0;
};

} // namespace lmp::core::presence
