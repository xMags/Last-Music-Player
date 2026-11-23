#include "core/account/WelcomeModel.h"

#include "core/net/ArtworkUrl.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace lmp::core::account {

namespace {

[[nodiscard]] bool isLanguageCode(const QString& code)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z]{2,3}$"));
    return pattern.match(code).hasMatch();
}

[[nodiscard]] bool isRegionCode(const QString& code)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z]{2}$"));
    return pattern.match(code).hasMatch();
}

// Each country's most listened languages, used only while the service has
// sent no table of its own. English is added after them.
[[nodiscard]] const QHash<QString, QStringList>& countryLanguages()
{
    static const QHash<QString, QStringList> table = [] {
        // A plain table of literals: each row is a country and its languages,
        // separated by spaces. (Nested initializer lists would point at
        // arrays that no longer exist once the outer list is built.)
        struct Row {
            const char* region;
            const char* languages;
        };
        static constexpr Row rows[] = {
            {"in", "hi bn ta te pa mr"}, {"np", "ne hi"}, {"bd", "bn hi"}, {"pk", "ur pa"}, {"lk", "ta"},
            {"us", "en es"}, {"ca", "en fr"}, {"gb", "en"}, {"ie", "en"}, {"au", "en"}, {"nz", "en"}, {"za", "en"},
            {"ng", "en"}, {"gh", "en"}, {"mx", "es"}, {"ar", "es"}, {"co", "es"}, {"cl", "es"}, {"pe", "es"},
            {"ve", "es"}, {"ec", "es"}, {"gt", "es"}, {"cr", "es"}, {"pa", "es"}, {"do", "es"}, {"uy", "es"},
            {"py", "es"}, {"bo", "es"}, {"sv", "es"}, {"hn", "es"}, {"ni", "es"}, {"es", "es"}, {"br", "pt"},
            {"pt", "pt"}, {"ao", "pt"}, {"mz", "pt"}, {"fr", "fr"}, {"be", "nl fr"}, {"lu", "fr de"},
            {"ch", "de fr it"}, {"de", "de"}, {"at", "de"}, {"it", "it"}, {"ro", "ro"}, {"md", "ro ru"},
            {"pl", "pl"}, {"nl", "nl"}, {"se", "sv"}, {"dk", "da"}, {"no", "no"}, {"fi", "fi sv"}, {"cz", "cs"},
            {"sk", "cs"}, {"hu", "hu"}, {"gr", "el"}, {"cy", "el tr"}, {"ua", "uk ru"}, {"ru", "ru"}, {"by", "ru"},
            {"kz", "ru"}, {"kg", "ru"}, {"uz", "ru"}, {"tj", "ru fa"}, {"am", "ru"}, {"az", "tr ru"}, {"tr", "tr"},
            {"il", "he ar"}, {"ir", "fa"}, {"af", "fa"}, {"sa", "ar"}, {"ae", "ar"}, {"eg", "ar"}, {"jo", "ar"},
            {"lb", "ar fr"}, {"kw", "ar"}, {"qa", "ar"}, {"bh", "ar"}, {"om", "ar"}, {"iq", "ar"}, {"ma", "ar fr"},
            {"dz", "ar fr"}, {"tn", "ar fr"}, {"ly", "ar"}, {"ye", "ar"}, {"kr", "ko"}, {"jp", "ja"}, {"cn", "zh"},
            {"tw", "zh"}, {"hk", "zh"}, {"mo", "zh"}, {"sg", "en zh ms ta"}, {"id", "id"}, {"my", "ms zh"},
            {"ph", "fil"}, {"th", "th"}, {"vn", "vi"}, {"ke", "sw"}, {"tz", "sw"}, {"ug", "sw"},
        };
        QHash<QString, QStringList> built;
        for (const Row& row : rows) {
            built.insert(QString::fromLatin1(row.region),
                         QString::fromLatin1(row.languages).split(QLatin1Char(' '), Qt::SkipEmptyParts));
        }
        return built;
    }();
    return table;
}

[[nodiscard]] QString stringField(const QJsonObject& object, QLatin1StringView key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
}

} // namespace

QList<LanguageInfo> normalizeLanguageCatalog(const QJsonValue& value)
{
    QList<LanguageInfo> result;
    if (!value.isArray()) {
        return result;
    }
    QSet<QString> seen;
    for (const QJsonValue& item : value.toArray()) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject raw = item.toObject();
        const QString code = stringField(raw, QLatin1StringView("code")).toLower();
        if (!isLanguageCode(code) || seen.contains(code)) {
            continue;
        }
        seen.insert(code);
        const QString name = stringField(raw, QLatin1StringView("name"));
        result.append({code, name.isEmpty() ? code.toUpper() : name, stringField(raw, QLatin1StringView("nativeName"))});
    }
    return result;
}

std::optional<RegionLanguageTable> normalizeRegionLanguages(const QJsonValue& value, const QList<LanguageInfo>& catalog)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    QSet<QString> known;
    for (const LanguageInfo& info : catalog) {
        known.insert(info.code);
    }
    RegionLanguageTable table;
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString region = it.key().trimmed().toLower();
        if (!isRegionCode(region) || table.contains(region)) {
            continue;
        }
        QStringList codes;
        for (const QJsonValue& item : it.value().toArray()) {
            if (!item.isString()) {
                continue;
            }
            const QString code = item.toString().trimmed().toLower();
            if (!known.contains(code) || codes.contains(code)) {
                continue;
            }
            codes.append(code);
            if (codes.size() >= MaxContentLanguages) {
                break;
            }
        }
        table.insert(region, codes);
    }
    return table;
}

QStringList suggestedLanguages(const QString& region, const std::optional<RegionLanguageTable>& regions)
{
    const QString code = region.trimmed().toLower();
    if (regions) {
        return regions->value(code);
    }
    const auto own = countryLanguages().constFind(code);
    if (own == countryLanguages().constEnd()) {
        return {};
    }
    QStringList languages = own.value();
    if (!languages.contains(QStringLiteral("en"))) {
        languages.append(QStringLiteral("en"));
    }
    return languages;
}

bool needsWelcome(bool libraryHasMusic, const QString& onboardingCompletedAt)
{
    return onboardingCompletedAt.isEmpty() && !libraryHasMusic;
}

QStringList regionLanguages(const QString& region, const std::optional<RegionLanguageTable>& regions)
{
    return suggestedLanguages(region, regions).mid(0, MaxContentLanguages);
}

QStringList initialLanguages(const std::optional<ContentLanguages>& saved, const QString& region,
                             const std::optional<RegionLanguageTable>& regions)
{
    if (saved && saved->mode == QLatin1StringView("custom") && !saved->languages.isEmpty()) {
        return saved->languages.mid(0, MaxContentLanguages);
    }
    return regionLanguages(region, regions);
}

bool languagesReady(qsizetype selected, WelcomeMode mode)
{
    if (selected > MaxContentLanguages) {
        return false;
    }
    return mode == WelcomeMode::Tune ? selected > 0 : selected >= MinWelcomeLanguages;
}

ArtistStepStatus artistStepStatus(int pickedCount, WelcomeMode mode, ArtistWaiver waiver)
{
    if (mode == WelcomeMode::Tune || waiver != ArtistWaiver::None) {
        return {true, 0};
    }
    const int missing = std::max(0, MinWelcomeArtists - pickedCount);
    return {missing == 0, missing};
}

QString artistKey(const LibraryArtistIdentity& identity)
{
    const QString provider = identity.provider.trimmed().toLower();
    const QString id = (identity.catalogId.isEmpty() ? identity.sourceUrl : identity.catalogId).trimmed();
    return id.isEmpty() ? provider + QStringLiteral(":name:") + identity.name.trimmed().toLower()
                        : provider + QLatin1Char(':') + id;
}

QString artistItemKey(const WelcomeArtist& artist)
{
    return artist.identity ? artistKey(*artist.identity) : QString();
}

QList<WelcomeArtist> uniqueArtists(const QList<WelcomeArtist>& artists)
{
    QSet<QString> seen;
    QList<WelcomeArtist> result;
    for (const WelcomeArtist& artist : artists) {
        const QString key = artistItemKey(artist);
        if (key.isEmpty() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        result.append(artist);
    }
    return result;
}

QList<WelcomeArtist> insertSimilarArtists(const QList<WelcomeArtist>& grid, const QString& pickedKey,
                                          const QList<WelcomeArtist>& similar, int limit)
{
    qsizetype index = -1;
    QSet<QString> held;
    for (qsizetype row = 0; row < grid.size(); ++row) {
        const QString key = artistItemKey(grid.at(row));
        held.insert(key);
        if (index < 0 && key == pickedKey) {
            index = row;
        }
    }
    if (index < 0) {
        return grid;
    }
    QList<WelcomeArtist> fresh;
    for (const WelcomeArtist& artist : uniqueArtists(similar)) {
        if (fresh.size() >= std::max(0, limit)) {
            break;
        }
        if (!held.contains(artistItemKey(artist))) {
            fresh.append(artist);
        }
    }
    if (fresh.isEmpty()) {
        return grid;
    }
    QList<WelcomeArtist> result = grid.mid(0, index + 1);
    result.append(fresh);
    result.append(grid.mid(index + 1));
    return result;
}

QList<WelcomeArtist> withFollowedFirst(const QList<WelcomeArtist>& suggested, const QList<WelcomeArtist>& followed)
{
    QList<WelcomeArtist> all = followed;
    all.append(suggested);
    return uniqueArtists(all);
}

QList<WelcomeArtist> parseSuggestedArtists(const QByteArray& payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    QList<WelcomeArtist> artists;
    for (const QJsonValue& entry : document.object().value(QLatin1StringView("artists")).toArray()) {
        const QJsonObject raw = entry.toObject();
        const QString catalogId = stringField(raw, QLatin1StringView("catalogId"));
        const QString sourceUrl = stringField(raw, QLatin1StringView("sourceUrl"));
        const QString name = stringField(raw, QLatin1StringView("name"));
        const QString provider = stringField(raw, QLatin1StringView("provider"));
        if ((catalogId.isEmpty() && sourceUrl.isEmpty()) || name.isEmpty() || provider.isEmpty()) {
            continue;
        }
        WelcomeArtist artist;
        artist.title = name;
        artist.artworkUrl = net::durableAccountArtworkUrl(stringField(raw, QLatin1StringView("artworkUrl")));
        artist.identity = LibraryArtistIdentity{catalogId, sourceUrl, name, provider};
        const QJsonValue followed = raw.value(QLatin1StringView("followed"));
        const QJsonValue libraryId = raw.value(QLatin1StringView("libraryArtistId"));
        if (followed.isBool() && followed.toBool() && libraryId.isString()) {
            artist.followedId = libraryId.toString();
        }
        for (const QJsonValue& code : raw.value(QLatin1StringView("languages")).toArray()) {
            if (code.isString()) {
                artist.languages.append(code.toString());
            }
        }
        artists.append(std::move(artist));
    }
    return artists;
}

} // namespace lmp::core::account
