#pragma once

#include <QString>

#include <type_traits>
#include <utility>
#include <variant>

namespace lmp::core {

// Why an operation failed. Text shown to the user is chosen from the kind;
// `Failure::detail` is for logs only.
enum class FailureKind {
    Network,
    Timeout,
    Unauthorized,
    Forbidden,
    NotFound,
    RateLimited,
    Server,
    InvalidResponse,
    Cancelled,
    Storage,
    NotAvailable,
    Unknown,
};

struct Failure {
    FailureKind kind = FailureKind::Unknown;
    // The HTTP status that caused it, or 0 when no response was involved.
    int httpStatus = 0;
    QString detail;

    [[nodiscard]] static Failure of(FailureKind kind, QString detail = {}, int httpStatus = 0)
    {
        return Failure{kind, httpStatus, std::move(detail)};
    }
};

// The outcome of an operation that can fail: a value of type T, or a Failure.
template <typename T>
class [[nodiscard]] Result {
    static_assert(!std::is_same_v<T, Failure>, "Result<Failure> is ambiguous");

public:
    // Implicit on purpose, so `return value;` and `return failure;` both read naturally.
    Result(T value) // NOLINT(google-explicit-constructor)
        : m_state(std::in_place_index<0>, std::move(value))
    {
    }

    Result(Failure failure) // NOLINT(google-explicit-constructor)
        : m_state(std::in_place_index<1>, std::move(failure))
    {
    }

    [[nodiscard]] bool isOk() const { return m_state.index() == 0; }
    explicit operator bool() const { return isOk(); }

    // Only valid when isOk().
    [[nodiscard]] const T& value() const& { return std::get<0>(m_state); }
    [[nodiscard]] T& value() & { return std::get<0>(m_state); }
    [[nodiscard]] T&& value() && { return std::get<0>(std::move(m_state)); }

    // Only valid when !isOk().
    [[nodiscard]] const Failure& failure() const { return std::get<1>(m_state); }

    [[nodiscard]] T valueOr(T fallback) const&
    {
        return isOk() ? value() : std::move(fallback);
    }

private:
    std::variant<T, Failure> m_state;
};

// The outcome of an operation that returns nothing when it succeeds.
template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;

    Result(Failure failure) // NOLINT(google-explicit-constructor)
        : m_failure(std::move(failure))
        , m_ok(false)
    {
    }

    [[nodiscard]] static Result success() { return {}; }

    [[nodiscard]] bool isOk() const { return m_ok; }
    explicit operator bool() const { return m_ok; }

    // Only valid when !isOk().
    [[nodiscard]] const Failure& failure() const { return m_failure; }

private:
    Failure m_failure;
    bool m_ok = true;
};

} // namespace lmp::core
