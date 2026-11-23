#include "core/cast/CastSession.h"

#include "core/cast/CastTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace lmp::core::cast {

namespace {

[[nodiscard]] QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QByteArray typed(const char* type)
{
    return compact(QJsonObject{{QStringLiteral("type"), QString::fromLatin1(type)}});
}

} // namespace

CastSession::CastSession(std::unique_ptr<ICastTransport> transport, QObject* parent)
    : QObject(parent)
    , m_transport(std::move(transport))
{
    m_transport->setParent(nullptr);
    connect(m_transport.get(), &ICastTransport::opened, this, &CastSession::onOpened);
    connect(m_transport.get(), &ICastTransport::received, this, &CastSession::onReceived);
    connect(m_transport.get(), &ICastTransport::closed, this, &CastSession::onTransportClosed);
    m_heartbeat.setInterval(HeartbeatIntervalMs);
    connect(&m_heartbeat, &QTimer::timeout, this,
            [this]() { send(HeartbeatNamespace, QString::fromLatin1(ReceiverId), typed("PING")); });
    m_readyTimeout.setSingleShot(true);
    m_readyTimeout.setInterval(ReadyTimeoutMs);
    connect(&m_readyTimeout, &QTimer::timeout, this, [this]() { finish(tr("The device did not answer in time.")); });
}

CastSession::~CastSession()
{
    m_finished = true;
    m_transport->close();
}

void CastSession::start(const QString& host, quint16 port)
{
    m_readyTimeout.start();
    m_transport->open(host, port);
}

void CastSession::disconnectFromDevice()
{
    if (m_finished) {
        return;
    }
    if (m_open) {
        send(ConnectionNamespace, QString::fromLatin1(ReceiverId), typed("CLOSE"));
    }
    m_finished = true;
    m_open = false;
    m_ready = false;
    m_heartbeat.stop();
    m_readyTimeout.stop();
    m_transport->close();
}

void CastSession::onOpened()
{
    if (m_finished) {
        return;
    }
    m_open = true;
    send(ConnectionNamespace, QString::fromLatin1(ReceiverId), typed("CONNECT"));
    send(ReceiverNamespace, QString::fromLatin1(ReceiverId),
         compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("LAUNCH")},
                             {QStringLiteral("appId"), QString::fromLatin1(DefaultMediaReceiverAppId)},
                             {QStringLiteral("requestId"), nextRequestId()}}));
    m_heartbeat.start();
}

void CastSession::onReceived(const QByteArray& bytes)
{
    m_decoder.append(bytes);
    bool broken = false;
    while (!m_finished) {
        const std::optional<QByteArray> body = m_decoder.next(broken);
        if (!body) {
            break;
        }
        if (const std::optional<CastMessage> message = decodeCastMessage(*body)) {
            handle(*message);
        }
    }
    if (broken && !m_finished) {
        finish(tr("The device sent a message that could not be read."));
    }
}

void CastSession::onTransportClosed(const QString& reason)
{
    if (m_finished) {
        return;
    }
    m_open = false;
    finish(reason);
}

void CastSession::finish(const QString& reason)
{
    if (m_finished) {
        return;
    }
    const bool wasReady = m_ready;
    m_finished = true;
    m_ready = false;
    m_heartbeat.stop();
    m_readyTimeout.stop();
    m_transport->close();
    if (wasReady) {
        emit closed(reason);
    } else {
        emit failed(reason);
    }
}

void CastSession::handle(const CastMessage& message)
{
    const QJsonObject json = QJsonDocument::fromJson(message.payload).object();
    const QString type = json.value(QStringLiteral("type")).toString();
    if (message.nameSpace == QLatin1StringView(HeartbeatNamespace)) {
        if (type == QLatin1StringView("PING")) {
            send(HeartbeatNamespace, message.source.isEmpty() ? QString::fromLatin1(ReceiverId) : message.source,
                 typed("PONG"));
        }
        return;
    }
    if (message.nameSpace == QLatin1StringView(ConnectionNamespace)) {
        if (type == QLatin1StringView("CLOSE")) {
            finish(tr("The device ended the session."));
        }
        return;
    }
    if (message.nameSpace == QLatin1StringView(ReceiverNamespace)) {
        if (type != QLatin1StringView("RECEIVER_STATUS") || m_ready) {
            return;
        }
        const QJsonArray applications =
            json.value(QStringLiteral("status")).toObject().value(QStringLiteral("applications")).toArray();
        for (const QJsonValue& value : applications) {
            const QJsonObject application = value.toObject();
            if (application.value(QStringLiteral("appId")).toString() != QLatin1StringView(DefaultMediaReceiverAppId)) {
                continue;
            }
            const QString transportId = application.value(QStringLiteral("transportId")).toString();
            if (transportId.isEmpty()) {
                continue;
            }
            m_transportId = transportId;
            m_ready = true;
            m_readyTimeout.stop();
            send(ConnectionNamespace, m_transportId, typed("CONNECT"));
            if (m_pendingLoad) {
                send(MediaNamespace, m_transportId, *m_pendingLoad);
                m_pendingLoad.reset();
            }
            emit ready();
            return;
        }
        return;
    }
    if (message.nameSpace == QLatin1StringView(MediaNamespace)) {
        if (type != QLatin1StringView("MEDIA_STATUS")) {
            return;
        }
        const QJsonArray statuses = json.value(QStringLiteral("status")).toArray();
        if (statuses.isEmpty()) {
            return;
        }
        const QJsonObject first = statuses.first().toObject();
        CastMediaStatus status;
        status.mediaSessionId = first.value(QStringLiteral("mediaSessionId")).toInteger();
        status.playerState = first.value(QStringLiteral("playerState")).toString();
        status.currentTime = first.value(QStringLiteral("currentTime")).toDouble();
        status.idleReason = first.value(QStringLiteral("idleReason")).toString();
        const QJsonValue duration = first.value(QStringLiteral("media")).toObject().value(QStringLiteral("duration"));
        if (duration.isDouble() && std::isfinite(duration.toDouble()) && duration.toDouble() > 0.0) {
            status.duration = duration.toDouble();
        }
        if (status.mediaSessionId != 0) {
            m_mediaSessionId = status.mediaSessionId;
        }
        emit mediaStatus(status);
    }
}

void CastSession::send(const char* nameSpace, const QString& destination, const QByteArray& json)
{
    if (!m_open || m_finished) {
        return;
    }
    m_transport->write(encodeCastFrame(QString::fromLatin1(nameSpace), destination, json));
}

void CastSession::load(const CastLoadRequest& request)
{
    QJsonObject metadata{{QStringLiteral("metadataType"), 3},
                         {QStringLiteral("title"), request.title},
                         {QStringLiteral("artist"), request.artist}};
    if (!request.artworkUrl.isEmpty()) {
        metadata.insert(QStringLiteral("images"), QJsonArray{QJsonObject{{QStringLiteral("url"), request.artworkUrl}}});
    }
    QJsonObject load{{QStringLiteral("type"), QStringLiteral("LOAD")},
                     {QStringLiteral("autoplay"), request.autoplay},
                     {QStringLiteral("requestId"), nextRequestId()},
                     {QStringLiteral("media"),
                      QJsonObject{{QStringLiteral("contentId"), request.url},
                                  {QStringLiteral("contentType"), QStringLiteral("audio/mpeg")},
                                  {QStringLiteral("streamType"), QStringLiteral("BUFFERED")},
                                  {QStringLiteral("metadata"), metadata}}}};
    if (request.startAtSeconds > 0.0 && std::isfinite(request.startAtSeconds)) {
        load.insert(QStringLiteral("currentTime"), request.startAtSeconds);
    }
    const QByteArray json = compact(load);
    if (!m_ready) {
        m_pendingLoad = json;
        return;
    }
    send(MediaNamespace, m_transportId, json);
}

void CastSession::sendMediaCommand(const char* type, std::optional<double> currentTime)
{
    if (!m_ready || m_mediaSessionId == 0) {
        return;
    }
    QJsonObject command{{QStringLiteral("type"), QString::fromLatin1(type)},
                        {QStringLiteral("mediaSessionId"), static_cast<double>(m_mediaSessionId)},
                        {QStringLiteral("requestId"), nextRequestId()}};
    if (currentTime) {
        command.insert(QStringLiteral("currentTime"), *currentTime);
    }
    send(MediaNamespace, m_transportId, compact(command));
}

void CastSession::play()
{
    sendMediaCommand("PLAY");
}

void CastSession::pause()
{
    sendMediaCommand("PAUSE");
}

void CastSession::stop()
{
    m_pendingLoad.reset();
    sendMediaCommand("STOP");
}

void CastSession::seek(double seconds)
{
    if (std::isfinite(seconds)) {
        sendMediaCommand("SEEK", std::max(0.0, seconds));
    }
}

void CastSession::requestStatus()
{
    if (!m_ready) {
        return;
    }
    QJsonObject command{{QStringLiteral("type"), QStringLiteral("GET_STATUS")},
                        {QStringLiteral("requestId"), nextRequestId()}};
    if (m_mediaSessionId != 0) {
        command.insert(QStringLiteral("mediaSessionId"), static_cast<double>(m_mediaSessionId));
    }
    send(MediaNamespace, m_transportId, compact(command));
}

void CastSession::setVolume(double level)
{
    if (!std::isfinite(level)) {
        return;
    }
    send(ReceiverNamespace, QString::fromLatin1(ReceiverId),
         compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("SET_VOLUME")},
                             {QStringLiteral("volume"), QJsonObject{{QStringLiteral("level"), std::clamp(level, 0.0, 1.0)}}},
                             {QStringLiteral("requestId"), nextRequestId()}}));
}

} // namespace lmp::core::cast
