#include "core/config/PrivateConfig.h"

#include "PrivateConfigValues.h"

namespace lmp::core {

namespace {

// "a, b,,c" as {"a", "b", "c"}.
QStringList commaList(const char16_t* text)
{
    QStringList values;
    for (const QString& part : QString::fromUtf16(text).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            values.append(trimmed);
        }
    }
    return values;
}

} // namespace

PrivateConfig PrivateConfig::fromBuild()
{
    const PrivateConfig compiled{
        .accountApiOrigin = QString::fromUtf16(generated::kAccountApiOrigin),
        .accountFrontendOrigin = QString::fromUtf16(generated::kAccountFrontendOrigin),
        .accountMediaOrigin = QString::fromUtf16(generated::kAccountMediaOrigin),
        .discordClientId = QString::fromUtf16(generated::kDiscordClientId),
        .sourceTagCatalog = QString::fromUtf16(generated::kSourceTagCatalog),
        .sourceTagsStream = commaList(generated::kSourceTagsStream),
        .sourceLabels = commaList(generated::kSourceLabels),
        .externalPlaylistIdPrefix = QString::fromUtf16(generated::kExternalPlaylistIdPrefix),
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
