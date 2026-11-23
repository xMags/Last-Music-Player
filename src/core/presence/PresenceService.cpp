#include "core/presence/PresenceService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocalSocket>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QTimer>

namespace lmp::core::presence {

namespace {

[[nodiscard]] qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace

PresenceService::PresenceService(QString clientId, QObject* parent)
    : QObject(parent)
    , m_clientId(std::move(clientId).trimmed())
{
    if (m_clientId.isEmpty()) {
        return;
    }
    m_worker = new PresenceWorker(m_clientId);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("Presence"));
    m_thread.start();
}

PresenceService::~PresenceService()
{
    shutdown();
}

void PresenceService::shutdown()
{
    if (m_worker == nullptr) {
        return;
    }
    PresenceWorker* worker = m_worker;
    m_worker = nullptr;
    QMetaObject::invokeMethod(worker, [worker]() { worker->stop(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void PresenceService::setEnabled(bool enabled)
{
    if (PresenceWorker* worker = m_worker) {
        QMetaObject::invokeMethod(worker, [worker, enabled]() { worker->setEnabled(enabled); }, Qt::QueuedConnection);
    }
}

void PresenceService::show(const PresencePayload& payload)
{
    if (PresenceWorker* worker = m_worker) {
        QMetaObject::invokeMethod(worker, [worker, payload]() { worker->setPayload(payload); }, Qt::QueuedConnection);
    }
}

void PresenceService::clear()
{
    show(PresencePayload{});
}

PresenceWorker::PresenceWorker(QString clientId)
    : m_clientId(std::move(clientId))
{
}

PresenceWorker::~PresenceWorker() = default;

void PresenceWorker::stop()
{
    m_stopping = true;
    if (m_socket != nullptr) {
        m_socket->abort();
    }
    delete m_sendTimer;
    delete m_reconnectTimer;
    delete m_handshakeTimer;
    delete m_socket;
    m_sendTimer = nullptr;
    m_reconnectTimer = nullptr;
    m_handshakeTimer = nullptr;
    m_socket = nullptr;
}

void PresenceWorker::setEnabled(bool enabled)
{
    if (m_stopping || enabled == m_enabled) {
        return;
    }
    m_enabled = enabled;
    if (!enabled) {
        if (m_socket != nullptr) {
            m_socket->abort();
        }
        m_ready = false;
        m_sentFingerprint.clear();
        return;
    }
    m_reconnectDelay = ReconnectStart;
    flush();
}

void PresenceWorker::setPayload(const PresencePayload& payload)
{
    if (m_stopping) {
        return;
    }
    m_payload = payload;
    flush();
}

QString PresenceWorker::nextNonce()
{
    return QString::number(nowMs()) + QLatin1Char('-') + QString::number(++m_nonce);
}

void PresenceWorker::write(Opcode opcode, const QByteArray& payload)
{
    if (m_socket != nullptr) {
        m_socket->write(encodeFrame(opcode, payload));
        m_socket->flush();
    }
}

void PresenceWorker::connectPipe()
{
    if (m_stopping || !m_enabled || m_ready) {
        return;
    }
    if (m_socket == nullptr) {
        m_socket = new QLocalSocket(this);
        connect(m_socket, &QLocalSocket::readyRead, this, &PresenceWorker::onReadyRead);
        connect(m_socket, &QLocalSocket::disconnected, this, &PresenceWorker::onDisconnected);
        m_handshakeTimer = new QTimer(this);
        m_handshakeTimer->setSingleShot(true);
        connect(m_handshakeTimer, &QTimer::timeout, this, [this]() {
            if (!m_ready && m_socket != nullptr) {
                m_socket->abort();
                scheduleReconnect();
            }
        });
    }
    if (m_socket->state() != QLocalSocket::UnconnectedState) {
        return;
    }
    // This thread exists for the pipe, so a short blocking connect is fine.
    for (int index = 0; index < 10; ++index) {
        m_socket->connectToServer(QStringLiteral("discord-ipc-") + QString::number(index));
        if (m_socket->waitForConnected(250)) {
            m_decoder.clear();
            write(Opcode::Handshake, handshakeJson(m_clientId));
            m_handshakeTimer->start(HandshakeTimeout);
            return;
        }
        m_socket->abort();
    }
    scheduleReconnect();
}

void PresenceWorker::onReadyRead()
{
    m_decoder.append(m_socket->readAll());
    bool broken = false;
    while (const std::optional<Frame> frame = m_decoder.next(broken)) {
        switch (static_cast<Opcode>(frame->opcode)) {
        case Opcode::Frame:
            if (!m_ready && isReadyFrame(frame->payload)) {
                m_ready = true;
                m_handshakeTimer->stop();
                m_reconnectDelay = ReconnectStart;
                m_sentFingerprint.clear();
                flush();
            } else if (isErrorFrame(frame->payload)) {
                // Rejected: send the latest activity again after the gap.
                m_sentFingerprint.clear();
                flush();
            }
            break;
        case Opcode::Ping:
            write(Opcode::Pong, frame->payload);
            break;
        case Opcode::Close:
            m_socket->abort();
            return;
        default:
            break;
        }
    }
    if (broken) {
        m_socket->abort();
    }
}

void PresenceWorker::onDisconnected()
{
    m_ready = false;
    m_sentFingerprint.clear();
    m_decoder.clear();
    if (m_enabled && !m_stopping) {
        scheduleReconnect();
    }
}

void PresenceWorker::scheduleReconnect()
{
    if (m_stopping || !m_enabled) {
        return;
    }
    if (m_reconnectTimer == nullptr) {
        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout, this, &PresenceWorker::connectPipe);
    }
    if (m_reconnectTimer->isActive()) {
        return;
    }
    const auto jitter = std::chrono::milliseconds(QRandomGenerator::global()->bounded(251));
    m_reconnectTimer->start(m_reconnectDelay + jitter);
    m_reconnectDelay = nextReconnectDelay(m_reconnectDelay);
}

void PresenceWorker::flush()
{
    if (m_stopping || !m_enabled) {
        return;
    }
    if (!m_ready) {
        if (m_payload && !m_payload->title.isEmpty()
            && (m_reconnectTimer == nullptr || !m_reconnectTimer->isActive())) {
            connectPipe();
        }
        return;
    }
    const PresencePayload payload = m_payload.value_or(PresencePayload{});
    const QString fingerprint = activityFingerprint(payload);
    if (fingerprint == m_sentFingerprint) {
        return;
    }
    const std::optional<std::chrono::milliseconds> since =
        m_lastSendMs < 0 ? std::nullopt : std::optional(std::chrono::milliseconds(nowMs() - m_lastSendMs));
    if (!sendAllowed(since)) {
        if (m_sendTimer == nullptr) {
            m_sendTimer = new QTimer(this);
            m_sendTimer->setSingleShot(true);
            connect(m_sendTimer, &QTimer::timeout, this, &PresenceWorker::flush);
        }
        if (!m_sendTimer->isActive()) {
            m_sendTimer->start(MinSendGap - *since);
        }
        return;
    }
    const qint64 pid = QCoreApplication::applicationPid();
    write(Opcode::Frame, payload.title.isEmpty() ? clearJson(pid, nextNonce())
                                                 : activityJson(payload, pid, nowMs(), nextNonce()));
    m_lastSendMs = nowMs();
    m_sentFingerprint = fingerprint;
}

} // namespace lmp::core::presence
