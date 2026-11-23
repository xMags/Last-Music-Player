#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QSslSocket;

namespace lmp::core::cast {

// The byte stream to one cast device. GUI thread.
class ICastTransport : public QObject {
    Q_OBJECT

public:
    ~ICastTransport() override = default;
    ICastTransport(const ICastTransport&) = delete;
    ICastTransport& operator=(const ICastTransport&) = delete;

    // Connects; `opened` or `closed` follows (never from inside the call).
    virtual void open(const QString& host, quint16 port) = 0;
    virtual void write(const QByteArray& bytes) = 0;
    // Closes without `closed`.
    virtual void close() = 0;

signals:
    void opened();
    void received(const QByteArray& bytes);
    // The connection failed or ended from the other side.
    void closed(const QString& reason);

protected:
    explicit ICastTransport(QObject* parent = nullptr) : QObject(parent) {}
};

// TLS to the device's port 8009. Cast devices present certificates that no
// public authority signs (made per device), so the certificate is not
// checked, as the earlier versions did; the channel only carries playback
// commands and an address the account service already signed for this
// song. Keep-alive is on so a device that went to sleep is noticed.
class SslCastTransport final : public ICastTransport {
    Q_OBJECT

public:
    explicit SslCastTransport(QObject* parent = nullptr);
    ~SslCastTransport() override;

    void open(const QString& host, quint16 port) override;
    void write(const QByteArray& bytes) override;
    void close() override;

private:
    QSslSocket* m_socket = nullptr;
    bool m_closing = false;
};

} // namespace lmp::core::cast
