#pragma once

#include "RollbackLaunchContract.hpp"

#include <cstdint>

namespace Horse
{
    enum class RollbackPreNewRoundPacketDisposition : uint8_t
    {
        Current,
        Stale,
        Future,
        Invalid,
    };

    class RollbackCompletedRoundEpochLatch
    {
    public:
        bool retain(uint64_t completed_epoch) noexcept
        {
            if (completed_epoch == 0) return false;
            if (m_value != 0) return m_value == completed_epoch;
            m_value = completed_epoch;
            return true;
        }

        bool clear_after_new_round_acceptance(
            bool baseline_accepted,
            uint64_t new_round_epoch) noexcept
        {
            if (!baseline_accepted || new_round_epoch == 0 || m_value == 0
                || new_round_epoch == m_value)
                return false;
            m_value = 0;
            return true;
        }

        void reset() noexcept { m_value = 0; }
        uint64_t value() const noexcept { return m_value; }
        bool valid() const noexcept { return m_value != 0; }

    private:
        uint64_t m_value {0};
    };

    static constexpr bool RollbackPreNewRoundReceiveWindowOpen(
        bool stock_inter_round,
        bool terminal_accepted,
        bool terminal_identity_immutable) noexcept
    {
        return stock_inter_round
            || (terminal_accepted && terminal_identity_immutable);
    }

    static constexpr uint64_t ResolveRollbackCompletedRoundEpoch(
        uint64_t retained_completed_epoch,
        uint64_t active_round_epoch,
        bool terminal_accepted,
        bool terminal_identity_immutable) noexcept
    {
        if (retained_completed_epoch != 0)
            return retained_completed_epoch;
        return terminal_accepted && terminal_identity_immutable
            ? active_round_epoch : 0;
    }

    struct RollbackPreNewRoundExpectedIdentity
    {
        uint8_t peer_player_slot {0};
        uint32_t completed_round_ordinal {0};
        uint32_t target_round_ordinal {0};
        uint64_t session_epoch {0};
        uint64_t completed_pair_epoch {0};
        uint64_t terminal_canonical_hash {0};
        uint64_t target_round_generation {0};
        uint64_t match_identity_digest {0};
        uint32_t native_stage_identity {0};
    };

    enum RollbackPreNewRoundCurrentIdentityMismatch : uint32_t
    {
        RollbackPreNewRoundCurrentIdentityMismatchNone = 0,
        RollbackPreNewRoundCurrentIdentityMismatchInvalid = 1u << 0,
        RollbackPreNewRoundCurrentIdentityMismatchPlayerSlot = 1u << 1,
        RollbackPreNewRoundCurrentIdentityMismatchCompletedOrdinal = 1u << 2,
        RollbackPreNewRoundCurrentIdentityMismatchTargetOrdinal = 1u << 3,
        RollbackPreNewRoundCurrentIdentityMismatchSession = 1u << 4,
        RollbackPreNewRoundCurrentIdentityMismatchCompletedEpoch = 1u << 5,
        RollbackPreNewRoundCurrentIdentityMismatchTerminalHash = 1u << 6,
        RollbackPreNewRoundCurrentIdentityMismatchGeneration = 1u << 7,
        RollbackPreNewRoundCurrentIdentityMismatchMatch = 1u << 8,
        RollbackPreNewRoundCurrentIdentityMismatchStage = 1u << 9,
    };

    static constexpr uint32_t RollbackPreNewRoundCurrentIdentityMismatchMask(
        const RollbackPreNewRoundBarrierMessage& message,
        const RollbackPreNewRoundExpectedIdentity& expected) noexcept
    {
        uint32_t mask = RollbackPreNewRoundCurrentIdentityMismatchNone;
        if (!RollbackPreNewRoundBarrierValid(message))
            mask |= RollbackPreNewRoundCurrentIdentityMismatchInvalid;
        if (message.local_player_slot != expected.peer_player_slot)
            mask |= RollbackPreNewRoundCurrentIdentityMismatchPlayerSlot;
        if (message.completed_round_ordinal
                != expected.completed_round_ordinal)
            mask |= RollbackPreNewRoundCurrentIdentityMismatchCompletedOrdinal;
        if (message.target_round_ordinal != expected.target_round_ordinal)
            mask |= RollbackPreNewRoundCurrentIdentityMismatchTargetOrdinal;
        if (message.session_epoch != expected.session_epoch)
            mask |= RollbackPreNewRoundCurrentIdentityMismatchSession;
        if (message.completed_pair_epoch != expected.completed_pair_epoch)
            mask |= RollbackPreNewRoundCurrentIdentityMismatchCompletedEpoch;
        if (message.terminal_canonical_hash
                != expected.terminal_canonical_hash)
            mask |= RollbackPreNewRoundCurrentIdentityMismatchTerminalHash;
        if (message.target_round_generation
                != expected.target_round_generation)
            mask |= RollbackPreNewRoundCurrentIdentityMismatchGeneration;
        if (message.match_identity_digest != expected.match_identity_digest)
            mask |= RollbackPreNewRoundCurrentIdentityMismatchMatch;
        if (message.native_stage_identity != expected.native_stage_identity)
            mask |= RollbackPreNewRoundCurrentIdentityMismatchStage;
        return mask;
    }

    static constexpr RollbackPreNewRoundPacketDisposition
    ClassifyRollbackPreNewRoundPacket(
        const RollbackPreNewRoundBarrierMessage& message,
        const RollbackPreNewRoundExpectedIdentity& expected) noexcept
    {
        if (!RollbackPreNewRoundBarrierValid(message)
            || expected.peer_player_slot >= 2
            || expected.target_round_generation <= 1)
        {
            return RollbackPreNewRoundPacketDisposition::Invalid;
        }
        if (message.target_round_generation
            < expected.target_round_generation)
        {
            return RollbackPreNewRoundPacketDisposition::Stale;
        }
        if (message.target_round_generation
            > expected.target_round_generation)
        {
            return RollbackPreNewRoundPacketDisposition::Future;
        }
        return RollbackPreNewRoundCurrentIdentityMismatchMask(
                    message, expected)
                == RollbackPreNewRoundCurrentIdentityMismatchNone
            ? RollbackPreNewRoundPacketDisposition::Current
            : RollbackPreNewRoundPacketDisposition::Invalid;
    }

    enum class RollbackNativePreNewRoundAction : uint8_t
    {
        PassThrough,
        Hold,
        Release,
        Reject,
    };

    // The stock round-result cinematic can restore an HgCpu snapshot before
    // the world-mode pump reaches E60 PostTick. Releasing E60 while that
    // restore is still armed is not stable: the restore can erase the queued
    // E20 mode and invoke E60 PostTick a second time. Admit the single native
    // transition only after the cinematic is quiescent. State 0 is idle.
    // State 1 is capture-only and is also safe while both triggers, both
    // palette controllers, and the interactive block are clear; only state 2
    // owns the active-HgCpu playback/restore transaction.
    struct RollbackNativePreNewRoundCinematicReleaseObservation
    {
        bool readable {false};
        int32_t state {-1};
        int32_t trigger {-1};
        int32_t aux_trigger {-1};
        int32_t palette_state_0 {-1};
        int32_t palette_state_1 {-1};
        uint32_t block_interactive_ops {0xFFFFFFFFu};
    };

    enum RollbackNativePreNewRoundCinematicReleaseMismatch : uint32_t
    {
        RollbackNativePreNewRoundCinematicReleaseMismatchNone = 0,
        RollbackNativePreNewRoundCinematicReleaseMismatchUnreadable = 1u << 0,
        RollbackNativePreNewRoundCinematicReleaseMismatchState = 1u << 1,
        RollbackNativePreNewRoundCinematicReleaseMismatchTrigger = 1u << 2,
        RollbackNativePreNewRoundCinematicReleaseMismatchAuxTrigger = 1u << 3,
        RollbackNativePreNewRoundCinematicReleaseMismatchPalette = 1u << 4,
        RollbackNativePreNewRoundCinematicReleaseMismatchInteractiveBlock =
            1u << 5,
    };

    static constexpr uint32_t
    RollbackNativePreNewRoundCinematicReleaseMismatchMask(
        const RollbackNativePreNewRoundCinematicReleaseObservation& value)
        noexcept
    {
        uint32_t mask =
            RollbackNativePreNewRoundCinematicReleaseMismatchNone;
        if (!value.readable)
            mask |= RollbackNativePreNewRoundCinematicReleaseMismatchUnreadable;
        if (value.state != 0 && value.state != 1)
            mask |= RollbackNativePreNewRoundCinematicReleaseMismatchState;
        if (value.trigger != 0)
            mask |= RollbackNativePreNewRoundCinematicReleaseMismatchTrigger;
        if (value.aux_trigger != 0)
            mask |= RollbackNativePreNewRoundCinematicReleaseMismatchAuxTrigger;
        if (value.palette_state_0 != 0 || value.palette_state_1 != 0)
            mask |= RollbackNativePreNewRoundCinematicReleaseMismatchPalette;
        if (value.block_interactive_ops != 0)
            mask |=
                RollbackNativePreNewRoundCinematicReleaseMismatchInteractiveBlock;
        return mask;
    }

    static constexpr bool RollbackNativePreNewRoundCinematicReadyForRelease(
        const RollbackNativePreNewRoundCinematicReleaseObservation& value)
        noexcept
    {
        return RollbackNativePreNewRoundCinematicReleaseMismatchMask(value)
            == RollbackNativePreNewRoundCinematicReleaseMismatchNone;
    }

    struct RollbackNativePreNewRoundGateReport
    {
        uint64_t arrivals {0};
        uint64_t holds {0};
        uint64_t releases {0};
        bool failed {false};
        const char* failure {"ok"};
    };

    class RollbackNativePreNewRoundGate
    {
    public:
        void reset() noexcept
        {
            m_local = {};
            m_peer = {};
            m_local_valid = false;
            m_peer_valid = false;
            m_released = false;
            m_report = {};
        }

        bool arrive(const RollbackPreNewRoundBarrierMessage& local) noexcept
        {
            if (!RollbackPreNewRoundBarrierValid(local)
                || local.stage != RollbackPreNewRoundBarrierStage::Ready)
            {
                return fail("pre-new-round-local-evidence-invalid");
            }
            if (m_released)
                return fail("pre-new-round-arrival-after-release");
            if (m_local_valid)
            {
                if (!RollbackPreNewRoundBarrierSamePeerIdentity(
                        m_local, local))
                    return fail("pre-new-round-local-evidence-changed");
                return true;
            }
            m_local = local;
            m_local_valid = true;
            if (m_peer_valid
                && !RollbackPreNewRoundBarriersMatch(m_local, m_peer))
            {
                return fail("pre-new-round-peer-evidence-mismatch");
            }
            ++m_report.arrivals;
            return true;
        }

        bool accept_peer(
            const RollbackPreNewRoundBarrierMessage& peer) noexcept
        {
            if (!RollbackPreNewRoundBarrierValid(peer))
                return fail("pre-new-round-peer-evidence-invalid");
            if (m_local_valid
                && !RollbackPreNewRoundBarriersMatch(m_local, peer))
            {
                return fail("pre-new-round-peer-evidence-mismatch");
            }
            if (m_peer_valid
                && !RollbackPreNewRoundBarrierSamePeerIdentity(
                    m_peer, peer))
            {
                return fail("pre-new-round-peer-evidence-changed");
            }
            if (m_peer_valid && static_cast<uint8_t>(peer.stage)
                    < static_cast<uint8_t>(m_peer.stage))
            {
                return true;
            }
            m_peer = peer;
            m_peer_valid = true;
            return true;
        }

        bool prepare_local_transition_preserving_peer() noexcept
        {
            if (m_local_valid || m_released)
                return fail("pre-new-round-local-state-not-clear");
            m_local = {};
            m_local_valid = false;
            m_released = false;
            m_report = {};
            return true;
        }

        RollbackNativePreNewRoundAction action(bool gate_active) noexcept
        {
            if (!gate_active)
                return RollbackNativePreNewRoundAction::PassThrough;
            if (m_report.failed || !m_local_valid)
                return RollbackNativePreNewRoundAction::Reject;
            if (!m_peer_valid
                || !RollbackPreNewRoundBarriersMatch(m_local, m_peer))
            {
                ++m_report.holds;
                return RollbackNativePreNewRoundAction::Hold;
            }
            if (m_released)
                return RollbackNativePreNewRoundAction::Reject;
            return RollbackNativePreNewRoundAction::Release;
        }

        bool mark_released() noexcept
        {
            if (m_released || !m_peer_valid
                || !RollbackPreNewRoundBarriersMatch(m_local, m_peer))
            {
                return fail("pre-new-round-release-invalid");
            }
            m_released = true;
            m_local.stage = RollbackPreNewRoundBarrierStage::Accepted;
            ++m_report.releases;
            return true;
        }

        bool local_valid() const noexcept { return m_local_valid; }
        bool peer_valid() const noexcept { return m_peer_valid; }
        bool released() const noexcept { return m_released; }
        bool release_ready() const noexcept
        {
            return !m_report.failed && m_local_valid && m_peer_valid
                && !m_released
                && RollbackPreNewRoundBarriersMatch(m_local, m_peer);
        }
        bool complete() const noexcept
        {
            return m_local_valid && m_peer_valid
                && RollbackPreNewRoundBarrierComplete(m_local, m_peer);
        }
        const RollbackPreNewRoundBarrierMessage& local() const noexcept
        {
            return m_local;
        }
        const RollbackPreNewRoundBarrierMessage& peer() const noexcept
        {
            return m_peer;
        }
        const RollbackNativePreNewRoundGateReport& report() const noexcept
        {
            return m_report;
        }

    private:
        bool fail(const char* failure) noexcept
        {
            m_report.failed = true;
            m_report.failure = failure;
            return false;
        }

        RollbackPreNewRoundBarrierMessage m_local {};
        RollbackPreNewRoundBarrierMessage m_peer {};
        bool m_local_valid {false};
        bool m_peer_valid {false};
        bool m_released {false};
        RollbackNativePreNewRoundGateReport m_report {};
    };

    struct RollbackPreNewRoundReceiveResult
    {
        RollbackPreNewRoundPacketDisposition disposition {
            RollbackPreNewRoundPacketDisposition::Invalid};
        bool accepted {false};
    };

    static inline RollbackPreNewRoundReceiveResult
    ReceiveRollbackPreNewRoundBarrier(
        RollbackNativePreNewRoundGate& gate,
        const RollbackPreNewRoundBarrierMessage& message,
        const RollbackPreNewRoundExpectedIdentity& expected) noexcept
    {
        const RollbackPreNewRoundPacketDisposition disposition =
            ClassifyRollbackPreNewRoundPacket(message, expected);
        if (disposition == RollbackPreNewRoundPacketDisposition::Stale)
            return {disposition, true};
        if (disposition != RollbackPreNewRoundPacketDisposition::Current)
            return {disposition, false};
        return {disposition, gate.accept_peer(message)};
    }

    static constexpr bool RollbackNativePreNewRoundTransitionHeld(
        bool local_valid,
        bool released,
        bool complete,
        bool post_release_validation_pending) noexcept
    {
        return local_valid
            && (!released || post_release_validation_pending || !complete);
    }

    static inline bool RollbackNativePreNewRoundTransitionHeld(
        const RollbackNativePreNewRoundGate& gate,
        bool post_release_validation_pending) noexcept
    {
        return RollbackNativePreNewRoundTransitionHeld(
            gate.local_valid(), gate.released(), gate.complete(),
            post_release_validation_pending);
    }

    static constexpr bool RollbackNativePreNewRoundServiceReleaseAllowed(
        bool stock_inter_round,
        bool on_simulation_thread,
        bool local_valid,
        bool released,
        bool post_release_validation_pending,
        bool complete,
        bool release_ready) noexcept
    {
        return stock_inter_round
            && on_simulation_thread
            && local_valid
            && !released
            && !post_release_validation_pending
            && !complete
            && release_ready;
    }

    static inline bool RollbackNativePreNewRoundServiceReleaseAllowed(
        const RollbackNativePreNewRoundGate& gate,
        bool stock_inter_round,
        bool on_simulation_thread,
        bool post_release_validation_pending) noexcept
    {
        return RollbackNativePreNewRoundServiceReleaseAllowed(
            stock_inter_round, on_simulation_thread, gate.local_valid(),
            gate.released(), post_release_validation_pending,
            gate.complete(), gate.release_ready());
    }

    static constexpr bool RollbackNativePreNewRoundShutdownAllowed(
        bool transition_held,
        bool out_of_battle_authorized) noexcept
    {
        return !transition_held || out_of_battle_authorized;
    }

    static constexpr bool RollbackNativePreNewRoundSuppressOuterCall(
        bool local_valid,
        bool release_ready,
        bool post_release_validation_pending,
        bool complete) noexcept
    {
        if (!local_valid) return false;
        if (post_release_validation_pending) return true;
        if (complete) return false;
        return !release_ready;
    }

    static inline bool RollbackNativePreNewRoundSuppressOuterCall(
        const RollbackNativePreNewRoundGate& gate,
        bool post_release_validation_pending) noexcept
    {
        return RollbackNativePreNewRoundSuppressOuterCall(
            gate.local_valid(), gate.release_ready(),
            post_release_validation_pending, gate.complete());
    }

    static inline bool RollbackNativePreNewRoundSuppressSimulationLoop(
        const RollbackNativePreNewRoundGate& gate,
        bool post_release_validation_pending,
        bool service_release_needs_outer_iteration) noexcept
    {
        return !service_release_needs_outer_iteration
            && RollbackNativePreNewRoundSuppressOuterCall(
                gate, post_release_validation_pending);
    }

    static constexpr bool RollbackNativeInterRoundServiceTickAllowed(
        bool stock_inter_round,
        bool on_simulation_thread,
        bool gate_complete,
        uint64_t simulation_hook_entries,
        uint64_t observed_simulation_hook_entries) noexcept
    {
        return stock_inter_round
            && on_simulation_thread
            && gate_complete
            && simulation_hook_entries == observed_simulation_hook_entries;
    }

    static constexpr bool RollbackNativePreNewRoundReleaseTransitAllowed(
        uintptr_t current_mode, uintptr_t expected_mode,
        bool release_ready) noexcept
    {
        return current_mode == expected_mode && release_ready;
    }

    static inline bool RollbackNativePreNewRoundReleaseTransitAllowed(
        uintptr_t current_mode, uintptr_t expected_mode,
        const RollbackNativePreNewRoundGate& gate) noexcept
    {
        return RollbackNativePreNewRoundReleaseTransitAllowed(
            current_mode, expected_mode, gate.release_ready());
    }

    // The final RoundResult native call enters PreNewRound reentrantly. The
    // transition detour defers that first callback until the outer call has
    // captured carried state. Admit exactly the following outer iteration:
    // once local evidence arrives this predicate becomes false.
    static constexpr bool RollbackNativePreNewRoundDeferredEntryAllowed(
        uintptr_t current_mode,
        uintptr_t expected_mode,
        bool transition_callback_deferred,
        bool carried_state_captured,
        bool local_gate_valid) noexcept
    {
        return current_mode == expected_mode
            && transition_callback_deferred
            && carried_state_captured
            && !local_gate_valid;
    }

    struct RollbackNativePreNewRoundHeldReentryObservation
    {
        bool memory_readable {false};
        bool stable_match_identity {false};
        bool retained_control_identity_current {false};
        bool local_evidence_retained {false};
        bool released {false};
        uintptr_t current_mode {0};
        uintptr_t queued_mode {0};
        uintptr_t expected_pre_new_round_mode {0};
        uint32_t live_round_ordinal {0};
        uint32_t target_round_ordinal {0};
        // Battle status is validated on the retained first entry. SC6 may
        // advance it while this peer waits, so it is not reentry identity.
        uint8_t battle_main_state {0};
    };

    enum RollbackNativePreNewRoundHeldReentryMismatch : uint32_t
    {
        RollbackNativePreNewRoundHeldReentryMismatchNone = 0,
        RollbackNativePreNewRoundHeldReentryMismatchUnreadable = 1u << 0,
        RollbackNativePreNewRoundHeldReentryMismatchMatch = 1u << 1,
        RollbackNativePreNewRoundHeldReentryMismatchControl = 1u << 2,
        RollbackNativePreNewRoundHeldReentryMismatchLocal = 1u << 3,
        RollbackNativePreNewRoundHeldReentryMismatchReleased = 1u << 4,
        RollbackNativePreNewRoundHeldReentryMismatchCurrentMode = 1u << 5,
        RollbackNativePreNewRoundHeldReentryMismatchQueuedMode = 1u << 6,
        RollbackNativePreNewRoundHeldReentryMismatchOrdinal = 1u << 7,
        RollbackNativePreNewRoundHeldReentryMismatchMainState = 1u << 8,
    };

    static constexpr uint32_t RollbackNativePreNewRoundHeldReentryMismatchMask(
        const RollbackNativePreNewRoundHeldReentryObservation& value) noexcept
    {
        uint32_t mask = RollbackNativePreNewRoundHeldReentryMismatchNone;
        if (!value.memory_readable)
            mask |= RollbackNativePreNewRoundHeldReentryMismatchUnreadable;
        if (!value.stable_match_identity)
            mask |= RollbackNativePreNewRoundHeldReentryMismatchMatch;
        if (!value.retained_control_identity_current)
            mask |= RollbackNativePreNewRoundHeldReentryMismatchControl;
        if (!value.local_evidence_retained)
            mask |= RollbackNativePreNewRoundHeldReentryMismatchLocal;
        if (value.released)
            mask |= RollbackNativePreNewRoundHeldReentryMismatchReleased;
        if (value.current_mode != value.expected_pre_new_round_mode)
            mask |= RollbackNativePreNewRoundHeldReentryMismatchCurrentMode;
        if (value.queued_mode != 0)
            mask |= RollbackNativePreNewRoundHeldReentryMismatchQueuedMode;
        if ((value.live_round_ordinal & 0xFFFFu)
                != (value.target_round_ordinal & 0xFFFFu))
            mask |= RollbackNativePreNewRoundHeldReentryMismatchOrdinal;
        if (value.battle_main_state != 2)
            mask |= RollbackNativePreNewRoundHeldReentryMismatchMainState;
        return mask;
    }

    static constexpr bool ValidateRollbackNativePreNewRoundHeldReentry(
        const RollbackNativePreNewRoundHeldReentryObservation& value) noexcept
    {
        return RollbackNativePreNewRoundHeldReentryMismatchMask(value)
            == RollbackNativePreNewRoundHeldReentryMismatchNone;
    }

    // Diagnostic contract for an unexpected callback after release. Stable
    // production flow prevents this by waiting for cinematic cleanup before
    // the single E60 PostTick call; observing this state afterward is retained
    // for attribution and must fail closed rather than suppressing a missing
    // transition or replaying broad activation side effects.
    struct RollbackNativePreNewRoundPendingValidationReentryObservation
    {
        bool memory_readable {false};
        bool stable_match_identity {false};
        bool retained_control_identity_current {false};
        bool local_evidence_retained {false};
        bool released {false};
        bool post_release_validation_pending {false};
        uintptr_t current_mode {0};
        uintptr_t queued_mode {0};
        uintptr_t expected_pre_new_round_mode {0};
        uint32_t live_round_ordinal {0};
        uint32_t target_round_ordinal {0};
        uint8_t battle_main_state {0};
    };

    enum RollbackNativePreNewRoundPendingValidationReentryMismatch : uint32_t
    {
        RollbackNativePreNewRoundPendingValidationReentryMismatchNone = 0,
        RollbackNativePreNewRoundPendingValidationReentryMismatchUnreadable =
            1u << 0,
        RollbackNativePreNewRoundPendingValidationReentryMismatchMatch =
            1u << 1,
        RollbackNativePreNewRoundPendingValidationReentryMismatchControl =
            1u << 2,
        RollbackNativePreNewRoundPendingValidationReentryMismatchLocal =
            1u << 3,
        RollbackNativePreNewRoundPendingValidationReentryMismatchReleased =
            1u << 4,
        RollbackNativePreNewRoundPendingValidationReentryMismatchPending =
            1u << 5,
        RollbackNativePreNewRoundPendingValidationReentryMismatchCurrentMode =
            1u << 6,
        RollbackNativePreNewRoundPendingValidationReentryMismatchQueuedMode =
            1u << 7,
        RollbackNativePreNewRoundPendingValidationReentryMismatchOrdinal =
            1u << 8,
        RollbackNativePreNewRoundPendingValidationReentryMismatchMainState =
            1u << 9,
    };

    static constexpr uint32_t
    RollbackNativePreNewRoundPendingValidationReentryMismatchMask(
        const RollbackNativePreNewRoundPendingValidationReentryObservation&
            value) noexcept
    {
        uint32_t mask =
            RollbackNativePreNewRoundPendingValidationReentryMismatchNone;
        if (!value.memory_readable)
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchUnreadable;
        if (!value.stable_match_identity)
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchMatch;
        if (!value.retained_control_identity_current)
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchControl;
        if (!value.local_evidence_retained)
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchLocal;
        if (!value.released)
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchReleased;
        if (!value.post_release_validation_pending)
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchPending;
        if (value.current_mode != value.expected_pre_new_round_mode)
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchCurrentMode;
        if (value.queued_mode != 0)
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchQueuedMode;
        if ((value.live_round_ordinal & 0xFFFFu)
                != (value.target_round_ordinal & 0xFFFFu))
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchOrdinal;
        if (value.battle_main_state != 2)
            mask |= RollbackNativePreNewRoundPendingValidationReentryMismatchMainState;
        return mask;
    }

    static constexpr bool
    ValidateRollbackNativePreNewRoundPendingValidationReentry(
        const RollbackNativePreNewRoundPendingValidationReentryObservation&
            value) noexcept
    {
        return RollbackNativePreNewRoundPendingValidationReentryMismatchMask(
                   value)
            == RollbackNativePreNewRoundPendingValidationReentryMismatchNone;
    }

    struct RollbackNativePreNewRoundPostReleaseObservation
    {
        bool memory_readable {false};
        bool stable_match_identity {false};
        bool released {false};
        uintptr_t current_mode {0};
        uintptr_t queued_mode {0};
        uintptr_t expected_new_round_mode {0};
        uint32_t mode_frame {0};
        uint32_t phase_timer {0};
        uint64_t per_frame_calls {0};
        uint64_t original_calls {0};
        uint64_t releases {0};
        uint64_t entry_serial {0};
        uint64_t release_serial {0};
    };

    static constexpr bool ValidateRollbackNativePreNewRoundPostRelease(
        const RollbackNativePreNewRoundPostReleaseObservation& value) noexcept
    {
        return value.memory_readable && value.stable_match_identity
            && value.released
            && value.current_mode == value.expected_new_round_mode
            && value.queued_mode == 0
            && value.mode_frame == 1 && value.phase_timer == 120
            && value.per_frame_calls == 1
            && value.original_calls == 1 && value.releases == 1
            && value.entry_serial != 0
            && value.release_serial > value.entry_serial;
    }
}
