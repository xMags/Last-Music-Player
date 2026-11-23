#include "core/net/ArtworkUrl.h"

#include "core/net/RemoteUrlPolicy.h"

#include <QByteArray>
#include <QChar>
#include <QLatin1StringView>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QStringView>
#include <QUrl>

namespace lmp::core::net {

namespace {

// Lower-cases one UTF-16 unit at a time, so every index into the result is
// also an index into the original. The rewrites below find a position in
// the lowered text and edit the original there, which a full Unicode
// lower-casing (it can change the length) would break.
QString lowerEachUnit(const QString& text)
{
    QString lowered = text;
    for (QChar& character : lowered) {
        character = character.toLower();
    }
    return lowered;
}

bool isAsciiDigit(QChar character)
{
    return character >= QLatin1Char('0') && character <= QLatin1Char('9');
}

// Replaces the first "<marker><digits>-h<digits>..." run, up to the next
// '?', '&' or '#', with `replacement`.
bool tryReplaceWidthHeightSize(QString& text, QLatin1StringView marker, QLatin1StringView replacement)
{
    const QString lowered = lowerEachUnit(text);
    qsizetype position = lowered.indexOf(marker);
    while (position >= 0) {
        qsizetype cursor = position + marker.size();
        const qsizetype widthStart = cursor;
        while (cursor < lowered.size() && isAsciiDigit(lowered.at(cursor))) {
            ++cursor;
        }
        if (cursor == widthStart || cursor + 2 > lowered.size()
            || QStringView(lowered).sliced(cursor, 2) != QLatin1StringView("-h")) {
            position = lowered.indexOf(marker, position + marker.size());
            continue;
        }

        cursor += 2;
        const qsizetype heightStart = cursor;
        while (cursor < lowered.size() && isAsciiDigit(lowered.at(cursor))) {
            ++cursor;
        }
        if (cursor == heightStart) {
            position = lowered.indexOf(marker, position + marker.size());
            continue;
        }

        qsizetype end = cursor;
        while (end < text.size() && text.at(end) != QLatin1Char('?') && text.at(end) != QLatin1Char('&')
               && text.at(end) != QLatin1Char('#')) {
            ++end;
        }
        text.replace(position, end - position, replacement);
        return true;
    }
    return false;
}

// Rewrites the last "<w>x<h>bb." or "<w>x<h>cc." segment of an address on
// the .mzstatic.com image host to 1200x1200, when that segment starts a path
// part ('/' or an encoded "%2f" before it).
bool tryReplaceCdnSquareSize(QString& text)
{
    const QString lowered = lowerEachUnit(text);
    if (!lowered.contains(QLatin1StringView(".mzstatic.com"))) {
        return false;
    }

    qsizetype suffix = lowered.lastIndexOf(QLatin1StringView("bb."));
    const qsizetype alternateSuffix = lowered.lastIndexOf(QLatin1StringView("cc."));
    if (alternateSuffix >= 0 && (suffix < 0 || alternateSuffix > suffix)) {
        suffix = alternateSuffix;
    }
    if (suffix < 0) {
        return false;
    }

    qsizetype heightStart = suffix;
    while (heightStart > 0 && isAsciiDigit(lowered.at(heightStart - 1))) {
        --heightStart;
    }
    if (heightStart == suffix || heightStart == 0 || lowered.at(heightStart - 1) != QLatin1Char('x')) {
        return false;
    }

    qsizetype widthStart = heightStart - 1;
    while (widthStart > 0 && isAsciiDigit(lowered.at(widthStart - 1))) {
        --widthStart;
    }
    if (widthStart == heightStart - 1) {
        return false;
    }

    const bool directSegment = widthStart > 0 && lowered.at(widthStart - 1) == QLatin1Char('/');
    const bool encodedSegment =
        widthStart >= 3 && QStringView(lowered).sliced(widthStart - 3, 3) == QLatin1StringView("%2f");
    if (!directSegment && !encodedSegment) {
        return false;
    }

    text.replace(widthStart, suffix - widthStart, QLatin1StringView("1200x1200"));
    return true;
}

// The raw (still percent-encoded) value of the first query field named
// `expectedName`, compared without regard to case; empty when absent.
QString queryParameter(const QString& url, QLatin1StringView expectedName)
{
    const qsizetype queryStart = url.indexOf(QLatin1Char('?'));
    if (queryStart < 0) {
        return {};
    }

    const QString expected = lowerEachUnit(QString(expectedName));
    const qsizetype fragmentStart = url.indexOf(QLatin1Char('#'), queryStart + 1);
    const qsizetype queryEnd = fragmentStart < 0 ? url.size() : fragmentStart;
    const QString query = url.mid(queryStart + 1, queryEnd - queryStart - 1);
    qsizetype start = 0;
    while (start <= query.size()) {
        const qsizetype ampersand = query.indexOf(QLatin1Char('&'), start);
        const QString part = ampersand < 0 ? query.mid(start) : query.mid(start, ampersand - start);
        const qsizetype equals = part.indexOf(QLatin1Char('='));
        const QString name = lowerEachUnit(equals < 0 ? part : part.left(equals));
        if (name == expected) {
            return equals < 0 ? QString() : part.mid(equals + 1);
        }
        if (ampersand < 0) {
            break;
        }
        start = ampersand + 1;
    }
    return {};
}

} // namespace

QString normalizeArtworkUrlForDisplay(const QString& artworkUrl)
{
    QString text = artworkUrl.trimmed();
    if (text.isEmpty()) {
        return {};
    }

    if (!tryReplaceWidthHeightSize(text, QLatin1StringView("=w"), QLatin1StringView("=w1200-h1200-l90-rj"))) {
        static_cast<void>(
            tryReplaceWidthHeightSize(text, QLatin1StringView("%3dw"), QLatin1StringView("%3Dw1200-h1200-l90-rj")));
    }
    static_cast<void>(tryReplaceCdnSquareSize(text));
    return text;
}

QString durableArtworkUrl(const QString& artworkUrl)
{
    const QString normalized = normalizeArtworkUrlForDisplay(artworkUrl);
    if (normalized.isEmpty() || !isSafeRemoteUrl(normalized, RemoteUrlUse::EphemeralMedia)) {
        return {};
    }

    const QUrl uri(normalized);
    if (!uri.isValid()) {
        return {};
    }
    const QString path = lowerEachUnit(uri.path(QUrl::FullyEncoded));
    constexpr QLatin1StringView kArtworkRelayPath{"/v1/artwork"};
    const bool isArtworkRelay =
        path == kArtworkRelayPath || (path.size() > kArtworkRelayPath.size() && path.endsWith(kArtworkRelayPath));
    if (!isArtworkRelay) {
        return isSafeRemoteUrl(normalized, RemoteUrlUse::Durable) ? normalized : QString();
    }

    const QString encodedTarget = queryParameter(normalized, QLatin1StringView("url"));
    if (encodedTarget.isEmpty()) {
        return {};
    }
    const QString target = normalizeArtworkUrlForDisplay(QUrl::fromPercentEncoding(encodedTarget.toUtf8()));
    return isSafeRemoteUrl(target, RemoteUrlUse::Durable) ? target : QString();
}

QString accountCoverLinkPath(const QString& url)
{
    static const QRegularExpression coverLink(
        QStringLiteral("^(?:https?://[^/?#]+)?(/v1/music/art/[A-Za-z0-9_-]{22})$"));
    const QRegularExpressionMatch match = coverLink.match(url.trimmed());
    return match.hasMatch() ? match.captured(1) : QString();
}

QString durableAccountArtworkUrl(const QString& artworkUrl)
{
    const QString coverLink = accountCoverLinkPath(artworkUrl);
    return coverLink.isEmpty() ? durableArtworkUrl(artworkUrl) : coverLink;
}

} // namespace lmp::core::net
