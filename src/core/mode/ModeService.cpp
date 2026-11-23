#include "core/mode/ModeService.h"

#include "core/account/AccountSessionService.h"
#include "core/db/AccountRepository.h"
#include "core/library/UserDataOperationGate.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <QLoggingCategory>

#include <algorithm>
#include <optional>

namespace lmp::core::mode {

namespace {

Q_LOGGING_CATEGORY(lcMode, "lmp.mode")

namespace keys = settings::keys;
using account::AccountSessionState;

[[nodiscard]] QFuture<ApiKeyConnectOutcome> readyOutcome(ApiKeyConnectOutcome outcome)
{
    return QtFuture::makeReadyValueFuture(outcome);
}

[[nodiscard]] bool hasControlCharacters(const QString& value)
{
    return std::any_of(value.cbegin(), value.cend(), [](QChar character) {
        const char16_t unit = character.unicode();
        return unit < 0x20 || unit == 0x7f;
    });
}

} // namespace

ModeService::ModeService(const Dependencies& dependencies, QObject* parent)
    : QObject(parent)
    , m_settings(dependencies.settings)
    , m_secrets(dependencies.secrets)
    , m_session(dependencies.session)
    , m_provider(dependencies.provider)
    , m_accounts(dependencies.accounts)
    , m_operationGate(dependencies.operationGate)
{
    connect(&m_session, &account::AccountSessionService::stateChanged, this, &ModeService::onSessionStateChanged);
    connect(&m_session, &account::AccountSessionService::ownerChanged, this, &ModeService::onOwnerChanged);
}

account::LegacyKeyMigration ModeService::start()
{
    // The key has to be in the credential store before the first-run rule
    // looks for it there.
    const account::LegacyKeyMigration migration = m_secrets.migrateLegacyProviderApiKey();
    m_providerConfigured = readProviderConfigured();
    m_mode = resolveStoredMode();
    applyDatabaseContext();
    emit changed();
    return migration;
}

bool ModeService::readProviderConfigured()
{
    if (m_settings.string(keys::ProviderBaseUrl).isEmpty()) {
        return false;
    }
    const Result<std::optional<QString>> key = m_secrets.readProviderApiKey();
    return key && key.value() && !key.value()->isEmpty();
}

SourceMode ModeService::resolveStoredMode()
{
    const QString stored = m_settings.string(keys::RemoteAccessMode);
    if (!stored.isEmpty()) {
        return sourceModeFromStored(stored);
    }
    const SourceMode initial = m_providerConfigured ? SourceMode::ApiKey : SourceMode::LocalOnly;
    m_settings.setString(keys::RemoteAccessMode, QString(sourceModeName(initial)));
    return initial;
}

ModeConditions ModeService::conditions() const
{
    return ModeConditions{m_session.isAvailable(), m_session.state(), m_providerConfigured};
}

bool ModeService::isAvailable(SourceMode mode) const
{
    return isSourceModeAvailable(mode, conditions());
}

bool ModeService::isSelectable(SourceMode mode) const
{
    return isSourceModeSelectable(mode, conditions());
}

bool ModeService::hasRemoteAccess() const
{
    return m_mode != SourceMode::LocalOnly && isAvailable(m_mode);
}

bool ModeService::setMode(SourceMode mode)
{
    if (!isSelectable(mode)) {
        return false;
    }
    if (!commitMode(mode) && mode == SourceMode::Account) {
        // Without its library context Account mode would show another
        // owner's (or no) data, so it is not kept.
        commitMode(SourceMode::LocalOnly);
        return false;
    }
    return true;
}

bool ModeService::commitMode(SourceMode mode)
{
    m_mode = mode;
    m_settings.setString(keys::RemoteAccessMode, QString(sourceModeName(mode)));
    ++m_generation;
    m_connects.invalidate();
    const bool contextApplied = applyDatabaseContext();
    emit changed();
    return contextApplied;
}

bool ModeService::applyDatabaseContext()
{
    Result<void> applied = Result<void>::success();
    if (m_mode == SourceMode::Account) {
        // Until the session names its owner (it has none while signed out),
        // Account mode reads the local context, as at every start.
        const QString owner = m_session.profile().id;
        applied = owner.isEmpty() ? m_accounts.setRemoteLibraryContext(QString(LocalOnlyModeName))
                                  : m_accounts.setRemoteLibraryContext(QString(AccountModeName), owner);
    } else {
        applied = m_accounts.setRemoteLibraryContext(QString(sourceModeName(m_mode)));
    }
    if (!applied) {
        qCWarning(lcMode) << "Could not set the active library context:" << applied.failure().detail;
        return false;
    }
    return true;
}

QString ModeService::providerBaseUrl() const
{
    return m_settings.string(keys::ProviderBaseUrl);
}

QFuture<ApiKeyConnectOutcome> ModeService::connectApiKey(const QString& baseUrl, const QString& apiKey)
{
    const Token token = m_connects.next();
    if (baseUrl.isEmpty()) {
        return readyOutcome(ApiKeyConnectOutcome::MissingAddress);
    }
    if (apiKey.isEmpty()) {
        return readyOutcome(ApiKeyConnectOutcome::MissingKey);
    }
    const QString normalized = provider::normalizeProviderBaseUrl(baseUrl);
    if (!provider::isUsableProviderBaseUrl(normalized)) {
        return readyOutcome(ApiKeyConnectOutcome::InvalidAddress);
    }
    if (hasControlCharacters(apiKey)) {
        return readyOutcome(ApiKeyConnectOutcome::InvalidKey);
    }
    const provider::ProviderConnection connection{normalized, apiKey};
    return m_provider.fetchStatus(connection)
        .then(this, [this, token, baseUrl, apiKey](const Result<provider::ProviderStatus>& status) {
            return finishConnect(token, baseUrl, apiKey, status);
        });
}

ApiKeyConnectOutcome ModeService::finishConnect(Token token, const QString& baseUrl, const QString& apiKey,
                                                const Result<provider::ProviderStatus>& status)
{
    if (!m_connects.isCurrent(token)) {
        return ApiKeyConnectOutcome::Superseded;
    }
    if (!status) {
        return status.failure().kind == FailureKind::Unauthorized ? ApiKeyConnectOutcome::Unauthorized
                                                                   : ApiKeyConnectOutcome::Unavailable;
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_operationGate.tryEnter();
    if (!lease) {
        return ApiKeyConnectOutcome::Busy;
    }
    // The key counts as stored only once it reads back unchanged.
    if (!m_secrets.writeProviderApiKey(apiKey)) {
        return ApiKeyConnectOutcome::StorageFailed;
    }
    const Result<std::optional<QString>> stored = m_secrets.readProviderApiKey();
    if (!stored || !stored.value() || *stored.value() != apiKey) {
        return ApiKeyConnectOutcome::StorageFailed;
    }
    if (!m_settings.trySetString(keys::ProviderBaseUrl, baseUrl)) {
        return ApiKeyConnectOutcome::StorageFailed;
    }
    m_providerConfigured = true;
    m_providerCapabilities = status.value().capabilities;
    commitMode(SourceMode::ApiKey);
    return ApiKeyConnectOutcome::Connected;
}

ApiKeyDisconnectOutcome ModeService::disconnectApiKey()
{
    const std::optional<UserDataOperationGate::Lease> lease = m_operationGate.tryEnter();
    if (!lease) {
        return ApiKeyDisconnectOutcome::Busy;
    }
    if (!m_secrets.removeProviderApiKey()) {
        return ApiKeyDisconnectOutcome::KeyRemovalFailed;
    }
    const bool addressRemoved = m_settings.remove(keys::ProviderBaseUrl);
    m_providerConfigured = false;
    m_providerCapabilities.clear();
    if (m_mode == SourceMode::ApiKey) {
        commitMode(SourceMode::LocalOnly);
    } else {
        ++m_generation;
        m_connects.invalidate();
        emit changed();
    }
    return addressRemoved ? ApiKeyDisconnectOutcome::Disconnected : ApiKeyDisconnectOutcome::AddressCleanupFailed;
}

std::optional<provider::ProviderConnection> ModeService::providerConnection() const
{
    if (m_mode != SourceMode::ApiKey || !m_providerConfigured) {
        return std::nullopt;
    }
    const Result<std::optional<QString>> key = m_secrets.readProviderApiKey();
    if (!key || !key.value() || key.value()->isEmpty()) {
        return std::nullopt;
    }
    return provider::ProviderConnection{providerBaseUrl(), *key.value()};
}

QFuture<Result<void>> ModeService::refreshProviderStatus()
{
    if (m_mode != SourceMode::ApiKey || !m_providerConfigured) {
        return QtFuture::makeReadyValueFuture(
            Result<void>(Failure::of(FailureKind::NotAvailable, QStringLiteral("no provider is in use"))));
    }
    const Result<std::optional<QString>> key = m_secrets.readProviderApiKey();
    if (!key || !key.value()) {
        return QtFuture::makeReadyValueFuture(
            Result<void>(Failure::of(FailureKind::NotAvailable, QStringLiteral("provider key could not be read"))));
    }
    const RemoteScope scope = captureScope();
    const provider::ProviderConnection connection{providerBaseUrl(), *key.value()};
    return m_provider.fetchStatus(connection)
        .then(this, [this, scope](const Result<provider::ProviderStatus>& status) -> Result<void> {
            if (!isCurrent(scope)) {
                return Failure::of(FailureKind::Cancelled, QStringLiteral("provider changed while waiting"));
            }
            if (!status) {
                return status.failure();
            }
            if (m_providerCapabilities != status.value().capabilities) {
                m_providerCapabilities = status.value().capabilities;
                emit changed();
            }
            return Result<void>::success();
        });
}

void ModeService::setConnectivity(Connectivity connectivity)
{
    if (connectivity.online == m_connectivity.online && connectivity.metered == m_connectivity.metered) {
        return;
    }
    m_connectivity = connectivity;
    emit changed();
}

Capabilities ModeService::capabilities() const
{
    return Capabilities::compute(CapabilityInputs{m_mode, conditions(), m_providerCapabilities, m_connectivity});
}

bool ModeService::isCurrent(const RemoteScope& scope) const
{
    return scope.generation == m_generation && scope.mode == m_mode;
}

void ModeService::onSessionStateChanged()
{
    if (m_mode != SourceMode::Account) {
        // Only the availability of Account mode changed.
        emit changed();
        return;
    }
    if (m_session.state() == AccountSessionState::SignedOut) {
        // Signed out, rejected or a failed restore: Account mode has no
        // session left to serve it.
        commitMode(SourceMode::LocalOnly);
        return;
    }
    // Requests made for the previous session state must not land.
    ++m_generation;
    emit changed();
}

void ModeService::onOwnerChanged(const QString& ownerId)
{
    ++m_generation;
    if (m_mode == SourceMode::Account && ownerId.isEmpty()) {
        commitMode(SourceMode::LocalOnly);
        return;
    }
    applyDatabaseContext();
    emit changed();
}

} // namespace lmp::core::mode
