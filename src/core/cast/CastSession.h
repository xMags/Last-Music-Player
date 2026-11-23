#pragma once

#include "core/cast/CastMessage.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>

namespace lmp::core::cast {

class ICastTransport;

// What a device reports about its media (MEDIA_STATUS).
struct CastMediaStatus {
    qint64 mediaSessionId = 0;
    QString playerState; // IDLE, BUFFERING, PLAYING, PAUSED, LOADING
    double currentTime = 0.0;
    std::optional<double> duration;
    QString idleReason; // FINISHED, ERROR, CANCELLED, INTERRUPTED
};

struct CastLoadRequest {
    QString url;
    QString title;
    QString artist;
    QString artworkUrl;
    double startAtSeconds = 0.0;
    bool autoplay = true;
};

// One connection to a cast device, as the earlier versions drove it:
// CONNECT to the receiver, LAUNCH the default media receiver, PING every
// 5 s (and PONG to the device's PING), CONNECT to the app's transport once
// the receiver reports it, then media commands on that transport. A LOAD
// asked for before the transport is known is sent when it is. The device
// must report the app within 6 s, or the session fails.
//
// GUI thread. Owns its transport.
class CastSession final : public QObject {
    Q_OBJECT

public:
    explicit CastSession(std::unique_ptr<ICastTransport> transport, QObject* parent = nullptr);
    ~CastSession() override;

    void start(const QString& host, quint16 port);
    // Closes the channel (CLOSE to the receiver) without `closed`.
    void disconnectFromDevice();

    [[nodiscard]] bool isReady() const noexcept { return m_ready; }

    void load(const CastLoadRequest& request);
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    void requestStatus();
    // 0 to 1, clamped.
    void setVolume(double level);

    static constexpr int HeartbeatIntervalMs = 5000;
    static constexpr int ReadyTimeoutMs = 6000;

signals:
    // The media channel is open; commands can be sent.
    void ready();
    // The session could not be set up (before `ready`).
    void failed(const QString& reason);
    // The connection ended after `ready` (the device or the network).
    void closed(const QString& reason);
    void mediaStatus(const lmp::core::cast::CastMediaStatus& status);

private:
    void onOpened();
    void onReceived(const QByteArray& bytes);
    void onTransportClosed(const QString& reason);
    void handle(const CastMessage& message);
    void send(const char* nameSpace, const QString& destination, const QByteArray& json);
    void sendMediaCommand(const char* type, std::optional<double> currentTime = std::nullopt);
    void finish(const QString& reason);
    [[nodiscard]] int nextRequestId() { return ++m_requestId; }

    std::unique_ptr<ICastTransport> m_transport;
    CastFrameDecoder m_decoder;
    QTimer m_heartbeat;
    QTimer m_readyTimeout;
    bool m_open = false;
    bool m_ready = false;
    bool m_finished = false;
    QString m_transportId;
    qint64 m_mediaSessionId = 0;
    std::optional<QByteArray> m_pendingLoad;
    int m_requestId = 0;
};

} // namespace lmp::core::cast
