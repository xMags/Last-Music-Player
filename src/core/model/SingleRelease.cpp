#include "core/model/SingleRelease.h"

#include <QRegularExpression>

#include "core/model/Track.h"

namespace lmp::core {

namespace {

// A title as compared with an album name: bracketed parts, featured artists
// and punctuation dropped.
QString comparableName(const QString& value)
{
    static const QRegularExpression brackets(QStringLiteral(R"(\([^)]*\)|\[[^\]]*\])"));
    static const QRegularExpression featuring(QStringLiteral(R"(\s(?:feat|ft)\.?\s.*$)"));
    static const QRegularExpression separators(QStringLiteral(R"([^\p{L}\p{N}]+)"),
                                               QRegularExpression::UseUnicodePropertiesOption);
    QString text = value.toLower();
    text.replace(brackets, QStringLiteral(" "));
    text.replace(featuring, QString());
    text.replace(separators, QStringLiteral(" "));
    return text.trimmed();
}

} // namespace

bool looksLikeSingle(const Track& track)
{
    const QString album = track.album.trimmed();
    if (album.isEmpty()) {
        return track.albumCatalogId.trimmed().isEmpty();
    }
    static const QRegularExpression singleSuffix(QStringLiteral(R"(\s[-\x{2013}]\s*single$)"),
                                                 QRegularExpression::CaseInsensitiveOption);
    if (singleSuffix.match(album).hasMatch()) {
        return true;
    }
    const QString name = comparableName(album);
    return !name.isEmpty() && name == comparableName(track.title);
}

} // namespace lmp::core
