#pragma once

#include "RollbackGameplayCrt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    // Raw replay input does not carry an independently recorded RNG tuple.
    // Only a consumed-input sidecar makes replay RNG authoritative at the
    // immutable frame-zero boundary.
    constexpr bool RollbackReplayRngBaselineRequired(
        bool replay_enabled,
        bool has_consumed_input_sidecar) noexcept
    {
        return replay_enabled && has_consumed_input_sidecar;
    }

    // Complete value-state for SC6's two battle RNGs. The LFSR hash uses the
    // same FNV-1a parameters as the consumed-input sidecar so the tuple can be
    // compared without changing that format.
    struct RollbackRngTuple
    {
        uint32_t lcg_state {0};
        std::array<uint8_t, 100> lfsr_state {};
        uint64_t lfsr_hash {0};
        uint32_t lfsr_index {0};
        RollbackGameplayCrtState gameplay_crt {};
        bool gameplay_crt_present {false};

        static uint64_t hash_lfsr(
            const std::array<uint8_t, 100>& state) noexcept
        {
            uint64_t hash = 1469598103934665603ull;
            for (const uint8_t byte : state)
            {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        static RollbackRngTuple capture(
            uint32_t lcg,
            const std::array<uint8_t, 100>& lfsr,
            uint32_t index,
            const RollbackGameplayCrtState* gameplay_crt = nullptr) noexcept
        {
            RollbackRngTuple tuple {};
            tuple.lcg_state = lcg;
            tuple.lfsr_state = lfsr;
            tuple.lfsr_hash = hash_lfsr(lfsr);
            tuple.lfsr_index = index;
            if (gameplay_crt)
            {
                tuple.gameplay_crt = *gameplay_crt;
                tuple.gameplay_crt_present = true;
            }
            return tuple;
        }

        bool valid() const noexcept
        {
            bool any_lfsr_state = false;
            for (const uint8_t byte : lfsr_state)
                any_lfsr_state = any_lfsr_state || byte != 0;
            return any_lfsr_state
                && lfsr_index <= 25
                && lfsr_hash != 0
                && lfsr_hash == hash_lfsr(lfsr_state)
                && (!gameplay_crt_present
                    || RollbackGameplayCrtStateIsCanonical(gameplay_crt));
        }

        bool complete_for_rollback() const noexcept
        {
            return valid() && gameplay_crt_present
                && RollbackGameplayCrtStateIsCanonical(gameplay_crt);
        }

        bool stock_rng_equal(const RollbackRngTuple& rhs) const noexcept
        {
            return lcg_state == rhs.lcg_state
                && lfsr_state == rhs.lfsr_state
                && lfsr_hash == rhs.lfsr_hash
                && lfsr_index == rhs.lfsr_index;
        }

        friend bool operator==(
            const RollbackRngTuple& lhs,
            const RollbackRngTuple& rhs) noexcept
        {
            return lhs.lcg_state == rhs.lcg_state
                && lhs.lfsr_state == rhs.lfsr_state
                && lhs.lfsr_hash == rhs.lfsr_hash
                && lhs.lfsr_index == rhs.lfsr_index
                && lhs.gameplay_crt_present == rhs.gameplay_crt_present
                && (!lhs.gameplay_crt_present
                    || RollbackGameplayCrtCanonicalEqual(
                        lhs.gameplay_crt, rhs.gameplay_crt));
        }

        friend bool operator!=(
            const RollbackRngTuple& lhs,
            const RollbackRngTuple& rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    // Identity of the immutable state captured immediately before Gekko owns
    // frame zero. Named component hashes make Save(-1) validation sensitive to
    // both the canonical aggregate and each explicit snapshot component.
    struct RollbackFrameZeroBaselineSnapshot
    {
        uint32_t round {0};
        uint64_t epoch {0};
        int32_t logical_frame {-1};
        uint64_t canonical_hash {0};
        uint64_t hgcpu_hash {0};
        uint64_t explicit_hash {0};
        uint64_t stage_hash {0};
        uint64_t wind_hash {0};
        RollbackRngTuple rng {};

        bool valid() const noexcept
        {
            return epoch != 0
                && logical_frame == -1
                && canonical_hash != 0
                && hgcpu_hash != 0
                && explicit_hash != 0
                && stage_hash != 0
                && wind_hash != 0
                && rng.complete_for_rollback();
        }

        friend bool operator==(
            const RollbackFrameZeroBaselineSnapshot& lhs,
            const RollbackFrameZeroBaselineSnapshot& rhs) noexcept
        {
            return lhs.round == rhs.round
                && lhs.epoch == rhs.epoch
                && lhs.logical_frame == rhs.logical_frame
                && lhs.canonical_hash == rhs.canonical_hash
                && lhs.hgcpu_hash == rhs.hgcpu_hash
                && lhs.explicit_hash == rhs.explicit_hash
                && lhs.stage_hash == rhs.stage_hash
                && lhs.wind_hash == rhs.wind_hash
                && lhs.rng == rhs.rng;
        }

        friend bool operator!=(
            const RollbackFrameZeroBaselineSnapshot& lhs,
            const RollbackFrameZeroBaselineSnapshot& rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    struct RollbackFrameZeroBaselineIdentity
    {
        uint32_t round {0};
        uint64_t epoch {0};
        uint64_t canonical_hash {0};

        bool valid() const noexcept
        {
            return epoch != 0 && canonical_hash != 0;
        }
    };

    enum class RollbackFrameZeroBaselinePhase : uint8_t
    {
        Unarmed = 0,
        Captured,
        BarrierAccepted,
        GekkoReady,
        BaselineSaved,
        FrameZeroAdvanced,
    };

    enum class RollbackFrameZeroBaselineResult : uint8_t
    {
        Accepted = 0,
        InvalidSnapshot,
        InvalidIdentity,
        InvalidTransition,
        IdentityMismatch,
        SnapshotMismatch,
        RngMismatch,
    };

    enum class RollbackFrameZeroRestoreDecision : uint8_t
    {
        AlreadyMatches = 0,
        RestoreRequired,
        NotArmed,
        FrameZeroAlreadyAdvanced,
    };

    // Pure per-round protocol. It never reads or writes SC6 memory: callers
    // capture a live tuple, apply a requested restore through the state adapter,
    // and then call verify_restored before invoking the native update.
    class RollbackFrameZeroBaselineGate
    {
    public:
        RollbackFrameZeroBaselineResult capture(
            const RollbackFrameZeroBaselineSnapshot& snapshot) noexcept
        {
            if (!snapshot.valid())
                return RollbackFrameZeroBaselineResult::InvalidSnapshot;
            if (m_phase != RollbackFrameZeroBaselinePhase::Unarmed)
                return RollbackFrameZeroBaselineResult::InvalidTransition;
            m_baseline = snapshot;
            m_saved = {};
            m_phase = RollbackFrameZeroBaselinePhase::Captured;
            m_restore_verifications = 0;
            return RollbackFrameZeroBaselineResult::Accepted;
        }

        RollbackFrameZeroBaselineResult accept_barrier(
            const RollbackFrameZeroBaselineIdentity& identity) noexcept
        {
            if (!identity.valid())
                return RollbackFrameZeroBaselineResult::InvalidIdentity;
            if (m_phase != RollbackFrameZeroBaselinePhase::Captured)
                return RollbackFrameZeroBaselineResult::InvalidTransition;
            if (!identity_matches(identity))
                return RollbackFrameZeroBaselineResult::IdentityMismatch;
            m_phase = RollbackFrameZeroBaselinePhase::BarrierAccepted;
            return RollbackFrameZeroBaselineResult::Accepted;
        }

        RollbackFrameZeroBaselineResult mark_gekko_ready() noexcept
        {
            if (m_phase != RollbackFrameZeroBaselinePhase::BarrierAccepted)
                return RollbackFrameZeroBaselineResult::InvalidTransition;
            m_phase = RollbackFrameZeroBaselinePhase::GekkoReady;
            return RollbackFrameZeroBaselineResult::Accepted;
        }

        RollbackFrameZeroRestoreDecision before_starting_gekko_update(
            const RollbackRngTuple& live_rng) const noexcept
        {
            if (m_phase == RollbackFrameZeroBaselinePhase::Unarmed)
                return RollbackFrameZeroRestoreDecision::NotArmed;
            if (m_phase == RollbackFrameZeroBaselinePhase::FrameZeroAdvanced)
                return RollbackFrameZeroRestoreDecision::
                    FrameZeroAlreadyAdvanced;
            return live_rng == m_baseline.rng
                ? RollbackFrameZeroRestoreDecision::AlreadyMatches
                : RollbackFrameZeroRestoreDecision::RestoreRequired;
        }

        RollbackFrameZeroBaselineResult verify_restored(
            const RollbackRngTuple& live_rng) noexcept
        {
            if (!restore_allowed())
                return RollbackFrameZeroBaselineResult::InvalidTransition;
            if (live_rng != m_baseline.rng)
                return RollbackFrameZeroBaselineResult::RngMismatch;
            ++m_restore_verifications;
            return RollbackFrameZeroBaselineResult::Accepted;
        }

        RollbackFrameZeroBaselineResult record_baseline_save(
            const RollbackFrameZeroBaselineSnapshot& saved) noexcept
        {
            if (m_phase != RollbackFrameZeroBaselinePhase::GekkoReady)
                return RollbackFrameZeroBaselineResult::InvalidTransition;
            if (!saved.valid())
                return RollbackFrameZeroBaselineResult::InvalidSnapshot;
            if (saved.rng != m_baseline.rng)
                return RollbackFrameZeroBaselineResult::RngMismatch;
            if (saved != m_baseline)
                return RollbackFrameZeroBaselineResult::SnapshotMismatch;
            m_saved = saved;
            m_phase = RollbackFrameZeroBaselinePhase::BaselineSaved;
            return RollbackFrameZeroBaselineResult::Accepted;
        }

        RollbackFrameZeroBaselineResult commit_frame_zero_advance(
            const RollbackFrameZeroBaselineSnapshot& pre_advance) noexcept
        {
            if (m_phase != RollbackFrameZeroBaselinePhase::BaselineSaved)
                return RollbackFrameZeroBaselineResult::InvalidTransition;
            if (!pre_advance.valid())
                return RollbackFrameZeroBaselineResult::InvalidSnapshot;
            if (pre_advance.rng != m_saved.rng)
                return RollbackFrameZeroBaselineResult::RngMismatch;
            if (pre_advance != m_saved)
                return RollbackFrameZeroBaselineResult::SnapshotMismatch;
            m_phase = RollbackFrameZeroBaselinePhase::FrameZeroAdvanced;
            return RollbackFrameZeroBaselineResult::Accepted;
        }

        // A new round is an explicit transaction boundary. Capture is
        // deliberately rejected after Advance(0) until this operation occurs,
        // preventing accidental restaging of the completed round.
        RollbackFrameZeroBaselineResult rearm_for_next_round() noexcept
        {
            if (m_phase != RollbackFrameZeroBaselinePhase::FrameZeroAdvanced)
                return RollbackFrameZeroBaselineResult::InvalidTransition;
            m_baseline = {};
            m_saved = {};
            m_phase = RollbackFrameZeroBaselinePhase::Unarmed;
            m_restore_verifications = 0;
            return RollbackFrameZeroBaselineResult::Accepted;
        }

        RollbackFrameZeroBaselinePhase phase() const noexcept
        {
            return m_phase;
        }

        const RollbackFrameZeroBaselineSnapshot& baseline() const noexcept
        {
            return m_baseline;
        }

        const RollbackFrameZeroBaselineSnapshot& saved() const noexcept
        {
            return m_saved;
        }

        uint32_t restore_verifications() const noexcept
        {
            return m_restore_verifications;
        }

        bool restore_allowed() const noexcept
        {
            return m_phase >= RollbackFrameZeroBaselinePhase::Captured
                && m_phase <= RollbackFrameZeroBaselinePhase::BaselineSaved;
        }

    private:
        bool identity_matches(
            const RollbackFrameZeroBaselineIdentity& identity) const noexcept
        {
            return identity.round == m_baseline.round
                && identity.epoch == m_baseline.epoch
                && identity.canonical_hash == m_baseline.canonical_hash;
        }

        RollbackFrameZeroBaselinePhase m_phase {
            RollbackFrameZeroBaselinePhase::Unarmed
        };
        RollbackFrameZeroBaselineSnapshot m_baseline {};
        RollbackFrameZeroBaselineSnapshot m_saved {};
        uint32_t m_restore_verifications {0};
    };
}
