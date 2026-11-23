#pragma once

#include "core/account/WelcomeModel.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

// The account's recommendation languages: which languages autoplay, Home's
// new releases, similar artists and recommendation searches keep to. Search,
// charts, the library and playlists always show everything.
//
// The service keeps the setting as `preferences.contentLanguages`. A
// preferences PUT is a shallow merge, so every save sends the whole object.
// `inferred` and `inferredAt` belong to the service: it ignores them on a
// save and fills them when it detects the languages from listening history.
//
// The language catalogue and the region table are read by
// account::normalizeLanguageCatalog and normalizeRegionLanguages (shared with
// the welcome flow). Pure functions, no I/O.
namespace lmp::core::catalog {

using account::LanguageInfo;
using account::RegionLanguageTable;

enum class LanguageMode {
    Auto,
    Custom,
    Off,
};

[[nodiscard]] QString languageModeName(LanguageMode mode);

struct LanguageSetting {
    LanguageMode mode = LanguageMode::Auto;
    QStringList languages;
    std::optional<QStringList> inferred;
    QString inferredAt;

    friend bool operator==(const LanguageSetting&, const LanguageSetting&) = default;
};

// Rapid chip taps are saved once they settle, not one PUT per tap.
inline constexpr int LanguageSaveDelayMs = 400;

namespace language_text {
inline constexpr const char* Help = "Autoplay, Home and similar artists keep to these. Search, charts, your library "
                                    "and playlists still show everything.";
inline constexpr const char* DetectedLead = "Detected from your listening:";
inline constexpr const char* NotEnoughListening =
    "Not enough listening yet. Keep playing and it will learn your languages.";
inline constexpr const char* RegionLead =
    "Not enough listening yet. Until then, recommendations keep to your region's languages:";
inline constexpr const char* Off = "Recommendations include every language.";
inline constexpr const char* KeepOne = "Keep at least one language";
inline constexpr const char* UpToTen = "Up to 10 languages";
inline constexpr const char* PickSome = "Pick at least one language";
inline constexpr const char* DetectUnavailable = "Couldn't detect your languages right now.";
inline constexpr const char* DetectRateLimited = "Try again in a few minutes.";
inline constexpr const char* SaveFailed = "Could not save your languages.";
} // namespace language_text

// The setting from service data, which is not trusted: an absent or
// malformed value is the service default, automatic with nothing detected.
// Codes are 2 or 3 lower-case letters, once each, at most ten.
[[nodiscard]] LanguageSetting normalizeLanguageSetting(const QJsonValue& value);
// The whole object a save sends: {"mode", "languages"}.
[[nodiscard]] QJsonObject languageSettingJson(const LanguageSetting& setting);

enum class LanguageArea {
    Indian,
    Europe,
    Asia,
    MiddleEastAfrica,
    Other,
};

// The area a language is listed under; an unknown code is Other ("More").
[[nodiscard]] LanguageArea languageArea(const QString& code);

struct LanguageGroup {
    // "suggested", "indian", "europe", "asia", "middle-east-africa", "other".
    QString id;
    QString label;
    QList<LanguageInfo> languages;
};

// The listener's region for the editor's suggestions.
struct LanguageRegion {
    QString code;
    QString name;
};

// Splits the catalogue for the editor. With a region that has suggestions,
// "Suggested for <country>" comes first in suggestion order, then the other
// languages by area, the area of the country's own language first; each
// language once. Indian languages have their own group only for a South Asian
// region; elsewhere they follow the other Asia & Pacific languages. Empty
// groups are left out.
[[nodiscard]] QList<LanguageGroup> groupLanguageCatalog(const QList<LanguageInfo>& catalog,
                                                        const std::optional<LanguageRegion>& region,
                                                        const std::optional<RegionLanguageTable>& regions);

// A match on the English or native name or a common other name, ignoring
// case and accents; an empty query matches everything.
[[nodiscard]] bool languageMatchesQuery(const LanguageInfo& info, const QString& query);
[[nodiscard]] QList<LanguageGroup> filterLanguageGroups(const QList<LanguageGroup>& groups, const QString& query);

// When Automatic last detected: "today", "yesterday", "3 days ago",
// "2 weeks ago", or empty for a missing or unreadable time.
[[nodiscard]] QString detectedAgo(const QString& inferredAt, std::int64_t nowMsSinceEpoch);

// The country's English name for a region code, for when the region list
// has not loaded; the upper-case code when it has no name.
[[nodiscard]] QString regionDisplayName(const QString& region);

// Whether Choose should open the editor at once: when the listener has no
// list of their own yet.
[[nodiscard]] bool opensEditorOnChoose(const LanguageSetting& value);
// The list the filter uses now: the stored one, else the detected one.
[[nodiscard]] QStringList effectiveLanguages(const LanguageSetting& value);
// The languages Automatic shows as detected.
[[nodiscard]] QStringList detectedLanguages(const LanguageSetting& value);

[[nodiscard]] LanguageSetting autoPayload();
[[nodiscard]] LanguageSetting offPayload(const LanguageSetting& current);
[[nodiscard]] LanguageSetting customPayload(const QStringList& languages);

enum class ToggleRefusal {
    None,
    Min,
    Max,
};

struct ToggleResult {
    ToggleRefusal refusal = ToggleRefusal::None;
    QStringList languages;
};

// Adds or removes one language from a custom list. The last language cannot
// be removed and an eleventh cannot be added; a new pick goes to the end.
[[nodiscard]] ToggleResult toggleLanguage(const QStringList& selected, const QString& code);

// Detection runs by itself on Automatic (or with nothing set) when the
// service has never detected the languages.
[[nodiscard]] bool needsAutoDetect(const std::optional<LanguageSetting>& value);
// The short message for a failed detection with this HTTP status.
[[nodiscard]] QString inferErrorMessage(int httpStatus);

} // namespace lmp::core::catalog
