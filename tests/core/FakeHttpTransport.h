#pragma once

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QPromise>
#include <QString>

#include <deque>
#include <utility>

#include "core/async/Result.h"
#include "core/net/HttpMessage.h"
#include "core/net/IHttpTransport.h"

namespace lmp::test {

// Answers requests from a script instead of the network. A request whose
// path has a scripted answer gets it at once; any other request waits until
// the test answers it with answer(), so a test decides exactly when each
// step of a flow finishes. Every request is recorded.
class FakeHttpTransport final : public core::net::IHttpTransport {
public:
    using Outcome = core::Result<core::net::HttpResponse>;

    QFuture<Outcome> send(const core::net::HttpRequest& request) override
    {
        requests.append(request);
        QPromise<Outcome> promise;
        promise.start();
        QFuture<Outcome> future = promise.future();
        const QString path = request.url.path();
        auto scripted = m_scripts.find(path);
        if (scripted != m_scripts.end() && !scripted->empty()) {
            Outcome outcome = std::move(scripted->front());
            scripted->pop_front();
            finish(promise, std::move(outcome));
            return future;
        }
        m_waiting.push_back({path, std::move(promise)});
        return future;
    }

    // The next request to `path` gets this answer as soon as it is sent.
    void script(const QString& path, Outcome outcome) { m_scripts[path].push_back(std::move(outcome)); }

    // Answers the oldest waiting request to `path`. False when none waits.
    bool answer(const QString& path, Outcome outcome)
    {
        for (auto waiting = m_waiting.begin(); waiting != m_waiting.end(); ++waiting) {
            if (waiting->first == path) {
                QPromise<Outcome> promise = std::move(waiting->second);
                m_waiting.erase(waiting);
                finish(promise, std::move(outcome));
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] int waitingCount(const QString& path) const
    {
        int count = 0;
        for (const auto& waiting : m_waiting) {
            count += waiting.first == path ? 1 : 0;
        }
        return count;
    }

    [[nodiscard]] int requestCount(const QString& path) const
    {
        int count = 0;
        for (const core::net::HttpRequest& request : requests) {
            count += request.url.path() == path ? 1 : 0;
        }
        return count;
    }

    [[nodiscard]] static Outcome ok(const QByteArray& body, int status = 200)
    {
        return core::net::HttpResponse{status, {}, body};
    }

    [[nodiscard]] static Outcome failed(core::FailureKind kind, int status = 0)
    {
        return core::Failure::of(kind, QStringLiteral("scripted failure"), status);
    }

    QList<core::net::HttpRequest> requests;

private:
    static void finish(QPromise<Outcome>& promise, Outcome outcome)
    {
        [[maybe_unused]] const bool stored = promise.addResult(std::move(outcome));
        promise.finish();
    }

    QHash<QString, std::deque<Outcome>> m_scripts;
    std::deque<std::pair<QString, QPromise<Outcome>>> m_waiting;
};

} // namespace lmp::test
