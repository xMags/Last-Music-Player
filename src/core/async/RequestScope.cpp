#include "core/async/RequestScope.h"

namespace lmp::core {

RequestScope::Token RequestScope::next()
{
    return ++m_current;
}

void RequestScope::invalidate()
{
    ++m_current;
}

} // namespace lmp::core
