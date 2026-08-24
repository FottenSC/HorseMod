#include "FloatingPointEnvironment.hpp"

#include <array>
#include <cstring>
#include <intrin.h>

namespace Horse::Deterministic
{
namespace
{
constexpr std::uint32_t mxcsr_status_mask = 0x3f;

template <typename T>
T load(const std::array<std::byte, 512>& image, std::size_t offset) noexcept
{
    T value{};
    std::memcpy(&value, image.data() + offset, sizeof(value));
    return value;
}
}

FloatingPointEnvironment CaptureFloatingPointEnvironment() noexcept
{
    alignas(16) std::array<std::byte, 512> image{};
    _fxsave64(image.data());
    return {
        load<std::uint16_t>(image, 0),
        load<std::uint16_t>(image, 2),
        load<std::uint32_t>(image, 24),
    };
}

bool FloatingPointControlMatches(
    const FloatingPointEnvironment& left,
    const FloatingPointEnvironment& right) noexcept
{
    return left.x87_control == right.x87_control
        && (left.mxcsr & ~mxcsr_status_mask)
            == (right.mxcsr & ~mxcsr_status_mask);
}

bool FloatingPointStatusMatches(
    const FloatingPointEnvironment& left,
    const FloatingPointEnvironment& right) noexcept
{
    return left.x87_status == right.x87_status
        && (left.mxcsr & mxcsr_status_mask) == (right.mxcsr & mxcsr_status_mask);
}
}
