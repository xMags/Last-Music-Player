#include "core/connect/WebSocketConnectSocket.h"

#include "core/connect/ConnectProtocol.h"

#include <QMetaObject>
#include <QNetworkRequest>
#include <QWebSocketProtocol>

namespace lmp::core::connect {

WebSocketConnectSocket::WebSocketConnectSocket(QObject* parent)
    : IConnectSocket(parent)
{
    m_socket.setMaxAllowedIncomingMessageSize(static_cast<quint64>(MaxMessageBytes));
    m_socket.setMaxAllowedIncomingFrameSize(static_cast<quint64>(MaxMessageBytes));
    connect(&m_socket, &QWebSocket::connected, this, &IConnectSocket::opened);
    connect(&m_socket, &QWebSocket::textMessageReceived, this,
            [this](const QString& text) { emit textReceived(text.toUtf8()); });
    connect(&m_socket, &QWebSocket::disconnected, this, [this]() {
        const int code = static_cast<int>(m_socket.closeCode());
        reportClosed(code == 0 ? static_cast<int>(QWebSocketProtocol::CloseCodeAbnormalDisconnection) : code);
    });
}

WebSocketConnectSocket::~WebSocketConnectSocket()
{
    // Nothing is reported from the destructor.
    m_reported = true;
    m_socket.disconnect(this);
    m_socket.abort();
}

void WebSocketConnectSocket::open(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    m_socket.open(request);
}

bool WebSocketConnectSocket::sendText(const QByteArray& text)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState || text.size() > MaxMessageBytes) {
        return false;
    }
    return m_socket.sendTextMessage(QString::fromUtf8(text)) > 0;
}

void WebSocketConnectSocket::close(int code, const QString& reason)
{
    if (m_socket.state() == QAbstractSocket::UnconnectedState) {
        // Never opened (or already gone): report it later, as the interface says.
        QMetaObject::invokeMethod(this, [this, code]() { reportClosed(code); }, Qt::QueuedConnection);
        return;
    }
    m_socket.close(static_cast<QWebSocketProtocol::CloseCode>(code), reason);
    // A dead link may never finish the closing handshake; the client does
    // not wait for it.
    m_socket.abort();
}

void WebSocketConnectSocket::reportClosed(int code)
{
    if (m_reported) {
        return;
    }
    m_reported = true;
    emit closed(code);
}

} // namespace lmp::core::connect
