// ============================================================================
// Horse::RollbackFrameStamp
//
// Explicit uint32 frame validity and RFC-1982-style comparisons.  Every
// rollback window is constrained to less than 2^31 frames, so subtraction is
// unambiguous even across the uint32 wrap boundary.
// ============================================================================

#pragma once

#include <cstdint>
#include <limits>

namespace Horse
{
    struct RollbackFrameStamp
    {
        uint32_t value {0};
        bool valid {false};

        static constexpr RollbackFrameStamp From(uint32_t frame) noexcept
        {
            return RollbackFrameStamp {frame, true};
        }

        constexpr void clear() noexcept
        {
            value = 0;
            valid = false;
        }

        constexpr RollbackFrameStamp& operator=(uint32_t frame) noexcept
        {
            value = frame;
            valid = true;
            return *this;
        }

        constexpr uint32_t wire_value(
            uint32_t invalid_value = 0xFFFFFFFFu) const noexcept
        {
            return valid ? value : invalid_value;
        }

        constexpr operator uint32_t() const noexcept
        {
            return wire_value();
        }
    };

    static constexpr bool RollbackFrameIsAfter(
        uint32_t candidate,
        uint32_t reference) noexcept
    {
        const uint32_t distance = candidate - reference;
        return distance != 0 && distance < 0x80000000u;
    }

    static constexpr bool RollbackFrameIsBefore(
        uint32_t candidate,
        uint32_t reference) noexcept
    {
        return RollbackFrameIsAfter(reference, candidate);
    }

    static constexpr bool RollbackFrameAtOrAfter(
        uint32_t candidate,
        uint32_t reference) noexcept
    {
        return candidate == reference
            || RollbackFrameIsAfter(candidate, reference);
    }

    static constexpr uint32_t RollbackFrameDistance(
        uint32_t newer,
        uint32_t older) noexcept
    {
        return newer - older;
    }

    static constexpr bool RollbackFrameWithinPastWindow(
        uint32_t candidate,
        uint32_t current,
        uint32_t window) noexcept
    {
        return candidate == current
            || (RollbackFrameIsBefore(candidate, current)
                && RollbackFrameDistance(current, candidate) <= window);
    }

    // Matches Gekko's confirmed health-check horizon:
    // (current - input_prediction_window) - 1. The first observed frame keeps
    // startup from underflowing into an apparently valid pre-session frame.
    static constexpr bool RollbackTryGetConfirmedFrame(
        uint32_t current,
        uint32_t first_observed,
        uint32_t prediction_window,
        uint32_t& confirmed) noexcept
    {
        if (prediction_window >= 0x7FFFFFFFu) return false;
        const uint32_t horizon = prediction_window + 1u;
        const uint32_t elapsed = RollbackFrameDistance(
            current, first_observed);
        if (elapsed >= 0x80000000u || elapsed < horizon) return false;
        confirmed = current - horizon;
        return true;
    }

    // GekkoNet exposes frame numbers as a signed int. Horse's transport and
    // snapshot helpers remain uint32/wrap-aware, but a production Gekko
    // session must not claim that it can cross the signed boundary. Stop with
    // a generous guard band while Gekko's internal signed arithmetic is still
    // far from INT_MAX. The production rollback window is at most 60 frames;
    // this reserve is intentionally more than one thousand times larger.
    static constexpr uint32_t kRollbackGekkoSignedFrameSafetyReserve =
        0x00010000u;
    static constexpr uint32_t kRollbackGekkoSignedFrameExclusiveCeiling =
        static_cast<uint32_t>((std::numeric_limits<int32_t>::max)())
        - kRollbackGekkoSignedFrameSafetyReserve;
    static constexpr int32_t kRollbackGekkoBaselineFrame = -1;
    static constexpr uint32_t kRollbackGekkoBaselineFrameKey = 0xFFFFFFFFu;

    static_assert(kRollbackGekkoSignedFrameExclusiveCeiling > 60u);
    static_assert(
        kRollbackGekkoSignedFrameExclusiveCeiling
        < static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()));

    static constexpr bool RollbackGekkoFrameIsProductionSafe(
        int32_t frame) noexcept
    {
        return frame >= 0
            && static_cast<uint32_t>(frame)
                < kRollbackGekkoSignedFrameExclusiveCeiling;
    }

    static_assert(RollbackGekkoFrameIsProductionSafe(0));
    static_assert(RollbackGekkoFrameIsProductionSafe(static_cast<int32_t>(
        kRollbackGekkoSignedFrameExclusiveCeiling - 1u)));
    static_assert(!RollbackGekkoFrameIsProductionSafe(static_cast<int32_t>(
        kRollbackGekkoSignedFrameExclusiveCeiling)));
    static_assert(!RollbackGekkoFrameIsProductionSafe(-1));

    // Gekko's first UpdateSession emits Save(-1) for the pre-frame baseline.
    // Keep gameplay/transport frames uint32 while mapping that one signed
    // baseline to the wrap-adjacent key immediately preceding frame zero.
    static constexpr bool RollbackGekkoStateFrameToKey(
        int32_t frame,
        uint32_t& key) noexcept
    {
        if (frame == kRollbackGekkoBaselineFrame)
        {
            key = kRollbackGekkoBaselineFrameKey;
            return true;
        }
        if (!RollbackGekkoFrameIsProductionSafe(frame)) return false;
        key = static_cast<uint32_t>(frame);
        return true;
    }

    static_assert([]() constexpr {
        uint32_t key = 0;
        return RollbackGekkoStateFrameToKey(-1, key)
            && key == kRollbackGekkoBaselineFrameKey;
    }());

    // Check this before submitting local input or calling
    // gekko_update_session(). update_calls is independent of save/load
    // rewinds, while high_water prevents a rollback event from hiding that a
    // later signed frame was already observed.
    static constexpr bool RollbackGekkoMayUpdateSession(
        const RollbackFrameStamp& high_water,
        uint64_t update_calls) noexcept
    {
        return update_calls < kRollbackGekkoSignedFrameExclusiveCeiling
            && (!high_water.valid
                || high_water.value
                    < kRollbackGekkoSignedFrameExclusiveCeiling - 1u);
    }

    static constexpr void RollbackObserveGekkoFrame(
        RollbackFrameStamp& high_water,
        uint32_t frame) noexcept
    {
        // Gekko production frames never wrap: numeric max is deliberate.
        if (!high_water.valid || frame > high_water.value)
            high_water = frame;
    }
}
