#include "ui/settings/RegionLanguagesViewModel.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/catalog/CatalogClient.h"
#include "core/catalog/CatalogService.h"
#include "core/catalog/ContentLanguageController.h"
#include "core/net/UserMessages.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "ui/toasts/ToastQueue.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QVariantMap>

#include <algorithm>

namespace lmp::ui {

namespace {

namespace catalog = core::catalog;

// The preferences answer's own object, under "preferences" when wrapped.
[[nodiscard]] QJsonObject preferencesObject(const QByteArray& body)
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    if (root.value(QLatin1StringView("preferences")).isObject()) {
        root = root.value(QLatin1StringView("preferences")).toObject();
    }
    return root;
}

[[nodiscard]] QFuture<core::Result<catalog::LanguageSetting>> settingFrom(QFuture<core::Result<QByteArray>> answer,
                                                                          std::optional<catalog::LanguageSetting> sent)
{
    return answer.then([sent](const core::Result<QByteArray>& body) -> core::Result<catalog::LanguageSetting> {
        if (!body) {
            return body.failure();
        }
        const QJsonObject preferences = preferencesObject(body.value());
        const QJsonValue value = preferences.value(QLatin1StringView("contentLanguages"));
        // A save answered without the setting keeps what was sent.
        if (!value.isObject() && sent) {
            return *sent;
        }
        return catalog::normalizeLanguageSetting(value);
    });
}

} // namespace

RegionLanguagesViewModel::RegionLanguagesViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_catalogService(parts.catalog)
    , m_client(parts.client)
    , m_accountClient(parts.accountClient)
    , m_session(parts.session)
    , m_settings(parts.settings)
    , m_toasts(parts.toasts)
{
    catalog::ContentLanguageController::Dependencies deps;
    deps.read = [this]() { return m_setting; };
    deps.write = [this](const std::optional<catalog::LanguageSetting>& value) { m_setting = value; };
    deps.save = [this](const catalog::LanguageSetting& payload) -> QFuture<core::Result<catalog::LanguageSetting>> {
        const std::optional<core::account::AccountOperationContext> context = m_session.captureOperation();
        if (!context) {
            return QtFuture::makeReadyValueFuture(core::Result<catalog::LanguageSetting>(
                core::Failure::of(core::FailureKind::Unauthorized, QStringLiteral("Sign in to save your languages."))));
        }
        return settingFrom(m_client.saveLanguages(context->bearerSession, catalog::languageSettingJson(payload)), payload);
    };
    deps.infer = [this]() -> QFuture<core::Result<catalog::LanguageSetting>> {
        const std::optional<core::account::AccountOperationContext> context = m_session.captureOperation();
        if (!context) {
            return QtFuture::makeReadyValueFuture(core::Result<catalog::LanguageSetting>(
                core::Failure::of(core::FailureKind::Unauthorized, QStringLiteral("Sign in first."))));
        }
        return settingFrom(m_client.inferLanguages(context->bearerSession), std::nullopt);
    };
    deps.fallbackLanguages = [this]() { return savedRegionLanguages(); };
    deps.saveFailed = [this](const core::Failure&) {
        (void)m_toasts.show(QString::fromUtf8(catalog::language_text::SaveFailed), true);
    };
    deps.changed = [this]() { emit languagesChanged(); };
    deps.clock = &parts.clock;
    m_controller = std::make_unique<catalog::ContentLanguageController>(std::move(deps));

    connect(&m_session, &core::account::AccountSessionService::ownerChanged, this,
            &RegionLanguagesViewModel::onOwnerChanged);
    connect(&m_settings, &core::SettingsStore::changed, this, [this](const QString& key) {
        if (key.isEmpty() || key == core::settings::keys::CatalogStorefront) {
            emit regionsChanged();
            emit languagesChanged();
        }
    });
}

RegionLanguagesViewModel::~RegionLanguagesViewModel() = default;

void RegionLanguagesViewModel::onOwnerChanged()
{
    m_controller->reset();
    ++m_epoch;
    m_setting.reset();
    m_settingLoaded = false;
    m_regions.clear();
    m_regionError.clear();
    m_regionsLoading = false;
    m_regionSearch.clear();
    m_editorOpen = false;
    m_search.clear();
    emit regionsChanged();
    emit languagesChanged();
}

void RegionLanguagesViewModel::refresh()
{
    if (!m_session.captureOperation()) {
        return;
    }
    // As the reference loads its regions: once, until a retry asks again.
    if (m_regions.isEmpty() && m_regionError.isEmpty()) {
        loadRegions();
    }
    if (!m_catalogLoaded) {
        loadCatalog();
    }
    loadSetting();
}

void RegionLanguagesViewModel::retryRegions()
{
    if (!m_session.captureOperation()) {
        return;
    }
    loadRegions();
}

void RegionLanguagesViewModel::loadRegions()
{
    if (m_regionsLoading) {
        return;
    }
    const std::uint64_t epoch = m_epoch;
    m_regionsLoading = true;
    m_regionError.clear();
    emit regionsChanged();
    m_catalogService.regions().then(this, [this, epoch](const core::Result<QList<catalog::Region>>& result) {
        if (epoch != m_epoch) {
            return;
        }
        m_regionsLoading = false;
        if (!result) {
            m_regionError = core::net::userMessageOr(result.failure(), tr("Could not load music regions."));
        } else {
            m_regionError.clear();
            m_regions = result.value();
        }
        emit regionsChanged();
        emit languagesChanged();
    });
}

void RegionLanguagesViewModel::loadCatalog()
{
    const std::optional<core::account::AccountOperationContext> context = m_session.captureOperation();
    if (!context) {
        return;
    }
    const std::uint64_t epoch = m_epoch;
    m_catalogLoading = true;
    m_catalogError.clear();
    emit languagesChanged();
    m_accountClient.languageCatalog(context->bearerSession)
        .then(this, [this, epoch](const core::Result<QByteArray>& answer) {
            if (epoch != m_epoch) {
                return;
            }
            m_catalogLoading = false;
            if (!answer) {
                m_catalogError = core::net::userMessageOr(answer.failure(), tr("Could not load the languages."));
                emit languagesChanged();
                return;
            }
            const QJsonObject body = QJsonDocument::fromJson(answer.value()).object();
            m_catalog = core::account::normalizeLanguageCatalog(body.value(QLatin1StringView("languages")));
            m_regionTable = core::account::normalizeRegionLanguages(body.value(QLatin1StringView("regions")), m_catalog);
            m_catalogLoaded = true;
            emit languagesChanged();
        });
}

void RegionLanguagesViewModel::loadSetting()
{
    const std::optional<core::account::AccountOperationContext> context = m_session.captureOperation();
    if (!context) {
        return;
    }
    const std::uint64_t epoch = m_epoch;
    m_accountClient.preferencesDocument(context->bearerSession)
        .then(this, [this, epoch](const core::Result<QByteArray>& answer) {
            if (epoch != m_epoch || !answer) {
                return;
            }
            const QJsonValue value = preferencesObject(answer.value()).value(QLatin1StringView("contentLanguages"));
            m_setting = value.isObject() ? std::optional(catalog::normalizeLanguageSetting(value)) : std::nullopt;
            m_settingLoaded = true;
            emit languagesChanged();
            (void)m_controller->autoDetectIfNeeded();
        });
}

void RegionLanguagesViewModel::retryCatalog()
{
    loadCatalog();
}

QString RegionLanguagesViewModel::regionLabel() const
{
    const int index = regionIndex();
    return index >= 0 ? m_regions.at(index).name : m_catalogService.region().toUpper();
}

void RegionLanguagesViewModel::setRegionSearch(const QString& search)
{
    if (m_regionSearch == search) {
        return;
    }
    m_regionSearch = search;
    emit regionsChanged();
}

QVariantList RegionLanguagesViewModel::regionChoices() const
{
    // The reference matches "<name> <code>", lower-cased, against the
    // trimmed, lower-cased search.
    const QString query = m_regionSearch.trimmed().toLower();
    const QString current = m_catalogService.region();
    QVariantList choices;
    for (const catalog::Region& region : m_regions) {
        const QString haystack = (region.name + QLatin1Char(' ') + region.code).toLower();
        if (!query.isEmpty() && !haystack.contains(query)) {
            continue;
        }
        choices.append(QVariantMap{{QStringLiteral("name"), region.name},
                                   {QStringLiteral("code"), region.code},
                                   {QStringLiteral("selected"), region.code == current}});
    }
    return choices;
}

int RegionLanguagesViewModel::regionIndex() const
{
    const QString code = m_catalogService.region();
    const auto found = std::find_if(m_regions.cbegin(), m_regions.cend(),
                                    [&code](const catalog::Region& region) { return region.code == code; });
    return found == m_regions.cend() ? -1 : static_cast<int>(found - m_regions.cbegin());
}

QString RegionLanguagesViewModel::regionName() const
{
    const int index = regionIndex();
    return index >= 0 ? m_regions.at(index).name : catalog::regionDisplayName(m_catalogService.region());
}

void RegionLanguagesViewModel::chooseRegion(const QString& code)
{
    // Only a region the service listed.
    const bool listed = std::any_of(m_regions.cbegin(), m_regions.cend(),
                                    [&code](const catalog::Region& region) { return region.code == code; });
    if (!listed || code == m_catalogService.region()) {
        return;
    }
    // Synced with the account as a preference; the catalogue clears itself.
    m_settings.setString(core::settings::keys::CatalogStorefront, code);
}

QStringList RegionLanguagesViewModel::savedRegionLanguages() const
{
    return core::account::suggestedLanguages(m_catalogService.region(), m_regionTable);
}

catalog::LanguageInfo RegionLanguagesViewModel::languageInfo(const QString& code) const
{
    const auto found = std::find_if(m_catalog.cbegin(), m_catalog.cend(),
                                    [&code](const catalog::LanguageInfo& info) { return info.code == code; });
    return found != m_catalog.cend() ? *found : catalog::LanguageInfo{code, code.toUpper(), {}};
}

QVariantList RegionLanguagesViewModel::languageList(const QStringList& codes, bool removable) const
{
    QVariantList list;
    for (const QString& code : codes) {
        const catalog::LanguageInfo info = languageInfo(code);
        list.append(QVariantMap{{QStringLiteral("code"), code},
                                {QStringLiteral("name"), info.name},
                                {QStringLiteral("nativeName"), info.nativeName == info.name ? QString() : info.nativeName},
                                {QStringLiteral("removable"), removable && codes.size() > 1}});
    }
    return list;
}

QString RegionLanguagesViewModel::mode() const
{
    return catalog::languageModeName(m_controller->current().mode);
}

QVariantList RegionLanguagesViewModel::detected() const
{
    return languageList(catalog::detectedLanguages(m_controller->current()), false);
}

QString RegionLanguagesViewModel::detectedAgo() const
{
    return catalog::detectedAgo(m_controller->current().inferredAt, QDateTime::currentMSecsSinceEpoch());
}

QVariantList RegionLanguagesViewModel::regionLanguages() const
{
    return languageList(savedRegionLanguages(), false);
}

QVariantList RegionLanguagesViewModel::chosen() const
{
    return languageList(m_controller->current().languages, true);
}

bool RegionLanguagesViewModel::detecting() const
{
    return m_controller->detecting();
}

QString RegionLanguagesViewModel::detectError() const
{
    return m_controller->detectError();
}

bool RegionLanguagesViewModel::editorOpen() const
{
    // An empty, unsaved Choose keeps the checklist open until a pick.
    const catalog::LanguageSetting value = m_controller->current();
    return value.mode == catalog::LanguageMode::Custom && (m_editorOpen || value.languages.isEmpty());
}

void RegionLanguagesViewModel::setSearch(const QString& search)
{
    if (search == m_search) {
        return;
    }
    m_search = search;
    emit languagesChanged();
}

QVariantList RegionLanguagesViewModel::groups() const
{
    const QStringList selected = m_controller->current().languages;
    const QString code = m_catalogService.region();
    const QList<catalog::LanguageGroup> grouped = catalog::filterLanguageGroups(
        catalog::groupLanguageCatalog(m_catalog, catalog::LanguageRegion{code, regionName()}, m_regionTable), m_search);
    QVariantList list;
    for (const catalog::LanguageGroup& group : grouped) {
        QVariantList languages;
        for (const catalog::LanguageInfo& info : group.languages) {
            languages.append(QVariantMap{
                {QStringLiteral("code"), info.code},
                {QStringLiteral("name"), info.name},
                {QStringLiteral("nativeName"), info.nativeName == info.name ? QString() : info.nativeName},
                {QStringLiteral("checked"), selected.contains(info.code)},
                {QStringLiteral("refused"),
                 catalog::toggleLanguage(selected, info.code).refusal != catalog::ToggleRefusal::None}});
        }
        list.append(QVariantMap{{QStringLiteral("id"), group.id},
                                {QStringLiteral("label"), group.label},
                                {QStringLiteral("languages"), languages}});
    }
    return list;
}

QString RegionLanguagesViewModel::hint() const
{
    const qsizetype count = m_controller->current().languages.size();
    if (count == 0) {
        return QString::fromUtf8(catalog::language_text::PickSome);
    }
    if (count >= core::account::MaxContentLanguages) {
        return QString::fromUtf8(catalog::language_text::UpToTen);
    }
    return count == 1 ? QString::fromUtf8(catalog::language_text::KeepOne) : QString();
}

QString RegionLanguagesViewModel::countText() const
{
    return tr("%1 of %2 chosen").arg(m_controller->current().languages.size()).arg(core::account::MaxContentLanguages);
}

void RegionLanguagesViewModel::selectMode(const QString& mode)
{
    const catalog::LanguageMode chosenMode = mode == QLatin1StringView("custom") ? catalog::LanguageMode::Custom
        : mode == QLatin1StringView("off")                                         ? catalog::LanguageMode::Off
                                                                                   : catalog::LanguageMode::Auto;
    if (chosenMode == catalog::LanguageMode::Custom && catalog::opensEditorOnChoose(m_controller->current())) {
        m_editorOpen = true;
    }
    m_controller->selectMode(chosenMode);
    emit languagesChanged();
}

void RegionLanguagesViewModel::toggleLanguage(const QString& code)
{
    m_controller->toggle(code);
}

void RegionLanguagesViewModel::detect()
{
    m_controller->detect();
}

void RegionLanguagesViewModel::toggleEditor()
{
    m_editorOpen = !m_editorOpen;
    if (!m_editorOpen) {
        m_search.clear();
    }
    emit languagesChanged();
}

} // namespace lmp::ui
