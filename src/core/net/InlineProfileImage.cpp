#include "core/net/InlineProfileImage.h"

#include "core/text/CaseFold.h"

#include <QChar>
#include <QLatin1StringView>
#include <QStringView>

#include <array>

namespace lmp::core::net {

namespace {

// Large enough for a real profile photo at a sensible resolution, small
// enough that a hostile response cannot make the app hold an unbounded
// string or store one per account.
constexpr qsizetype kMaxInlineImageCharacters = 1'500'000;

bool isBase64Character(QChar character)
{
    return (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
        || (character >= QLatin1Char('a') && character <= QLatin1Char('z'))
        || (character >= QLatin1Char('0') && character <= QLatin1Char('9')) || character == QLatin1Char('+')
        || character == QLatin1Char('/');
}

} // namespace

bool isSafeInlineProfileImage(const QString& value)
{
    constexpr QLatin1StringView kScheme{"data:"};
    constexpr QLatin1StringView kEncoding{";base64,"};
    static constexpr std::array<QLatin1StringView, 5> kAllowedMediaTypes{
        QLatin1StringView("image/jpeg"), QLatin1StringView("image/png"), QLatin1StringView("image/gif"),
        QLatin1StringView("image/webp"), QLatin1StringView("image/bmp"),
    };

    if (value.isEmpty() || value.size() > kMaxInlineImageCharacters) {
        return false;
    }
    const qsizetype separator = value.indexOf(QLatin1Char(','));
    if (separator < 0) {
        return false;
    }

    // Only the header is case-folded. The payload past the comma is base64,
    // where case carries data.
    const QString header = value.left(separator + 1).toLower();
    if (!header.startsWith(kScheme) || !header.endsWith(kEncoding)) {
        return false;
    }
    const QStringView mediaType =
        QStringView(header).sliced(kScheme.size(), header.size() - kScheme.size() - kEncoding.size());
    bool allowed = false;
    for (const QLatin1StringView candidate : kAllowedMediaTypes) {
        if (mediaType == candidate) {
            allowed = true;
            break;
        }
    }
    if (!allowed) {
        return false;
    }

    const QStringView payload = QStringView(value).sliced(separator + 1);
    if (payload.isEmpty() || payload.size() % 4 != 0) {
        return false;
    }

    // Validated here rather than left to the decoder, so a malformed payload
    // is rejected at the boundary instead of failing later in the UI or being
    // stored and replayed on every launch.
    qsizetype dataLength = payload.size();
    while (dataLength > 0 && payload.at(dataLength - 1) == QLatin1Char('=')) {
        --dataLength;
    }
    if (dataLength == 0 || payload.size() - dataLength > 2) {
        return false;
    }
    for (qsizetype index = 0; index < dataLength; ++index) {
        if (!isBase64Character(payload.at(index))) {
            return false;
        }
    }
    return true;
}

bool isInlineProfileImageData(const QString& value)
{
    constexpr QLatin1StringView kScheme{"data:"};
    return value.size() >= kScheme.size() && foldCaseAscii(QStringView(value).first(kScheme.size())) == kScheme;
}

} // namespace lmp::core::net
