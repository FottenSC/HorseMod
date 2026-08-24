#pragma once

#include <cstdint>

#include "Types.hpp"

namespace Horse::Deterministic
{
struct FloatingPointEnvironment
{
    std::uint16_t x87_control{};
    std::uint16_t x87_status{};
    std::uint32_t mxcsr{};

    friend bool operator==(
        const FloatingPointEnvironment&,
        const FloatingPointEnvironment&) = default;
};

[[nodiscard]] FloatingPointEnvironment CaptureFloatingPointEnvironment() noexcept;
[[nodiscard]] bool FloatingPointControlMatches(
    const FloatingPointEnvironment& left,
    const FloatingPointEnvironment& right) noexcept;
[[nodiscard]] bool FloatingPointStatusMatches(
    const FloatingPointEnvironment& left,
    const FloatingPointEnvironment& right) noexcept;
[[nodiscard]] bool FloatingPointX87StatusMatches(
    const FloatingPointEnvironment& left,
    const FloatingPointEnvironment& right) noexcept;
[[nodiscard]] bool FloatingPointMxcsrStatusMatches(
    const FloatingPointEnvironment& left,
    const FloatingPointEnvironment& right) noexcept;

class ScopedFloatingPointEnvironment
{
public:
    ScopedFloatingPointEnvironment() noexcept;
    ~ScopedFloatingPointEnvironment();

    ScopedFloatingPointEnvironment(const ScopedFloatingPointEnvironment&) = delete;
    ScopedFloatingPointEnvironment& operator=(
        const ScopedFloatingPointEnvironment&) = delete;

    [[nodiscard]] Status Finish() noexcept;

private:
    FloatingPointEnvironment before_{};
    bool finished_{};
};
}
