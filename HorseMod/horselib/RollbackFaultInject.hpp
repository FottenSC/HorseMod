// ============================================================================
// Horse::RollbackFaultInject
//
// Deterministic fault-injection metadata for the rollback lab. The injector is
// inactive until the lab backend can own a proven frame boundary.
// ============================================================================

#pragma once

#include <cstdint>

namespace Horse
{
    enum class RollbackFaultKind : uint8_t
    {
        None,
        DelayedRemoteInput,
        DroppedInput,
        ReorderedInput,
        DuplicateInput,
        StalePrediction,
        WrongFrameId,
        CorruptedInputByte,
        CorruptedCacheTag,
        MasterClockJump,
        SkippedManualTick,
        DoubleManualTick,
        RngPerturbation,
        SideEffectLeak,
        RoundBoundaryRefusal,
    };

    struct RollbackFaultConfig
    {
        RollbackFaultKind kind {RollbackFaultKind::None};
        uint32_t seed {0x5C6B0001u};
        int32_t frame {-1};
        uint8_t player_slot {1};
        uint32_t delay_frames {0};
        uint64_t mutation {0};
    };

    class RollbackDeterministicRng
    {
    public:
        explicit RollbackDeterministicRng(uint32_t seed) noexcept
            : m_state(seed ? seed : 0x5C6B0001u)
        {
        }

        uint32_t next() noexcept
        {
            m_state = m_state * 1664525u + 1013904223u;
            return m_state;
        }

    private:
        uint32_t m_state;
    };
}

