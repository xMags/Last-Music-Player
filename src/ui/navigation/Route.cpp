#include "ui/navigation/Route.h"

#include <utility>

namespace lmp::ui {

Route::Route(Kind kind, QMap<QString, QString> params)
    : m_kind(kind)
    , m_params(std::move(params))
{
}

bool Route::isMainView() const
{
    switch (m_kind) {
    case Kind::Home:
    case Kind::Browse:
    case Kind::Library:
    case Kind::Settings:
    case Kind::Offline:
        return true;
    default:
        return false;
    }
}

QString Route::param(const QString& name) const
{
    return m_params.value(name);
}

} // namespace lmp::ui
