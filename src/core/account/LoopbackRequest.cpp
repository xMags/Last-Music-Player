#include "core/account/LoopbackRequest.h"

#include <QByteArray>

#include <map>
#include <optional>
#include <utility>

namespace lmp::core::account {

namespace {

QByteArray lowerAscii(QByteArrayView value)
{
    QByteArray lowered = value.toByteArray();
    for (char& character : lowered) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return lowered;
}

QByteArrayView trimSpacesAndTabs(QByteArrayView value)
{
    const auto isBlank = [](char character) { return character == ' ' || character == '\t'; };
    qsizetype first = 0;
    qsizetype last = value.size();
    while (first < last && isBlank(value.at(first))) {
        ++first;
    }
    while (last > first && isBlank(value.at(last - 1))) {
        --last;
    }
    return value.sliced(first, last - first);
}

int hexValue(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

// Form decoding: "%XX" with two hex digits, "+" as a space. A stray "%" fails.
std::optional<QByteArray> percentDecode(QByteArrayView value)
{
    QByteArray decoded;
    decoded.reserve(value.size());
    for (qsizetype index = 0; index < value.size(); ++index) {
        const char character = value.at(index);
        if (character != '%') {
            decoded.append(character == '+' ? ' ' : character);
            continue;
        }
        if (index + 2 >= value.size()) {
            return std::nullopt;
        }
        const int high = hexValue(value.at(index + 1));
        const int low = hexValue(value.at(index + 2));
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        decoded.append(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return decoded;
}

// The value of the only Host header, A to Z folded; nothing when there is none,
// it repeats, or a header line is malformed.
std::optional<QByteArray> hostHeader(QByteArrayView request, qsizetype lineStart)
{
    std::optional<QByteArray> host;
    while (lineStart < request.size()) {
        const qsizetype lineEnd = request.indexOf("\r\n", lineStart);
        if (lineEnd < 0) {
            return std::nullopt;
        }
        if (lineEnd == lineStart) {
            break;
        }
        const QByteArrayView line = request.sliced(lineStart, lineEnd - lineStart);
        const qsizetype colon = line.indexOf(':');
        if (colon < 0) {
            return std::nullopt;
        }
        if (lowerAscii(line.first(colon)) == "host") {
            if (host) {
                return std::nullopt;
            }
            host = lowerAscii(trimSpacesAndTabs(line.sliced(colon + 1)));
        }
        lineStart = lineEnd + 2;
    }
    return host;
}

// Decoded query fields by name, or nothing when a field is empty, does not
// decode or repeats a name.
std::optional<std::map<QByteArray, QByteArray>> queryFields(QByteArrayView query)
{
    std::map<QByteArray, QByteArray> values;
    qsizetype partStart = 0;
    while (partStart <= query.size()) {
        const qsizetype partEnd = query.indexOf('&', partStart);
        const QByteArrayView part = query.sliced(partStart, (partEnd < 0 ? query.size() : partEnd) - partStart);
        if (part.isEmpty()) {
            return std::nullopt;
        }
        const qsizetype equals = part.indexOf('=');
        std::optional<QByteArray> name = percentDecode(equals < 0 ? part : part.first(equals));
        std::optional<QByteArray> value = percentDecode(equals < 0 ? QByteArrayView() : part.sliced(equals + 1));
        if (!name || !value || values.contains(*name)) {
            return std::nullopt;
        }
        values.emplace(std::move(*name), std::move(*value));
        if (partEnd < 0) {
            break;
        }
        partStart = partEnd + 1;
    }
    return values;
}

bool isAscii(QByteArrayView value)
{
    for (const char character : value) {
        if (static_cast<unsigned char>(character) > 0x7f) {
            return false;
        }
    }
    return true;
}

} // namespace

LoopbackRequestResult parseLoopbackRequest(QByteArrayView request, QByteArrayView expectedAuthority,
                                           QByteArrayView expectedPath, QByteArrayView expectedState)
{
    const LoopbackRequestResult invalid;
    if (request.isEmpty() || request.size() > kMaxLoopbackRequestBytes) {
        return invalid;
    }

    const qsizetype requestLineEnd = request.indexOf("\r\n");
    if (requestLineEnd < 0) {
        return invalid;
    }
    const QByteArrayView requestLine = request.first(requestLineEnd);
    const qsizetype firstSpace = requestLine.indexOf(' ');
    const qsizetype secondSpace = firstSpace < 0 ? -1 : requestLine.indexOf(' ', firstSpace + 1);
    if (firstSpace < 0 || secondSpace < 0 || requestLine.first(firstSpace) != "GET") {
        return invalid;
    }
    const QByteArrayView target = requestLine.sliced(firstSpace + 1, secondSpace - firstSpace - 1);
    const QByteArrayView version = requestLine.sliced(secondSpace + 1);
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        return invalid;
    }

    const std::optional<QByteArray> host = hostHeader(request, requestLineEnd + 2);
    if (!host || *host != lowerAscii(expectedAuthority)) {
        return invalid;
    }

    if (target.contains('#')) {
        return invalid;
    }
    const qsizetype queryStart = target.indexOf('?');
    if ((queryStart < 0 ? target : target.first(queryStart)) != expectedPath) {
        return invalid;
    }

    std::map<QByteArray, QByteArray> values;
    if (queryStart >= 0) {
        std::optional<std::map<QByteArray, QByteArray>> fields = queryFields(target.sliced(queryStart + 1));
        if (!fields) {
            return invalid;
        }
        values = std::move(*fields);
    }

    const auto state = values.find(QByteArrayLiteral("state"));
    if (state == values.end() || state->second != expectedState) {
        return invalid;
    }
    if (values.contains(QByteArrayLiteral("error"))) {
        return LoopbackRequestResult{.outcome = LoopbackRequestOutcome::Cancelled, .code = {}};
    }
    const auto code = values.find(QByteArrayLiteral("code"));
    if (code == values.end() || code->second.isEmpty() || code->second.size() > kMaxLoopbackCodeBytes
        || !isAscii(code->second)) {
        return invalid;
    }
    return LoopbackRequestResult{.outcome = LoopbackRequestOutcome::Code, .code = QString::fromLatin1(code->second)};
}

} // namespace lmp::core::account
