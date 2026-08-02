#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace LastMusicPlayer::Backend
{
    // Coordinates long-running operations that can persist user data with the
    // destructive "Clean up everything" flow. Leases are thread-independent:
    // they may be acquired before a coroutine suspends and released on any
    // thread when its frame is destroyed.
    class UserDataOperationGate final
    {
    private:
        struct SharedState;

    public:
        class Lease final
        {
        public:
            Lease() = default;
            Lease(Lease const&) = delete;
            Lease& operator=(Lease const&) = delete;
            Lease(Lease&& other) noexcept;
            Lease& operator=(Lease&& other) noexcept;
            ~Lease();

            explicit operator bool() const noexcept;

        private:
            friend class UserDataOperationGate;
            explicit Lease(std::shared_ptr<SharedState> state) noexcept;
            void Release() noexcept;

            std::shared_ptr<SharedState> m_state;
        };

        UserDataOperationGate();

        [[nodiscard]] std::optional<Lease> TryEnter();
        [[nodiscard]] bool CloseAdmissions();
        [[nodiscard]] bool WaitForIdle(std::chrono::milliseconds timeout);
        void Reopen();
        [[nodiscard]] bool IsAccepting() const noexcept;
        [[nodiscard]] std::uint64_t Generation() const noexcept;
        [[nodiscard]] bool IsCurrent(std::uint64_t generation) const noexcept;

    private:
        struct SharedState
        {
            mutable std::mutex Mutex;
            std::condition_variable Idle;
            bool Accepting{ true };
            std::size_t Active{};
            std::uint64_t Generation{ 1 };
        };

        std::shared_ptr<SharedState> m_state;
    };
}
