#include "ui/shell/ProfileViewModel.h"

#include <QList>
#include <QRegularExpression>
#include <QStringList>

#include <utility>

#include "core/account/AccountSessionService.h"
#include "core/account/AccountSessionState.h"
#include "core/account/AccountSignOutCoordinator.h"
#include "core/account/AccountUrlPolicy.h"
#include "core/mode/ModeService.h"
#include "core/mode/SourceMode.h"
#include "core/net/InlineProfileImage.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"

namespace lmp::ui {

namespace {

const QString kSettingsItem = QStringLiteral("settings");
const QString kSignOutItem = QStringLiteral("signout");

} // namespace

ProfileViewModel::ProfileViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_settings(parts.settings)
    , m_mode(parts.mode)
    , m_session(parts.session)
    , m_signOut(parts.signOut)
    , m_navigator(parts.navigator)
{
    connect(&m_mode, &core::mode::ModeService::changed, this, &ProfileViewModel::refresh);
    connect(&m_session, &core::account::AccountSessionService::stateChanged, this, &ProfileViewModel::refresh);
    connect(&m_settings, &core::SettingsStore::changed, this, [this](const QString& key) {
        if (key.isEmpty() || key == core::settings::keys::UserDisplayName) {
            refresh();
        }
    });
    refresh();
}

void ProfileViewModel::refresh()
{
    core::account::ProfileIdentity identity = core::account::chooseProfileIdentity(
        m_mode.mode(), m_session.state(), m_session.profile(),
        m_settings.string(core::settings::keys::UserDisplayName));
    if (identity == m_identity) {
        return;
    }
    m_identity = std::move(identity);
    emit changed();
}

QString ProfileViewModel::plan() const
{
    if (m_identity.source != core::account::ProfileIdentitySource::Account) {
        return {};
    }
    return core::account::accountTierLabel(m_identity.planLabel);
}

QString ProfileViewModel::initials() const
{
    return initialsOf(m_identity.name);
}

QUrl ProfileViewModel::avatarUrl() const
{
    const QString& avatar = m_identity.avatarUrl;
    if (avatar.isEmpty()) {
        return {};
    }
    if (!core::account::isSafeAccountProfileUrl(avatar) && !core::net::isSafeInlineProfileImage(avatar)) {
        return {};
    }
    return QUrl(avatar);
}

bool ProfileViewModel::signedIn() const
{
    const core::account::AccountSessionState state = m_session.state();
    return state == core::account::AccountSessionState::Validated
        || state == core::account::AccountSessionState::Offline;
}

void ProfileViewModel::prepareMenu()
{
    QList<MenuItemListModel::Item> items{
        {.id = kSettingsItem, .label = tr("Settings"), .iconName = QStringLiteral("settings")},
    };
    if (signedIn()) {
        items.append({.id = kSignOutItem, .label = tr("Sign out"), .iconName = QStringLiteral("logout")});
    }
    m_menu.setItems(std::move(items));
}

void ProfileViewModel::triggerMenu(const QString& itemId)
{
    if (itemId == kSettingsItem) {
        m_navigator.openView(static_cast<int>(Route::Kind::Settings));
        return;
    }
    if (itemId == kSignOutItem && signedIn()) {
        // How it ended is reported where the sign-out is (a toast).
        [[maybe_unused]] const QFuture<core::account::AccountLogoutResult> ended = m_signOut.signOut();
    }
}

QString ProfileViewModel::initialsOf(const QString& name)
{
    static const QRegularExpression whiteSpace(QStringLiteral("\\s+"));
    const QStringList parts = name.trimmed().split(whiteSpace, Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return QStringLiteral("L");
    }
    QString letters;
    for (const QString& part : parts) {
        letters.append(part.front());
        if (letters.size() == 2) {
            break;
        }
    }
    return letters.toUpper();
}

} // namespace lmp::ui
