#include "core/config/PrivateConfig.h"

#include "PrivateConfigValues.h"

namespace lmp::core {

PrivateConfig PrivateConfig::fromBuild()
{
    return PrivateConfig{
        .accountApiOrigin = QString::fromUtf16(generated::kAccountApiOrigin),
        .accountFrontendOrigin = QString::fromUtf16(generated::kAccountFrontendOrigin),
        .accountMediaOrigin = QString::fromUtf16(generated::kAccountMediaOrigin),
        .discordClientId = QString::fromUtf16(generated::kDiscordClientId),
    };
}

bool PrivateConfig::hasAccountService() const
{
    return !accountApiOrigin.isEmpty()
        && !accountFrontendOrigin.isEmpty()
        && !accountMediaOrigin.isEmpty();
}

} // namespace lmp::core
