#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    enum class RollbackPoseProducerCheckpoint : uint8_t
    {
        TickMainEnter = 0,
        FinalizeEnter,
        FinalizeExit,
        EvaluateEnter,
        EvaluateExit,
        TickMainExit,
    };

    struct RollbackPoseProducerTraceLedger
    {
        static constexpr size_t kMaximumCheckpoints = 6;
        std::array<
            RollbackPoseProducerCheckpoint,
            kMaximumCheckpoints> checkpoints {};
        size_t count {0};
        bool valid {true};

        bool admit(RollbackPoseProducerCheckpoint checkpoint) noexcept
        {
            if (count >= checkpoints.size())
            {
                valid = false;
                return false;
            }
            checkpoints[count++] = checkpoint;
            valid = valid && is_valid_prefix();
            return valid;
        }

        bool complete() const noexcept
        {
            return valid && (
                equals(kEarlyExit)
                || equals(kBaseSolveOnly)
                || equals(kMoveVmOverlay));
        }

    private:
        static constexpr std::array<
            RollbackPoseProducerCheckpoint, 2> kEarlyExit {
                RollbackPoseProducerCheckpoint::TickMainEnter,
                RollbackPoseProducerCheckpoint::TickMainExit,
            };
        static constexpr std::array<
            RollbackPoseProducerCheckpoint, 4> kBaseSolveOnly {
                RollbackPoseProducerCheckpoint::TickMainEnter,
                RollbackPoseProducerCheckpoint::FinalizeEnter,
                RollbackPoseProducerCheckpoint::FinalizeExit,
                RollbackPoseProducerCheckpoint::TickMainExit,
            };
        static constexpr std::array<
            RollbackPoseProducerCheckpoint, 6> kMoveVmOverlay {
                RollbackPoseProducerCheckpoint::TickMainEnter,
                RollbackPoseProducerCheckpoint::FinalizeEnter,
                RollbackPoseProducerCheckpoint::FinalizeExit,
                RollbackPoseProducerCheckpoint::EvaluateEnter,
                RollbackPoseProducerCheckpoint::EvaluateExit,
                RollbackPoseProducerCheckpoint::TickMainExit,
            };

        template<size_t N>
        bool prefix_of(
            const std::array<RollbackPoseProducerCheckpoint, N>& expected)
            const noexcept
        {
            if (count > N) return false;
            for (size_t index = 0; index < count; ++index)
                if (checkpoints[index] != expected[index]) return false;
            return true;
        }

        template<size_t N>
        bool equals(
            const std::array<RollbackPoseProducerCheckpoint, N>& expected)
            const noexcept
        {
            return count == N && prefix_of(expected);
        }

        bool is_valid_prefix() const noexcept
        {
            return prefix_of(kEarlyExit)
                || prefix_of(kBaseSolveOnly)
                || prefix_of(kMoveVmOverlay);
        }
    };
}
