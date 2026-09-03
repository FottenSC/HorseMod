#pragma once

#include <cstdint>
#include <string_view>

namespace Horse::Qualification
{
enum class ReplaySeekReadiness : std::uint8_t
{
    Ready,
    RangeUnavailable,
    RangeInvalid,
    RangeTooShort,
    PhaseUnavailable,
    PhaseInactive,
};

[[nodiscard]] constexpr ReplaySeekReadiness ClassifyReplaySeekReadiness(
    bool range_available, std::uint64_t first, std::uint64_t last,
    std::uint64_t required_span, bool phase_available,
    std::uint32_t round_state_frame) noexcept
{
    if (!range_available) return ReplaySeekReadiness::RangeUnavailable;
    if (first >= last) return ReplaySeekReadiness::RangeInvalid;
    if (last - first < required_span)
        return ReplaySeekReadiness::RangeTooShort;
    if (!phase_available) return ReplaySeekReadiness::PhaseUnavailable;
    if (round_state_frame == 0) return ReplaySeekReadiness::PhaseInactive;
    return ReplaySeekReadiness::Ready;
}

[[nodiscard]] constexpr std::string_view ReplaySeekReadinessName(
    ReplaySeekReadiness readiness) noexcept
{
    switch (readiness)
    {
    case ReplaySeekReadiness::Ready: return "ready";
    case ReplaySeekReadiness::RangeUnavailable: return "range_unavailable";
    case ReplaySeekReadiness::RangeInvalid: return "range_invalid";
    case ReplaySeekReadiness::RangeTooShort: return "range_too_short";
    case ReplaySeekReadiness::PhaseUnavailable: return "phase_unavailable";
    case ReplaySeekReadiness::PhaseInactive: return "phase_inactive";
    }
    return "unknown";
}
}
