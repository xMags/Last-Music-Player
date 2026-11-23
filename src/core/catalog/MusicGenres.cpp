#include "core/catalog/MusicGenres.h"

#include <QChar>
#include <QSet>

#include <QList>

namespace lmp::core::catalog {

namespace {

struct GenreRule {
    QStringList words;
    QString genre;
};

// Most specific first: "Indian Pop" must not fall to "Pop", "Christian Rock"
// to "Rock", "Dancehall" to "Dance".
const QList<GenreRule>& rules()
{
    static const QList<GenreRule> list{
        {{QStringLiteral("christmas"), QStringLiteral("holiday")}, QStringLiteral("Holiday")},
        {{QStringLiteral("sufi"), QStringLiteral("ghazal"), QStringLiteral("ghazals"), QStringLiteral("qawwali")}, QStringLiteral("Sufi & Ghazal")},
        {{QStringLiteral("devotional"), QStringLiteral("spiritual"), QStringLiteral("bhajan"), QStringLiteral("bhajans"), QStringLiteral("bhakti"), QStringLiteral("gospel"), QStringLiteral("christian"), QStringLiteral("worship"), QStringLiteral("religious"), QStringLiteral("praise")}, QStringLiteral("Devotional")},
        {{QStringLiteral("indian classical"), QStringLiteral("carnatic"), QStringLiteral("hindustani")}, QStringLiteral("Indian Classical")},
        {{QStringLiteral("bollywood"), QStringLiteral("filmi")}, QStringLiteral("Bollywood")},
        {{QStringLiteral("indian pop"), QStringLiteral("indipop")}, QStringLiteral("Indian Pop")},
        {{QStringLiteral("punjabi"), QStringLiteral("bhangra")}, QStringLiteral("Punjabi")},
        {{QStringLiteral("tamil")}, QStringLiteral("Tamil")},
        {{QStringLiteral("telugu"), QStringLiteral("tollywood")}, QStringLiteral("Telugu")},
        {{QStringLiteral("malayalam")}, QStringLiteral("Malayalam")},
        {{QStringLiteral("kannada")}, QStringLiteral("Kannada")},
        {{QStringLiteral("bengali"), QStringLiteral("bangla")}, QStringLiteral("Bengali")},
        {{QStringLiteral("marathi")}, QStringLiteral("Marathi")},
        {{QStringLiteral("gujarati")}, QStringLiteral("Gujarati")},
        {{QStringLiteral("bhojpuri")}, QStringLiteral("Bhojpuri")},
        {{QStringLiteral("k pop"), QStringLiteral("kpop"), QStringLiteral("korean")}, QStringLiteral("K-Pop")},
        {{QStringLiteral("j pop"), QStringLiteral("jpop"), QStringLiteral("j rock"), QStringLiteral("anime"), QStringLiteral("japanese"), QStringLiteral("kayokyoku"), QStringLiteral("enka")}, QStringLiteral("J-Pop")},
        {{QStringLiteral("c pop"), QStringLiteral("mandopop"), QStringLiteral("cantopop"), QStringLiteral("chinese"), QStringLiteral("taiwanese")}, QStringLiteral("C-Pop")},
        {{QStringLiteral("latin"), QStringLiteral("latino"), QStringLiteral("reggaeton"), QStringLiteral("salsa"), QStringLiteral("bachata"), QStringLiteral("cumbia"), QStringLiteral("regional mexican"), QStringLiteral("mexicana"), QStringLiteral("corridos"), QStringLiteral("urbano"), QStringLiteral("flamenco")}, QStringLiteral("Latin")},
        {{QStringLiteral("arabic"), QStringLiteral("khaleeji"), QStringLiteral("rai")}, QStringLiteral("Arabic")},
        {{QStringLiteral("afrobeats"), QStringLiteral("afrobeat"), QStringLiteral("afro"), QStringLiteral("african"), QStringLiteral("afropop"), QStringLiteral("amapiano"), QStringLiteral("highlife")}, QStringLiteral("African")},
        {{QStringLiteral("lo fi"), QStringLiteral("lofi"), QStringLiteral("chill"), QStringLiteral("chillout"), QStringLiteral("lounge"), QStringLiteral("easy listening"), QStringLiteral("meditation"), QStringLiteral("new age"), QStringLiteral("ambient"), QStringLiteral("relaxation"), QStringLiteral("instrumental"), QStringLiteral("sleep"), QStringLiteral("focus")}, QStringLiteral("Lo-Fi & Chill")},
        {{QStringLiteral("soundtrack"), QStringLiteral("soundtracks"), QStringLiteral("original score"), QStringLiteral("film score"), QStringLiteral("musicals"), QStringLiteral("musical")}, QStringLiteral("Soundtrack")},
        {{QStringLiteral("hip hop"), QStringLiteral("hiphop"), QStringLiteral("rap"), QStringLiteral("trap"), QStringLiteral("drill"), QStringLiteral("grime")}, QStringLiteral("Hip-Hop")},
        {{QStringLiteral("r and b"), QStringLiteral("rnb"), QStringLiteral("soul"), QStringLiteral("neo soul"), QStringLiteral("funk"), QStringLiteral("motown"), QStringLiteral("disco")}, QStringLiteral("R&B")},
        {{QStringLiteral("metal"), QStringLiteral("hardcore"), QStringLiteral("metalcore")}, QStringLiteral("Metal")},
        {{QStringLiteral("reggae"), QStringLiteral("dancehall"), QStringLiteral("ska"), QStringLiteral("dub")}, QStringLiteral("Reggae")},
        {{QStringLiteral("electronic"), QStringLiteral("electronica"), QStringLiteral("edm"), QStringLiteral("techno"), QStringLiteral("house"), QStringLiteral("trance"), QStringLiteral("dubstep"), QStringLiteral("drum and bass"), QStringLiteral("bass"), QStringLiteral("dance"), QStringLiteral("electro"), QStringLiteral("breakbeat"), QStringLiteral("garage"), QStringLiteral("hardstyle"), QStringLiteral("idm"), QStringLiteral("downtempo")}, QStringLiteral("Electronic")},
        {{QStringLiteral("alternative"), QStringLiteral("indie"), QStringLiteral("punk"), QStringLiteral("grunge"), QStringLiteral("emo"), QStringLiteral("shoegaze")}, QStringLiteral("Indie & Alternative")},
        {{QStringLiteral("rock")}, QStringLiteral("Rock")},
        {{QStringLiteral("jazz"), QStringLiteral("swing"), QStringLiteral("bebop"), QStringLiteral("bossa nova")}, QStringLiteral("Jazz")},
        {{QStringLiteral("blues")}, QStringLiteral("Blues")},
        {{QStringLiteral("classical"), QStringLiteral("opera"), QStringLiteral("orchestral"), QStringLiteral("baroque"), QStringLiteral("chamber music"), QStringLiteral("romantic era"), QStringLiteral("symphony")}, QStringLiteral("Classical")},
        {{QStringLiteral("folk"), QStringLiteral("americana"), QStringLiteral("bluegrass")}, QStringLiteral("Folk")},
        {{QStringLiteral("country")}, QStringLiteral("Country")},
        {{QStringLiteral("kids"), QStringLiteral("children"), QStringLiteral("children s music"), QStringLiteral("lullaby"), QStringLiteral("lullabies")}, QStringLiteral("Kids")},
        {{QStringLiteral("singer songwriter"), QStringLiteral("pop")}, QStringLiteral("Pop")},
    };
    return list;
}

[[nodiscard]] bool isMetaName(const QString& normalized)
{
    static const QSet<QString> names{QStringLiteral("music"),         QStringLiteral("indian"),
                                     QStringLiteral("regional indian"), QStringLiteral("world"),
                                     QStringLiteral("worldwide"),     QStringLiteral("international"),
                                     QStringLiteral("vocal")};
    return names.contains(normalized);
}

[[nodiscard]] bool matches(const GenreRule& rule, const QString& padded)
{
    for (const QString& word : rule.words) {
        if (padded.contains(QLatin1Char(' ') + word + QLatin1Char(' '))) {
            return true;
        }
    }
    return false;
}

} // namespace

const QStringList& musicGenres()
{
    static const QStringList genres{
        QStringLiteral("Bollywood"),     QStringLiteral("Indian Pop"),        QStringLiteral("Punjabi"),
        QStringLiteral("Tamil"),         QStringLiteral("Telugu"),            QStringLiteral("Malayalam"),
        QStringLiteral("Kannada"),       QStringLiteral("Bengali"),           QStringLiteral("Marathi"),
        QStringLiteral("Gujarati"),      QStringLiteral("Bhojpuri"),          QStringLiteral("Devotional"),
        QStringLiteral("Indian Classical"), QStringLiteral("Sufi & Ghazal"),  QStringLiteral("Pop"),
        QStringLiteral("Hip-Hop"),       QStringLiteral("R&B"),               QStringLiteral("Rock"),
        QStringLiteral("Metal"),         QStringLiteral("Electronic"),        QStringLiteral("Indie & Alternative"),
        QStringLiteral("Jazz"),          QStringLiteral("Blues"),             QStringLiteral("Classical"),
        QStringLiteral("Soundtrack"),    QStringLiteral("Folk"),              QStringLiteral("Country"),
        QStringLiteral("Latin"),         QStringLiteral("K-Pop"),             QStringLiteral("J-Pop"),
        QStringLiteral("C-Pop"),         QStringLiteral("Arabic"),            QStringLiteral("African"),
        QStringLiteral("Reggae"),        QStringLiteral("Lo-Fi & Chill"),     QStringLiteral("Holiday"),
        QStringLiteral("Kids"),
    };
    return genres;
}

QString normalizeGenreName(QStringView name)
{
    // Compatibility decomposition, then the combining marks dropped.
    const QString decomposed = name.toString().normalized(QString::NormalizationForm_KD);
    QString result;
    result.reserve(decomposed.size());
    bool pendingSpace = false;
    for (const QChar character : decomposed) {
        if (character.isMark()) {
            continue;
        }
        if (character == QLatin1Char('&')) {
            pendingSpace = true;
            if (!result.isEmpty()) {
                result.append(QLatin1Char(' '));
            }
            result.append(QStringLiteral("and"));
            continue;
        }
        if (character.isLetterOrNumber()) {
            if (pendingSpace && !result.isEmpty()) {
                result.append(QLatin1Char(' '));
            }
            pendingSpace = false;
            result.append(character.toLower());
            continue;
        }
        pendingSpace = true;
    }
    return result;
}

QString genreForName(QStringView name)
{
    const QString normalized = normalizeGenreName(name);
    if (normalized.isEmpty() || isMetaName(normalized)) {
        return {};
    }
    const QString padded = QLatin1Char(' ') + normalized + QLatin1Char(' ');
    for (const GenreRule& rule : rules()) {
        if (matches(rule, padded)) {
            return rule.genre;
        }
    }
    return {};
}

QString canonicalGenre(const QStringList& names)
{
    for (qsizetype index = 0; index < names.size() && index < 16; ++index) {
        QString genre = genreForName(names.at(index));
        if (!genre.isEmpty()) {
            return genre;
        }
    }
    return {};
}

} // namespace lmp::core::catalog
