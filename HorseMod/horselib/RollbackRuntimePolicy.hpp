// ============================================================================
// Horse::RollbackRuntimePolicy
//
// Candidate-bound rollback save and peer-lead pacing policy.  These values
// are negotiated before ownership so peers cannot silently simulate with
// different frame clocks or snapshot retention rules.
// ============================================================================

#pragma once

#include <cstdint>

namespace Horse
{
    static constexpr uint16_t RollbackFramesToMilliframes(
        float frames) noexcept
    {
        return frames <= 0.0f ? 0u
            : frames >= 65.535f ? UINT16_MAX
            : static_cast<uint16_t>(frames * 1000.0f + 0.5f);
    }

    static constexpr float RollbackMilliframesToFrames(
        uint16_t milliframes) noexcept
    {
        return static_cast<float>(milliframes) / 1000.0f;
    }

    enum class RollbackSavePolicy : uint8_t
    {
        EveryAdvance = 0,
        ConfirmedSpeculative = 1,
    };

    static constexpr const char* RollbackSavePolicyName(
        RollbackSavePolicy policy) noexcept
    {
        switch (policy)
        {
        case RollbackSavePolicy::EveryAdvance: return "every-advance";
        case RollbackSavePolicy::ConfirmedSpeculative:
            return "confirmed-speculative";
        }
        return "invalid";
    }

    static constexpr bool RollbackSavePolicyValid(
        RollbackSavePolicy policy) noexcept
    {
        return policy == RollbackSavePolicy::EveryAdvance
            || policy == RollbackSavePolicy::ConfirmedSpeculative;
    }

    struct RollbackLeadPacingConfig
    {
        bool enabled {true};
        float enter_frames {1.5f};
        float exit_frames {0.5f};
        uint8_t maximum_consecutive_holds {2};

        constexpr bool valid() const noexcept
        {
            return enter_frames > 0.0f
                && exit_frames >= 0.0f
                && exit_frames < enter_frames
                && maximum_consecutive_holds != 0
                && maximum_consecutive_holds <= 8;
        }
    };

    enum class RollbackLeadPacingDecision : uint8_t
    {
        Advance,
        HoldAndFlushCorrections,
    };

    class RollbackLeadPacingController
    {
    public:
        constexpr void reset() noexcept
        {
            m_holding = false;
            m_consecutive_holds = 0;
            m_force_advance = false;
            m_forced_advance_last_decision = false;
        }

        constexpr RollbackLeadPacingDecision decide(
            const RollbackLeadPacingConfig& config,
            float frames_ahead,
            bool eligible) noexcept
        {
            if (!eligible || !config.enabled || !config.valid())
            {
                reset();
                return RollbackLeadPacingDecision::Advance;
            }
            if (m_force_advance)
            {
                m_force_advance = false;
                m_forced_advance_last_decision = true;
                m_consecutive_holds = 0;
                return RollbackLeadPacingDecision::Advance;
            }
            if (m_holding && frames_ahead <= config.exit_frames)
            {
                reset();
                return RollbackLeadPacingDecision::Advance;
            }
            if (!m_holding && frames_ahead < config.enter_frames)
            {
                m_forced_advance_last_decision = false;
                return RollbackLeadPacingDecision::Advance;
            }

            m_forced_advance_last_decision = false;
            m_holding = true;
            ++m_consecutive_holds;
            if (m_consecutive_holds >= config.maximum_consecutive_holds)
                m_force_advance = true;
            return RollbackLeadPacingDecision::HoldAndFlushCorrections;
        }

        constexpr bool holding() const noexcept { return m_holding; }
        constexpr uint8_t consecutive_holds() const noexcept
        {
            return m_consecutive_holds;
        }
        constexpr bool forced_advance_last_decision() const noexcept
        {
            return m_forced_advance_last_decision;
        }

    private:
        bool m_holding {false};
        uint8_t m_consecutive_holds {0};
        bool m_force_advance {false};
        bool m_forced_advance_last_decision {false};
    };
}
