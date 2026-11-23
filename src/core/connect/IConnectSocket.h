#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

namespace lmp::core::connect {

// One socket of the device link, behind an interface so tests can play the
// server. A socket opens once; after `closed` it is spent and the client
// makes a new one. Signals arrive on the thread that owns the socket, never
// from inside open() or sendText(); close() may report `closed` at once, so
// a caller that is done with a socket disconnects from it first.
class IConnectSocket : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void open(const QUrl& url) = 0;
    // False when the socket is not open; nothing is sent then.
    virtual bool sendText(const QByteArray& text) = 0;
    // Closes with `code`; `closed` follows with that code. A socket that
    // never opened reports `closed` too.
    virtual void close(int code, const QString& reason) = 0;

signals:
    void opened();
    void textReceived(const QByteArray& text);
    // The server's close code, or 1006 for a lost connection.
    void closed(int code);
};

} // namespace lmp::core::connect
