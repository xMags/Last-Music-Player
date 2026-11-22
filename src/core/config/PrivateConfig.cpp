#include "core/config/PrivateConfig.h"

#include "PrivateConfigValues.h"

namespace lmp::core {

PrivateConfig PrivateConfig::fromBuild()
{
    const PrivateConfig compiled{
        .accountApiOrigin = QString::fromUtf16(generated::kAccountApiOrigin),
        .accountFrontendOrigin = QString::fromUtf16(generated::kAccountFrontendOrigin),
        .accountMediaOrigin = QString::fromUtf16(generated::kAccountMediaOrigin),
        .discordClientId = QString::fromUtf16(generated::kDiscordClientId),
    };
    return compiled.withDefaults();
}

PrivateConfig PrivateConfig::withDefaults() const
{
    PrivateConfig config = *this;
    if (config.accountMediaOrigin.isEmpty()) {
        config.accountMediaOrigin = config.accountApiOrigin;
    }
    return config;
}

bool PrivateConfig::hasAccountService() const
{
    return !accountApiOrigin.isEmpty();
}

} // namespace lmp::core
