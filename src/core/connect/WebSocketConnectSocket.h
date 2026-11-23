#pragma once

#include "core/connect/IConnectSocket.h"

#include <QWebSocket>

namespace lmp::core::connect {

// The device link over QWebSocket. Answers the server's pings itself (the
// QWebSocket default). Refuses frames over MaxMessageBytes.
class WebSocketConnectSocket final : public IConnectSocket {
    Q_OBJECT

public:
    explicit WebSocketConnectSocket(QObject* parent = nullptr);
    ~WebSocketConnectSocket() override;

    void open(const QUrl& url) override;
    bool sendText(const QByteArray& text) override;
    void close(int code, const QString& reason) override;

private:
    void reportClosed(int code);

    QWebSocket m_socket;
    bool m_reported = false;
};

} // namespace lmp::core::connect
