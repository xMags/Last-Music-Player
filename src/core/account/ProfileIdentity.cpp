#include "core/account/ProfileIdentity.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace lmp::core::account {

namespace {

bool isSignedIn(AccountSessionState state)
{
    // Offline counts: the cached profile is the one last signed in as, and
    // the account library is still what plays.
    return state == AccountSessionState::Validated || state == AccountSessionState::Offline;
}

bool isTrimmedSpace(QChar c)
{
    return c == QLatin1Char(' ') || (c >= QLatin1Char('\t') && c <= QLatin1Char('\r'));
}

bool isAsciiLetter(QChar c)
{
    return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'));
}

bool isPlanCharacter(QChar c)
{
    return isAsciiLetter(c) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char(' ')
        || c == QLatin1Char('_') || c == QLatin1Char('-');
}

} // namespace

QString defaultListenerName()
{
    return QCoreApplication::translate("ProfileIdentity", "Listener");
}

QString trimProfileName(const QString& value)
{
    qsizetype begin = 0;
    qsizetype end = value.size();
    while (begin < end && isTrimmedSpace(value.at(begin))) {
        ++begin;
    }
    while (end > begin && isTrimmedSpace(value.at(end - 1))) {
        --end;
    }
    return value.sliced(begin, end - begin);
}

ProfileIdentity chooseProfileIdentity(mode::SourceMode mode, AccountSessionState state, const AccountProfile& profile,
                                      const QString& manualName)
{
    const QString manual = trimProfileName(manualName);
    if (mode != mode::SourceMode::Account || !isSignedIn(state)) {
        return ProfileIdentity{
            .name = manual.isEmpty() ? defaultListenerName() : manual,
            .avatarUrl = {},
            .planLabel = {},
            .source = ProfileIdentitySource::Manual,
        };
    }

    // The service treats display name, username and plan as optional, so
    // each falls back on its own instead of dropping the whole identity.
    QString name = trimProfileName(profile.displayName);
    if (name.isEmpty()) {
        name = trimProfileName(profile.username);
    }
    if (name.isEmpty()) {
        name = manual;
    }
    return ProfileIdentity{
        .name = name.isEmpty() ? defaultListenerName() : name,
        .avatarUrl = profile.avatarUrl,
        .planLabel = trimProfileName(profile.planLabel),
        .source = ProfileIdentitySource::Account,
    };
}

QString accountTierLabel(const QString& plan)
{
    // trimmed() removes Unicode white space, as JavaScript's trim() does.
    const QString trimmed = plan.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 24 || !isAsciiLetter(trimmed.front())) {
        return {};
    }
    for (const QChar c : trimmed) {
        if (!isPlanCharacter(c)) {
            return {};
        }
    }
    static const QRegularExpression separators(QStringLiteral("[_-]+"));
    QString label = trimmed;
    label.replace(separators, QStringLiteral(" "));
    return label.toUpper();
}

} // namespace lmp::core::account
