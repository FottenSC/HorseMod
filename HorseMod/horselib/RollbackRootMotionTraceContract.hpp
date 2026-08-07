// ============================================================================
// Horse::RollbackRootMotionTraceContract
//
// Pure admission contract for the three verified native callsites of
// LuxBattleChara_UpdateRootMotionDeltasFromBone1. The optional early
// push-override sample and the common P1/P2 samples form one ordered native
// collision transaction.
// ============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    enum class RollbackRootMotionSamplePhase : uint8_t
    {
        Unknown,
        PushOverrideEarly,
        CommonP1,
        CommonP2,
    };

    static constexpr RollbackRootMotionSamplePhase
    ClassifyRollbackRootMotionSampleReturnRva(
        uintptr_t return_rva) noexcept
    {
        switch (return_rva)
        {
        case 0x33D030:
            return RollbackRootMotionSamplePhase::PushOverrideEarly;
        case 0x33D6E0:
            return RollbackRootMotionSamplePhase::CommonP1;
        case 0x33D6EC:
            return RollbackRootMotionSamplePhase::CommonP2;
        default:
            return RollbackRootMotionSamplePhase::Unknown;
        }
    }

    static constexpr const char* RollbackRootMotionSamplePhaseName(
        RollbackRootMotionSamplePhase phase) noexcept
    {
        switch (phase)
        {
        case RollbackRootMotionSamplePhase::PushOverrideEarly:
            return "push-override-early";
        case RollbackRootMotionSamplePhase::CommonP1:
            return "common-p1";
        case RollbackRootMotionSamplePhase::CommonP2:
            return "common-p2";
        default:
            return "unknown";
        }
    }

    static constexpr bool ShouldBeginRollbackRootMotionTraceTransaction(
        bool active,
        RollbackRootMotionSamplePhase phase) noexcept
    {
        return !active
            && (phase == RollbackRootMotionSamplePhase::PushOverrideEarly
                || phase == RollbackRootMotionSamplePhase::CommonP1);
    }

    struct RollbackRootMotionTraceAdmission
    {
        uint64_t transaction_id {0};
        uint32_t call_ordinal {0};
        bool transaction_active {false};
        bool began_transaction {false};
        bool ended_transaction {false};
    };

    class RollbackRootMotionTraceLedger
    {
    public:
        void begin_outer_transaction(uint64_t transaction_id) noexcept
        {
            begin(transaction_id);
        }

        void finish_outer_transaction() noexcept
        {
            m_active = false;
        }

        RollbackRootMotionTraceAdmission admit(
            RollbackRootMotionSamplePhase phase,
            int player_index,
            uint64_t new_transaction_id) noexcept
        {
            RollbackRootMotionTraceAdmission result {};
            if (ShouldBeginRollbackRootMotionTraceTransaction(
                    m_active, phase))
            {
                begin(new_transaction_id);
                result.began_transaction = true;
            }

            result.transaction_id = m_transaction_id;
            result.transaction_active = m_active;
            if (m_active && player_index >= 0 && player_index < 2)
            {
                result.call_ordinal =
                    ++m_player_ordinals[static_cast<size_t>(player_index)];
            }
            if (m_active
                && phase == RollbackRootMotionSamplePhase::CommonP2)
            {
                m_active = false;
                result.ended_transaction = true;
            }
            return result;
        }

        bool active() const noexcept { return m_active; }
        uint64_t transaction_id() const noexcept
        {
            return m_transaction_id;
        }

    private:
        void begin(uint64_t transaction_id) noexcept
        {
            m_transaction_id = transaction_id;
            m_player_ordinals = {};
            m_active = transaction_id != 0;
        }

        uint64_t m_transaction_id {0};
        std::array<uint32_t, 2> m_player_ordinals {};
        bool m_active {false};
    };
}
