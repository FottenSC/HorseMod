#pragma once

#include <cstdint>

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
}
