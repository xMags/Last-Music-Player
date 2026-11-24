#include "ui/welcome/WelcomeViewModel.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/account/MusicSyncService.h"
#include "core/account/PortablePreferenceSettings.h"
#include "core/catalog/CatalogService.h"
#include "core/catalog/ContentLanguages.h"
#include "core/db/AccountRepository.h"
#include "core/model/Track.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "ui/player/CoverSource.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>

namespace lmp::ui {

namespace {

namespace account = core::account;

// The preferences answer's own object, under "preferences" or "data" when
// wrapped.
[[nodiscard]] QJsonObject preferencesObject(const QByteArray& body)
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    for (int depth = 0; depth < 2; ++depth) {
        if (root.value(QLatin1StringView("preferences")).isObject()) {
            root = root.value(QLatin1StringView("preferences")).toObject();
        } else if (root.value(QLatin1StringView("data")).isObject()) {
            root = root.value(QLatin1StringView("data")).toObject();
        }
    }
    return root;
}

[[nodiscard]] QString onboardingCompletedAt(const QJsonObject& preferences)
{
    const QJsonValue completed =
        preferences.value(QLatin1StringView("onboarding")).toObject().value(QLatin1StringView("completedAt"));
    return completed.isString() ? completed.toString() : QString();
}

[[nodiscard]] std::optional<account::ContentLanguages> savedLanguages(const QJsonObject& preferences)
{
    const QJsonValue value = preferences.value(QLatin1StringView("contentLanguages"));
    if (!value.isObject()) {
        return std::nullopt;
    }
    account::ContentLanguages saved;
    saved.mode = value.toObject().value(QLatin1StringView("mode")).toString();
    for (const QJsonValue& code : value.toObject().value(QLatin1StringView("languages")).toArray()) {
        if (code.isString()) {
            saved.languages.append(code.toString());
        }
    }
    return saved;
}

} // namespace

WelcomeViewModel::WelcomeViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_client(parts.client)
    , m_session(parts.session)
    , m_sync(parts.sync)
    , m_accounts(parts.accounts)
    , m_settings(parts.settings)
    , m_catalogService(parts.catalog)
    , m_regionName(std::move(parts.regionName))
{
    // SEARCH_DELAY_MS: the search waits for typing to pause.
    m_searchDelay.setSingleShot(true);
    m_searchDelay.setInterval(350);
    connect(&m_searchDelay, &QTimer::timeout, this, &WelcomeViewModel::runSearch);
    connect(&m_sync, &account::MusicSyncService::libraryChanged, this, &WelcomeViewModel::checkAfterSync);
    // Opened while the stored session was still being checked: the languages
    // load once it is.
    connect(&m_session, &account::AccountSessionService::stateChanged, this, [this]() {
        if (m_visible && m_step == QLatin1StringView("languages") && m_catalog.isEmpty() && !m_busy
            && m_session.captureOperation()) {
            m_status.clear();
            loadLanguages();
        }
    });
}

QString WelcomeViewModel::region() const
{
    return m_settings.string(core::settings::keys::CatalogStorefront, QString(account::DefaultCatalogStorefront));
}

void WelcomeViewModel::setStatus(const QString& status)
{
    m_status = status;
    emit changed();
}

void WelcomeViewModel::checkAfterSync()
{
    const std::optional<account::AccountOperationContext> context = m_session.captureOperation();
    if (!context || m_visible || m_checkedOwners.contains(context->ownerId)) {
        return;
    }
    m_checkedOwners.insert(context->ownerId);
    // Music in the account means the listener is not new.
    bool hasMusic = false;
    if (const std::optional<core::db::AccountLibrarySnapshot> library = m_accounts.loadAccountLibrary(context->ownerId)) {
        hasMusic = std::any_of(library->tracks.cbegin(), library->tracks.cend(),
                               [](const core::db::AccountTrackSnapshot& entry) {
                                   return entry.track.liked || entry.playCount > 0;
                               })
            || std::any_of(library->playlists.cbegin(), library->playlists.cend(),
                           [](const core::db::AccountPlaylistSnapshot& playlist) {
                               return !playlist.remoteTrackIds.isEmpty();
                           });
    }
    if (hasMusic) {
        return;
    }
    m_client.preferencesDocument(context->bearerSession)
        .then(this, [this, context = *context](const core::Result<QByteArray>& answer) {
            if (!answer || !m_session.isCurrent(context)) {
                return; // checked again after the next launch's sync
            }
            if (account::needsWelcome(false, onboardingCompletedAt(preferencesObject(answer.value())))) {
                open(true);
            }
        });
}

void WelcomeViewModel::open(bool firstRun)
{
    m_mode = firstRun ? account::WelcomeMode::FirstRun : account::WelcomeMode::Tune;
    m_step = QStringLiteral("languages");
    m_picked.clear();
    m_pickOrder.clear();
    m_followIds.clear();
    m_pending.clear();
    m_grid.clear();
    m_languageQuery.clear();
    m_artistQuery.clear();
    m_searchStatus = QStringLiteral("idle");
    m_searchResults.clear();
    m_languagesFailed = false;
    m_saving = false;
    m_waiver = account::ArtistWaiver::None;
    m_status.clear();
    if (!m_visible) {
        m_visible = true;
        emit visibleChanged();
    }
    emit changed();
    loadLanguages();
}

void WelcomeViewModel::loadLanguages()
{
    const std::optional<account::AccountOperationContext> context = m_session.captureOperation();
    if (!context) {
        setStatus(tr("Sign in to choose your languages."));
        return;
    }
    const core::RequestScope::Token token = m_loads.next();
    m_busy = true;
    emit changed();
    const QString bearer = context->bearerSession;
    m_client.languageCatalog(bearer).then(this, [this, token, bearer](const core::Result<QByteArray>& catalog) {
        if (!m_loads.isCurrent(token)) {
            return;
        }
        if (!catalog) {
            m_busy = false;
            m_languagesFailed = true;
            emit changed();
            return;
        }
        m_languagesFailed = false;
        const QJsonObject body = QJsonDocument::fromJson(catalog.value()).object();
        m_catalog = account::normalizeLanguageCatalog(body.value(QLatin1StringView("languages")));
        const std::optional<account::RegionLanguageTable> regions =
            account::normalizeRegionLanguages(body.value(QLatin1StringView("regions")), m_catalog);
        m_regions = regions;
        m_client.preferencesDocument(bearer).then(this, [this, token, regions](const core::Result<QByteArray>& saved) {
            if (!m_loads.isCurrent(token)) {
                return;
            }
            m_busy = false;
            const std::optional<account::ContentLanguages> previous =
                saved ? savedLanguages(preferencesObject(saved.value())) : std::nullopt;
            m_selectedLanguages = account::initialLanguages(previous, region(), regions);
            emit changed();
        });
    });
}

void WelcomeViewModel::loadArtists()
{
    const std::optional<account::AccountOperationContext> context = m_session.captureOperation();
    if (!context) {
        return;
    }
    const core::RequestScope::Token token = m_loads.next();
    m_busy = true;
    emit changed();
    m_client.suggestedArtists(context->bearerSession, region())
        .then(this, [this, token](const core::Result<QByteArray>& answer) {
            if (!m_loads.isCurrent(token)) {
                return;
            }
            m_busy = false;
            const QList<account::WelcomeArtist> suggested =
                answer ? account::parseSuggestedArtists(answer.value()) : QList<account::WelcomeArtist>();
            QList<account::WelcomeArtist> followed;
            for (const account::WelcomeArtist& artist : suggested) {
                if (!artist.followedId.isEmpty()) {
                    followed.append(artist);
                }
            }
            m_grid = account::withFollowedFirst(suggested, followed);
            for (const account::WelcomeArtist& artist : followed) {
                m_picked.insert(account::artistItemKey(artist));
                m_pickOrder.append(account::artistItemKey(artist));
            }
            if (m_grid.isEmpty()) {
                // Nobody is held back by suggestions that did not come.
                m_waiver = account::ArtistWaiver::SuggestionsUnavailable;
                m_status = tr("Suggestions are not available right now. You can continue.");
            }
            emit changed();
        });
}

void WelcomeViewModel::reloadLanguages()
{
    m_languagesFailed = false;
    loadLanguages();
}

QString WelcomeViewModel::eyebrow() const
{
    const bool tune = m_mode == account::WelcomeMode::Tune;
    if (m_step == QLatin1StringView("languages")) {
        return tune ? tr("Tune your recommendations") : tr("Welcome to Last Music");
    }
    return tune ? tr("Tune your recommendations") : tr("Almost there");
}

QString WelcomeViewModel::title() const
{
    if (m_step == QLatin1StringView("languages")) {
        return tr("What do you listen to?");
    }
    return m_mode == account::WelcomeMode::Tune ? tr("Pick artists you like")
                                                : tr("Pick %1 or more artists you like").arg(account::MinWelcomeArtists);
}

QString WelcomeViewModel::subtitle() const
{
    if (m_step == QLatin1StringView("languages")) {
        return tr("Pick the languages you want in your recommendations. Search, charts and your library always show "
                  "everything.");
    }
    return tr("Each pick is followed for you and brings in more artists like it. Your Home is built around them.");
}

QString WelcomeViewModel::hint() const
{
    if (!m_status.isEmpty()) {
        return m_status;
    }
    if (m_step == QLatin1StringView("languages")) {
        const qsizetype count = m_selectedLanguages.size();
        if (count == 0) {
            return tr("Pick at least one language");
        }
        if (count >= account::MaxContentLanguages) {
            return tr("%1 chosen, the most you can pick").arg(count);
        }
        return count == 1 ? tr("1 language chosen") : tr("%1 languages chosen").arg(count);
    }
    const int count = static_cast<int>(m_picked.size());
    const int still = missing();
    if (still > 0) {
        return count == 0 ? tr("Pick %1 artists to continue").arg(still) : tr("%1 picked, %2 more to go").arg(count).arg(still);
    }
    if (count == 0) {
        return tr("No artists picked");
    }
    return count == 1 ? tr("1 artist picked") : tr("%1 artists picked").arg(count);
}

QString WelcomeViewModel::nextLabel() const
{
    if (m_step == QLatin1StringView("languages")) {
        return m_saving ? tr("Saving…") : tr("Continue");
    }
    if (m_saving) {
        return tr("Finishing…");
    }
    return m_mode == account::WelcomeMode::Tune ? tr("Done") : tr("Start listening");
}

QString WelcomeViewModel::notice() const
{
    if (m_mode == account::WelcomeMode::Tune || m_step != QLatin1StringView("artists")) {
        return {};
    }
    switch (m_waiver) {
    case account::ArtistWaiver::FollowNotAllowed:
        return tr("Your plan does not include following artists, so you can continue without picking any.");
    case account::ArtistWaiver::SuggestionsUnavailable:
        return tr("Artist suggestions are not available right now. Search for artists you like, or continue and "
                  "follow some later.");
    case account::ArtistWaiver::None:
        break;
    }
    return {};
}

QVariantList WelcomeViewModel::languageGroups() const
{
    const QString code = region();
    const QString name = m_regionName ? m_regionName() : QString();
    const QList<core::catalog::LanguageGroup> groups = core::catalog::filterLanguageGroups(
        core::catalog::groupLanguageCatalog(m_catalog, core::catalog::LanguageRegion{code, name.isEmpty() ? code : name},
                                            m_regions),
        m_languageQuery);
    QVariantList list;
    for (const core::catalog::LanguageGroup& group : groups) {
        QVariantList languages;
        for (const account::LanguageInfo& info : group.languages) {
            languages.append(QVariantMap{{QStringLiteral("code"), info.code},
                                         {QStringLiteral("name"), info.name},
                                         {QStringLiteral("nativeName"), info.nativeName == info.name ? QString() : info.nativeName},
                                         {QStringLiteral("selected"), m_selectedLanguages.contains(info.code)}});
        }
        list.append(QVariantMap{{QStringLiteral("id"), group.id},
                                {QStringLiteral("label"), group.label},
                                {QStringLiteral("languages"), languages}});
    }
    return list;
}

void WelcomeViewModel::setLanguageQuery(const QString& query)
{
    if (query == m_languageQuery) {
        return;
    }
    m_languageQuery = query;
    emit changed();
}

void WelcomeViewModel::setArtistQuery(const QString& query)
{
    if (query == m_artistQuery) {
        return;
    }
    m_artistQuery = query;
    m_searches.invalidate();
    m_searchDelay.stop();
    if (query.trimmed().isEmpty() || m_catalogService == nullptr) {
        m_searchStatus = QStringLiteral("idle");
        m_searchResults.clear();
    } else {
        m_searchStatus = QStringLiteral("loading");
        m_searchDelay.start();
    }
    emit changed();
}

void WelcomeViewModel::runSearch()
{
    if (m_catalogService == nullptr) {
        return;
    }
    const QString query = m_artistQuery.trimmed();
    const core::RequestScope::Token token = m_searches.next();
    m_catalogService->searchAll(query).then(this, [this, token](const core::Result<core::catalog::SearchAll>& found) {
        if (!m_searches.isCurrent(token)) {
            return;
        }
        m_searchResults.clear();
        if (!found) {
            m_searchStatus = QStringLiteral("error");
            emit changed();
            return;
        }
        QSet<QString> seen;
        for (const core::catalog::CatalogItem& item : found.value().artists) {
            if (!item.artistTarget) {
                continue;
            }
            account::WelcomeArtist artist;
            artist.title = item.title;
            artist.artworkUrl = item.artworkUrl;
            artist.identity = item.artistTarget->identity;
            const QString key = account::artistItemKey(artist);
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            m_searchResults.append(artist);
        }
        m_searchStatus = QStringLiteral("ready");
        emit changed();
    });
}

QVariantMap WelcomeViewModel::artistMap(const account::WelcomeArtist& artist) const
{
    core::Track cover;
    cover.artworkUrl = artist.artworkUrl;
    cover.sourceKind = QStringLiteral("remote");
    const QString key = account::artistItemKey(artist);
    return {{QStringLiteral("key"), key},
            {QStringLiteral("title"), artist.title},
            {QStringLiteral("artworkUrl"), CoverSource::forTrack(cover).displayUrl()},
            {QStringLiteral("picked"), m_picked.contains(key)},
            {QStringLiteral("pending"), m_pending.contains(key)}};
}

QVariantList WelcomeViewModel::searchResults() const
{
    QVariantList list;
    for (const account::WelcomeArtist& artist : m_searchResults) {
        list.append(artistMap(artist));
    }
    return list;
}

QVariantList WelcomeViewModel::trayFaces() const
{
    constexpr qsizetype TrayFaces = 5;
    QVariantList list;
    const qsizetype start = std::max<qsizetype>(0, m_pickOrder.size() - TrayFaces);
    for (qsizetype index = start; index < m_pickOrder.size(); ++index) {
        const QString& key = m_pickOrder.at(index);
        for (const account::WelcomeArtist& artist : m_grid) {
            if (account::artistItemKey(artist) == key) {
                list.append(artistMap(artist));
                break;
            }
        }
    }
    return list;
}

int WelcomeViewModel::trayMore() const
{
    return static_cast<int>(std::max<qsizetype>(0, m_pickOrder.size() - 5));
}

QVariantList WelcomeViewModel::languages() const
{
    QVariantList list;
    for (const account::LanguageInfo& language : m_catalog) {
        list.append(QVariantMap{{QStringLiteral("code"), language.code},
                                {QStringLiteral("name"), language.name},
                                {QStringLiteral("nativeName"), language.nativeName},
                                {QStringLiteral("selected"), m_selectedLanguages.contains(language.code)}});
    }
    return list;
}

QVariantList WelcomeViewModel::artists() const
{
    QVariantList list;
    for (const account::WelcomeArtist& artist : m_grid) {
        list.append(artistMap(artist));
    }
    return list;
}

bool WelcomeViewModel::canContinue() const
{
    if (m_busy) {
        return false;
    }
    if (m_step == QLatin1StringView("languages")) {
        return account::languagesReady(m_selectedLanguages.size(), m_mode);
    }
    return account::artistStepStatus(static_cast<int>(m_picked.size()), m_mode, m_waiver).ready && m_pending.isEmpty();
}

int WelcomeViewModel::missing() const
{
    return account::artistStepStatus(static_cast<int>(m_picked.size()), m_mode, m_waiver).missing;
}

void WelcomeViewModel::toggleLanguage(const QString& code)
{
    if (m_selectedLanguages.contains(code)) {
        m_selectedLanguages.removeAll(code);
    } else if (m_selectedLanguages.size() < account::MaxContentLanguages) {
        m_selectedLanguages.append(code);
    }
    emit changed();
}

void WelcomeViewModel::toggleArtist(const QString& key)
{
    const auto matches = [&key](const account::WelcomeArtist& artist) { return account::artistItemKey(artist) == key; };
    std::optional<account::WelcomeArtist> found;
    if (const auto inGrid = std::find_if(m_grid.cbegin(), m_grid.cend(), matches); inGrid != m_grid.cend()) {
        found = *inGrid;
    } else if (const auto inSearch = std::find_if(m_searchResults.cbegin(), m_searchResults.cend(), matches);
               inSearch != m_searchResults.cend()) {
        found = *inSearch;
    }
    const std::optional<account::AccountOperationContext> context = m_session.captureOperation();
    // One request per artist at a time.
    if (!found || !found->identity || m_pending.contains(key) || !context) {
        return;
    }
    const bool pick = !m_picked.contains(key);
    // Shown at once; put back if the service refuses.
    if (pick) {
        m_picked.insert(key);
        m_pickOrder.append(key);
        // A pick made from the search joins the grid, so it stays once the
        // search is cleared.
        if (std::none_of(m_grid.cbegin(), m_grid.cend(), matches)) {
            m_grid.prepend(*found);
        }
    } else {
        m_picked.remove(key);
        m_pickOrder.removeAll(key);
    }
    m_pending.insert(key);
    emit changed();

    // Undoing a follow made here needs the id the service answered with.
    const QString followedId = m_followIds.value(key, found->followedId);
    if (pick) {
        m_client.followArtistByIdentity(context->bearerSession, *found->identity, found->artworkUrl)
            .then(this, [this, key](const core::Result<QString>& result) {
                if (result && !result.value().isEmpty()) {
                    m_followIds.insert(key, result.value());
                }
                settleFollow(key, true, result ? core::Result<void>::success() : core::Result<void>(result.failure()));
            });
        return;
    }
    QFuture<core::Result<void>> change = followedId.isEmpty()
        ? QtFuture::makeReadyValueFuture(core::Result<void>::success())
        : m_client.setArtistFollowed(context->bearerSession, followedId, false);
    change.then(this, [this, key](const core::Result<void>& result) { settleFollow(key, false, result); });
}

void WelcomeViewModel::settleFollow(const QString& key, bool pick, const core::Result<void>& result)
{
    m_pending.remove(key);
    if (!pick && result) {
        m_followIds.remove(key);
    }
    if (!result) {
        if (pick) {
            m_picked.remove(key);
            m_pickOrder.removeAll(key);
        } else {
            m_picked.insert(key);
            m_pickOrder.append(key);
        }
        if (result.failure().httpStatus == 403) {
            // Following is not allowed for this account: the minimum cannot
            // be met, so it is waived.
            m_waiver = account::ArtistWaiver::FollowNotAllowed;
        }
        m_status = tr("Could not update artist follow state.");
    }
    emit changed();
}

void WelcomeViewModel::next()
{
    if (!canContinue()) {
        return;
    }
    const std::optional<account::AccountOperationContext> context = m_session.captureOperation();
    if (m_step == QLatin1StringView("languages")) {
        if (context) {
            // The region and languages are the account's from now on.
            const QJsonObject body{{QStringLiteral("storefront"), region()},
                                   {QStringLiteral("contentLanguages"),
                                    QJsonObject{{QStringLiteral("mode"), QStringLiteral("custom")},
                                                {QStringLiteral("languages"),
                                                 QJsonArray::fromStringList(m_selectedLanguages)}}}};
            (void)m_client.savePreferencesDocument(context->bearerSession,
                                                   QJsonDocument(body).toJson(QJsonDocument::Compact));
        }
        m_step = QStringLiteral("artists");
        m_status.clear();
        emit changed();
        loadArtists();
        return;
    }
    finish();
}

void WelcomeViewModel::back()
{
    if (m_step == QLatin1StringView("artists")) {
        m_step = QStringLiteral("languages");
        emit changed();
    }
}

void WelcomeViewModel::finish()
{
    const std::optional<account::AccountOperationContext> context = m_session.captureOperation();
    if (context && m_mode == account::WelcomeMode::FirstRun) {
        const QJsonObject body{{QStringLiteral("onboarding"),
                                QJsonObject{{QStringLiteral("completedAt"),
                                             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}}};
        (void)m_client.savePreferencesDocument(context->bearerSession, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    m_loads.invalidate();
    m_visible = false;
    emit visibleChanged();
    // Follows and languages shape what the library and Home show.
    (void)m_sync.requestSync(account::MusicSyncService::Lane::All);
}

void WelcomeViewModel::close()
{
    if (!closable()) {
        return;
    }
    m_loads.invalidate();
    m_visible = false;
    emit visibleChanged();
}

} // namespace lmp::ui
