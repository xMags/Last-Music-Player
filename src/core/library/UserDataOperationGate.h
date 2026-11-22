#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace lmp::core {

// Lets long-running work that can write user data coexist with the
// destructive "clean up everything" flow. Work holds a Lease while it runs;
// the clean-up closes admissions, waits until no lease is left, wipes, and
// reopens. Closing also bumps a generation number, so a continuation that
// captured the old generation can tell that its results must be dropped.
//
// Thread-safe. A lease is independent of threads: it may be taken on one
// thread and released on another, and it may outlive the gate (the state is
// shared between the gate and its leases).
class UserDataOperationGate final {
private:
    struct SharedState;

public:
    class Lease final {
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        ~Lease();

        [[nodiscard]] explicit operator bool() const noexcept;

    private:
        friend class UserDataOperationGate;
        explicit Lease(std::shared_ptr<SharedState> state) noexcept;
        void release() noexcept;

        std::shared_ptr<SharedState> m_state;
    };

    UserDataOperationGate();

    // A lease while admissions are open, otherwise nothing.
    [[nodiscard]] std::optional<Lease> tryEnter();
    // Stops new leases and starts a new generation. False when admissions
    // were already closed, so only one clean-up owns the gate at a time.
    [[nodiscard]] bool closeAdmissions();
    // True once no lease is held; false when `timeout` passed first.
    [[nodiscard]] bool waitForIdle(std::chrono::milliseconds timeout);
    // Opens admissions again. The generation stays the one closing started.
    void reopen();
    [[nodiscard]] bool isAccepting() const;
    [[nodiscard]] std::uint64_t generation() const;
    [[nodiscard]] bool isCurrent(std::uint64_t candidate) const;

private:
    struct SharedState {
        mutable std::mutex mutex;
        std::condition_variable idle;
        bool accepting = true;
        std::size_t active = 0;
        std::uint64_t generation = 1;
    };

    std::shared_ptr<SharedState> m_state;
};

} // namespace lmp::core
