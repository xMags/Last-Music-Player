#pragma once

#include <QByteArray>
#include <QFuture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPromise>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>
#include <utility>

#include "FakePlaybackClock.h"
#include "core/async/Result.h"
#include "core/connect/ConnectClient.h"
#include "core/connect/IConnectSocket.h"

// The server's side of the device link for tests: every socket the client
// makes writes to one shared log, and the test plays the server on the
// latest one.
struct FakeSocketLog {
    QList<QUrl> opened;
    QList<QByteArray> sent;
    QList<int> closeCodes;
    class FakeConnectSocket* current = nullptr;
    int created = 0;
};

class FakeConnectSocket final : public lmp::core::connect::IConnectSocket {
public:
    explicit FakeConnectSocket(std::shared_ptr<FakeSocketLog> log)
        : m_log(std::move(log))
    {
        ++m_log->created;
        m_log->current = this;
    }
    ~FakeConnectSocket() override
    {
        if (m_log->current == this) {
            m_log->current = nullptr;
        }
    }

    void open(const QUrl& url) override { m_log->opened.append(url); }
    bool sendText(const QByteArray& text) override
    {
        if (!m_open) {
            return false;
        }
        m_log->sent.append(text);
        return true;
    }
    void close(int code, const QString&) override
    {
        m_log->closeCodes.append(code);
        m_open = false;
    }

    // The server's moves.
    void serverOpen()
    {
        m_open = true;
        emit opened();
    }
    void serverSend(const QJsonObject& message)
    {
        emit textReceived(QJsonDocument(message).toJson(QJsonDocument::Compact));
    }
    void serverClose(int code)
    {
        m_open = false;
        emit closed(code);
    }

private:
    std::shared_ptr<FakeSocketLog> m_log;
    bool m_open = false;
};

// A client on a fake socket, the fake clock and a ticket the test controls.
class ConnectHarness {
public:
    explicit ConnectHarness(FakePlaybackClock& clock, QString deviceId = QStringLiteral("this-pc-0001"))
    {
        client = std::make_unique<lmp::core::connect::ConnectClient>(lmp::core::connect::ConnectClient::Dependencies{
            .requestTicket =
                [this]() {
                    ++ticketCalls;
                    if (holdTicket) {
                        heldTicket = std::make_unique<QPromise<lmp::core::Result<QByteArray>>>();
                        heldTicket->start();
                        return heldTicket->future();
                    }
                    if (ticketFails) {
                        return QtFuture::makeReadyValueFuture(lmp::core::Result<QByteArray>(
                            lmp::core::Failure::of(lmp::core::FailureKind::Network, QStringLiteral("offline"))));
                    }
                    return QtFuture::makeReadyValueFuture(lmp::core::Result<QByteArray>(
                        QByteArrayLiteral(R"({"ticket":"tick-1","path":"/v1/music/connect","expiresInSec":60})")));
                },
            .apiOrigin = []() { return QStringLiteral("https://api.example.test"); },
            .makeSocket = [this]() -> std::unique_ptr<lmp::core::connect::IConnectSocket> {
                return std::make_unique<FakeConnectSocket>(log);
            },
            .clock = clock,
            .random = []() { return 0.5; }, // no jitter: 1 s, 2 s, 4 s ...
            .deviceId = std::move(deviceId),
            .deviceName = QStringLiteral("Windows PC")});
    }

    [[nodiscard]] FakeConnectSocket& socket() { return *log->current; }
    [[nodiscard]] QJsonObject lastSent() const
    {
        return log->sent.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(log->sent.constLast()).object();
    }
    [[nodiscard]] QList<QJsonObject> sentOfType(const QString& type) const
    {
        QList<QJsonObject> messages;
        for (const QByteArray& text : log->sent) {
            const QJsonObject object = QJsonDocument::fromJson(text).object();
            if (object.value(QStringLiteral("type")).toString() == type) {
                messages.append(object);
            }
        }
        return messages;
    }

    std::shared_ptr<FakeSocketLog> log = std::make_shared<FakeSocketLog>();
    std::unique_ptr<lmp::core::connect::ConnectClient> client;
    // Answers the held ticket request.
    void releaseTicket()
    {
        heldTicket->addResult(lmp::core::Result<QByteArray>(
            QByteArrayLiteral(R"({"ticket":"tick-1","path":"/v1/music/connect","expiresInSec":60})")));
        heldTicket->finish();
    }

    bool ticketFails = false;
    // The ticket request waits for releaseTicket().
    bool holdTicket = false;
    std::unique_ptr<QPromise<lmp::core::Result<QByteArray>>> heldTicket;
    int ticketCalls = 0;
};
