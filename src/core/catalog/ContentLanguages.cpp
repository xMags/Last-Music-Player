#include "core/catalog/ContentLanguages.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QLatin1StringView>
#include <QLocale>
#include <QSet>

#include <algorithm>

namespace lmp::core::catalog {

namespace {

[[nodiscard]] bool isLanguageCode(const QString& code)
{
    return (code.size() == 2 || code.size() == 3) && std::all_of(code.cbegin(), code.cend(), [](QChar character) {
               return character >= QLatin1Char('a') && character <= QLatin1Char('z');
           });
}

[[nodiscard]] QStringList normalizeCodes(const QJsonValue& value)
{
    QStringList codes;
    for (const QJsonValue& item : value.toArray()) {
        if (!item.isString()) {
            continue;
        }
        const QString code = item.toString().trimmed().toLower();
        if (!isLanguageCode(code) || codes.contains(code)) {
            continue;
        }
        codes.append(code);
        if (codes.size() >= account::MaxContentLanguages) {
            break;
        }
    }
    return codes;
}

const QHash<QString, LanguageArea>& areaOfLanguage()
{
    static const QHash<QString, LanguageArea> table = []() {
        QHash<QString, LanguageArea> result;
        const auto add = [&result](LanguageArea area, std::initializer_list<const char*> codes) {
            for (const char* code : codes) {
                result.insert(QString::fromLatin1(code), area);
            }
        };
        add(LanguageArea::Indian, {"hi", "bn", "pa", "ta", "te", "ml", "kn", "mr", "gu", "or", "as", "ur", "ne", "bho"});
        add(LanguageArea::Europe, {"en", "es", "pt", "fr", "de", "it", "ro", "nl", "pl", "sv", "da", "no", "fi", "cs",
                                   "hu", "el", "uk", "ru", "tr"});
        add(LanguageArea::Asia, {"ko", "ja", "zh", "th", "vi", "id", "ms", "fil"});
        add(LanguageArea::MiddleEastAfrica, {"ar", "he", "fa", "sw"});
        return result;
    }();
    return table;
}

[[nodiscard]] QString areaId(LanguageArea area)
{
    switch (area) {
    case LanguageArea::Indian:
        return QStringLiteral("indian");
    case LanguageArea::Europe:
        return QStringLiteral("europe");
    case LanguageArea::Asia:
        return QStringLiteral("asia");
    case LanguageArea::MiddleEastAfrica:
        return QStringLiteral("middle-east-africa");
    case LanguageArea::Other:
        break;
    }
    return QStringLiteral("other");
}

[[nodiscard]] QString areaLabel(LanguageArea area)
{
    switch (area) {
    case LanguageArea::Indian:
        return QStringLiteral("Indian");
    case LanguageArea::Europe:
        return QStringLiteral("Europe & the Americas");
    case LanguageArea::Asia:
        return QStringLiteral("Asia & Pacific");
    case LanguageArea::MiddleEastAfrica:
        return QStringLiteral("Middle East & Africa");
    case LanguageArea::Other:
        break;
    }
    return QStringLiteral("More");
}

// Lower case without accents, so "espanol" finds Español.
[[nodiscard]] QString searchForm(const QString& text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString result;
    result.reserve(decomposed.size());
    for (const QChar character : decomposed) {
        if (!character.isMark()) {
            result.append(character.toLower());
        }
    }
    return result;
}

[[nodiscard]] QStringList aliases(const QString& code)
{
    static const QHash<QString, QStringList> table{
        {QStringLiteral("bn"), {QStringLiteral("Bangla")}},
        {QStringLiteral("pa"), {QStringLiteral("Panjabi")}},
        {QStringLiteral("or"), {QStringLiteral("Oriya")}},
        {QStringLiteral("zh"), {QStringLiteral("Mandarin"), QStringLiteral("Cantonese")}},
        {QStringLiteral("es"), {QStringLiteral("Castellano")}},
        {QStringLiteral("fa"), {QStringLiteral("Farsi")}},
        {QStringLiteral("fil"), {QStringLiteral("Tagalog"), QStringLiteral("Pilipino")}},
        {QStringLiteral("ms"), {QStringLiteral("Bahasa Malaysia")}},
        {QStringLiteral("he"), {QStringLiteral("Ivrit")}},
        {QStringLiteral("nl"), {QStringLiteral("Flemish")}},
        {QStringLiteral("no"), {QStringLiteral("Bokmal"), QStringLiteral("Nynorsk")}},
        {QStringLiteral("el"), {QStringLiteral("Hellenic")}},
        {QStringLiteral("ko"), {QStringLiteral("Hangul")}},
    };
    return table.value(code);
}

} // namespace

QString languageModeName(LanguageMode mode)
{
    switch (mode) {
    case LanguageMode::Custom:
        return QStringLiteral("custom");
    case LanguageMode::Off:
        return QStringLiteral("off");
    case LanguageMode::Auto:
        break;
    }
    return QStringLiteral("auto");
}

LanguageSetting normalizeLanguageSetting(const QJsonValue& value)
{
    const QJsonObject raw = value.toObject();
    const QString mode = raw.value(QLatin1StringView("mode")).toString();
    LanguageSetting setting;
    setting.mode = mode == QLatin1StringView("custom") ? LanguageMode::Custom
        : mode == QLatin1StringView("off")             ? LanguageMode::Off
                                                       : LanguageMode::Auto;
    setting.languages = normalizeCodes(raw.value(QLatin1StringView("languages")));
    if (raw.value(QLatin1StringView("inferred")).isArray()) {
        setting.inferred = normalizeCodes(raw.value(QLatin1StringView("inferred")));
    }
    const QJsonValue inferredAt = raw.value(QLatin1StringView("inferredAt"));
    if (inferredAt.isString()) {
        setting.inferredAt = inferredAt.toString();
    }
    return setting;
}

QJsonObject languageSettingJson(const LanguageSetting& setting)
{
    return QJsonObject{{QStringLiteral("mode"), languageModeName(setting.mode)},
                       {QStringLiteral("languages"), QJsonArray::fromStringList(setting.languages)}};
}

LanguageArea languageArea(const QString& code)
{
    return areaOfLanguage().value(code, LanguageArea::Other);
}

QList<LanguageGroup> groupLanguageCatalog(const QList<LanguageInfo>& catalog, const std::optional<LanguageRegion>& region,
                                          const std::optional<RegionLanguageTable>& regions)
{
    const QStringList suggested = region ? account::suggestedLanguages(region->code, regions) : QStringList();
    QHash<QString, LanguageInfo> byCode;
    for (const LanguageInfo& info : catalog) {
        byCode.insert(info.code, info);
    }
    QList<LanguageInfo> suggestedInfo;
    QSet<QString> taken;
    for (const QString& code : suggested) {
        const auto found = byCode.constFind(code);
        if (found != byCode.cend()) {
            suggestedInfo.append(*found);
            taken.insert(code);
        }
    }
    // The country's own area leads: that of its first suggestion other than
    // English, which is everyone's.
    const auto home = std::find_if(suggested.cbegin(), suggested.cend(),
                                   [](const QString& code) { return code != QLatin1StringView("en"); });
    const QString homeCode = home != suggested.cend() ? *home : (suggested.isEmpty() ? QString() : suggested.first());
    const std::optional<LanguageArea> homeArea = homeCode.isEmpty() ? std::nullopt
                                                                    : std::optional<LanguageArea>(languageArea(homeCode));
    const bool southAsian = homeArea == LanguageArea::Indian;
    QList<LanguageArea> areas{LanguageArea::Europe, LanguageArea::Asia, LanguageArea::MiddleEastAfrica,
                              LanguageArea::Indian, LanguageArea::Other};
    if (!southAsian) {
        areas.removeAll(LanguageArea::Indian);
    }
    if (homeArea && areas.contains(*homeArea)) {
        areas.removeAll(*homeArea);
        areas.prepend(*homeArea);
    }
    QList<LanguageInfo> rest;
    for (const LanguageInfo& info : catalog) {
        if (!taken.contains(info.code)) {
            rest.append(info);
        }
    }
    const auto inArea = [&rest](LanguageArea area) {
        QList<LanguageInfo> result;
        for (const LanguageInfo& info : rest) {
            if (languageArea(info.code) == area) {
                result.append(info);
            }
        }
        return result;
    };
    QList<LanguageGroup> groups;
    if (region && !suggestedInfo.isEmpty()) {
        groups.append({QStringLiteral("suggested"), QStringLiteral("Suggested for ") + region->name, suggestedInfo});
    }
    for (const LanguageArea area : areas) {
        QList<LanguageInfo> languages = inArea(area);
        if (area == LanguageArea::Asia && !southAsian) {
            languages += inArea(LanguageArea::Indian);
        }
        if (!languages.isEmpty()) {
            groups.append({areaId(area), areaLabel(area), languages});
        }
    }
    return groups;
}

bool languageMatchesQuery(const LanguageInfo& info, const QString& query)
{
    const QString needle = searchForm(query.trimmed());
    if (needle.isEmpty()) {
        return true;
    }
    QStringList names{info.name, info.nativeName};
    names += aliases(info.code);
    return std::any_of(names.cbegin(), names.cend(),
                       [&needle](const QString& name) { return searchForm(name).contains(needle); });
}

QList<LanguageGroup> filterLanguageGroups(const QList<LanguageGroup>& groups, const QString& query)
{
    QList<LanguageGroup> result;
    for (const LanguageGroup& group : groups) {
        LanguageGroup filtered{group.id, group.label, {}};
        for (const LanguageInfo& info : group.languages) {
            if (languageMatchesQuery(info, query)) {
                filtered.languages.append(info);
            }
        }
        if (!filtered.languages.isEmpty()) {
            result.append(std::move(filtered));
        }
    }
    return result;
}

QString detectedAgo(const QString& inferredAt, std::int64_t nowMsSinceEpoch)
{
    const QDateTime time = QDateTime::fromString(inferredAt, Qt::ISODateWithMs);
    if (inferredAt.isEmpty() || !time.isValid()) {
        return {};
    }
    const std::int64_t days = std::max<std::int64_t>(0, (nowMsSinceEpoch - time.toMSecsSinceEpoch()) / 86'400'000);
    if (days == 0) {
        return QStringLiteral("today");
    }
    if (days == 1) {
        return QStringLiteral("yesterday");
    }
    if (days < 14) {
        return QStringLiteral("%1 days ago").arg(days);
    }
    return QStringLiteral("%1 weeks ago").arg(days / 7);
}

QString regionDisplayName(const QString& region)
{
    const QString code = region.trimmed().toUpper();
    const QLocale::Territory territory = QLocale::codeToTerritory(code);
    if (code.size() != 2 || territory == QLocale::AnyTerritory) {
        return code;
    }
    return QLocale::territoryToString(territory);
}

bool opensEditorOnChoose(const LanguageSetting& value)
{
    if (value.mode == LanguageMode::Custom) {
        return false;
    }
    return value.mode == LanguageMode::Auto || value.languages.isEmpty();
}

QStringList effectiveLanguages(const LanguageSetting& value)
{
    return !value.languages.isEmpty() ? value.languages : value.inferred.value_or(QStringList());
}

QStringList detectedLanguages(const LanguageSetting& value)
{
    if (value.inferred && !value.inferred->isEmpty()) {
        return *value.inferred;
    }
    return value.mode == LanguageMode::Auto ? value.languages : QStringList();
}

LanguageSetting autoPayload()
{
    return LanguageSetting{LanguageMode::Auto, {}, std::nullopt, {}};
}

LanguageSetting offPayload(const LanguageSetting& current)
{
    return LanguageSetting{LanguageMode::Off, current.languages, std::nullopt, {}};
}

LanguageSetting customPayload(const QStringList& languages)
{
    return LanguageSetting{LanguageMode::Custom, languages, std::nullopt, {}};
}

ToggleResult toggleLanguage(const QStringList& selected, const QString& code)
{
    if (selected.contains(code)) {
        if (selected.size() <= 1) {
            return {ToggleRefusal::Min, {}};
        }
        QStringList remaining = selected;
        remaining.removeAll(code);
        return {ToggleRefusal::None, remaining};
    }
    if (selected.size() >= account::MaxContentLanguages) {
        return {ToggleRefusal::Max, {}};
    }
    return {ToggleRefusal::None, selected + QStringList{code}};
}

bool needsAutoDetect(const std::optional<LanguageSetting>& value)
{
    return !value || (value->mode == LanguageMode::Auto && value->inferredAt.isEmpty());
}

QString inferErrorMessage(int httpStatus)
{
    return QString::fromUtf8(httpStatus == 429 ? language_text::DetectRateLimited : language_text::DetectUnavailable);
}

} // namespace lmp::core::catalog
