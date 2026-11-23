#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QUrl>
#include <QtGlobal>

#include <chrono>
#include <utility>

namespace lmp::core::net {

enum class HttpMethod { Get, Head, Post, Put, Patch, Delete };

enum class RedirectPolicy {
    // A 3xx response is a failure and its Location is never requested.
    // Account requests use this.
    Refuse,
    // Follows redirects that do not downgrade from https to http.
    Follow,
};

using HttpHeaders = QList<std::pair<QByteArray, QByteArray>>;

// One request. The timeout and the size limit have no defaults on purpose:
// every caller states them, and a transport fails a request that leaves
// either unset.
struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    QUrl url;
    HttpHeaders headers;
    QByteArray body;
    // The whole exchange, from sending to the last byte of the body.
    std::chrono::milliseconds timeout{0};
    RedirectPolicy redirects = RedirectPolicy::Refuse;
    // The largest body accepted; anything larger is abandoned as soon as it
    // is announced or received.
    qint64 maxResponseBytes = 0;
    // Finish as soon as the status and headers arrive: the body is never
    // read (the response's body is empty) and the size limit does not apply.
    // For checking an address whose body may be large, such as audio.
    bool headersOnly = false;

    // The first header with this name, compared without case; empty when
    // there is none.
    [[nodiscard]] QByteArray header(QByteArrayView name) const;
};

// A response with a 2xx status. Every other status is a Failure.
struct HttpResponse {
    int status = 0;
    HttpHeaders headers;
    QByteArray body;

    // The first header with this name, compared without case; empty when
    // there is none.
    [[nodiscard]] QByteArray header(QByteArrayView name) const;
};

// The first header with this name in `headers`, compared without case.
[[nodiscard]] QByteArray findHeader(const HttpHeaders& headers, QByteArrayView name);

} // namespace lmp::core::net
