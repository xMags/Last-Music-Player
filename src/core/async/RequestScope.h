#pragma once

#include <QtGlobal>

namespace lmp::core {

// Generation tokens for work that a newer request can supersede (a search, a
// page load, a track load). Take a token when the work starts and check it
// before applying the result; anything started before the latest next() or
// invalidate() is stale.
//
// Not thread-safe by design: a scope lives on one thread (normally the thread
// of the object that owns it) and is only touched there, including by the
// continuations that check their token.
class RequestScope {
public:
    using Token = quint64;

    // Starts a new request; every earlier token stops being current.
    [[nodiscard]] Token next();

    [[nodiscard]] bool isCurrent(Token token) const { return token == m_current; }

    // Makes every outstanding token stale without starting a request.
    void invalidate();

private:
    Token m_current = 0;
};

} // namespace lmp::core
