#pragma once

#include "core/account/LibraryArtistContract.h"

#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// The welcome flow's rules, as the reference client has them: who sees the
// flow, what each step needs before Continue, how the artist grid grows, and
// the content language catalogue the first step offers. No UI and no I/O.
namespace lmp::core::account {

inline constexpr int MaxContentLanguages = 10;
inline constexpr int MinWelcomeLanguages = 1;
inline constexpr int MinWelcomeArtists = 3;
// How many similar artists one pick adds to the grid.
inline constexpr int SimilarPerPick = 3;
// The language catalogue's address on the account service.
inline constexpr QLatin1StringView LanguageCatalogPath{"/v1/music/languages?v=46"};

struct LanguageInfo {
    // Two or three lower-case letters.
    QString code;
    QString name;
    QString nativeName;
};

// Region code to its ordered suggested languages, from the service.
using RegionLanguageTable = QHash<QString, QStringList>;

// What the listener chose before: "custom" (their own list) or "auto" (the
// service's guess).
struct ContentLanguages {
    QString mode;
    QStringList languages;
};

// "first-run" gates the app and has minimums; "tune" is opened later and can
// be closed.
enum class WelcomeMode { FirstRun, Tune };

// Why the artist minimum is waived: nobody may be locked out by something
// they cannot fix.
enum class ArtistWaiver { None, SuggestionsUnavailable, FollowNotAllowed };

struct ArtistStepStatus {
    bool ready = false;
    // How many more picks Continue still needs; 0 when ready.
    int missing = 0;

    friend bool operator==(const ArtistStepStatus&, const ArtistStepStatus&) = default;
};

// An artist card of the flow. Without an identity it cannot be followed.
struct WelcomeArtist {
    QString title;
    QString artworkUrl;
    std::optional<LibraryArtistIdentity> identity;
    // The public id when the listener already follows it.
    QString followedId;
    // Languages the service suggested it for.
    QStringList languages;
};

// The catalogue's languages: entries with a valid code, each once; a missing
// name becomes the upper-case code.
[[nodiscard]] QList<LanguageInfo> normalizeLanguageCatalog(const QJsonValue& value);
// The service's region table: two-letter regions, each once, holding only
// languages of the catalogue, each once, at most MaxContentLanguages.
// Nothing when the value is not an object.
[[nodiscard]] std::optional<RegionLanguageTable> normalizeRegionLanguages(const QJsonValue& value,
                                                                          const QList<LanguageInfo>& catalog);
// A region's suggested languages: the service's table when there is one,
// else the built-in table with English added at the end.
[[nodiscard]] QStringList suggestedLanguages(const QString& region, const std::optional<RegionLanguageTable>& regions);

// Whether the account still owes the flow: it never finished it and holds no
// music. Call it only with what the service just confirmed.
[[nodiscard]] bool needsWelcome(bool libraryHasMusic, const QString& onboardingCompletedAt);

// The first step's languages: the listener's own list (when they chose one),
// else their region's.
[[nodiscard]] QStringList initialLanguages(const std::optional<ContentLanguages>& saved, const QString& region,
                                           const std::optional<RegionLanguageTable>& regions);
[[nodiscard]] QStringList regionLanguages(const QString& region, const std::optional<RegionLanguageTable>& regions);
[[nodiscard]] bool languagesReady(qsizetype selected, WelcomeMode mode);
[[nodiscard]] ArtistStepStatus artistStepStatus(int pickedCount, WelcomeMode mode, ArtistWaiver waiver);

// One key per artist: "<provider>:<catalogue id or address>", or
// "<provider>:name:<name>" without either (lower-cased, trimmed).
[[nodiscard]] QString artistKey(const LibraryArtistIdentity& identity);
// A card's key, or empty for one that cannot be followed.
[[nodiscard]] QString artistItemKey(const WelcomeArtist& artist);
// Without repeats or unfollowable cards, first occurrence kept.
[[nodiscard]] QList<WelcomeArtist> uniqueArtists(const QList<WelcomeArtist>& artists);
// Up to `limit` artists the grid does not hold yet go right after the pick.
[[nodiscard]] QList<WelcomeArtist> insertSimilarArtists(const QList<WelcomeArtist>& grid, const QString& pickedKey,
                                                        const QList<WelcomeArtist>& similar, int limit = SimilarPerPick);
// Artists already followed lead the grid.
[[nodiscard]] QList<WelcomeArtist> withFollowedFirst(const QList<WelcomeArtist>& suggested,
                                                     const QList<WelcomeArtist>& followed);

// The suggested artists answer ({"artists": [...]}): cards with a name, a
// provider and a catalogue id or address.
[[nodiscard]] QList<WelcomeArtist> parseSuggestedArtists(const QByteArray& payload);

} // namespace lmp::core::account
