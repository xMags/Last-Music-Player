#pragma once

#include "core/async/Result.h"
#include "core/net/HttpMessage.h"

#include <QFuture>

namespace lmp::core::net {

// Something that sends one HTTP request and reports how it ended. HttpClient
// is the real one; tests answer requests themselves.
//
// The contract every implementation keeps: the returned future finishes
// exactly once, on the thread that called send(); a 2xx status is a value and
// everything else is a Failure with the kinds HttpClient documents.
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    [[nodiscard]] virtual QFuture<Result<HttpResponse>> send(const HttpRequest& request) = 0;

protected:
    IHttpTransport() = default;
    IHttpTransport(const IHttpTransport&) = default;
    IHttpTransport& operator=(const IHttpTransport&) = default;
};

} // namespace lmp::core::net
