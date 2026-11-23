#pragma once

#include <QString>

#include "core/account/AccountSessionProtocol.h"
#include "core/account/AccountSessionState.h"
#include "core/mode/SourceMode.h"

namespace lmp::core::account {

// Shown wherever the app has no name of its own to use.
[[nodiscard]] QString defaultListenerName();

enum class ProfileIdentitySource {
    // The name typed into Settings. No picture, no plan.
    Manual,
    // The profile behind the active account session.
    Account,
};

struct ProfileIdentity {
    QString name;
    // As the profile gave it; check it (AccountUrlPolicy) before loading.
    QString avatarUrl;
    QString planLabel;
    ProfileIdentitySource source = ProfileIdentitySource::Manual;

    friend bool operator==(const ProfileIdentity&, const ProfileIdentity&) = default;
};

// Which identity the app presents (the sidebar, later the greeting). The
// account owns it only in Account mode with a session that is validated or
// offline; every other combination uses the typed name, which is never
// changed here, so leaving and re-entering Account mode brings it back as it
// was. A signed-in profile missing a part falls back part by part: display
// name, then username, then the typed name, then the default name.
[[nodiscard]] ProfileIdentity chooseProfileIdentity(mode::SourceMode mode, AccountSessionState state,
                                                    const AccountProfile& profile, const QString& manualName);

// White space trimmed from both ends, the C locale's (ASCII) white space as
// earlier versions trimmed it.
[[nodiscard]] QString trimProfileName(const QString& value);

// The plan as the profile shows it ("PREMIUM", "FREE"): a short word of
// letters, digits, spaces, "_" or "-" (1 to 24 characters, starting with a
// letter), with "_" and "-" runs read as one space, in capitals. Anything
// else, including no plan, gives nothing, so no arbitrary server text
// reaches the sidebar.
[[nodiscard]] QString accountTierLabel(const QString& plan);

} // namespace lmp::core::account
