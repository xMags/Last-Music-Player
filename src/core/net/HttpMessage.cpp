#include "core/net/HttpMessage.h"

namespace lmp::core::net {

QByteArray findHeader(const HttpHeaders& headers, QByteArrayView name)
{
    for (const auto& [key, value] : headers) {
        if (key.compare(name, Qt::CaseInsensitive) == 0) {
            return value;
        }
    }
    return {};
}

QByteArray HttpRequest::header(QByteArrayView name) const
{
    return findHeader(headers, name);
}

QByteArray HttpResponse::header(QByteArrayView name) const
{
    return findHeader(headers, name);
}

} // namespace lmp::core::net
