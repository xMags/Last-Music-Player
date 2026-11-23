#include "ui/welcome/WelcomeViewModel.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/account/MusicSyncService.h"
#include "core/account/PortablePreferenceSettings.h"
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
{
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
    m_pending.clear();
    m_grid.clear();
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
            setStatus(tr("Languages could not be loaded. Try again shortly."));
            return;
        }
        const QJsonObject body = QJsonDocument::fromJson(catalog.value()).object();
        m_catalog = account::normalizeLanguageCatalog(body.value(QLatin1StringView("languages")));
        const std::optional<account::RegionLanguageTable> regions =
            account::normalizeRegionLanguages(body.value(QLatin1StringView("regions")), m_catalog);
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
            }
            if (m_grid.isEmpty()) {
                // Nobody is held back by suggestions that did not come.
                m_waiver = account::ArtistWaiver::SuggestionsUnavailable;
                m_status = tr("Suggestions are not available right now. You can continue.");
            }
            emit changed();
        });
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
        core::Track cover;
        cover.artworkUrl = artist.artworkUrl;
        cover.sourceKind = QStringLiteral("remote");
        const QString key = account::artistItemKey(artist);
        list.append(QVariantMap{{QStringLiteral("key"), key},
                                {QStringLiteral("title"), artist.title},
                                {QStringLiteral("artworkUrl"), CoverSource::forTrack(cover).displayUrl()},
                                {QStringLiteral("picked"), m_picked.contains(key)}});
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
    const auto found = std::find_if(m_grid.cbegin(), m_grid.cend(),
                                    [&key](const account::WelcomeArtist& artist) {
                                        return account::artistItemKey(artist) == key;
                                    });
    const std::optional<account::AccountOperationContext> context = m_session.captureOperation();
    if (found == m_grid.cend() || !found->identity || m_pending.contains(key) || !context) {
        return;
    }
    const bool pick = !m_picked.contains(key);
    // Shown at once; put back if the service refuses.
    if (pick) {
        m_picked.insert(key);
    } else {
        m_picked.remove(key);
    }
    m_pending.insert(key);
    emit changed();

    QFuture<core::Result<void>> change =
        pick ? m_client.followArtistByIdentity(context->bearerSession, *found->identity, found->artworkUrl)
             : (found->followedId.isEmpty()
                    ? QtFuture::makeReadyValueFuture(core::Result<void>::success())
                    : m_client.setArtistFollowed(context->bearerSession, found->followedId, false));
    change.then(this, [this, key, pick](const core::Result<void>& result) {
        m_pending.remove(key);
        if (!result) {
            if (pick) {
                m_picked.remove(key);
            } else {
                m_picked.insert(key);
            }
            if (result.failure().httpStatus == 403) {
                // Following is not allowed for this account: the minimum
                // cannot be met, so it is waived.
                m_waiver = account::ArtistWaiver::FollowNotAllowed;
            }
            m_status = tr("Could not update artist follow state.");
        }
        emit changed();
    });
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
