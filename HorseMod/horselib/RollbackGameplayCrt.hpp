// ============================================================================
// Horse::RollbackGameplayCrt
//
// Explicit rollback-owned model of SC6's battle-thread MSVC/UCRT rand stream.
// The native presentation stream remains process-local. Gameplay callers in
// an owned simulation transaction consume this snapshottable stream instead.
// ============================================================================

#pragma once

#include <cstdint>

namespace Horse
{
    static constexpr uint32_t kRollbackCrtWarmupReturnRva = 0x34F658u;
    static constexpr uint32_t kRollbackCrtMoveVmReturnRva = 0x366FF4u;
    static constexpr uint32_t kRollbackCrtRannyuReturnRva = 0x326368u;
    static constexpr uint32_t kRollbackCrtParticleEmitterReturnRva =
        0x1F9BD5Cu;
    static constexpr uint32_t kRollbackCrtAudioVoiceReturnRva = 0x54F91Eu;
    static constexpr uint32_t kRollbackCrtGroundDebrisBaseYawReturnRva =
        0x895D6Eu;
    static constexpr uint32_t kRollbackCrtGroundDebrisYawJitterReturnRva =
        0x896105u;
    static constexpr uint32_t kRollbackCrtParticleModuleSeedReturnRva =
        0x1FA0932u;
    static constexpr uint32_t kRollbackCrtEmitterDelayRangeReturnRva =
        0x1FA5B0Bu;
    static constexpr uint32_t kRollbackCrtEmitterDurationRangeReturnRva =
        0x1FA5B52u;

    constexpr uint32_t AdvanceRollbackGameplayCrtState(
        uint32_t state) noexcept
    {
        return state * 214013u + 2531011u;
    }

    constexpr uint32_t RollbackGameplayCrtOutput(uint32_t state) noexcept
    {
        return (state >> 16) & 0x7fffu;
    }

    constexpr bool RollbackCrtCallerIsGameplay(uint32_t return_rva) noexcept
    {
        // MoveVM RAND changes bytecode control flow. Rannyu initializes a
        // persistent camera action consumed by side-dependent battle logic.
        return return_rva == kRollbackCrtMoveVmReturnRva
            || return_rva == kRollbackCrtRannyuReturnRva;
    }

    constexpr bool RollbackCrtCallerIsPresentation(
        uint32_t return_rva) noexcept
    {
        // Particle initialization seeds emitter-local FRandomStream state;
        // audio allocates a random active-voice id; ground debris randomizes
        // only scene-component placement. These process-local presentation
        // choices must not advance the snapshottable gameplay stream merely
        // because they run inside an owned iteration.
        return return_rva == kRollbackCrtParticleEmitterReturnRva
            || return_rva == kRollbackCrtAudioVoiceReturnRva
            || return_rva == kRollbackCrtGroundDebrisBaseYawReturnRva
            || return_rva == kRollbackCrtGroundDebrisYawJitterReturnRva
            || return_rva == kRollbackCrtParticleModuleSeedReturnRva
            || return_rva == kRollbackCrtEmitterDelayRangeReturnRva
            || return_rva == kRollbackCrtEmitterDurationRangeReturnRva;
    }

    enum class RollbackGameplayCrtPhase : uint8_t
    {
        Uninitialized,
        Seeding,
        Ready,
    };

    struct RollbackGameplayCrtState
    {
        uint32_t internal_state {0};
        uint32_t full_round_seed {0};
        // Round-local gameplay position. Process-lifetime counters are
        // deliberately excluded from rollback state.
        uint32_t gameplay_draw_ordinal {0};
        uint32_t warmup_draws {0};
        uint32_t owner_thread_id {0};
        RollbackGameplayCrtPhase phase {
            RollbackGameplayCrtPhase::Uninitialized};
        bool native_seed_observed {false};
    };

    constexpr bool RollbackGameplayCrtStateIsCanonical(
        const RollbackGameplayCrtState& state) noexcept
    {
        return state.phase == RollbackGameplayCrtPhase::Ready
            && state.owner_thread_id != 0
            && state.native_seed_observed
            && state.warmup_draws == (state.full_round_seed & 0xfffu);
    }

    constexpr bool RollbackGameplayCrtCanonicalEqual(
        const RollbackGameplayCrtState& lhs,
        const RollbackGameplayCrtState& rhs) noexcept
    {
        return lhs.internal_state == rhs.internal_state
            && lhs.full_round_seed == rhs.full_round_seed
            && lhs.gameplay_draw_ordinal == rhs.gameplay_draw_ordinal
            && lhs.warmup_draws == rhs.warmup_draws
            && lhs.phase == rhs.phase
            && lhs.native_seed_observed == rhs.native_seed_observed;
    }

    struct RollbackGameplayCrtDrawResult
    {
        int value {0};
        bool handled {false};
        bool fatal {false};
        bool known_gameplay_caller {false};
        const char* failure {"ok"};
    };

    class RollbackGameplayCrtBroker
    {
    public:
        bool begin_seed_transaction(
            uint32_t full_seed,
            uint32_t thread_id,
            bool pending_side_effect_or_shadow_debt) noexcept
        {
            if (thread_id == 0 || pending_side_effect_or_shadow_debt
                || m_state.phase == RollbackGameplayCrtPhase::Seeding)
            {
                return fail(pending_side_effect_or_shadow_debt
                    ? "crt-seed-with-pending-debt"
                    : "invalid-crt-seed-transaction");
            }
            m_state = {};
            m_state.internal_state = full_seed >> 4;
            m_state.full_round_seed = full_seed;
            m_state.owner_thread_id = thread_id;
            m_state.phase = RollbackGameplayCrtPhase::Seeding;
            m_failure = "ok";
            m_fatal = false;
            return true;
        }

        bool observe_native_seed(
            uint32_t native_seed,
            uint32_t thread_id) noexcept
        {
            if (!seed_thread_matches(thread_id)
                || m_state.native_seed_observed
                || native_seed != (m_state.full_round_seed >> 4))
            {
                return fail("crt-native-seed-mismatch");
            }
            m_state.native_seed_observed = true;
            return true;
        }

        RollbackGameplayCrtDrawResult observe_native_warmup_draw(
            uint32_t return_rva,
            uint32_t thread_id,
            int native_value) noexcept
        {
            RollbackGameplayCrtDrawResult result {};
            result.handled = true;
            if (!seed_thread_matches(thread_id)
                || !m_state.native_seed_observed
                || return_rva != kRollbackCrtWarmupReturnRva
                || m_state.warmup_draws
                    >= (m_state.full_round_seed & 0xfffu))
            {
                result.fatal = true;
                result.failure = "unexpected-crt-seed-warmup-draw";
                fail(result.failure);
                return result;
            }
            m_state.internal_state = AdvanceRollbackGameplayCrtState(
                m_state.internal_state);
            result.value = static_cast<int>(RollbackGameplayCrtOutput(
                m_state.internal_state));
            ++m_state.warmup_draws;
            if ((static_cast<uint32_t>(native_value) & 0x7fffu)
                != static_cast<uint32_t>(result.value))
            {
                result.fatal = true;
                result.failure = "crt-seed-warmup-output-mismatch";
                fail(result.failure);
            }
            return result;
        }

        bool finish_seed_transaction(uint32_t thread_id) noexcept
        {
            if (!seed_thread_matches(thread_id)
                || !m_state.native_seed_observed
                || m_state.warmup_draws
                    != (m_state.full_round_seed & 0xfffu))
            {
                return fail("incomplete-crt-seed-transaction");
            }
            m_state.phase = RollbackGameplayCrtPhase::Ready;
            return !m_fatal;
        }

        RollbackGameplayCrtDrawResult draw_owned(
            uint32_t return_rva,
            uint32_t thread_id) noexcept
        {
            RollbackGameplayCrtDrawResult result {};
            result.handled = true;
            if (!RollbackGameplayCrtStateIsCanonical(m_state)
                || thread_id != m_state.owner_thread_id)
            {
                result.fatal = true;
                result.failure = "crt-owned-draw-without-ready-stream";
                fail(result.failure);
                return result;
            }

            m_state.internal_state = AdvanceRollbackGameplayCrtState(
                m_state.internal_state);
            result.value = static_cast<int>(RollbackGameplayCrtOutput(
                m_state.internal_state));
            ++m_state.gameplay_draw_ordinal;
            result.known_gameplay_caller =
                RollbackCrtCallerIsGameplay(return_rva);
            if (!result.known_gameplay_caller)
            {
                // Preserve deterministic execution for this call, but do not
                // permit an unclassified native consumer to continue the
                // rollback session silently.
                result.fatal = true;
                result.failure = "unknown-owned-crt-caller";
                fail(result.failure);
            }
            return result;
        }

        RollbackGameplayCrtDrawResult observe_native_gameplay_draw(
            uint32_t return_rva,
            uint32_t thread_id,
            int native_value) noexcept
        {
            RollbackGameplayCrtDrawResult result = draw_owned(
                return_rva, thread_id);
            if (!result.handled || result.fatal) return result;
            if ((static_cast<uint32_t>(native_value) & 0x7fffu)
                != static_cast<uint32_t>(result.value))
            {
                result.fatal = true;
                result.failure = "crt-preownership-output-mismatch";
                fail(result.failure);
            }
            return result;
        }

        bool restore(const RollbackGameplayCrtState& state) noexcept
        {
            if (!RollbackGameplayCrtStateIsCanonical(state))
                return fail("invalid-crt-restore-state");
            m_state = state;
            m_failure = "ok";
            m_fatal = false;
            return true;
        }

        void reset() noexcept
        {
            m_state = {};
            m_failure = "ok";
            m_fatal = false;
        }

        const RollbackGameplayCrtState& state() const noexcept
        {
            return m_state;
        }
        bool fatal() const noexcept { return m_fatal; }
        const char* failure() const noexcept { return m_failure; }

    private:
        bool seed_thread_matches(uint32_t thread_id) const noexcept
        {
            return m_state.phase == RollbackGameplayCrtPhase::Seeding
                && thread_id != 0
                && thread_id == m_state.owner_thread_id;
        }

        bool fail(const char* reason) noexcept
        {
            m_fatal = true;
            m_failure = reason;
            return false;
        }

        RollbackGameplayCrtState m_state {};
        const char* m_failure {"ok"};
        bool m_fatal {false};
    };
}
