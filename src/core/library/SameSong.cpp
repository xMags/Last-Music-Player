#include "core/library/SameSong.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace lmp::core {

namespace {

constexpr QRegularExpression::PatternOptions kOptions =
    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;

const QSet<QString>& variantMarkers()
{
    static const QSet<QString> markers{
        QStringLiteral("remix"),   QStringLiteral("remixed"), QStringLiteral("cover"),    QStringLiteral("karaoke"),
        QStringLiteral("instrumental"), QStringLiteral("acoustic"), QStringLiteral("unplugged"), QStringLiteral("live"),
        QStringLiteral("sped"),    QStringLiteral("slowed"),  QStringLiteral("reverb"),   QStringLiteral("lofi"),
        QStringLiteral("8d"),      QStringLiteral("mashup"),  QStringLiteral("remake"),   QStringLiteral("reprise"),
        QStringLiteral("medley"),  QStringLiteral("concert"),
    };
    return markers;
}

const QSet<QString>& genericArtistWords()
{
    static const QSet<QString> words{
        QStringLiteral("artist"),   QStringLiteral("artists"),  QStringLiteral("official"), QStringLiteral("music"),
        QStringLiteral("records"),  QStringLiteral("record"),   QStringLiteral("company"),  QStringLiteral("entertainment"),
        QStringLiteral("studios"),  QStringLiteral("studio"),   QStringLiteral("films"),    QStringLiteral("film"),
        QStringLiteral("channel"),  QStringLiteral("the"),      QStringLiteral("and"),      QStringLiteral("feat"),
        QStringLiteral("ft"),       QStringLiteral("featuring"), QStringLiteral("vevo"),    QStringLiteral("topic"),
        QStringLiteral("unknown"),  QStringLiteral("various"),
    };
    return words;
}

// A dash part that only names the upload, never the song.
const QRegularExpression& noisePart()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^(?:official\b.*|lyric(?:s|al)?\b.*|full\s+(?:video|song|audio)\b.*|video\s+song\b.*|)"
                       R"((?:music\s+)?video\b.*|audio\b.*|visuali[sz]er\b.*)$)"),
        kOptions);
    return pattern;
}

// A dubbed version of a film song, "Song (From "Film") - HINDI".
const QRegularExpression& languageSuffix()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\s+[-\x{2013}\x{2014}]\s+(?:hindi|tamil|telugu|kannada|malayalam|bengali|marathi|punjabi|)"
                       R"(gujarati|bhojpuri|odia|english)\s*$)"),
        kOptions);
    return pattern;
}

const QRegularExpression& dash()
{
    static const QRegularExpression pattern(QStringLiteral(R"(\s+[-\x{2013}\x{2014}]\s+)"), kOptions);
    return pattern;
}

const QRegularExpression& pipe()
{
    static const QRegularExpression pattern(QStringLiteral(R"(\s*[|\x{FF5C}]\s*)"), kOptions);
    return pattern;
}

QStringList tokens(const QString& value)
{
    const QString text = comparableText(value);
    return text.isEmpty() ? QStringList{} : text.split(QLatin1Char(' '));
}

QSet<QString> artistTokens(const QString& value)
{
    QSet<QString> result;
    for (const QString& token : tokens(value)) {
        if (token.size() > 1 && !genericArtistWords().contains(token)) {
            result.insert(token);
        }
    }
    return result;
}

bool namesAnArtist(const QString& side, const QSet<QString>& fieldArtists)
{
    const QSet<QString> sideArtists = artistTokens(side);
    return std::any_of(sideArtists.cbegin(), sideArtists.cend(),
                       [&fieldArtists](const QString& token) { return fieldArtists.contains(token); });
}

} // namespace

QString comparableText(const QString& value)
{
    static const QRegularExpression marks(QStringLiteral(R"(\p{M})"), kOptions);
    static const QRegularExpression separators(QStringLiteral(R"([^\p{L}\p{N}]+)"), kOptions);
    QString text = value.normalized(QString::NormalizationForm_KD);
    text.remove(marks);
    text = text.toLower();
    text.replace(separators, QStringLiteral(" "));
    return text.trimmed();
}

SongKey songKey(const QString& title, const QString& artist)
{
    static const QRegularExpression whiteSpace(QStringLiteral(R"(\s+)"), kOptions);
    QString cleanTitle = title;
    cleanTitle.replace(whiteSpace, QStringLiteral(" "));
    cleanTitle = cleanTitle.trimmed();

    SongKey key;
    for (const QString& token : tokens(cleanTitle)) {
        if (variantMarkers().contains(token)) {
            key.variants.insert(token);
        }
    }

    QStringList parts;
    for (const QString& part : cleanTitle.split(pipe())) {
        if (!part.trimmed().isEmpty()) {
            parts.append(part.trimmed());
        }
    }
    QString song = parts.value(0);
    song.replace(languageSuffix(), QString());
    QStringList credits = parts.mid(1);

    // "Song - Artist" or "Artist - Song": the side naming the artist is credits.
    const QSet<QString> fieldArtists = artistTokens(artist);
    if (dash().match(song).hasMatch()) {
        const QStringList sides = song.split(dash());
        const QString left = sides.value(0);
        const QString right = sides.mid(1).join(QStringLiteral(" - "));
        if (noisePart().match(right).hasMatch()) {
            song = left;
        } else if (namesAnArtist(right, fieldArtists)) {
            song = left;
            credits.append(right);
        } else if (namesAnArtist(left, fieldArtists)) {
            song = right;
            credits.append(left);
        }
    }

    static const QRegularExpression leadingNoise(QStringLiteral(R"(^[^\p{L}\p{N}]+)"), kOptions);
    song.replace(leadingNoise, QString());
    song = song.trimmed();

    // "Film: Song", a label's naming for soundtrack singles.
    static const QRegularExpression filmPrefix(QStringLiteral(R"(^([^:[\]()]{2,40}):\s+(.{2,})$)"),
                                               QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression notAFilm(QStringLiteral(R"(\b(?:feat|ft|remix|live|version)\b)"), kOptions);
    const QRegularExpressionMatch film = filmPrefix.match(song);
    if (film.hasMatch() && !notAFilm.match(film.captured(1)).hasMatch()) {
        song = film.captured(2);
    }

    static const QRegularExpression brackets(QStringLiteral(R"([[(][^\])]*[\])])"),
                                             QRegularExpression::UseUnicodePropertiesOption);
    QString bare = song;
    bare.replace(brackets, QStringLiteral(" "));
    key.name = comparableText(bare);
    key.artists = artistTokens((QStringList{artist} + credits).join(QLatin1Char(' ')));
    return key;
}

bool sameSongKeys(const SongKey& a, const SongKey& b)
{
    if (a.name.size() < 2 || a.name != b.name) {
        return false;
    }
    if (a.variants != b.variants) {
        return false;
    }
    return a.artists.intersects(b.artists);
}

bool isSameSong(const SongIdentity& a, const SongIdentity& b)
{
    if (!a.id.isEmpty() && a.id == b.id) {
        return true;
    }
    return sameSongKeys(songKey(a.title, a.artist), songKey(b.title, b.artist));
}

QList<SongIdentity> sameSongMatches(const SongIdentity& song, const QList<SongIdentity>& pool)
{
    const SongKey key = songKey(song.title, song.artist);
    QList<SongIdentity> matches;
    for (const SongIdentity& candidate : pool) {
        if (candidate.id == song.id
            || sameSongKeys(key, songKey(candidate.title, candidate.artist))) {
            matches.append(candidate);
        }
    }
    return matches;
}

} // namespace lmp::core
