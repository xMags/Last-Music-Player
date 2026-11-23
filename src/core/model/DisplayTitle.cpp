#include "core/model/DisplayTitle.h"

#include <QRegularExpression>
#include <QStringList>

#include <functional>

namespace lmp::core {

namespace {

constexpr QRegularExpression::PatternOptions kOptions =
    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;

// Bracket contents that only describe the upload, never the recording.
const QRegularExpression& uploadNoise()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\bofficial\b|\bvideo\b|\blyric(?:s|al)?\b|\bvisuali[sz]er\b|\bfull\s+(?:song|audio)\b|)"
                       R"(^(?:audio|audio\s+song|m\s*\/\s*v|mv|hd|hq|4k|uhd|8k|1080p|720p)$)"),
        kOptions);
    return pattern;
}

// A film credit: "From "Film"", "Movie: Film", "Film: Film".
const QRegularExpression& filmCredit()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^\s*(?:from\b|(?:movie|film)\s*:))"), kOptions);
    return pattern;
}

// Credits shown elsewhere: featured artists, producers, soundtrack labels.
const QRegularExpression& creditGroup()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^\s*(?:feat\b|feat\.|ft\b|ft\.|featuring\b|with\s|prod\b|prod\.|produced\s+by\b)|)"
                       R"(\b(?:original\s+(?:motion\s+picture\s+)?(?:sound\s*track|score)|)"
                       R"((?:motion\s+picture\s+)?sound\s*track|ost)\b)"),
        kOptions);
    return pattern;
}

// Groups that carry real information even if they also mention the upload.
const QRegularExpression& keepGroup()
{
    static const QRegularExpression pattern(
        QStringLiteral(
            R"(\b(?:remix|live|cover|acoustic|unplugged|reprise|version|slowed|reverb|8d|instrumental|karaoke)\b)"),
        kOptions);
    return pattern;
}

// An unbracketed " feat. X" / " ft. X" / " prod. by X", up to a dash or a bracket.
const QRegularExpression& looseCredit()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\s+(?:feat\.|ft\.|featuring|prod\.?\s+by|produced\s+by)\s+[^()[\]【】]*?)"
                       R"((?=\s+[-\x{2013}\x{2014}]\s|\s*[([【]|$))"),
        kOptions);
    return pattern;
}

// A trailing " - Official Video" style suffix, or a film credit after a dash.
// Everything after the dash has to be upload wording, so "Artist - Video
// Games" keeps its title.
const QRegularExpression& dashSuffix()
{
    static const QString uploadWord = QStringLiteral(
        R"((?:official|oficial|music|video|lyric(?:s|al)?|full|song|audio|visuali[sz]er|mv|hd|hq|4k|uhd|8k|1080p|720p|(?:19|20)\d{2}))");
    static const QRegularExpression pattern(
        QStringLiteral(R"(\s+[-\x{2013}\x{2014}]\s+(?:from\s+["“”'].*|(?:official|lyric(?:s|al)?|full\s+(?:video|song|audio)|)"
                       R"(video|music\s+video)(?:\s+%1)*)\s*$)")
            .arg(uploadWord),
        kOptions);
    return pattern;
}

// A "#tag". A number ("Love Potion #9", "#9 Dream") is part of the title.
const QRegularExpression& hashtag()
{
    static const QRegularExpression pattern(QStringLiteral(R"((?:^|\s)#(?=[\p{N}_]*\p{L})[\p{L}\p{N}_]+)"), kOptions);
    return pattern;
}

const QRegularExpression& bracketGroup()
{
    static const QRegularExpression pattern(QStringLiteral(R"(\s*([([【])([^()[\]【】]*)([)\]】]))"), kOptions);
    return pattern;
}

const QRegularExpression& whiteSpaceRun()
{
    static const QRegularExpression pattern(QStringLiteral(R"(\s+)"), kOptions);
    return pattern;
}

const QRegularExpression& trailingPunctuation()
{
    static const QRegularExpression pattern(QStringLiteral(R"([\s\-\x{2013}\x{2014}:,|]+$)"), kOptions);
    return pattern;
}

QString collapsed(const QString& value)
{
    QString text = value;
    text.replace(whiteSpaceRun(), QStringLiteral(" "));
    return text.trimmed();
}

// Replaces every bracket group with what `keep` decides: the whole group, or
// nothing.
QString filterGroups(const QString& text, const std::function<bool(const QString& content)>& keep)
{
    QString result;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = bracketGroup().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += text.mid(last, match.capturedStart() - last);
        const QString content = match.captured(2).trimmed();
        if (!content.isEmpty() && keep(content)) {
            result += match.captured(0);
        }
        last = match.capturedEnd();
    }
    result += text.mid(last);
    return result;
}

} // namespace

QString displayTitle(const QString& title)
{
    const QString original = collapsed(title);
    if (original.isEmpty()) {
        return {};
    }

    // The first non-empty segment between "|" separators.
    QString text = original;
    static const QRegularExpression separator(QStringLiteral(R"(\s*[|｜]\s*)"), kOptions);
    for (const QString& segment : original.split(separator)) {
        if (!segment.trimmed().isEmpty()) {
            text = segment.trimmed();
            break;
        }
    }

    text = filterGroups(text, [](const QString& content) {
        if (filmCredit().match(content).hasMatch()) {
            return false;
        }
        if (keepGroup().match(content).hasMatch()) {
            return true;
        }
        if (creditGroup().match(content).hasMatch()) {
            return false;
        }
        return !uploadNoise().match(content).hasMatch();
    });

    // Hashtags first: they trail the wording a dash suffix has to end with.
    text.replace(hashtag(), QString());
    text.replace(looseCredit(), QString());
    text.replace(dashSuffix(), QString());
    text.replace(whiteSpaceRun(), QStringLiteral(" "));
    text.replace(trailingPunctuation(), QString());
    text = text.trimmed();
    return text.isEmpty() ? original : text;
}

QString displayAlbumTitle(const QString& album)
{
    const QString original = collapsed(album);
    if (original.isEmpty()) {
        return {};
    }
    static const QRegularExpression featuring(QStringLiteral(R"(^\s*(?:feat\b|feat\.|ft\b|ft\.|featuring\b))"),
                                              kOptions);
    static const QRegularExpression soundtrack(QStringLiteral(R"(\b(?:sound\s*track|ost|original\s+score)\b)"),
                                               kOptions);
    static const QRegularExpression singleSuffix(QStringLiteral(R"(\s+[-\x{2013}\x{2014}]\s+single$)"), kOptions);

    QString text = filterGroups(original, [](const QString& content) {
        if (filmCredit().match(content).hasMatch() || featuring.match(content).hasMatch()) {
            return false;
        }
        return !soundtrack.match(content).hasMatch();
    });
    text.replace(singleSuffix, QString());
    text.replace(whiteSpaceRun(), QStringLiteral(" "));
    text.replace(trailingPunctuation(), QString());
    text = text.trimmed();
    return text.isEmpty() ? original : text;
}

} // namespace lmp::core
