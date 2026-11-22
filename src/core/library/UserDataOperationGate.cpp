#include "core/library/UserDataOperationGate.h"

#include <utility>

namespace lmp::core {

UserDataOperationGate::Lease::Lease(std::shared_ptr<SharedState> state) noexcept
    : m_state(std::move(state))
{
}

UserDataOperationGate::Lease::Lease(Lease&& other) noexcept
    : m_state(std::move(other.m_state))
{
}

UserDataOperationGate::Lease& UserDataOperationGate::Lease::operator=(Lease&& other) noexcept
{
    if (this != &other) {
        release();
        m_state = std::move(other.m_state);
    }
    return *this;
}

UserDataOperationGate::Lease::~Lease()
{
    release();
}

UserDataOperationGate::Lease::operator bool() const noexcept
{
    return static_cast<bool>(m_state);
}

void UserDataOperationGate::Lease::release() noexcept
{
    std::shared_ptr<SharedState> state = std::move(m_state);
    if (!state) {
        return;
    }

    {
        const std::lock_guard lock{state->mutex};
        if (state->active > 0) {
            --state->active;
        }
    }
    // Notified after the lock is released so a woken waiter does not block on it.
    state->idle.notify_all();
}

UserDataOperationGate::UserDataOperationGate()
    : m_state(std::make_shared<SharedState>())
{
}

std::optional<UserDataOperationGate::Lease> UserDataOperationGate::tryEnter()
{
    const std::lock_guard lock{m_state->mutex};
    if (!m_state->accepting) {
        return std::nullopt;
    }

    ++m_state->active;
    return Lease{m_state};
}

bool UserDataOperationGate::closeAdmissions()
{
    const std::lock_guard lock{m_state->mutex};
    if (!m_state->accepting) {
        return false;
    }

    m_state->accepting = false;
    ++m_state->generation;
    return true;
}

bool UserDataOperationGate::waitForIdle(std::chrono::milliseconds timeout)
{
    std::unique_lock lock{m_state->mutex};
    SharedState* const state = m_state.get();
    return state->idle.wait_for(lock, timeout, [state]() { return state->active == 0; });
}

void UserDataOperationGate::reopen()
{
    {
        const std::lock_guard lock{m_state->mutex};
        m_state->accepting = true;
    }
    m_state->idle.notify_all();
}

bool UserDataOperationGate::isAccepting() const
{
    const std::lock_guard lock{m_state->mutex};
    return m_state->accepting;
}

std::uint64_t UserDataOperationGate::generation() const
{
    const std::lock_guard lock{m_state->mutex};
    return m_state->generation;
}

bool UserDataOperationGate::isCurrent(std::uint64_t candidate) const
{
    const std::lock_guard lock{m_state->mutex};
    return candidate == m_state->generation;
}

} // namespace lmp::core
