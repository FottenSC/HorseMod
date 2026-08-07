#pragma once

#include "RollbackLaunchContract.hpp"
#include "RollbackSideEffectLedger.hpp"

#include <array>
#include <cstdint>

namespace Horse
{
    static constexpr bool RollbackFixtureRequiresFirstLoadEffectWitness(
        bool predictor,
        bool replay_input_enabled) noexcept
    {
        // The tagged first-load witness proves fixture-authored presentation
        // conservation. Replay-authored runs have their own exact consumed
        // input and correction/load witness, and deliberately do not tag
        // presentation records as fixture evidence.
        return predictor && !replay_input_enabled;
    }

    struct RollbackFixtureCorrectionCandidate
    {
        bool valid {false};
        uint64_t epoch {0};
        uint32_t fixture_id {0};
        uint32_t load_frame {0};
        uint32_t affected_frame {0};
        std::array<uint32_t, 2> predicted_input {};
        std::array<uint32_t, kRollbackSideEffectTypeCount>
            discarded_first_frame {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            discarded_count {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            discarded_digest {};
    };

    enum class RollbackFixtureCorrectionAction : uint8_t
    {
        Ignore,
        RejectedUnrelated,
        Promoted,
    };

    class RollbackFixtureCorrectionGate
    {
    public:
        void reset() noexcept { m_candidate = {}; }

        bool arm(
            const RollbackDeterministicInputConfig& config,
            uint16_t input_delay,
            uint64_t epoch,
            uint32_t load_frame,
            uint32_t speculative_high_water,
            const std::array<uint32_t, 2>& predicted_input,
            const std::array<uint32_t, kRollbackSideEffectTypeCount>&
                discarded_first_frame,
            const std::array<uint64_t, kRollbackSideEffectTypeCount>&
                discarded_count,
            const std::array<uint64_t, kRollbackSideEffectTypeCount>&
                discarded_digest) noexcept
        {
            if (!config.enabled || !config.valid() || epoch == 0
                || m_candidate.valid
                || !RollbackFixtureLoadAffectsEvidence(
                    config, input_delay, load_frame)
                || speculative_high_water < RollbackFixtureLastHeldFrame(config))
            {
                return false;
            }
            const uint32_t affected_frame = load_frame < config.correction_start
                ? config.correction_start : config.correction_start + 1u;
            if (affected_frame >= config.correction_start
                    + config.hold_updates)
            {
                return false;
            }
            for (size_t type = 0; type < kRollbackSideEffectTypeCount; ++type)
            {
                if ((discarded_count[type] == 0)
                    != (discarded_digest[type] == 0))
                    return false;
            }
            m_candidate.valid = true;
            m_candidate.epoch = epoch;
            m_candidate.fixture_id = config.fixture_id;
            m_candidate.load_frame = load_frame;
            m_candidate.affected_frame = affected_frame;
            m_candidate.predicted_input = predicted_input;
            m_candidate.discarded_first_frame = discarded_first_frame;
            m_candidate.discarded_count = discarded_count;
            m_candidate.discarded_digest = discarded_digest;
            return true;
        }

        RollbackFixtureCorrectionAction observe_advance(
            uint64_t epoch,
            uint32_t frame,
            bool rolling_back,
            const std::array<uint32_t, 2>& corrected_input,
            const std::array<uint32_t, 2>& authored_input,
            uint8_t delay_owner_slot) noexcept
        {
            if (!m_candidate.valid) return RollbackFixtureCorrectionAction::Ignore;
            if (epoch != m_candidate.epoch || delay_owner_slot >= 2
                || frame > m_candidate.affected_frame)
            {
                reset();
                return RollbackFixtureCorrectionAction::RejectedUnrelated;
            }
            if (frame < m_candidate.affected_frame)
                return RollbackFixtureCorrectionAction::Ignore;
            const bool fixture_correction = rolling_back
                && corrected_input == authored_input
                && corrected_input[delay_owner_slot]
                    != m_candidate.predicted_input[delay_owner_slot];
            if (!fixture_correction)
            {
                reset();
                return RollbackFixtureCorrectionAction::RejectedUnrelated;
            }
            return RollbackFixtureCorrectionAction::Promoted;
        }

        const RollbackFixtureCorrectionCandidate& candidate() const noexcept
        {
            return m_candidate;
        }

    private:
        RollbackFixtureCorrectionCandidate m_candidate {};
    };

    enum class RollbackFixtureCheckpointAction : uint8_t
    {
        PassThrough,
        StopAtTarget,
        Reject,
    };

    inline bool RollbackFixturePresentationCheckpointRequired(
        bool deterministic_input_enabled,
        bool fixture_correction_pending,
        bool native_correction_only) noexcept
    {
        return deterministic_input_enabled
            && fixture_correction_pending
            && !native_correction_only;
    }

    class RollbackFixtureCheckpointGate
    {
    public:
        void reset() noexcept { *this = {}; }

        bool configure(uint64_t epoch, uint32_t target) noexcept
        {
            if (epoch == 0) return false;
            reset();
            m_epoch = epoch;
            m_target = target;
            m_configured = true;
            return true;
        }

        RollbackFixtureCheckpointAction observe_frontier(
            uint64_t epoch, uint32_t frame) noexcept
        {
            if (!m_configured || epoch != m_epoch)
                return RollbackFixtureCheckpointAction::Reject;
            if (m_captured) return RollbackFixtureCheckpointAction::PassThrough;
            if (frame < m_target) return RollbackFixtureCheckpointAction::PassThrough;
            if (frame == m_target && !m_confirmation_scheduled)
            {
                m_confirmation_scheduled = true;
                return RollbackFixtureCheckpointAction::StopAtTarget;
            }
            return RollbackFixtureCheckpointAction::Reject;
        }

        bool begin_confirmation(uint64_t epoch, uint32_t frame) noexcept
        {
            if (!m_configured || epoch != m_epoch || frame != m_target
                || !m_confirmation_scheduled || m_confirmation_started)
                return false;
            m_confirmation_started = true;
            return true;
        }

        bool commit_validated(uint64_t epoch, uint32_t frame) noexcept
        {
            if (!m_confirmation_started || epoch != m_epoch
                || frame != m_target || m_commit_validated)
                return false;
            m_commit_validated = true;
            return true;
        }

        bool checkpoint_captured(uint64_t epoch, uint32_t frame) noexcept
        {
            if (!m_commit_validated || epoch != m_epoch
                || frame != m_target || m_captured)
                return false;
            m_captured = true;
            return true;
        }

        bool captured() const noexcept { return m_captured; }
        bool awaiting_confirmation() const noexcept
        {
            return m_confirmation_scheduled && !m_captured;
        }
        uint32_t target() const noexcept { return m_target; }
        bool configured_for(uint64_t epoch, uint32_t target) const noexcept
        {
            return m_configured && m_epoch == epoch && m_target == target;
        }

    private:
        uint64_t m_epoch {0};
        uint32_t m_target {0};
        bool m_configured {false};
        bool m_confirmation_scheduled {false};
        bool m_confirmation_started {false};
        bool m_commit_validated {false};
        bool m_captured {false};
    };
}
