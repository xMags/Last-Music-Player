#include "core/cast/CastTransport.h"

#include <QSslConfiguration>
#include <QSslSocket>

namespace lmp::core::cast {

SslCastTransport::SslCastTransport(QObject* parent)
    : ICastTransport(parent)
    , m_socket(new QSslSocket(this))
{
    QSslConfiguration configuration = m_socket->sslConfiguration();
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    m_socket->setSslConfiguration(configuration);
    connect(m_socket, &QSslSocket::encrypted, this, [this]() {
        m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        emit opened();
    });
    connect(m_socket, &QSslSocket::readyRead, this, [this]() { emit received(m_socket->readAll()); });
    connect(m_socket, &QSslSocket::disconnected, this, [this]() {
        if (!m_closing) {
            emit closed(tr("The device closed the connection."));
        }
    });
    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_closing) {
            return;
        }
        m_closing = true;
        const QString reason = m_socket->errorString();
        m_socket->abort();
        emit closed(reason);
    });
}

SslCastTransport::~SslCastTransport()
{
    m_closing = true;
    m_socket->abort();
}

void SslCastTransport::open(const QString& host, quint16 port)
{
    m_closing = false;
    m_socket->connectToHostEncrypted(host, port);
}

void SslCastTransport::write(const QByteArray& bytes)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(bytes);
    }
}

void SslCastTransport::close()
{
    m_closing = true;
    m_socket->flush();
    m_socket->disconnectFromHost();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
}

} // namespace lmp::core::cast
