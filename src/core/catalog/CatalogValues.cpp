#include "core/catalog/CatalogValues.h"

#include <QJsonArray>
#include <QLatin1StringView>

#include <cmath>
#include <limits>

namespace lmp::core::catalog::catalog_values {

QString text(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1StringView(key));
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::isfinite(number) && number != 0) {
            return QString::number(number, 'g', 17);
        }
    }
    return {};
}

QString textOr(const QJsonObject& object, const char* key, const QString& fallback)
{
    QString value = text(object, key);
    return value.isEmpty() ? fallback : value;
}

QStringList strings(const QJsonValue& value)
{
    QStringList result;
    for (const QJsonValue& entry : value.toArray()) {
        if (entry.isString() && !entry.toString().isEmpty()) {
            result.append(entry.toString());
        }
    }
    return result;
}

int nonNegativeInteger(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return 0;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number != std::floor(number)
        || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return 0;
    }
    return static_cast<int>(number);
}

ResourceType itemType(const QJsonObject& object)
{
    const QString name = object.value(QLatin1StringView("resourceType")).toString();
    return name.endsWith(QLatin1Char('s')) ? ResourceType::Unknown : parseResourceType(name);
}

} // namespace lmp::core::catalog::catalog_values
