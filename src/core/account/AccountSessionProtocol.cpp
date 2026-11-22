#include "core/account/AccountSessionProtocol.h"

#include "core/app/AppIdentity.h"
#include "core/net/InlineProfileImage.h"
#include "core/net/Json.h"
#include "core/text/CaseFold.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QUrl>

#include <algorithm>

namespace lmp::core::account {

namespace {

constexpr qsizetype MaxBearerCharacters = 16 * 1024;
constexpr qsizetype MaxOwnerIdCharacters = 256;
constexpr qsizetype MaxDisplayNameCharacters = 256;
constexpr qsizetype MaxUsernameCharacters = 128;
constexpr qsizetype MaxPlanCharacters = 128;

// Text of a string member; any other type counts as absent.
[[nodiscard]] QString stringMember(const QJsonObject& object, QLatin1StringView key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

[[nodiscard]] bool hasControlCharacter(const QString& value)
{
    return std::any_of(value.cbegin(), value.cend(), [](QChar character) {
        const char16_t unit = character.unicode();
        return unit < 0x20 || (unit >= 0x7f && unit <= 0x9f);
    });
}

[[nodiscard]] bool isTokenCharacter(char16_t unit)
{
    return (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || (unit >= u'0' && unit <= u'9')
        || unit == u'-' || unit == u'.' || unit == u'_' || unit == u'~' || unit == u'+' || unit == u'/';
}

// Token characters, then only '=' padding, with at least one token character.
[[nodiscard]] bool isValidBearerToken(const QString& value)
{
    bool sawData = false;
    bool sawPadding = false;
    for (const QChar character : value) {
        const char16_t unit = character.unicode();
        if (unit == u'=') {
            sawPadding = true;
            continue;
        }
        if (sawPadding || !isTokenCharacter(unit)) {
            return false;
        }
        sawData = true;
    }
    return sawData;
}

[[nodiscard]] QString boundedText(const QString& value, qsizetype maximumCharacters)
{
    return value.size() <= maximumCharacters && !hasControlCharacter(value) ? value : QString();
}

[[nodiscard]] bool isAcceptedAvatar(const QString& value, LoopbackHttp loopback)
{
    // The service sends either an address to fetch or the picture itself as
    // a data URI, so each form is checked by its own rule.
    return isSafeAccountProfileUrl(value, loopback) || net::isSafeInlineProfileImage(value);
}

} // namespace

QString buildAccountAuthorizeUrl(const QString& apiOrigin, const QString& redirectUri,
                                 const PkceTransaction& transaction)
{
    if (apiOrigin.isEmpty() || redirectUri.isEmpty() || transaction.state.isEmpty()
        || transaction.challenge.isEmpty()) {
        return {};
    }
    const auto encoded = [](const QString& value) {
        return QString::fromLatin1(QUrl::toPercentEncoding(value));
    };
    return apiOrigin + QStringLiteral("/v1/sso/authorize?client_id=") + AppIdentity::accountClientId()
        + QStringLiteral("&redirect_uri=") + encoded(redirectUri) + QStringLiteral("&state=")
        + encoded(transaction.state) + QStringLiteral("&code_challenge=") + encoded(transaction.challenge)
        + QStringLiteral("&code_challenge_method=S256");
}

QString parseAccountBearerSession(const QByteArray& payload)
{
    const Result<QJsonObject> root = json::parseObject(payload);
    if (!root) {
        return {};
    }
    const QJsonObject& object = root.value();
    if (object.contains(QLatin1StringView("token_type"))) {
        const QJsonValue tokenType = object.value(QLatin1StringView("token_type"));
        if (!tokenType.isString() || foldCaseAscii(tokenType.toString()) != QLatin1StringView("bearer")) {
            return {};
        }
    }
    const QString bearer = stringMember(object, QLatin1StringView("access_token"));
    if (bearer.isEmpty() || bearer.size() > MaxBearerCharacters || hasControlCharacter(bearer)
        || !isValidBearerToken(bearer)) {
        return {};
    }
    return bearer;
}

AccountProfile parseAccountProfile(const QByteArray& payload, LoopbackHttp loopback)
{
    const Result<QJsonObject> root = json::parseObject(payload);
    if (!root) {
        return {};
    }
    QJsonObject user = root.value();
    if (user.contains(QLatin1StringView("user"))) {
        const QJsonValue nested = user.value(QLatin1StringView("user"));
        if (!nested.isObject()) {
            return {};
        }
        user = nested.toObject();
    }

    AccountProfile profile;
    profile.id = boundedText(stringMember(user, QLatin1StringView("id")), MaxOwnerIdCharacters);
    if (profile.id.isEmpty()
        || std::any_of(profile.id.cbegin(), profile.id.cend(), [](QChar character) { return character.isSpace(); })) {
        return {};
    }
    profile.displayName = boundedText(stringMember(user, QLatin1StringView("displayName")), MaxDisplayNameCharacters);
    if (profile.displayName.isEmpty()) {
        profile.displayName = boundedText(stringMember(user, QLatin1StringView("fullName")), MaxDisplayNameCharacters);
    }
    profile.username = boundedText(stringMember(user, QLatin1StringView("username")), MaxUsernameCharacters);
    profile.planLabel = boundedText(stringMember(user, QLatin1StringView("plan")), MaxPlanCharacters);
    const QString avatar = stringMember(user, QLatin1StringView("avatarUrl"));
    if (isAcceptedAvatar(avatar, loopback)) {
        profile.avatarUrl = avatar;
    }
    return profile;
}

} // namespace lmp::core::account
