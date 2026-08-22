#include "pch.h"
#include "Backend/UserDataOperationGate.h"

#include <utility>

namespace LastMusicPlayer::Backend
{
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
        if (this != &other)
        {
            Release();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    UserDataOperationGate::Lease::~Lease()
    {
        Release();
    }

    UserDataOperationGate::Lease::operator bool() const noexcept
    {
        return static_cast<bool>(m_state);
    }

    void UserDataOperationGate::Lease::Release() noexcept
    {
        auto state = std::move(m_state);
        if (!state)
        {
            return;
        }

        {
            std::lock_guard lock{ state->Mutex };
            if (state->Active > 0)
            {
                --state->Active;
            }
        }
        state->Idle.notify_all();
    }

    UserDataOperationGate::UserDataOperationGate()
        : m_state(std::make_shared<SharedState>())
    {
    }

    std::optional<UserDataOperationGate::Lease> UserDataOperationGate::TryEnter()
    {
        std::lock_guard lock{ m_state->Mutex };
        if (!m_state->Accepting)
        {
            return std::nullopt;
        }

        ++m_state->Active;
        return Lease{ m_state };
    }

    bool UserDataOperationGate::CloseAdmissions()
    {
        std::lock_guard lock{ m_state->Mutex };
        if (!m_state->Accepting)
        {
            return false;
        }

        m_state->Accepting = false;
        ++m_state->Generation;
        return true;
    }

    bool UserDataOperationGate::WaitForIdle(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock{ m_state->Mutex };
        return m_state->Idle.wait_for(lock, timeout, [this]
        {
            return m_state->Active == 0;
        });
    }

    void UserDataOperationGate::Reopen()
    {
        {
            std::lock_guard lock{ m_state->Mutex };
            m_state->Accepting = true;
        }
        m_state->Idle.notify_all();
    }

    bool UserDataOperationGate::IsAccepting() const noexcept
    {
        std::lock_guard lock{ m_state->Mutex };
        return m_state->Accepting;
    }

    std::uint64_t UserDataOperationGate::Generation() const noexcept
    {
        std::lock_guard lock{ m_state->Mutex };
        return m_state->Generation;
    }

    bool UserDataOperationGate::IsCurrent(std::uint64_t generation) const noexcept
    {
        std::lock_guard lock{ m_state->Mutex };
        return generation == m_state->Generation;
    }
}
