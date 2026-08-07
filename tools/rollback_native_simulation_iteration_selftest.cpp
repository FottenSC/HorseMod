#include "RollbackInstallOnceCallbackGate.hpp"
#include "RollbackLiveToken.hpp"
#include "RollbackNativeSimulationIteration.hpp"
#include "RollbackNativeInputCallbackSnapshot.hpp"
#include "RollbackNativeSimulationState.hpp"
#include "RollbackStepStateStorage.hpp"
#include "RollbackPreallocatedCaptureGate.hpp"
#include "RollbackPreallocatedHgCpuCapacity.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    struct MockStorageNode
    {
        uintptr_t address {0};
        uintptr_t vtable {0};
        std::vector<uint8_t> bytes;
    };
    struct MockStorageChain { uintptr_t address {0}; };
    struct MockStorageList { std::vector<uint32_t> nodes; };
    struct MockStorageState
    {
        struct HgCpu
        {
            std::vector<uint8_t> bytes;
            std::array<MockStorageList, 2> khit_topology;
            struct { std::vector<uint8_t> control_bytes, bytes; } motion_banks;
            struct { std::vector<uint8_t> bytes; } motion_tail;
            struct
            {
                uintptr_t chara[2] {};
                std::vector<uint8_t> inline_bytes;
                std::vector<MockStorageNode> aux_nodes;
                std::vector<MockStorageChain> chains;
                std::vector<MockStorageNode> spring_nodes;
            } skeleton_runtime;
            struct
            {
                std::vector<uint8_t> root_bytes, backing_bytes;
                std::vector<uint32_t> nodes;
            } timer_node;
        } hgcpu;
        struct
        {
            std::vector<uint8_t> payload;
            std::vector<uint32_t> writer_nodes;
        } palette_variants;
        struct
        {
            std::vector<uint8_t> bytes;
            std::vector<uint32_t> ranges;
        } explicit_snapshot;
        struct { std::vector<uint32_t> records; } breakable_stage;
        uint64_t canonical_hash {0};
    };

    struct MockMemory
    {
        std::array<uint8_t, 0x2000> bytes {};

        bool read(uintptr_t address, void* out, size_t size) const noexcept
        {
            if (address + size > bytes.size()) return false;
            std::memcpy(out, bytes.data() + address, size);
            return true;
        }

        bool write(uintptr_t address, const void* in, size_t size) noexcept
        {
            if (address + size > bytes.size()) return false;
            std::memcpy(bytes.data() + address, in, size);
            return true;
        }
    };

    struct MockOneInputDelta
    {
        enum Step : uint8_t { PerFrame, Callback, RoundState, ActiveState };
        std::array<Step, 8> order {};
        uint32_t order_count {0};
        int32_t fighter_state {0};
        int32_t callback_state {0};
        uint8_t round_state {1};
        uint32_t active_state {0};
        bool loop_again {false};
        uint32_t input_delta_count {0};
        uint32_t input_pair_injections {0};
        uint32_t native_substeps {0};

        void run_one_input_delta(int32_t input) noexcept
        {
            ++input_delta_count;
            ++input_pair_injections;
            do
            {
                ++native_substeps;
                const bool repeat = loop_again;
                loop_again = false;
                order[order_count++] = PerFrame;
                fighter_state += input;
                order[order_count++] = Callback;
                ++callback_state;
                order[order_count++] = RoundState;
                if (round_state == 1) round_state = 2;
                order[order_count++] = ActiveState;
                active_state = fighter_state != 0 ? 1u : 0u;
                if (!repeat) break;
            } while (true);
        }
    };
}

int main()
{
    if (Horse::RollbackOwnedSimulationServiceTickAllowed(
            true, true, true, true, true, false, 3)
        || Horse::RollbackOwnedSimulationServiceTickAllowed(
            true, true, true, true, false, false, 2)
        || !Horse::RollbackOwnedSimulationServiceTickAllowed(
            true, true, true, true, false, false, 3)
        || !Horse::RollbackOwnedSimulationServiceTickAllowed(
            true, true, true, true, false, true, 0)
        || Horse::RollbackOwnedSimulationServiceTickAllowed(
            false, true, true, true, false, true, 3)
        || Horse::RollbackOwnedSimulationServiceTickAllowed(
            true, false, true, true, false, true, 3)
        || Horse::RollbackOwnedSimulationServiceTickAllowed(
            true, true, false, true, false, true, 3)
        || Horse::RollbackOwnedSimulationServiceTickAllowed(
            true, true, true, false, false, true, 3))
        return 260;

    if (!Horse::ShouldTraceRollbackNativeRngCallers(
            3, 32, false, true, true)
        || !Horse::ShouldTraceRollbackNativeRngCallers(
            3, 32, true, true, true)
        || Horse::ShouldTraceRollbackNativeRngCallers(
            32, 32, false, true, true)
        || Horse::ShouldTraceRollbackNativeRngCallers(
            3, 32, true, false, true)
        || Horse::ShouldTraceRollbackNativeRngCallers(
            3, 32, true, true, false))
    {
        std::fprintf(
            stderr,
            "RNG caller tracing must cover forward and rollback attempts\n");
        return 1;
    }
    using InputPairDisposition =
        Horse::RollbackNativeInputPairHookDisposition;
    if (Horse::ClassifyRollbackNativeInputPairHookInvocation(
            0x1000, 0x2210, 0x24A8, 0)
            != InputPairDisposition::PublishExpected
        || Horse::ClassifyRollbackNativeInputPairHookInvocation(
            0x1000, 0x2210, 0x24A8, 1)
            != InputPairDisposition::RepeatedExpected
        || Horse::ClassifyRollbackNativeInputPairHookInvocation(
            0x1000, 0x2210, 0x24A9, 0)
            != InputPairDisposition::InvalidExpectedBoundary
        || Horse::ClassifyRollbackNativeInputPairHookInvocation(
            0x1000, 0x3000, 0x24A8, 1)
            != InputPairDisposition::PassThroughUnrelated
        || Horse::ClassifyRollbackNativeInputPairHookInvocation(
            0x1000, 0x3000, 0x4000, 0)
            != InputPairDisposition::PassThroughUnrelated
        || Horse::ClassifyRollbackNativeInputPairHookInvocation(
            0, 0x1210, 0x14A8, 0)
            != InputPairDisposition::InvalidExpectedBoundary)
        return 102;

    Horse::RollbackNativeSimulationClock before {};
    before.input_log_last_frame = 44;
    before.input_log_master_clock = 120;
    before.battle_last_frame = 12;
    before.battle_last_applied = 19;
    Horse::RollbackNativeSimulationClock scoped {};
    if (!Horse::PrepareRollbackNativeSingleIteration(before, scoped)
        || scoped.battle_last_frame != 44
        || scoped.battle_last_applied != 119
        || !Horse::ValidateRollbackNativeSimulationClockArm(scoped, scoped)
        || Horse::ValidateRollbackNativeSimulationClockArm(
            scoped, before)
        || !Horse::ValidateRollbackNativeSingleIterationResult(
            scoped, 44, 120, 44, 120)
        || Horse::ValidateRollbackNativeSingleIterationResult(
            scoped, 44, 121, 44, 120))
        return 1;

    Horse::RollbackNativeSimulationClock aligned {};
    uint32_t discarded_frames = 0;
    if (!Horse::PrepareRollbackNativeNoCatchUpHandoff(
            before, aligned, discarded_frames)
        || aligned.battle_last_frame != before.input_log_last_frame
        || aligned.battle_last_applied
            != before.input_log_master_clock - 1u
        || discarded_frames != 119)
        return 103;
    uint32_t pending_delta = 0;
    if (!Horse::RollbackNativePendingDelta(aligned, pending_delta)
        || pending_delta != 1)
        return 104;
    Horse::RollbackNativeSimulationClock bounded {};
    if (!Horse::PrepareRollbackNativeBoundedPassThrough(
            before, 120, bounded, discarded_frames)
        || bounded.battle_last_frame != before.input_log_last_frame
        || bounded.battle_last_applied
            != before.input_log_master_clock - 1u
        || discarded_frames != 119
        || !Horse::RollbackNativePendingDelta(bounded, pending_delta)
        || pending_delta != 1
        || Horse::PrepareRollbackNativeBoundedPassThrough(
            before, 119, bounded, discarded_frames))
        return 227;
    if (!Horse::PrepareRollbackNativeBoundedPassThrough(
            bounded, 1, aligned, discarded_frames)
        || std::memcmp(&bounded, &aligned, sizeof(bounded)) != 0
        || discarded_frames != 0)
        return 228;
    Horse::RollbackNativeSimulationClock live_terminal_backlog {};
    live_terminal_backlog.input_log_last_frame = 0;
    live_terminal_backlog.input_log_master_clock = 2083;
    live_terminal_backlog.battle_last_frame = 0;
    live_terminal_backlog.battle_last_applied = 118;
    if (!Horse::PrepareRollbackNativeNoCatchUpHandoff(
            live_terminal_backlog, aligned, discarded_frames)
        || aligned.battle_last_frame != 0
        || aligned.battle_last_applied != 2082
        || discarded_frames != 1964
        || !Horse::RollbackNativePendingDelta(aligned, pending_delta)
        || pending_delta != 1)
        return 210;
    if (!Horse::ValidateRollbackNativeNoCatchUpHandoffObservation(
            1965, 1964, 1, 1, 1, 0)
        || Horse::ValidateRollbackNativeNoCatchUpHandoffObservation(
            1965, 1965, 1, 1, 1, 0)
        || Horse::ValidateRollbackNativeNoCatchUpHandoffObservation(
            1965, 1964, 0, 1, 1, 0)
        || Horse::ValidateRollbackNativeNoCatchUpHandoffObservation(
            1965, 1964, 1, 2, 1, 0)
        || Horse::ValidateRollbackNativeNoCatchUpHandoffObservation(
            1965, 1964, 1, 1, 0, 0)
        || Horse::ValidateRollbackNativeNoCatchUpHandoffObservation(
            1965, 1964, 1, 1, 1, 1))
        return 211;
    if (!Horse::ValidateRollbackNativeInterRoundControlTick(
            1, 1, 1, 0)
        || Horse::ValidateRollbackNativeInterRoundControlTick(
            0, 1, 1, 0)
        || Horse::ValidateRollbackNativeInterRoundControlTick(
            1, 2, 1, 0)
        || Horse::ValidateRollbackNativeInterRoundControlTick(
            1, 1, 0, 0)
        || Horse::ValidateRollbackNativeInterRoundControlTick(
            1, 1, 1, 1))
        return 212;
    if (!Horse::ValidateRollbackNativeInterRoundPassThrough(
            0, 0, 1, 0, true, false)
        || !Horse::ValidateRollbackNativeInterRoundPassThrough(
            1, 0, 1, 1, true, false)
        || !Horse::ValidateRollbackNativeInterRoundPassThrough(
            1, 1, 1, 1, true, false)
        || Horse::ValidateRollbackNativeInterRoundPassThrough(
            2, 0, 1, 2, true, false)
        || Horse::ValidateRollbackNativeInterRoundPassThrough(
            1, 0, 2, 1, true, false)
        || Horse::ValidateRollbackNativeInterRoundPassThrough(
            1, 0, 1, 2, true, false)
        || Horse::ValidateRollbackNativeInterRoundPassThrough(
            1, 0, 1, 1, false, false)
        || Horse::ValidateRollbackNativeInterRoundPassThrough(
            1, 0, 1, 1, true, true))
        return 226;
    using InterRoundSchedulingAction =
        Horse::RollbackNativeInterRoundSchedulingAction;
    if (Horse::ClassifyRollbackNativeInterRoundSchedulingAction(
            false, false, false, 0)
            != InterRoundSchedulingAction::Reject
        || Horse::ClassifyRollbackNativeInterRoundSchedulingAction(
            true, false, false, 0)
            != InterRoundSchedulingAction::AlignTerminalBacklog
        || Horse::ClassifyRollbackNativeInterRoundSchedulingAction(
            true, false, true, 1)
            != InterRoundSchedulingAction::AlignTerminalBacklog
        || Horse::ClassifyRollbackNativeInterRoundSchedulingAction(
             true, true, false, 0)
            != InterRoundSchedulingAction::AwaitNativeClock
        || Horse::ClassifyRollbackNativeInterRoundSchedulingAction(
             true, true, true, 0)
            != InterRoundSchedulingAction::AwaitNativeClock
        || Horse::ClassifyRollbackNativeInterRoundSchedulingAction(
             true, true, false, 1)
            != InterRoundSchedulingAction::ControlSingleIteration
        || Horse::ClassifyRollbackNativeInterRoundSchedulingAction(
             true, true, true, 1)
            != InterRoundSchedulingAction::PassThroughNative
        || Horse::ClassifyRollbackNativeInterRoundSchedulingAction(
             true, true, true, 2)
            != InterRoundSchedulingAction::PassThroughNative)
        return 227;
    const bool candidate_before_arm_case =
        Horse::ClassifyRollbackNativeInterRoundOuterAction(
            true,
            Horse::RollbackNativeInterRoundControlState::ActiveGameplay)
        == Horse::RollbackNativeInterRoundOuterAction::FreezeCandidate;
    const bool active_gameplay_refusal_case =
        Horse::ClassifyRollbackNativeInterRoundControlState(
            true, true, false, true, 2, 2)
            == Horse::RollbackNativeInterRoundControlState::ActiveGameplay
        && Horse::ClassifyRollbackNativeInterRoundControlState(
            true, true, true, false, 2, 2)
            == Horse::RollbackNativeInterRoundControlState::ActiveGameplay
        && Horse::ClassifyRollbackNativeInterRoundOuterAction(
            false,
            Horse::RollbackNativeInterRoundControlState::ActiveGameplay)
            == Horse::RollbackNativeInterRoundOuterAction::FailClosed;
    const bool verified_inter_round_state_case =
        Horse::ClassifyRollbackNativeInterRoundControlState(
            true, true, true, false, 2, 3)
            == Horse::RollbackNativeInterRoundControlState::Allowed
        && Horse::ClassifyRollbackNativeInterRoundControlState(
            true, true, true, true, 2, 3)
            == Horse::RollbackNativeInterRoundControlState::Allowed
        && Horse::ClassifyRollbackNativeInterRoundOuterAction(
            false, Horse::RollbackNativeInterRoundControlState::Allowed)
            == Horse::RollbackNativeInterRoundOuterAction::ArmControlTick
        && Horse::ClassifyRollbackNativeInterRoundControlState(
            true, true, false, false, 2, 3)
            == Horse::RollbackNativeInterRoundControlState::
                UnexpectedWorldMode
        && Horse::ClassifyRollbackNativeInterRoundControlState(
            true, true, true, false, 2, 7)
            == Horse::RollbackNativeInterRoundControlState::
                UnexpectedBattleState
        && Horse::ClassifyRollbackNativeInterRoundControlState(
            false, true, true, false, 2, 3)
            == Horse::RollbackNativeInterRoundControlState::Unreadable;
    const bool exact_active_pregameplay_bridge_case =
        Horse::RollbackNativeCurrentActivePreGameplayBridgeAllowed(
            true, true, 2, 1)
        && Horse::RollbackNativeCurrentActivePreGameplayBridgeAllowed(
            true, true, 2, 3)
        && !Horse::RollbackNativeCurrentActivePreGameplayBridgeAllowed(
            false, true, 2, 3)
        && !Horse::RollbackNativeCurrentActivePreGameplayBridgeAllowed(
            true, false, 2, 3)
        && !Horse::RollbackNativeCurrentActivePreGameplayBridgeAllowed(
            true, true, 4, 3)
        && !Horse::RollbackNativeCurrentActivePreGameplayBridgeAllowed(
            true, true, 2, 2)
        && !Horse::RollbackNativeCurrentActivePreGameplayBridgeAllowed(
            true, true, 2, 5)
        && !Horse::RollbackNativeCurrentActivePreGameplayBridgeAllowed(
            true, true, 2, 9);
    if (!candidate_before_arm_case
        || !active_gameplay_refusal_case
        || !verified_inter_round_state_case
        || !exact_active_pregameplay_bridge_case)
        return 215;
    auto inter_round_clock = aligned;
    uint64_t repeated_native_calls = 0;
    uint64_t repeated_per_frame_calls = 0;
    bool repeated_exact_one_control_tick_case = true;
    for (uint32_t tick = 0; tick < 4; ++tick)
    {
        Horse::RollbackNativeSimulationClock armed {};
        if (!Horse::PrepareRollbackNativeSingleIteration(
                inter_round_clock, armed)
            || !Horse::RollbackNativePendingDelta(armed, pending_delta)
            || pending_delta != 1)
            repeated_exact_one_control_tick_case = false;
        ++repeated_native_calls;
        ++repeated_per_frame_calls;
        inter_round_clock = armed;
        inter_round_clock.battle_last_applied =
            inter_round_clock.input_log_master_clock;
        if (!Horse::RollbackNativePendingDelta(
                inter_round_clock, pending_delta)
            || !Horse::ValidateRollbackNativeInterRoundControlTick(
                1, 1, 1, pending_delta))
            repeated_exact_one_control_tick_case = false;
    }
    if (!repeated_exact_one_control_tick_case
        || repeated_native_calls != 4
        || repeated_per_frame_calls != 4)
        return 216;

    struct MockInterRoundControlledRoute
    {
        Horse::RollbackNativeSimulationClock clock {};
        bool match_identity_stable {true};
        Horse::RollbackNativeInterRoundControlState post_state {
            Horse::RollbackNativeInterRoundControlState::Allowed};
        uint32_t original_calls {0};
        uint32_t per_frame_calls {0};
        uint32_t successful_calls {0};
        uint32_t failed_calls {0};

        void run()
        {
            Horse::RollbackNativeSimulationClock armed {};
            uint32_t pending_armed = 0;
            if (!Horse::PrepareRollbackNativeSingleIteration(clock, armed)
                || !Horse::RollbackNativePendingDelta(
                    armed, pending_armed))
            {
                ++failed_calls;
                return;
            }
            ++original_calls;
            ++per_frame_calls;
            clock = armed;
            clock.battle_last_applied = clock.input_log_master_clock;
            uint32_t pending_after = 0;
            const bool after_readable =
                Horse::RollbackNativePendingDelta(clock, pending_after);
            if (after_readable
                && Horse::ValidateRollbackNativeInterRoundControlledPostCall(
                    pending_armed, 1, per_frame_calls, pending_after,
                    match_identity_stable, post_state))
            {
                ++successful_calls;
            }
            else
            {
                ++failed_calls;
            }
        }
    };
    MockInterRoundControlledRoute valid_control_route {aligned};
    valid_control_route.run();
    MockInterRoundControlledRoute changed_identity_route {aligned};
    changed_identity_route.match_identity_stable = false;
    changed_identity_route.run();
    MockInterRoundControlledRoute active_gameplay_route {aligned};
    active_gameplay_route.post_state =
        Horse::RollbackNativeInterRoundControlState::ActiveGameplay;
    active_gameplay_route.run();
    if (valid_control_route.original_calls != 1
        || valid_control_route.per_frame_calls != 1
        || valid_control_route.successful_calls != 1
        || valid_control_route.failed_calls != 0
        || changed_identity_route.original_calls != 1
        || changed_identity_route.per_frame_calls != 1
        || changed_identity_route.successful_calls != 0
        || changed_identity_route.failed_calls != 1
        || active_gameplay_route.original_calls != 1
        || active_gameplay_route.per_frame_calls != 1
        || active_gameplay_route.successful_calls != 0
        || active_gameplay_route.failed_calls != 1)
        return 228;
    // NewRound OnEnter can publish a new InputLog frame identity while the
    // retained BattleManager still carries the completed round's cursor.
    // The existing scoped arm must reduce that cross-round backlog to exactly
    // one native iteration before the complete SimulationLoop runs.
    Horse::RollbackNativeSimulationClock new_round_clock {
        8, 120, 7, 2091,
    };
    Horse::RollbackNativeSimulationClock new_round_armed {};
    uint32_t new_round_pending_before = 0;
    uint32_t new_round_pending_armed = 0;
    uint32_t new_round_pending_after = 0;
    if (!Horse::RollbackNativePendingDelta(
            new_round_clock, new_round_pending_before)
        || new_round_pending_before != 120
        || !Horse::PrepareRollbackNativeSingleIteration(
            new_round_clock, new_round_armed)
        || !Horse::RollbackNativePendingDelta(
            new_round_armed, new_round_pending_armed)
        || new_round_pending_armed != 1)
        return 217;
    new_round_armed.battle_last_applied =
        new_round_armed.input_log_master_clock;
    if (!Horse::RollbackNativePendingDelta(
            new_round_armed, new_round_pending_after)
        || !Horse::ValidateRollbackNativeInterRoundControlTick(
            new_round_pending_armed, 1, 1, new_round_pending_after))
        return 218;
    const Horse::RollbackNativeSimulationClock epoch_before {
        8, 0, 7, 2091,
    };
    const Horse::RollbackNativeSimulationClock epoch_after {
        8, 0, 8, 0,
    };
    auto epoch_same_identity = epoch_before;
    epoch_same_identity.battle_last_frame = epoch_before.input_log_last_frame;
    auto epoch_nonzero_master = epoch_before;
    epoch_nonzero_master.input_log_master_clock = 1;
    auto epoch_bad_after = epoch_after;
    epoch_bad_after.battle_last_applied = 1;
    if (!Horse::RollbackNativeNeedsZeroDeltaEpochSync(epoch_before)
        || Horse::RollbackNativeNeedsZeroDeltaEpochSync(epoch_same_identity)
        || Horse::RollbackNativeNeedsZeroDeltaEpochSync(epoch_nonzero_master)
        || !Horse::ValidateRollbackNativeZeroDeltaEpochSync(
            epoch_before, epoch_after, 0, 0, 1, 0)
        || Horse::ValidateRollbackNativeZeroDeltaEpochSync(
            epoch_before, epoch_after, 1, 0, 1, 0)
        || Horse::ValidateRollbackNativeZeroDeltaEpochSync(
            epoch_before, epoch_after, 0, 0, 1, 1)
        || Horse::ValidateRollbackNativeZeroDeltaEpochSync(
            epoch_before, epoch_bad_after, 0, 0, 1, 0))
        return 219;
    Horse::RollbackNativeZeroDeltaTransitionEvidence side_effects {};
    side_effects.terminal_secondary_captured = true;
    side_effects.terminal_rng_captured = true;
    side_effects.committed_secondary_captured = true;
    side_effects.committed_rng_captured = true;
    if (!Horse::ValidateRollbackNativeZeroDeltaTransition(side_effects))
        return 229;
    for (uint32_t missing = 0; missing < 4; ++missing)
    {
        auto incomplete = side_effects;
        switch (missing)
        {
        case 0: incomplete.terminal_secondary_captured = false; break;
        case 1: incomplete.terminal_rng_captured = false; break;
        case 2: incomplete.committed_secondary_captured = false; break;
        case 3: incomplete.committed_rng_captured = false; break;
        }
        if (Horse::ValidateRollbackNativeZeroDeltaTransition(incomplete))
            return 230 + static_cast<int>(missing);
    }
    using ZeroDeltaSecondaryAction =
        Horse::RollbackNativeZeroDeltaSecondaryAction;
    if (Horse::ClassifyRollbackNativeZeroDeltaSecondaryAction(
            2, 2, true, 0x1111, true, 0x1111)
            != ZeroDeltaSecondaryAction::Preserve
        || Horse::ClassifyRollbackNativeZeroDeltaSecondaryAction(
            2, 2, true, 0x1111, true, 0x2222)
            != ZeroDeltaSecondaryAction::RestoreTerminal
        || Horse::ClassifyRollbackNativeZeroDeltaSecondaryAction(
            1, 1, true, 0x1111, true, 0x2222)
            != ZeroDeltaSecondaryAction::Reject
        || Horse::ClassifyRollbackNativeZeroDeltaSecondaryAction(
            2, 3, true, 0x1111, true, 0x2222)
            != ZeroDeltaSecondaryAction::Reject
        || Horse::ClassifyRollbackNativeZeroDeltaSecondaryAction(
            2, 2, false, 0x1111, true, 0x2222)
            != ZeroDeltaSecondaryAction::Reject
        || Horse::ClassifyRollbackNativeZeroDeltaSecondaryAction(
            2, 2, true, 0, true, 0x2222)
            != ZeroDeltaSecondaryAction::Reject
        || Horse::ClassifyRollbackNativeZeroDeltaSecondaryAction(
            2, 2, true, 0x1111, false, 0x2222)
            != ZeroDeltaSecondaryAction::Reject)
    {
        return 234;
    }
    const Horse::RollbackNativeSimulationClock consumed_epoch {
        8, 1, 8, 0,
    };
    if (!Horse::RollbackNativeNeedsConsumedZeroDeltaCommitObservation(
            true, true, consumed_epoch, 1, 1, 1, 0)
        || !Horse::RollbackNativeNeedsConsumedZeroDeltaCommitObservation(
            true, true, consumed_epoch, 0, 1, 1, 0)
        || Horse::RollbackNativeNeedsConsumedZeroDeltaCommitObservation(
            false, true, consumed_epoch, 1, 1, 1, 0)
        || Horse::RollbackNativeNeedsConsumedZeroDeltaCommitObservation(
            true, false, consumed_epoch, 1, 1, 1, 0)
        || Horse::RollbackNativeNeedsConsumedZeroDeltaCommitObservation(
            true, true, consumed_epoch, 2, 1, 1, 0)
        || Horse::RollbackNativeNeedsConsumedZeroDeltaCommitObservation(
            true, true, consumed_epoch, 0, 2, 1, 0)
        || Horse::RollbackNativeNeedsConsumedZeroDeltaCommitObservation(
            true, true, epoch_before, 0, 1, 1, 0)
        || Horse::RollbackNativeNeedsConsumedZeroDeltaCommitObservation(
            true, true, consumed_epoch, 1, 1, 1, 2))
    {
        return 236;
    }
    struct MockEpochSyncRoute
    {
        uint32_t native_original_calls {0};
        uint32_t one_delta_arm_calls {0};
        uint32_t success_evidence {0};
        uint32_t failures {0};
        uint64_t admitted_generation {0};
    };
    const auto run_epoch_route = [](
            bool stock_inter_round, bool new_round_control,
            const Horse::RollbackNativeSimulationClock& clock,
            uint64_t round_generation, uint64_t admitted_generation,
            bool native_clock_valid, bool new_round_state_valid) {
        MockEpochSyncRoute route {};
        route.admitted_generation = admitted_generation;
        const auto action =
            Horse::ClassifyRollbackNativeInterRoundClockAction(
                stock_inter_round, new_round_control, clock);
        if (action == Horse::RollbackNativeInterRoundClockAction::
                RunZeroDeltaEpochSync)
        {
            if (!Horse::RollbackNativeZeroDeltaEpochSyncAdmissionAllowed(
                    round_generation, route.admitted_generation))
            {
                ++route.failures;
                return route;
            }
            route.admitted_generation = round_generation + 1u;
            ++route.native_original_calls;
            if (Horse::RollbackNativeZeroDeltaEpochSyncCommitAllowed(
                    action, native_clock_valid, new_round_state_valid))
                ++route.success_evidence;
            else
                ++route.failures;
        }
        else if (action == Horse::RollbackNativeInterRoundClockAction::
                ArmSingleIteration)
        {
            ++route.one_delta_arm_calls;
        }
        return route;
    };
    const MockEpochSyncRoute valid_epoch_route = run_epoch_route(
        true, true, epoch_before, 1, 0, true, true);
    const MockEpochSyncRoute invalid_epoch_route = run_epoch_route(
        true, true, epoch_before, 1, 0, true, false);
    const MockEpochSyncRoute duplicate_epoch_route = run_epoch_route(
        true, true, epoch_before, 1, 2, true, true);
    const MockEpochSyncRoute skipped_first_epoch_route = run_epoch_route(
        true, true, epoch_before, 2, 0, true, true);
    const MockEpochSyncRoute next_epoch_route = run_epoch_route(
        true, true, epoch_before, 2, 2, true, true);
    const MockEpochSyncRoute skipped_second_epoch_route = run_epoch_route(
        true, true, epoch_before, 3, 2, true, true);
    const MockEpochSyncRoute duplicate_target_route = run_epoch_route(
        true, true, epoch_before, 2, 3, true, true);
    const MockEpochSyncRoute future_target_route = run_epoch_route(
        true, true, epoch_before, 2, 4, true, true);
    const MockEpochSyncRoute initial_startup_route = run_epoch_route(
        false, true, epoch_before, 1, 0, true, true);
    const MockEpochSyncRoute unrelated_mismatch_route = run_epoch_route(
        true, false, epoch_before, 1, 0, true, true);
    const MockEpochSyncRoute ordinary_new_round_route = run_epoch_route(
        true, true, new_round_clock, 1, 0, true, true);
    if (valid_epoch_route.native_original_calls != 1
        || valid_epoch_route.one_delta_arm_calls != 0
        || valid_epoch_route.success_evidence != 1
        || valid_epoch_route.failures != 0
        || valid_epoch_route.admitted_generation != 2
        || invalid_epoch_route.native_original_calls != 1
        || invalid_epoch_route.success_evidence != 0
        || invalid_epoch_route.failures != 1
        || duplicate_epoch_route.native_original_calls != 0
        || duplicate_epoch_route.success_evidence != 0
        || duplicate_epoch_route.failures != 1
        || skipped_first_epoch_route.native_original_calls != 1
        || skipped_first_epoch_route.success_evidence != 1
        || skipped_first_epoch_route.failures != 0
        || skipped_first_epoch_route.admitted_generation != 3
        || next_epoch_route.native_original_calls != 1
        || next_epoch_route.success_evidence != 1
        || next_epoch_route.failures != 0
        || next_epoch_route.admitted_generation != 3
        || skipped_second_epoch_route.native_original_calls != 1
        || skipped_second_epoch_route.success_evidence != 1
        || skipped_second_epoch_route.failures != 0
        || skipped_second_epoch_route.admitted_generation != 4
        || duplicate_target_route.native_original_calls != 0
        || duplicate_target_route.success_evidence != 0
        || duplicate_target_route.failures != 1
        || future_target_route.native_original_calls != 0
        || future_target_route.success_evidence != 0
        || future_target_route.failures != 1
        || initial_startup_route.native_original_calls != 0
        || initial_startup_route.one_delta_arm_calls != 0
        || initial_startup_route.success_evidence != 0
        || unrelated_mismatch_route.native_original_calls != 0
        || unrelated_mismatch_route.one_delta_arm_calls != 1
        || unrelated_mismatch_route.success_evidence != 0
        || ordinary_new_round_route.native_original_calls != 0
        || ordinary_new_round_route.one_delta_arm_calls != 1
        || ordinary_new_round_route.success_evidence != 0)
        return 225;
    auto one_pending = aligned;
    ++one_pending.input_log_master_clock;
    if (!Horse::RollbackNativePendingDelta(one_pending, pending_delta)
        || pending_delta != 2)
        return 105;
    auto no_pending = before;
    no_pending.battle_last_frame = no_pending.input_log_last_frame;
    no_pending.battle_last_applied = no_pending.input_log_master_clock;
    if (Horse::PrepareRollbackNativeNoCatchUpHandoff(
            no_pending, aligned, discarded_frames))
        return 110;
    auto invalid_order = aligned;
    invalid_order.battle_last_applied =
        invalid_order.input_log_master_clock + 1;
    if (Horse::PrepareRollbackNativeNoCatchUpHandoff(
            invalid_order, aligned, discarded_frames)
        || Horse::RollbackNativePendingDelta(
            invalid_order, pending_delta))
        return 106;

    Horse::RollbackNativeSimulationClockArmVerifyReport verify_report {};
    auto verify_read = [&](uintptr_t address, void* out,
                           size_t size) noexcept {
        if (address == 0x2488)
            std::memcpy(out, &scoped.battle_last_frame, size);
        else if (address == 0x248C)
            std::memcpy(out, &scoped.battle_last_applied, size);
        else
            return false;
        return true;
    };
    uint32_t verify_restore_calls = 0;
    auto verify_write = [&](uintptr_t, const void*, size_t) noexcept {
        ++verify_restore_calls;
        return true;
    };
    if (Horse::VerifyRollbackNativeSimulationClockArmOrRestore(
            0x1000, before, scoped, verify_read, verify_write,
            verify_report)
            != Horse::RollbackNativeSimulationClockArmVerifyResult::Ok
        || verify_restore_calls != 0 || !verify_report.frame_read
        || !verify_report.applied_read)
        return 97;

    Horse::RollbackNativeSimulationClock observed_arm = scoped;
    int32_t restored_frame = 0;
    uint32_t restored_applied = 0;
    auto observed_read = [&](uintptr_t address, void* out,
                             size_t size) noexcept {
        if (address == 0x2488)
            std::memcpy(out, &observed_arm.battle_last_frame, size);
        else if (address == 0x248C)
            std::memcpy(out, &observed_arm.battle_last_applied, size);
        else
            return false;
        return true;
    };
    auto restore_write = [&](uintptr_t address, const void* value,
                             size_t size) noexcept {
        ++verify_restore_calls;
        if (address == 0x2488)
            std::memcpy(&restored_frame, value, size);
        else if (address == 0x248C)
            std::memcpy(&restored_applied, value, size);
        else
            return false;
        return true;
    };
    observed_arm.battle_last_frame += 1;
    verify_restore_calls = 0;
    if (Horse::VerifyRollbackNativeSimulationClockArmOrRestore(
            0x1000, before, scoped, observed_read, restore_write,
            verify_report)
            != Horse::RollbackNativeSimulationClockArmVerifyResult::
                MismatchRecovered
        || verify_restore_calls != 2
        || restored_frame != before.battle_last_frame
        || restored_applied != before.battle_last_applied)
        return 98;
    observed_arm = scoped;
    observed_arm.battle_last_applied += 1;
    verify_restore_calls = 0;
    if (Horse::VerifyRollbackNativeSimulationClockArmOrRestore(
            0x1000, before, scoped, observed_read, restore_write,
            verify_report)
            != Horse::RollbackNativeSimulationClockArmVerifyResult::
                MismatchRecovered
        || verify_restore_calls != 2)
        return 99;
    uint32_t verify_read_calls = 0;
    auto first_read_fails = [&](uintptr_t address, void* out,
                                size_t size) noexcept {
        ++verify_read_calls;
        if (verify_read_calls == 1) return false;
        return observed_read(address, out, size);
    };
    verify_restore_calls = 0;
    if (Horse::VerifyRollbackNativeSimulationClockArmOrRestore(
            0x1000, before, scoped, first_read_fails, restore_write,
            verify_report)
            != Horse::RollbackNativeSimulationClockArmVerifyResult::
                ReadFailedRecovered
        || verify_read_calls != 2 || verify_restore_calls != 2
        || verify_report.frame_read || !verify_report.applied_read)
        return 100;
    verify_read_calls = 0;
    verify_restore_calls = 0;
    auto second_restore_fails = [&](uintptr_t address, const void* value,
                                    size_t size) noexcept {
        const bool stored = restore_write(address, value, size);
        return stored && verify_restore_calls != 2;
    };
    if (Horse::VerifyRollbackNativeSimulationClockArmOrRestore(
            0x1000, before, scoped, first_read_fails,
            second_restore_fails, verify_report)
            != Horse::RollbackNativeSimulationClockArmVerifyResult::
                RecoveryFailed
        || verify_read_calls != 2 || verify_restore_calls != 2
        || !verify_report.restore.frame_recovery
        || verify_report.restore.applied_recovery)
        return 101;
    before.input_log_master_clock = 0;
    if (Horse::PrepareRollbackNativeSingleIteration(before, scoped))
        return 2;

    // Every arm/restore path attempts both cursor writes. Partial failure can
    // never strand one cursor in the scoped state.
    Horse::RollbackNativeSimulationClockWriteReport write_report {};
    uint32_t write_calls = 0;
    auto first_arm_write_fails =
        [&](uintptr_t, const void*, size_t) noexcept {
            return ++write_calls != 1;
        };
    if (Horse::ArmRollbackNativeSimulationClock(
            0x1000, before, scoped, first_arm_write_fails, write_report)
            != Horse::RollbackNativeSimulationClockWriteResult::
                ArmFailedRecovered
        || write_calls != 4 || write_report.frame_write
        || !write_report.applied_write || !write_report.frame_recovery
        || !write_report.applied_recovery)
        return 20;
    write_calls = 0;
    auto second_arm_and_second_recovery_fail =
        [&](uintptr_t, const void*, size_t) noexcept {
            ++write_calls;
            return write_calls != 2 && write_calls != 4;
        };
    if (Horse::ArmRollbackNativeSimulationClock(
            0x1000, before, scoped, second_arm_and_second_recovery_fail,
            write_report)
            != Horse::RollbackNativeSimulationClockWriteResult::
                ArmRecoveryFailed
        || write_calls != 4 || !write_report.frame_write
        || write_report.applied_write || !write_report.frame_recovery
        || write_report.applied_recovery)
        return 21;
    write_calls = 0;
    auto first_restore_fails =
        [&](uintptr_t, const void*, size_t) noexcept {
            return ++write_calls != 1;
        };
    if (Horse::RestoreRollbackNativeSimulationClock(
            0x1000, before, first_restore_fails, write_report)
            != Horse::RollbackNativeSimulationClockWriteResult::RestoreFailed
        || write_calls != 2 || write_report.frame_recovery
        || !write_report.applied_recovery)
        return 22;

    Horse::RollbackNativeInputLogClockWriteReport input_log_write_report {};
    write_calls = 0;
    auto first_input_log_restore_fails =
        [&](uintptr_t, const void*, size_t) noexcept {
            return ++write_calls != 1;
        };
    if (Horse::RestoreRollbackNativeInputLogClock(
            0x2000, before, first_input_log_restore_fails,
            input_log_write_report)
        || write_calls != 2
        || input_log_write_report.last_frame_restore
        || !input_log_write_report.master_clock_restore)
        return 23;
    write_calls = 0;
    auto input_log_restore_succeeds =
        [&](uintptr_t, const void*, size_t) noexcept {
            ++write_calls;
            return true;
        };
    if (!Horse::RestoreRollbackNativeInputLogClock(
            0x2000, before, input_log_restore_succeeds,
            input_log_write_report)
        || write_calls != 2
        || !input_log_write_report.last_frame_restore
        || !input_log_write_report.master_clock_restore)
        return 24;

    before.input_log_last_frame = 44;
    before.input_log_master_clock = 120;
    int32_t live_input_log_last_frame = 44;
    uint32_t live_input_log_master_clock = 120;
    auto input_log_read = [&](uintptr_t address, void* destination,
                              size_t size) noexcept {
        if (address == 0x2000 + 0x3A0
            && size == sizeof(live_input_log_last_frame))
        {
            std::memcpy(destination, &live_input_log_last_frame, size);
            return true;
        }
        if (address == 0x2000 + 0x3A4
            && size == sizeof(live_input_log_master_clock))
        {
            std::memcpy(destination, &live_input_log_master_clock, size);
            return true;
        }
        return false;
    };
    // Cursor capture attempts both fields so partial-read telemetry is exact.
    Horse::RollbackNativeInputLogClockReadReport input_log_read_report {};
    Horse::RollbackNativeSimulationClock captured_clock {};
    uint32_t prelude_read_calls = 0;
    if (Horse::CaptureRollbackNativeInputLogClock(
            0x2000,
            [&](uintptr_t, void*, size_t) noexcept {
                return ++prelude_read_calls != 1;
            },
            captured_clock, input_log_read_report)
        || prelude_read_calls != 2
        || input_log_read_report.last_frame_read
        || !input_log_read_report.master_clock_read)
        return 90;
    prelude_read_calls = 0;
    if (Horse::CaptureRollbackNativeInputLogClock(
            0x2000,
            [&](uintptr_t, void*, size_t) noexcept {
                return ++prelude_read_calls != 2;
            },
            captured_clock, input_log_read_report)
        || prelude_read_calls != 2
        || !input_log_read_report.last_frame_read
        || input_log_read_report.master_clock_read)
        return 96;
    if (!Horse::CaptureRollbackNativeInputLogClock(
            0x2000, input_log_read, captured_clock,
            input_log_read_report)
        || !input_log_read_report.last_frame_read
        || !input_log_read_report.master_clock_read
        || captured_clock.input_log_last_frame != 44
        || captured_clock.input_log_master_clock != 120)
        return 91;

    // Owned nesting is thread-local and tied to one exact manager/InputLog.
    int owner = 0;
    int camera = 0;
    bool other_thread_matched = true;
    {
        Horse::RollbackNativeSimulationScope native_scope(
            &owner, 0x3000, 0x2000, &camera, before, scoped, false, 755);
        if (!native_scope
            || !Horse::CurrentRollbackNativeSimulationScope(
                &owner, 0x3000, 0x2000)
            || Horse::CurrentRollbackNativeSimulationScope(
                    &owner, 0x3000, 0x2000)->logical_frame != 755
            || Horse::CurrentRollbackNativeSimulationScope(
                &owner, 0x3001, 0x2000))
            return 92;
        Horse::RollbackNativeSimulationScope nested_scope(
            &owner, 0x3000, 0x2000, &camera, before, scoped);
        if (nested_scope) return 93;
        std::thread other([&]() noexcept {
            other_thread_matched =
                Horse::CurrentRollbackNativeSimulationScope(&owner)
                != nullptr;
        });
        other.join();
        if (other_thread_matched) return 94;
    }
    if (Horse::CurrentRollbackNativeSimulationScope(&owner)) return 95;

    // The initial audit scope has no nomination authority. It only proves the
    // one permitted stock tail did not attempt a gameplay PerFrame callback.
    bool other_thread_marked = true;
    {
        Horse::RollbackInitialBoundaryScope boundary_scope(&owner, 0x3000);
        if (!boundary_scope
            || Horse::RollbackInitialBoundaryGameplayAttempted(
                &owner, 0x3000))
            return 107;
        Horse::RollbackInitialBoundaryScope nested_boundary_scope(
            &owner, 0x3000);
        if (nested_boundary_scope) return 108;
        std::thread other([&]() noexcept {
            other_thread_marked =
                Horse::MarkRollbackInitialBoundaryGameplayAttempt(&owner);
        });
        other.join();
        if (other_thread_marked
            || !Horse::MarkRollbackInitialBoundaryGameplayAttempt(&owner)
            || !Horse::RollbackInitialBoundaryGameplayAttempted(
                &owner, 0x3000)
            || Horse::RollbackInitialBoundaryGameplayAttempted(
                &owner, 0x3001))
            return 109;
    }
    if (Horse::MarkRollbackInitialBoundaryGameplayAttempt(&owner)
        || Horse::RollbackInitialBoundaryGameplayAttempted(&owner, 0x3000))
        return 110;

    // The expected input-pair callback is suppressed before callback-gate
    // admission. Unrelated collections are never claimed by this scope.
    {
        Horse::RollbackInitialBoundaryScope boundary_scope(&owner, 0x3000);
        if (!boundary_scope
            || Horse::SuppressRollbackInitialBoundaryInputPair(
                &owner, 0x4211, 0x44A8)
            || !Horse::SuppressRollbackInitialBoundaryInputPair(
                &owner, 0x4210, 0x44A8)
            || !Horse::RollbackInitialBoundaryInputPairSuppressed(
                &owner, 0x3000))
            return 153;
    }
    if (Horse::SuppressRollbackInitialBoundaryInputPair(
            &owner, 0x4210, 0x44A8)
        || Horse::RollbackInitialBoundaryInputPairSuppressed(&owner, 0x3000))
        return 154;

    // An admitted outer callback can overlap logical gate closure. Its
    // audited nested PerFrame must be marked and suppressed before trying to
    // acquire a new (now closed) lease, so stock gameplay is never called.
    {
        Horse::RollbackInstallOnceCallbackGate gate {};
        gate.open();
        bool outer_admitted = false;
        gate.enter(outer_admitted);
        if (!outer_admitted) return 116;
        Horse::RollbackInitialBoundaryScope boundary_scope(&owner, 0x3000);
        if (!boundary_scope) return 117;
        std::atomic<bool> close_started {false};
        std::atomic<bool> close_returned {false};
        std::thread closer([&]() noexcept {
            close_started.store(true, std::memory_order_release);
            gate.close_and_drain();
            close_returned.store(true, std::memory_order_release);
        });
        while (!close_started.load(std::memory_order_acquire)
            || gate.accepting())
            std::this_thread::yield();
        uint32_t owned_suppressed = 0;
        uint32_t pass_through = 0;
        uint32_t original_calls = 0;
        if (Horse::MarkRollbackInitialBoundaryGameplayAttempt(&owner))
            ++owned_suppressed;
        else
        {
            bool nested_admitted = false;
            gate.enter(nested_admitted);
            ++pass_through;
            ++original_calls;
            gate.leave(nested_admitted);
        }
        const bool input_pair_suppressed_before_closed_gate =
            Horse::SuppressRollbackInitialBoundaryInputPair(
                &owner, 0x4210, 0x44A8);
        const bool nested_suppressed = owned_suppressed == 1
            && pass_through == 0 && original_calls == 0
            && input_pair_suppressed_before_closed_gate
            && Horse::RollbackInitialBoundaryInputPairSuppressed(
                &owner, 0x3000)
            && Horse::RollbackInitialBoundaryGameplayAttempted(
                &owner, 0x3000)
            && !close_returned.load(std::memory_order_acquire);
        gate.leave(outer_admitted);
        closer.join();
        if (!nested_suppressed || !close_returned.load()
            || gate.inflight() != 0)
            return 118;
    }

    // Parsing the peer baseline closes the peer-wait lane immediately.
    if (!Horse::ShouldRunRollbackInitialPeerWaitTail(true, 1, true, false)
        || Horse::ShouldRunRollbackInitialPeerWaitTail(true, 1, true, true)
        || Horse::ShouldRunRollbackInitialPeerWaitTail(false, 1, true, false)
        || Horse::ShouldRunRollbackInitialPeerWaitTail(true, 2, true, false))
        return 155;

    Horse::RollbackLiveToken live_a {};
    live_a.battle_manager = 1;
    live_a.input_log = 2;
    live_a.chara = {3, 4};
    live_a.stage_actor_manager = 5;
    live_a.round_start_digest = 6;
    live_a.stage_actor_order_digest = 7;
    live_a.native_stage_identity = 8;
    live_a.input_log_frame = 9;
    live_a.round_ordinal = 10;
    live_a.presence = 8;
    live_a.battle_main_state = 2;
    live_a.battle_status = 1;
    live_a.pvp_active = true;
    live_a.auto_advance_armed = false;
    live_a.valid = true;
    Horse::RollbackLiveToken live_b = live_a;
    if (!Horse::RollbackLiveTokensExactlyMatch(live_a, live_b)) return 156;
    ++live_b.input_log_frame;
    if (Horse::RollbackLiveTokensExactlyMatch(live_a, live_b)) return 157;
    live_b = live_a;
    live_b.battle_status = 7;
    if (Horse::RollbackLiveTokensExactlyMatch(live_a, live_b)) return 158;

    MockStorageState storage_source {};
    storage_source.hgcpu.bytes.resize(2, 1);
    storage_source.palette_variants.payload.resize(1);
    storage_source.palette_variants.writer_nodes.resize(1);
    storage_source.hgcpu.khit_topology[0].nodes.resize(1);
    storage_source.hgcpu.motion_banks.control_bytes.resize(1);
    storage_source.hgcpu.motion_banks.bytes.resize(1);
    storage_source.hgcpu.motion_tail.bytes.resize(1);
    storage_source.hgcpu.skeleton_runtime.inline_bytes.resize(1);
    storage_source.hgcpu.skeleton_runtime.aux_nodes.resize(1);
    storage_source.hgcpu.skeleton_runtime.aux_nodes[0].bytes.resize(1, 2);
    storage_source.hgcpu.skeleton_runtime.chains.resize(1);
    storage_source.hgcpu.skeleton_runtime.spring_nodes.resize(1);
    storage_source.hgcpu.skeleton_runtime.spring_nodes[0].bytes.resize(1, 3);
    storage_source.hgcpu.timer_node.root_bytes.resize(1);
    storage_source.hgcpu.timer_node.backing_bytes.resize(1);
    storage_source.hgcpu.timer_node.nodes.resize(1);
    storage_source.explicit_snapshot.bytes.resize(1);
    storage_source.explicit_snapshot.ranges.resize(1);
    storage_source.breakable_stage.records.resize(1);
    storage_source.canonical_hash = 0x1234;
    const auto storage_before =
        Horse::CaptureRollbackStepStateStorageIdentity<512, 512>(
            storage_source);
    MockStorageState storage_destination {};
    if (!storage_before.valid || storage_before.count != 20
        || !Horse::TransferRollbackStepStateStorage<512, 512>(
            storage_destination, storage_source))
        return 161;
    const auto storage_after =
        Horse::CaptureRollbackStepStateStorageIdentity<512, 512>(
            storage_destination);
    const auto capacity_limits =
        Horse::CaptureRollbackStepStateCapacityLimits<512, 512>(
            storage_destination);
    if (!(storage_before == storage_after)
        || !capacity_limits.valid || capacity_limits.count != 20
        || storage_destination.canonical_hash != 0x1234
        || Horse::TransferRollbackStepStateStorage<512, 512>(
            storage_destination, storage_destination))
        return 162;
    storage_destination.hgcpu.skeleton_runtime.aux_nodes[0].bytes.reserve(
        storage_destination.hgcpu.skeleton_runtime.aux_nodes[0].bytes.capacity()
            + 8);
    if (Horse::CaptureRollbackStepStateCapacityLimits<512, 512>(
            storage_destination) == capacity_limits)
        return 166;
    MockStorageState storage_limit {};
    storage_limit.hgcpu.skeleton_runtime.aux_nodes.resize(512);
    storage_limit.hgcpu.skeleton_runtime.spring_nodes.resize(512);
    if (!Horse::CaptureRollbackStepStateStorageIdentity<512, 512>(
            storage_limit).valid)
        return 163;
    storage_limit.hgcpu.skeleton_runtime.aux_nodes.resize(513);
    if (Horse::CaptureRollbackStepStateStorageIdentity<512, 512>(
            storage_limit).valid)
        return 164;
    storage_limit.hgcpu.skeleton_runtime.aux_nodes.resize(512);
    storage_limit.hgcpu.skeleton_runtime.spring_nodes.resize(513);
    if (Horse::CaptureRollbackStepStateStorageIdentity<512, 512>(
            storage_limit).valid)
        return 165;

    // The exact gate used by production is read-only and short-circuits in
    // shape -> explicit -> state -> scratch -> stage order. A refusal cannot
    // reach the later preparation/recycle work owned by the caller.
    {
        struct GateState { uint64_t marker {0xAABBCCDD}; } state {};
        struct GateStage { uint64_t marker {0x1122}; } stage {};
        struct GateScratch { uint64_t marker {0x3344}; } scratch {};
        struct GateLimits { bool valid {true}; } limits {};
        int calls = 0;
        int reject_at = 0;
        auto shape = [&](const auto&, const auto&) noexcept {
            ++calls; return reject_at != 1;
        };
        auto explicit_ready = [&](const auto&) noexcept {
            ++calls; return reject_at != 2;
        };
        auto state_failure = [&](const auto&) noexcept {
            ++calls; return reject_at == 3 ? "state-rejected" : "ok";
        };
        auto scratch_failure = [&](const auto&) noexcept {
            ++calls; return reject_at == 4 ? "scratch-rejected" : "ok";
        };
        auto stage_ready = [&](const auto&, const auto&) noexcept {
            ++calls; return reject_at != 5;
        };
        const auto run = [&]() noexcept {
            calls = 0;
            return Horse::ValidateRollbackPreallocatedCaptureGate(
                state, &stage, &scratch, &limits, shape, explicit_ready,
                state_failure, scratch_failure, stage_ready);
        };
        const auto accepted = run();
        if (!accepted.ok || calls != 5) return 167;
        for (reject_at = 1; reject_at <= 5; ++reject_at)
        {
            const uint64_t state_before = state.marker;
            const uint64_t stage_before = stage.marker;
            const uint64_t scratch_before = scratch.marker;
            const auto refused = run();
            if (refused.ok || calls != reject_at
                || state.marker != state_before
                || stage.marker != stage_before
                || scratch.marker != scratch_before)
                return 168;
        }
        reject_at = 0;
        limits.valid = false;
        const auto invalid_contract = run();
        if (invalid_contract.ok || calls != 0) return 169;
    }

    // The production HgCpu wrapper delegates to this pure predicate with
    // native constants. Exercise every variable-capacity family hermetically.
    {
        constexpr size_t required = 8;
        constexpr size_t required_nodes = 4;
        const auto make_capacity_case = [=](int omitted) {
            MockStorageState::HgCpu frame {};
            if (omitted != 1) frame.bytes.reserve(required);
            if (omitted != 2)
                for (auto& topology : frame.khit_topology)
                    topology.nodes.reserve(required_nodes);
            if (omitted != 3)
            {
                frame.motion_banks.bytes.reserve(required);
                frame.motion_banks.control_bytes.reserve(required);
                frame.motion_tail.bytes.reserve(required);
            }
            if (omitted != 4)
            {
                frame.skeleton_runtime.chara[0] = 1;
                frame.skeleton_runtime.chara[1] = 2;
                frame.skeleton_runtime.inline_bytes.resize(required);
            }
            if (omitted != 5)
            {
                frame.timer_node.root_bytes.reserve(required);
                frame.timer_node.backing_bytes.reserve(required);
                frame.timer_node.nodes.reserve(required_nodes);
            }
            return frame;
        };
        const auto failure = [=](const auto& frame) noexcept {
            return Horse::RollbackPreallocatedHgCpuCapacityFailure(
                frame, required, required_nodes, required, required,
                required, required, required, required, required_nodes);
        };
        auto good = make_capacity_case(0);
        const auto* bytes_ptr = good.bytes.data();
        const auto* khit_ptr = good.khit_topology[0].nodes.data();
        const auto* motion_ptr = good.motion_banks.bytes.data();
        const auto* skeleton_ptr =
            good.skeleton_runtime.inline_bytes.data();
        const auto* timer_ptr = good.timer_node.nodes.data();
        if (std::strcmp(failure(good), "ok") != 0
            || good.bytes.data() != bytes_ptr
            || good.khit_topology[0].nodes.data() != khit_ptr
            || good.motion_banks.bytes.data() != motion_ptr
            || good.skeleton_runtime.inline_bytes.data() != skeleton_ptr
            || good.timer_node.nodes.data() != timer_ptr)
            return 170;
        static constexpr const char* failures[] = {
            "hgcpu-buffer-capacity-insufficient",
            "khit-node-capacity-insufficient",
            "motion-capacity-insufficient",
            "skeleton-template-capacity-insufficient",
            "timer-node-capacity-insufficient",
        };
        for (int omitted = 1; omitted <= 5; ++omitted)
        {
            const auto frame = make_capacity_case(omitted);
            if (std::strcmp(failure(frame), failures[omitted - 1]) != 0)
                return 171;
        }
        auto aux_missing = make_capacity_case(0);
        aux_missing.skeleton_runtime.aux_nodes.resize(1);
        aux_missing.skeleton_runtime.aux_nodes[0].address = 1;
        aux_missing.skeleton_runtime.aux_nodes[0].vtable = 2;
        auto spring_missing = make_capacity_case(0);
        spring_missing.skeleton_runtime.spring_nodes.resize(1);
        spring_missing.skeleton_runtime.spring_nodes[0].address = 3;
        spring_missing.skeleton_runtime.spring_nodes[0].vtable = 4;
        if (std::strcmp(failure(aux_missing),
                "skeleton-aux-byte-capacity-insufficient") != 0
            || std::strcmp(failure(spring_missing),
                "skeleton-spring-byte-capacity-insufficient") != 0)
            return 172;
    }

    // Successful prestart verification invokes capture and restore exactly
    // once. Later service ticks observe completion without repeating either.
    {
        Horse::RollbackInitialBaselinePrestartGate gate {};
        uint32_t captures = 0;
        uint32_t restores = 0;
        auto capture = [&]() noexcept { ++captures; return true; };
        auto restore = [&]() noexcept { ++restores; return true; };
        if (!gate.verify_once(capture, restore)
            || !gate.verify_once(capture, restore)
            || !gate.verified() || captures != 1 || restores != 1)
            return 159;
    }
    {
        Horse::RollbackInitialBaselinePrestartGate gate {};
        uint32_t restores = 0;
        if (gate.verify_once([]() noexcept { return false; },
                [&]() noexcept { ++restores; return true; })
            || gate.verified() || restores != 0)
            return 160;
    }

    bool initial_boundary_frozen = false;
    uint32_t enclosing_calls = 0;
    uint32_t commits = 0;
    auto run_zero_gameplay_tail = [&](bool attempt_gameplay) noexcept {
        if (initial_boundary_frozen) return false;
        constexpr uint32_t kPreDelta = 0;
        constexpr uint32_t kPostDelta = 0;
        constexpr uint32_t kPerFrameBefore = 7;
        constexpr uint32_t kInputPairBefore = 5;
        uint32_t per_frame_after = kPerFrameBefore;
        uint32_t input_pair_after = kInputPairBefore;
        ++enclosing_calls;
        Horse::RollbackInitialBoundaryScope boundary_scope(&owner, 0x3000);
        if (!boundary_scope) return false;
        if (attempt_gameplay)
        {
            if (!Horse::MarkRollbackInitialBoundaryGameplayAttempt(&owner))
                return false;
            ++per_frame_after;
        }
        const bool gameplay_attempted =
            Horse::RollbackInitialBoundaryGameplayAttempted(
                &owner, 0x3000);
        if (!Horse::ValidateRollbackInitialBoundaryTail(
                kPreDelta, kPostDelta, gameplay_attempted,
                kPerFrameBefore, per_frame_after,
                kInputPairBefore, input_pair_after))
            return false;
        initial_boundary_frozen = true;
        ++commits;
        return true;
    };
    if (!run_zero_gameplay_tail(false) || run_zero_gameplay_tail(false)
        || enclosing_calls != 1 || commits != 1)
        return 111;
    if (!Horse::ValidateRollbackInitialBoundaryTail(
            0, 0, false, 7, 7, 5, 5)
        || !Horse::ValidateRollbackInitialBoundaryTail(
            1, 1, false, 7, 7, 5, 5)
        || Horse::ValidateRollbackInitialBoundaryTail(
            1, 0, false, 7, 7, 5, 5)
        || Horse::ValidateRollbackInitialBoundaryTail(
            1, 2, false, 7, 7, 5, 5)
        || Horse::ValidateRollbackInitialBoundaryTail(
            2, 2, false, 7, 7, 5, 5)
        || Horse::ValidateRollbackInitialBoundaryTail(
            0, 1, false, 7, 7, 5, 5)
        || Horse::ValidateRollbackInitialBoundaryTail(
            0, 0, true, 7, 7, 5, 5)
        || Horse::ValidateRollbackInitialBoundaryTail(
            0, 0, false, 7, 8, 5, 5)
        || Horse::ValidateRollbackInitialBoundaryTail(
            0, 0, false, 7, 7, 5, 6))
        return 114;

    {
        Horse::RollbackNativeSimulationClock clock {};
        clock.input_log_last_frame = 4;
        clock.battle_last_frame = 4;
        clock.input_log_master_clock = 9;
        clock.battle_last_applied = 9;
        uint32_t pending = 99;
        if (Horse::ClassifyRollbackInitialBoundaryClock(clock, pending)
                != Horse::RollbackInitialBoundaryClockAction::
                    AuditZeroDeltaTail
            || pending != 0)
            return 119;
        clock.input_log_master_clock = 10;
        if (Horse::ClassifyRollbackInitialBoundaryClock(clock, pending)
                != Horse::RollbackInitialBoundaryClockAction::
                    AuditOnePendingDeltaTail
            || pending != 1)
            return 120;
        clock.input_log_master_clock = 11;
        if (Horse::ClassifyRollbackInitialBoundaryClock(clock, pending)
                != Horse::RollbackInitialBoundaryClockAction::Invalid
            || pending != 2)
            return 121;
        clock.input_log_master_clock = 9;
        clock.battle_last_applied = 10;
        if (Horse::ClassifyRollbackInitialBoundaryClock(clock, pending)
                != Horse::RollbackInitialBoundaryClockAction::Invalid)
            return 122;
        clock.battle_last_applied = 9;
        clock.battle_last_frame = 3;
        if (Horse::ClassifyRollbackInitialBoundaryClock(clock, pending)
                != Horse::RollbackInitialBoundaryClockAction::Invalid)
            return 123;
    }

    {
        Horse::RollbackNativeSimulationClock before_tail {};
        before_tail.input_log_last_frame = 4;
        before_tail.input_log_master_clock = 10;
        before_tail.battle_last_frame = 4;
        before_tail.battle_last_applied = 9;
        Horse::RollbackNativeSimulationClock aligned_tail = before_tail;
        aligned_tail.battle_last_applied = 10;
        int32_t memory_frame = before_tail.battle_last_frame;
        uint32_t memory_applied = before_tail.battle_last_applied;
        auto tail_write = [&](uintptr_t address, const void* source,
                              size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
                std::memcpy(&memory_frame, source, size);
            else if (address == 0x248C
                     && size == sizeof(memory_applied))
                std::memcpy(&memory_applied, source, size);
            else
                return false;
            return true;
        };
        auto tail_read = [&](uintptr_t address, void* destination,
                             size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
                std::memcpy(destination, &memory_frame, size);
            else if (address == 0x248C
                     && size == sizeof(memory_applied))
                std::memcpy(destination, &memory_applied, size);
            else
                return false;
            return true;
        };
        Horse::RollbackNativeSimulationClockWriteReport tail_write_report {};
        if (Horse::ArmRollbackNativeSimulationClock(
                0x1000, before_tail, aligned_tail, tail_write,
                tail_write_report)
                != Horse::RollbackNativeSimulationClockWriteResult::Ok
            || memory_frame != aligned_tail.battle_last_frame
            || memory_applied != aligned_tail.battle_last_applied)
            return 124;
        Horse::RollbackNativeSimulationClockArmVerifyReport
            tail_verify_report {};
        if (Horse::VerifyRollbackNativeSimulationClockArmOrRestore(
                0x1000, before_tail, aligned_tail, tail_read, tail_write,
                tail_verify_report)
                != Horse::RollbackNativeSimulationClockArmVerifyResult::Ok)
            return 125;
        if (Horse::RestoreRollbackNativeSimulationClock(
                0x1000, before_tail, tail_write, tail_write_report)
                != Horse::RollbackNativeSimulationClockWriteResult::Ok
            || memory_frame != before_tail.battle_last_frame
            || memory_applied != before_tail.battle_last_applied)
            return 126;
        uint32_t restored_delta = 0;
        if (!Horse::RollbackNativePendingDelta(
                before_tail, restored_delta)
            || restored_delta != 1
            || !Horse::ValidateRollbackInitialBoundaryTail(
                1, restored_delta, false, 7, 7, 5, 5))
            return 127;
    }

    {
        uint32_t attempts = 0, verified = 0, fallbacks = 0, failures = 0;
        uint32_t restore_calls = 0, verify_calls = 0;
        int32_t frame = 10;
        uint32_t applied = 11;
        auto restore = [&]() noexcept {
            ++restore_calls;
            frame = 4;
            applied = 9;
            return true;
        };
        auto verify = [&]() noexcept {
            ++verify_calls;
            return true;
        };
        {
            Horse::RollbackInitialBoundaryCursorRestoreGuard guard(
                attempts, verified, fallbacks, failures, restore, verify);
            guard.mark_armed();
            if (!guard.armed() || attempts != 0 || verified != 0
                || fallbacks != 0 || failures != 0
                || !guard.restore_and_verify() || guard.armed())
                return 128;
        }
        if (attempts != 1 || verified != 1 || fallbacks != 0
            || failures != 0 || restore_calls != 1 || verify_calls != 1)
            return 129;
    }

    // A peer that reaches pre-control first may need several complete stock
    // control deltas before the slower peer reaches the same boundary. Each
    // call consumes only the newest InputLog delta while nested gameplay and
    // the exact input-pair publication remain suppressed. Successful cursor
    // progress is retained until the frozen baseline is restored.
    {
        uint32_t attempts = 0, verified = 0, fallbacks = 0, failures = 0;
        uint32_t arms = 0, tails = 0, validations = 0;
        int32_t memory_frame = 4;
        uint32_t memory_applied = 6;
        for (uint32_t iteration = 0; iteration < 3; ++iteration)
        {
            Horse::RollbackNativeSimulationClock before {};
            before.input_log_last_frame = 4;
            before.input_log_master_clock = 10 + iteration;
            before.battle_last_frame = memory_frame;
            before.battle_last_applied = memory_applied;
            auto restore = [&]() noexcept {
                memory_frame = before.battle_last_frame;
                memory_applied = before.battle_last_applied;
                return true;
            };
            auto verify = [&]() noexcept {
                return memory_frame == before.battle_last_frame
                    && memory_applied == before.battle_last_applied;
            };
            Horse::RollbackInitialBoundaryCursorRestoreGuard guard(
                attempts, verified, fallbacks, failures, restore, verify);
            Horse::RollbackNativeSimulationClock scoped {};
            uint32_t before_delta = 0;
            if (!Horse::RollbackNativePendingDelta(before, before_delta)
                || !Horse::PrepareRollbackNativeSingleIteration(
                    before, scoped))
                return 150;
            memory_frame = scoped.battle_last_frame;
            memory_applied = scoped.battle_last_applied;
            guard.mark_armed();
            ++arms;
            ++tails;
            // Model the complete native SimulationLoop consuming the armed
            // one-delta cursor. The hook-observed owned counters remain
            // unchanged because both nested callbacks were suppressed.
            memory_applied = scoped.input_log_master_clock;
            Horse::RollbackNativeSimulationClock after = scoped;
            after.battle_last_frame = memory_frame;
            after.battle_last_applied = memory_applied;
            uint32_t after_delta = 0;
            if (!Horse::RollbackNativePendingDelta(after, after_delta)
                || !Horse::ValidateRollbackInitialPeerWaitControlDelta(
                    before_delta, after_delta, true, true,
                    7, 7, 5, 5))
                return 152;
            guard.disarm_without_restore();
            ++validations;
        }
        if (arms != 3 || tails != 3 || validations != 3
            || memory_frame != 4 || memory_applied != 12
            || attempts != 0 || verified != 0
            || fallbacks != 0 || failures != 0)
            return 153;
        if (Horse::ValidateRollbackInitialPeerWaitControlDelta(
                4, 1, true, true, 7, 7, 5, 5)
            || Horse::ValidateRollbackInitialPeerWaitControlDelta(
                4, 0, false, true, 7, 7, 5, 5)
            || Horse::ValidateRollbackInitialPeerWaitControlDelta(
                4, 0, true, false, 7, 7, 5, 5)
            || Horse::ValidateRollbackInitialPeerWaitControlDelta(
                4, 0, true, true, 7, 8, 5, 5)
            || Horse::ValidateRollbackInitialPeerWaitControlDelta(
                4, 0, true, true, 7, 7, 5, 6))
            return 154;
    }

    // Retained peer-wait cursor progress is outside the gameplay snapshot.
    // The original frozen-boundary clock therefore has its own exactly-once
    // restore gate before frame zero or logical pass-through release.
    {
        Horse::RollbackNativeSimulationClock frozen {};
        frozen.battle_last_frame = 4;
        frozen.battle_last_applied = 6;
        int32_t memory_frame = 4;
        uint32_t memory_applied = 12;
        Horse::RollbackFrozenBoundaryClockRestoreGate gate {};
        auto read = [&](uintptr_t address, void* destination,
                        size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
            {
                std::memcpy(destination, &memory_frame, size);
                return true;
            }
            if (address == 0x248C && size == sizeof(memory_applied))
            {
                std::memcpy(destination, &memory_applied, size);
                return true;
            }
            return false;
        };
        auto write = [&](uintptr_t address, const void* source,
                         size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
            {
                std::memcpy(&memory_frame, source, size);
                return true;
            }
            if (address == 0x248C && size == sizeof(memory_applied))
            {
                std::memcpy(&memory_applied, source, size);
                return true;
            }
            return false;
        };
        if (!gate.arm(0x1000, frozen) || !gate.pending()
            || gate.arm(0x1000, frozen)
            || !gate.restore_once(read, write)
            || !gate.restored() || gate.pending() || gate.failed()
            || memory_frame != 4 || memory_applied != 6
            || gate.attempts() != 1 || gate.verified_restores() != 1
            || gate.failures() != 0
            || !gate.restore_once(read, write)
            || gate.attempts() != 1 || gate.verified_restores() != 1)
            return 180;
        gate.clear();
        if (!gate.empty() || gate.pending() || gate.restored() || gate.failed()
            || gate.attempts() != 0 || gate.verified_restores() != 0
            || gate.failures() != 0
            || !gate.restore_once(read, write))
            return 181;
    }
    {
        Horse::RollbackNativeSimulationClock frozen {};
        frozen.battle_last_frame = 4;
        frozen.battle_last_applied = 6;
        int32_t memory_frame = 9;
        uint32_t memory_applied = 12;
        Horse::RollbackFrozenBoundaryClockRestoreGate gate {};
        if (!gate.arm(0x1000, frozen)) return 182;
        auto read = [&](uintptr_t address, void* destination,
                        size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
                std::memcpy(destination, &memory_frame, size);
            else if (address == 0x248C
                && size == sizeof(memory_applied))
                std::memcpy(destination, &memory_applied, size);
            else
                return false;
            return true;
        };
        auto partial_write = [&](uintptr_t address, const void* source,
                                 size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
            {
                std::memcpy(&memory_frame, source, size);
                return true;
            }
            return false;
        };
        if (gate.restore_once(read, partial_write) || !gate.failed()
            || gate.restored() || gate.attempts() != 1
            || gate.verified_restores() != 0 || gate.failures() != 1
            || gate.restore_once(read, partial_write)
            || gate.attempts() != 1 || gate.failures() != 1)
            return 183;
    }
    {
        Horse::RollbackNativeSimulationClock frozen {};
        frozen.battle_last_frame = 4;
        frozen.battle_last_applied = 6;
        int32_t memory_frame = 9;
        uint32_t memory_applied = 12;
        Horse::RollbackFrozenBoundaryClockRestoreGate gate {};
        if (!gate.arm(0x1000, frozen)) return 184;
        auto unreadable = [](uintptr_t, void*, size_t) noexcept {
            return false;
        };
        auto write = [&](uintptr_t address, const void* source,
                         size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
                std::memcpy(&memory_frame, source, size);
            else if (address == 0x248C
                && size == sizeof(memory_applied))
                std::memcpy(&memory_applied, source, size);
            else
                return false;
            return true;
        };
        if (gate.restore_once(unreadable, write) || !gate.failed()
            || gate.attempts() != 1 || gate.verified_restores() != 0
            || gate.failures() != 1)
            return 185;
    }
    {
        Horse::RollbackNativeSimulationClock frozen {};
        frozen.battle_last_frame = 4;
        frozen.battle_last_applied = 6;
        int32_t memory_frame = 4;
        uint32_t memory_applied = 12;
        auto read = [&](uintptr_t address, void* destination,
                        size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
                std::memcpy(destination, &memory_frame, size);
            else if (address == 0x248C
                && size == sizeof(memory_applied))
                std::memcpy(destination, &memory_applied, size);
            else
                return false;
            return true;
        };
        auto write = [&](uintptr_t address, const void* source,
                         size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
                std::memcpy(&memory_frame, source, size);
            else if (address == 0x248C
                && size == sizeof(memory_applied))
                std::memcpy(&memory_applied, source, size);
            else
                return false;
            return true;
        };
        Horse::RollbackFrozenBoundaryReleaseCoordinator lifecycle {};
        if (!lifecycle.arm(0x1000, frozen)
            || lifecycle.pass_through_allowed())
            return 186;
        // Model fail_closed inside the peer-wait callback. Reentrant
        // shutdown must remain owned/frozen while the per-call guard unwinds.
        uint32_t attempts = 0, verified = 0, fallbacks = 0, failures = 0;
        {
            auto restore_previous_call = [&]() noexcept {
                memory_frame = 4;
                memory_applied = 10;
                return true;
            };
            auto verify_previous_call = [&]() noexcept {
                return memory_frame == 4 && memory_applied == 10;
            };
            Horse::RollbackInitialBoundaryCursorRestoreGuard guard(
                attempts, verified, fallbacks, failures,
                restore_previous_call, verify_previous_call);
            guard.mark_armed();
            lifecycle.note_fail_closed();
            lifecycle.defer_shutdown();
            if (!lifecycle.fatal_frozen()
                || !lifecycle.shutdown_deferred()
                || !lifecycle.needs_fatal_restore()
                || lifecycle.pass_through_allowed())
                return 187;
        }
        if (memory_frame != 4 || memory_applied != 10
            || attempts != 0 || verified != 0
            || fallbacks != 1 || failures != 0)
            return 194;
        // The next service tick restores the original boundary. Restoration
        // alone does not authorize pass-through.
        if (!lifecycle.service_fatal_restore(read, write)
            || memory_frame != 4 || memory_applied != 6
            || lifecycle.attempts() != 1
            || lifecycle.verified_restores() != 1
            || lifecycle.needs_fatal_restore()
            || lifecycle.pass_through_allowed())
            return 188;
        // Nonreentrant shutdown may now release exactly once without another
        // clock write/read cycle.
        if (!lifecycle.prepare_pass_through_release(read, write)
            || !lifecycle.pass_through_allowed()
            || lifecycle.shutdown_deferred()
            || lifecycle.attempts() != 1
            || lifecycle.verified_restores() != 1)
            return 189;
    }
    {
        Horse::RollbackNativeSimulationClock frozen {};
        frozen.battle_last_frame = 4;
        frozen.battle_last_applied = 6;
        int32_t memory_frame = 4;
        uint32_t memory_applied = 12;
        auto read = [&](uintptr_t address, void* destination,
                        size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
                std::memcpy(destination, &memory_frame, size);
            else if (address == 0x248C
                && size == sizeof(memory_applied))
                std::memcpy(destination, &memory_applied, size);
            else
                return false;
            return true;
        };
        auto partial_write = [&](uintptr_t address, const void* source,
                                 size_t size) noexcept {
            if (address != 0x2488 || size != sizeof(memory_frame))
                return false;
            std::memcpy(&memory_frame, source, size);
            return true;
        };
        Horse::RollbackFrozenBoundaryReleaseCoordinator lifecycle {};
        if (!lifecycle.arm(0x1000, frozen)) return 190;
        lifecycle.note_fail_closed();
        if (lifecycle.service_fatal_restore(read, partial_write)
            || !lifecycle.failed() || !lifecycle.fatal_frozen()
            || lifecycle.pass_through_allowed()
            || lifecycle.attempts() != 1 || lifecycle.failures() != 1)
            return 191;
        // A failure latch is permanent: shutdown cannot retry the writes or
        // make pass-through reachable.
        if (lifecycle.prepare_pass_through_release(read, partial_write)
            || lifecycle.pass_through_allowed()
            || lifecycle.attempts() != 1 || lifecycle.failures() != 1)
            return 192;
    }
    {
        Horse::RollbackNativeSimulationClock frozen {};
        frozen.battle_last_frame = 4;
        frozen.battle_last_applied = 6;
        int32_t memory_frame = 4;
        uint32_t memory_applied = 12;
        auto read = [&](uintptr_t address, void* destination,
                        size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
                std::memcpy(destination, &memory_frame, size);
            else if (address == 0x248C
                && size == sizeof(memory_applied))
                std::memcpy(destination, &memory_applied, size);
            else
                return false;
            return true;
        };
        auto write = [&](uintptr_t address, const void* source,
                         size_t size) noexcept {
            if (address == 0x2488 && size == sizeof(memory_frame))
                std::memcpy(&memory_frame, source, size);
            else if (address == 0x248C
                && size == sizeof(memory_applied))
                std::memcpy(&memory_applied, source, size);
            else
                return false;
            return true;
        };
        Horse::RollbackFrozenBoundaryReleaseCoordinator lifecycle {};
        if (!lifecycle.arm(0x1000, frozen)
            || !lifecycle.restore_before_frame_zero(read, write)
            || !lifecycle.restored()
            || lifecycle.pass_through_allowed()
            || lifecycle.attempts() != 1
            || lifecycle.verified_restores() != 1)
            return 193;
    }
    {
        uint32_t attempts = 0, verified = 0, fallbacks = 0, failures = 0;
        uint32_t restore_calls = 0, verify_calls = 0;
        int32_t frame = 10;
        uint32_t applied = 11;
        auto restore = [&]() noexcept {
            ++restore_calls;
            frame = 4;
            applied = 9;
            return true;
        };
        auto verify = [&]() noexcept {
            ++verify_calls;
            return true;
        };
        {
            Horse::RollbackInitialBoundaryCursorRestoreGuard guard(
                attempts, verified, fallbacks, failures, restore, verify);
            guard.mark_armed();
        }
        if (attempts != 0 || verified != 0 || fallbacks != 1
            || failures != 0 || restore_calls != 1 || verify_calls != 0
            || frame != 4 || applied != 9)
            return 130;
    }
    {
        uint32_t attempts = 0, verified = 0, fallbacks = 0, failures = 0;
        int32_t frame = 10;
        uint32_t applied = 11;
        auto restore = [&]() noexcept {
            frame = 4;
            return false;
        };
        auto verify = []() noexcept { return true; };
        {
            Horse::RollbackInitialBoundaryCursorRestoreGuard guard(
                attempts, verified, fallbacks, failures, restore, verify);
            guard.mark_armed();
        }
        if (attempts != 0 || verified != 0 || fallbacks != 1
            || failures != 1 || frame != 4 || applied != 11)
            return 131;
    }
    // The guard deliberately sees only a boolean verifier result. Exercise
    // both production causes: unreadable cursor fields and readable mismatch.
    for (int verify_case = 0; verify_case < 2; ++verify_case)
    {
        uint32_t attempts = 0, verified = 0, fallbacks = 0, failures = 0;
        uint32_t restore_calls = 0, verify_calls = 0;
        auto restore = [&]() noexcept {
            ++restore_calls;
            return true;
        };
        auto verify = [&]() noexcept {
            ++verify_calls;
            return false;
        };
        {
            Horse::RollbackInitialBoundaryCursorRestoreGuard guard(
                attempts, verified, fallbacks, failures, restore, verify);
            guard.mark_armed();
            if (guard.restore_and_verify() || !guard.armed()
                || attempts != 1 || verified != 0 || fallbacks != 0
                || failures != 1)
                return verify_case == 0 ? 132 : 133;
        }
        if (attempts != 1 || verified != 0 || fallbacks != 1
            || failures != 1 || restore_calls != 2 || verify_calls != 1)
            return verify_case == 0 ? 134 : 135;
    }

    initial_boundary_frozen = false;
    enclosing_calls = 0;
    commits = 0;
    if (run_zero_gameplay_tail(true) || enclosing_calls != 1
        || commits != 0 || initial_boundary_frozen)
        return 115;

    Horse::RollbackNativeSimulationIterationToken token {};
    token.valid = true;
    token.input_callbacks = {0x1000, 2, 0, 0x1111, 0x3333};
    token.simulation_callbacks = {0x2000, 1, 0, 0x2222, 0x4444};
    token.loop_again = 0;
    token.pending_dispatch = 0;
    token.unpause_grace_period = 0;
    auto after = token;
    if (!Horse::ValidateRollbackNativeSimulationIterationCompletion(
            token, after))
        return 3;
    if (!Horse::RollbackNativeCallbackSelectionMatches(
            0x10003u, 0x10003u, 0x1234u, 0x1234u)
        || Horse::RollbackNativeCallbackSelectionMatches(
            0x10004u, 0x10003u, 0x1234u, 0x1234u)
        || Horse::RollbackNativeCallbackSelectionMatches(
            0x10003u, 0x10003u, 0x5678u, 0x1234u))
        return 36;
    using SelectionStatus = Horse::RollbackNativeCallbackSelectionStatus;
    if (Horse::ClassifyRollbackNativeCallbackSelection(
            0, 0x10003u, 0x1234u, 0x1234u)
            != SelectionStatus::Waiting
        || Horse::ClassifyRollbackNativeCallbackSelection(
            0x10003u, 0x10003u, 0, 0x1234u)
            != SelectionStatus::Waiting
        || Horse::ClassifyRollbackNativeCallbackSelection(
            0x10004u, 0x10003u, 0x1234u, 0x1234u)
            != SelectionStatus::StageMismatch
        || Horse::ClassifyRollbackNativeCallbackSelection(
            0x10003u, 0x10003u, 0x5678u, 0x1234u)
            != SelectionStatus::CharacterMismatch)
        return 37;
    using BindingResult = Horse::RollbackStockSelectionBindingResult;
    Horse::RollbackStockSelectionBinding observed_binding {};
    if (observed_binding.observe(
            true, 0x10003u, 0x1111u, 0, 0)
            != BindingResult::Waiting
        || observed_binding.valid())
        return 182;
    if (observed_binding.observe(
            true, 0x10003u, 0x1111u, 0x10009u, 0x9999u)
            != BindingResult::Bound
        || observed_binding.native_stage_identity != 0x10009u
        || observed_binding.selection_hash != 0x9999u
        || observed_binding.observe(
            true, 0x10003u, 0x1111u, 0x10009u, 0x9999u)
            != BindingResult::Match
        || observed_binding.observe(
            true, 0x10003u, 0x1111u, 0x1000Au, 0x9999u)
            != BindingResult::Mismatch)
        return 183;
    observed_binding.reset();
    if (observed_binding.valid()) return 184;
    Horse::RollbackStockSelectionBinding configured_binding {};
    if (configured_binding.observe(
            false, 0x10003u, 0x3333u, 0x10009u, 0x9999u)
            != BindingResult::Bound
        || configured_binding.native_stage_identity != 0x10003u
        || configured_binding.selection_hash != 0x3333u
        || configured_binding.observe(
            false, 0x10004u, 0x3333u, 0x10009u, 0x9999u)
            != BindingResult::Mismatch)
        return 185;
    after.simulation_callbacks.count = 0;
    if (Horse::ValidateRollbackNativeSimulationIterationCompletion(
            token, after))
        return 4;
    after = token;
    after.loop_again = 1;
    if (Horse::ValidateRollbackNativeSimulationIterationCompletion(
            token, after))
        return 5;
    after = token;
    after.unpause_grace_period = 1;
    if (Horse::ValidateRollbackNativeSimulationIterationCompletion(
            token, after))
        return 27;
    auto terminal_after = token;
    terminal_after.unpause_grace_period =
        Horse::kRollbackNativeDeferredTerminalGraceStart;
    if (!Horse::ValidateRollbackNativeSimulationIterationCompletionForDeferredTerminal(
            token, terminal_after, false, true))
        return 104;
    auto terminal_next = terminal_after;
    --terminal_next.unpause_grace_period;
    if (!Horse::ValidateRollbackNativeSimulationIterationCompletionForDeferredTerminal(
            terminal_after, terminal_next, true, true))
        return 105;
    if (Horse::ValidateRollbackNativeSimulationIterationCompletionForDeferredTerminal(
            token, terminal_after, false, false))
        return 106;
    auto terminal_unchanged = terminal_after;
    if (Horse::ValidateRollbackNativeSimulationIterationCompletionForDeferredTerminal(
            terminal_after, terminal_unchanged, true, true))
        return 107;
    auto terminal_callback_edge = token;
    terminal_callback_edge.unpause_grace_period = 1;
    if (Horse::ValidateRollbackNativeSimulationIterationBoundaryForDeferredTerminal(
            terminal_callback_edge, true))
        return 108;
    auto terminal_callback_fired = token;
    if (Horse::ValidateRollbackNativeSimulationIterationCompletionForDeferredTerminal(
            terminal_callback_edge, terminal_callback_fired, true, true))
        return 109;
    auto terminal_topology_changed = terminal_next;
    terminal_topology_changed.simulation_callbacks.target_digest ^= 1;
    if (Horse::ValidateRollbackNativeSimulationIterationCompletionForDeferredTerminal(
            terminal_after, terminal_topology_changed, true, true))
        return 110;
    if (Horse::FilterRollbackNativeCallbackCoverageMismatchForDeferredTerminal(
            Horse::RollbackNativeCallbackCoverageMismatchBoundary,
            terminal_after, true) != 0)
        return 111;
    const uint32_t boundary_and_topology =
        Horse::RollbackNativeCallbackCoverageMismatchBoundary
        | Horse::RollbackNativeCallbackCoverageMismatchSimulationCollection;
    if (Horse::FilterRollbackNativeCallbackCoverageMismatchForDeferredTerminal(
            boundary_and_topology, terminal_after, true)
        != Horse::RollbackNativeCallbackCoverageMismatchSimulationCollection)
        return 112;
    Horse::RollbackNativeOwnedIterationFailureEvidence iteration_failure {};
    if (!iteration_failure.latch(
            1151, false,
            Horse::RollbackNativeCallbackCoverageMismatchBoundary,
            Horse::RollbackNativeCallbackCoverageMismatchBoundary,
            terminal_next, terminal_after,
            true, false,
            true, 1136,
            true, 1136,
            1, 1,
            false)
        || !iteration_failure.valid
        || iteration_failure.kind
            != Horse::RollbackNativeOwnedIterationFailureKind::Boundary
        || iteration_failure.logical_frame != 1151
        || iteration_failure.rolling_back
        || iteration_failure.raw_coverage_mismatch
            != Horse::RollbackNativeCallbackCoverageMismatchBoundary
        || iteration_failure.effective_coverage_mismatch
            != Horse::RollbackNativeCallbackCoverageMismatchBoundary
        || !iteration_failure.terminal_pending_before
        || iteration_failure.terminal_pending_after
        || iteration_failure.before.unpause_grace_period
            != Horse::kRollbackNativeDeferredTerminalGraceStart - 1
        || iteration_failure.after.unpause_grace_period
            != Horse::kRollbackNativeDeferredTerminalGraceStart)
        return 113;
    if (iteration_failure.latch(
            1152, true,
            Horse::RollbackNativeCallbackCoverageMismatchSimulationCollection,
            Horse::RollbackNativeCallbackCoverageMismatchSimulationCollection,
            token, token,
            false, false,
            false, 0,
            false, 0,
            1, 2,
            false)
        || iteration_failure.logical_frame != 1151
        || iteration_failure.rolling_back
        || iteration_failure.kind
            != Horse::RollbackNativeOwnedIterationFailureKind::Boundary
        || iteration_failure.notification_suppressions_after != 1)
        return 114;
    Horse::RollbackNativeOwnedIterationFailureEvidence topology_failure {};
    if (!topology_failure.latch(
            9, true,
            Horse::RollbackNativeCallbackCoverageMismatchBoundary
                | Horse::RollbackNativeCallbackCoverageMismatchInputCollection,
            Horse::RollbackNativeCallbackCoverageMismatchInputCollection,
            token, token,
            false, false,
            false, 0,
            false, 0,
            0, 0,
            false)
        || topology_failure.kind
            != Horse::RollbackNativeOwnedIterationFailureKind::Topology)
        return 115;
    Horse::RollbackNativeOwnedIterationFailureEvidence completion_failure {};
    if (!completion_failure.latch(
            10, true, 0, 0,
            token, token,
            false, false,
            false, 0,
            false, 0,
            0, 0,
            false)
        || completion_failure.kind
            != Horse::RollbackNativeOwnedIterationFailureKind::Completion)
        return 116;
    if (Horse::FilterRollbackNativeCallbackCoverageMismatchForDeferredTerminal(
            Horse::RollbackNativeCallbackCoverageMismatchBoundary,
            terminal_callback_edge, true)
        != Horse::RollbackNativeCallbackCoverageMismatchBoundary)
        return 113;

    std::array<uint64_t, 6> callback_target_storage {
        0x1000, 0x2000, 0x3000, 0xAABBCCDD00000004ull,
        0x5000, 0x6000};
    const uint64_t callback_topology =
        Horse::HashRollbackNativeCallbackTargetTopology(
            0x9000, callback_target_storage, 0xA000, 0xA004,
            0xB000, 0xC000, 0xD000);
    auto mutable_callback_payload = callback_target_storage;
    mutable_callback_payload[3] ^= 0xFFFF000000000000ull;
    mutable_callback_payload[4] ^= 0x1111;
    if (Horse::HashRollbackNativeCallbackTargetTopology(
            0x9000, mutable_callback_payload, 0xA000, 0xA004,
            0xB000, 0xC000, 0xD000) != callback_topology)
        return 195;
    auto changed_callback_target = callback_target_storage;
    changed_callback_target[2] ^= 1;
    if (Horse::HashRollbackNativeCallbackTargetTopology(
            0x9000, changed_callback_target, 0xA000, 0xA004,
            0xB000, 0xC000, 0xD000) == callback_topology)
        return 196;
    changed_callback_target = callback_target_storage;
    changed_callback_target[5] ^= 1;
    if (Horse::HashRollbackNativeCallbackTargetTopology(
            0x9000, changed_callback_target, 0xA000, 0xA004,
            0xB000, 0xC000, 0xD000) == callback_topology)
        return 199;
    if (Horse::HashRollbackNativeCallbackTargetTopology(
            0x9000, callback_target_storage, 0xA000, 0xA004,
            0xB000, 0xC000, 0xD001) == callback_topology)
        return 197;
    const uint64_t callback_entry_topology =
        Horse::HashRollbackNativeCallbackEntryTopology(
            0xE000, 0xF000, 3);
    if (Horse::HashRollbackNativeCallbackEntryTopology(
            0xE000, 0xF000, 3) != callback_entry_topology
        || Horse::HashRollbackNativeCallbackEntryTopology(
            0xE000, 0xF001, 3) == callback_entry_topology)
        return 198;

    MockMemory memory {};
    for (size_t i = 0; i < memory.bytes.size(); ++i)
        memory.bytes[i] = static_cast<uint8_t>((i * 13u) & 0xFFu);
    constexpr uintptr_t kCallbackCollection = 0x400;
    constexpr uintptr_t kCallbackEntries = 0x800;
    constexpr uintptr_t kCallbackTarget = 0xA00;
    constexpr uintptr_t kCallbackVtable = 0xC00;
    constexpr uintptr_t kCallbackDispatch = 0x140123450;
    constexpr uintptr_t kResolvedObject = 0xE00;
    constexpr uintptr_t kResolvedVtable = 0x1000;
    constexpr uintptr_t kBoundFunction = 0x140427940;
    constexpr uintptr_t kVirtual5f8Thunk = 0x1408954A4;
    constexpr uintptr_t kResolvedVirtual5f8 = 0x140765430;
    constexpr int32_t kThisAdjustment = 0;
    const int32_t callback_count = 1;
    const int32_t callback_recursion = 0;
    const uint32_t callback_state = 1;
    if (!memory.write(kCallbackCollection + 0x40,
            &kCallbackEntries, sizeof(kCallbackEntries))
        || !memory.write(kCallbackCollection + 0x50,
            &callback_count, sizeof(callback_count))
        || !memory.write(kCallbackCollection + 0x64,
            &callback_recursion, sizeof(callback_recursion))
        || !memory.write(kCallbackEntries + 0x20,
            &kCallbackTarget, sizeof(kCallbackTarget))
        || !memory.write(kCallbackEntries + 0x30,
            &callback_state, sizeof(callback_state))
        || !memory.write(kCallbackTarget,
             &kCallbackVtable, sizeof(kCallbackVtable))
        || !memory.write(kCallbackVtable + 0x68,
             &kCallbackDispatch, sizeof(kCallbackDispatch))
        || !memory.write(kCallbackTarget + 0x10,
             &kBoundFunction, sizeof(kBoundFunction))
        || !memory.write(kCallbackTarget + 0x18,
             &kThisAdjustment, sizeof(kThisAdjustment))
        )
        return 30;
    const auto is_executable = [=](uintptr_t address) noexcept {
        return address == kCallbackDispatch
            || address == kBoundFunction
            || address == kVirtual5f8Thunk
            || address == kResolvedVirtual5f8;
    };
    Horse::RollbackNativeCallbackCollectionInventory callback_inventory {};
    if (!Horse::CaptureRollbackNativeCallbackCollectionInventory(
            kCallbackCollection,
            Horse::RollbackNativeCallbackCollectionKind::Input,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return memory.read(address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return memory.read(address, out, size);
            }, [&](uintptr_t address, uintptr_t& value) noexcept {
                if (address != kCallbackTarget + 0x08) return false;
                value = kResolvedObject;
                return true;
            }, is_executable, kVirtual5f8Thunk, callback_inventory)
        || callback_inventory.count != 1
        || callback_inventory.identity_digest == 0
        || callback_inventory.targets[0].target != kCallbackTarget
        || callback_inventory.targets[0].vtable != kCallbackVtable
        || callback_inventory.targets[0].dispatch != kCallbackDispatch
        || callback_inventory.targets[0].bound_function != kBoundFunction
        || callback_inventory.targets[0].resolved_object != kResolvedObject
        || callback_inventory.targets[0].resolved_this != kResolvedObject
        || callback_inventory.targets[0].resolved_vtable != 0
        || callback_inventory.targets[0].virtual_5f8_applies
        || callback_inventory.targets[0].effective_target != kBoundFunction
        || !callback_inventory.targets[0].external_target
        || callback_inventory.targets[0].entry_state != callback_state)
        return 31;
    Horse::RollbackNativeCallbackCollectionInventory repeated_inventory {};
    if (!Horse::CaptureRollbackNativeCallbackCollectionInventory(
            kCallbackCollection,
            Horse::RollbackNativeCallbackCollectionKind::Input,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return memory.read(address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return memory.read(address, out, size);
            }, [&](uintptr_t, uintptr_t& value) noexcept {
                value = kResolvedObject;
                return true;
            }, is_executable, kVirtual5f8Thunk, repeated_inventory)
        || repeated_inventory.identity_digest
            != callback_inventory.identity_digest)
        return 35;
    Horse::RollbackNativeCallbackCollectionInventory unresolved_inventory {};
    if (Horse::CaptureRollbackNativeCallbackCollectionInventory(
            kCallbackCollection,
            Horse::RollbackNativeCallbackCollectionKind::Input,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return memory.read(address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return memory.read(address, out, size);
            }, [&](uintptr_t, uintptr_t&) noexcept {
                return false;
            }, is_executable, kVirtual5f8Thunk, unresolved_inventory))
        return 39;
    MockMemory adjusted_memory = memory;
    constexpr int32_t kPositiveAdjustment = 0x20;
    if (!adjusted_memory.write(kCallbackTarget + 0x18,
            &kPositiveAdjustment, sizeof(kPositiveAdjustment))
        || !adjusted_memory.write(kCallbackTarget + 0x10,
            &kVirtual5f8Thunk, sizeof(kVirtual5f8Thunk))
        || !adjusted_memory.write(kResolvedObject + kPositiveAdjustment,
            &kResolvedVtable, sizeof(kResolvedVtable))
        || !adjusted_memory.write(kResolvedVtable + 0x5F8,
            &kResolvedVirtual5f8, sizeof(kResolvedVirtual5f8))
        || !Horse::CaptureRollbackNativeCallbackCollectionInventory(
            kCallbackCollection,
            Horse::RollbackNativeCallbackCollectionKind::Input,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return adjusted_memory.read(address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return adjusted_memory.read(address, out, size);
            }, [&](uintptr_t, uintptr_t& value) noexcept {
                value = kResolvedObject;
                return true;
            }, is_executable, kVirtual5f8Thunk, unresolved_inventory)
        || unresolved_inventory.targets[0].this_adjustment
            != kPositiveAdjustment
        || unresolved_inventory.targets[0].resolved_this
            != kResolvedObject + kPositiveAdjustment
        || !unresolved_inventory.targets[0].virtual_5f8_applies
        || unresolved_inventory.targets[0].resolved_virtual_5f8
            != kResolvedVirtual5f8
        || unresolved_inventory.targets[0].effective_target
            != kResolvedVirtual5f8)
        return 40;
    MockMemory missing_virtual_memory = adjusted_memory;
    const uintptr_t missing_virtual = 0;
    if (!missing_virtual_memory.write(kResolvedVtable + 0x5F8,
            &missing_virtual, sizeof(missing_virtual))
        || Horse::CaptureRollbackNativeCallbackCollectionInventory(
            kCallbackCollection,
            Horse::RollbackNativeCallbackCollectionKind::Input,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return missing_virtual_memory.read(
                    address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return missing_virtual_memory.read(address, out, size);
            }, [&](uintptr_t, uintptr_t& value) noexcept {
                value = kResolvedObject;
                return true;
            }, is_executable, kVirtual5f8Thunk, unresolved_inventory))
        return 41;
    MockMemory non_executable_virtual_memory = adjusted_memory;
    constexpr uintptr_t kNonExecutableVirtual = 0x1234;
    if (!non_executable_virtual_memory.write(kResolvedVtable + 0x5F8,
            &kNonExecutableVirtual, sizeof(kNonExecutableVirtual))
        || Horse::CaptureRollbackNativeCallbackCollectionInventory(
            kCallbackCollection,
            Horse::RollbackNativeCallbackCollectionKind::Input,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return non_executable_virtual_memory.read(
                    address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return non_executable_virtual_memory.read(
                    address, out, size);
            }, [&](uintptr_t, uintptr_t& value) noexcept {
                value = kResolvedObject;
                return true;
            }, is_executable, kVirtual5f8Thunk, unresolved_inventory))
        return 42;
    const MockMemory valid_callback_memory = memory;
    uint64_t changed_target_word = UINT64_C(0xCAFEBABE12345678);
    if (!memory.write(kCallbackTarget + 0x08,
            &changed_target_word, sizeof(changed_target_word))
        || !Horse::CaptureRollbackNativeCallbackCollectionInventory(
            kCallbackCollection,
            Horse::RollbackNativeCallbackCollectionKind::Input,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return memory.read(address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return memory.read(address, out, size);
            }, [&](uintptr_t, uintptr_t& value) noexcept {
                value = kResolvedObject;
                return true;
            }, is_executable, kVirtual5f8Thunk, repeated_inventory)
        || repeated_inventory.identity_digest
            == callback_inventory.identity_digest)
        return 36;
    memory = valid_callback_memory;
    const int32_t over_capacity_count = 33;
    if (!memory.write(kCallbackCollection + 0x50,
            &over_capacity_count, sizeof(over_capacity_count))
        || Horse::CaptureRollbackNativeCallbackCollectionInventory(
            kCallbackCollection,
            Horse::RollbackNativeCallbackCollectionKind::Input,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return memory.read(address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return memory.read(address, out, size);
            }, [&](uintptr_t, uintptr_t& value) noexcept {
                value = kResolvedObject;
                return true;
            }, is_executable, kVirtual5f8Thunk, repeated_inventory))
        return 37;
    memory = valid_callback_memory;
    const int32_t active_recursion = 1;
    if (!memory.write(kCallbackCollection + 0x64,
            &active_recursion, sizeof(active_recursion))
        || Horse::CaptureRollbackNativeCallbackCollectionInventory(
            kCallbackCollection,
            Horse::RollbackNativeCallbackCollectionKind::Input,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return memory.read(address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return memory.read(address, out, size);
            }, [&](uintptr_t, uintptr_t& value) noexcept {
                value = kResolvedObject;
                return true;
            }, is_executable, kVirtual5f8Thunk, repeated_inventory))
        return 38;
    memory = valid_callback_memory;

    // Exact executable callback policy. Object/table/topology are immutable;
    // indices, action mode, gates, and subelement records are snapshot state.
    constexpr uintptr_t kImageBase = 0x140000000;
    constexpr uintptr_t kInputStateObject = 0xE00;
    constexpr uintptr_t kPausedStateObject = 0x1400;
    constexpr uintptr_t kRefreshStateObject = 0x1600;
    constexpr uintptr_t kSlotTable = 0x1C00;
    constexpr uintptr_t kSubelements = 0x1B00;
    constexpr uintptr_t kEventMasks = 0x1E00;
    const int32_t table_index = -1;
    const int32_t slot_index = -1;
    const uint8_t stock_pvp_action_mode = 0;
    const int32_t mode_frame_counter = 12;
    const uint8_t pending_window_gate = 1;
    const uint8_t special_slot_dirty = 2;
    const uint8_t event_window_gate = 3;
    const uint16_t mode_state_reserved = 0x4455;
    const int32_t completion_delay_frames = 6;
    const uint8_t untouched_padding_481 = 0xA1;
    const uint8_t untouched_padding_489 = 0xA9;
    const int32_t subelement_count = 2;
    const int32_t subelement_capacity = 4;
    const int32_t event_mask_count = 2;
    const int32_t event_mask_capacity = 2;
    const std::array<uint64_t, 2> event_masks {
        UINT64_C(0x11), UINT64_C(0x2200000000),
    };
    std::array<uint8_t, 0x40> subelements {};
    for (size_t i = 0; i < subelements.size(); ++i)
        subelements[i] = static_cast<uint8_t>(i + 1);
    if (!memory.write(kInputStateObject + 0x470,
            &kSlotTable, sizeof(kSlotTable))
        || !memory.write(kInputStateObject + 0x478,
            &table_index, sizeof(table_index))
        || !memory.write(kInputStateObject + 0x47C,
            &slot_index, sizeof(slot_index))
        || !memory.write(kInputStateObject + 0x480,
            &stock_pvp_action_mode, sizeof(stock_pvp_action_mode))
        || !memory.write(kInputStateObject + 0x481,
            &untouched_padding_481, sizeof(untouched_padding_481))
        || !memory.write(kInputStateObject + 0x484,
            &mode_frame_counter, sizeof(mode_frame_counter))
        || !memory.write(kInputStateObject + 0x488,
            &pending_window_gate, sizeof(pending_window_gate))
        || !memory.write(kInputStateObject + 0x489,
            &untouched_padding_489, sizeof(untouched_padding_489))
        || !memory.write(kInputStateObject + 0x490,
            &special_slot_dirty, sizeof(special_slot_dirty))
        || !memory.write(kInputStateObject + 0x491,
            &event_window_gate, sizeof(event_window_gate))
        || !memory.write(kInputStateObject + 0x492,
            &mode_state_reserved, sizeof(mode_state_reserved))
        || !memory.write(kInputStateObject + 0x494,
            &completion_delay_frames, sizeof(completion_delay_frames))
        || !memory.write(kInputStateObject + 0x498,
            &kSubelements, sizeof(kSubelements))
        || !memory.write(kInputStateObject + 0x4A0,
            &subelement_count, sizeof(subelement_count))
        || !memory.write(kInputStateObject + 0x4A4,
            &subelement_capacity, sizeof(subelement_capacity))
        || !memory.write(kInputStateObject + 0x4A8,
            &kEventMasks, sizeof(kEventMasks))
        || !memory.write(kInputStateObject + 0x4B0,
            &event_mask_count, sizeof(event_mask_count))
        || !memory.write(kInputStateObject + 0x4B4,
            &event_mask_capacity, sizeof(event_mask_capacity))
        || !memory.write(kSubelements, subelements.data(),
            subelements.size())
        || !memory.write(kEventMasks, event_masks.data(),
            sizeof(event_masks)))
        return 43;

    const auto make_descriptor = [=](
        uintptr_t entry, uintptr_t object, uintptr_t dispatch,
        uintptr_t bound, uintptr_t effective,
        bool virtual_dispatch = false,
        uintptr_t resolved_vtable = 0) noexcept {
        Horse::RollbackNativeCallbackTargetDescriptor descriptor {};
        descriptor.entry = entry;
        descriptor.target = entry + 0x100;
        descriptor.vtable = kImageBase + 0x1000;
        descriptor.dispatch = dispatch;
        descriptor.bound_function = bound;
        descriptor.resolved_object = object;
        descriptor.resolved_this = object;
        descriptor.resolved_vtable = resolved_vtable;
        descriptor.resolved_virtual_5f8 = virtual_dispatch
            ? effective : 0;
        descriptor.effective_target = effective;
        descriptor.entry_digest = entry + 1;
        descriptor.target_storage_digest = entry + 2;
        descriptor.resolved_storage_digest = entry + 3;
        descriptor.entry_state = 3;
        descriptor.external_target = true;
        descriptor.virtual_5f8_applies = virtual_dispatch;
        return descriptor;
    };
    Horse::RollbackNativeCallbackCollectionInventory exact_input {};
    exact_input.kind =
        Horse::RollbackNativeCallbackCollectionKind::Input;
    exact_input.collection = 0x300;
    exact_input.count = 1;
    exact_input.identity_digest = 0xA001;
    exact_input.targets[0] = make_descriptor(
        0x500, kInputStateObject,
        kImageBase + Horse::kRollbackNativeCallbackRvaInputDispatch,
        kImageBase + Horse::kRollbackNativeCallbackRvaAttackStateInput,
        kImageBase + Horse::kRollbackNativeCallbackRvaAttackStateInput);
    exact_input.targets[0].vtable = kImageBase
        + Horse::kRollbackNativeCallbackWrapperVtableRvaInput;
    exact_input.targets[0].resolved_storage[0] =
        kImageBase + Horse::kRollbackNativeCallbackVtableRvaInput;
    Horse::RollbackNativeCallbackCollectionInventory exact_simulation {};
    exact_simulation.kind =
        Horse::RollbackNativeCallbackCollectionKind::Simulation;
    exact_simulation.collection = 0x380;
    exact_simulation.count = 2;
    exact_simulation.identity_digest = 0xA002;
    exact_simulation.targets[0] = make_descriptor(
        0x600, kPausedStateObject,
        kImageBase + Horse::kRollbackNativeCallbackRvaSimulationDispatch,
        kImageBase + Horse::kRollbackNativeCallbackRvaPausedTickThunk,
        kImageBase + Horse::kRollbackNativeCallbackRvaPausedTickTarget,
        true,
        kImageBase + Horse::kRollbackNativeCallbackVtableRvaPausedTick);
    exact_simulation.targets[0].vtable = kImageBase
        + Horse::kRollbackNativeCallbackWrapperVtableRvaSimulation;
    exact_simulation.targets[0].resolved_storage[0] =
        kImageBase + Horse::kRollbackNativeCallbackVtableRvaPausedTick;
    exact_simulation.targets[1] = make_descriptor(
        0x700, kRefreshStateObject,
        kImageBase + Horse::kRollbackNativeCallbackRvaSimulationDispatch,
        kImageBase + Horse::kRollbackNativeCallbackRvaRefreshMoveCaches,
        kImageBase + Horse::kRollbackNativeCallbackRvaRefreshMoveCaches);
    exact_simulation.targets[1].vtable = kImageBase
        + Horse::kRollbackNativeCallbackWrapperVtableRvaSimulation;
    exact_simulation.targets[1].resolved_storage[0] =
        kImageBase + Horse::kRollbackNativeCallbackVtableRvaMoveCache;
    Horse::RollbackNativeSimulationIterationToken exact_token {};
    exact_token.valid = true;
    exact_token.input_callbacks = {0x800, 1, 0, 0xB001, 0xC001};
    exact_token.simulation_callbacks = {0x900, 2, 0, 0xB002, 0xC002};
    exact_token.loop_again = 0;
    exact_token.pending_dispatch = 0;
    exact_token.unpause_grace_period = 0;
    Horse::RollbackNativeCallbackCoverage coverage {};
    Horse::RollbackNativeInputCallbackState current_input_state {};
    const auto read_memory = [&](uintptr_t address, void* out,
                                 size_t size) noexcept {
        return memory.read(address, out, size);
    };
    if (!Horse::BuildRollbackNativeCallbackCoverage(
            kImageBase, exact_token, exact_input, exact_simulation,
            read_memory, coverage)
        || !Horse::CaptureRollbackNativeInputCallbackState(
            kInputStateObject, read_memory, current_input_state)
        || !coverage.accepts(exact_token, current_input_state)
        || !Horse::RollbackNativeCallbackPreflightAllowsHookInstallation(
            exact_token, coverage, current_input_state, false)
        || Horse::RollbackNativeCallbackPreflightAllowsHookInstallation(
            exact_token, coverage, current_input_state, true))
        return 44;
    if (Horse::DiagnoseRollbackNativeCallbackCoverage(
            exact_token, coverage, current_input_state)
        != Horse::RollbackNativeCallbackCoverageMismatchNone)
        return 96;
    auto invalid_sentinel = current_input_state;
    invalid_sentinel.table_index = -2;
    if (invalid_sentinel.valid_for_stock_pvp()) return 68;
    invalid_sentinel = current_input_state;
    invalid_sentinel.slot_index = -2;
    if (invalid_sentinel.valid_for_stock_pvp()) return 69;
    auto changed_token = exact_token;
    changed_token.simulation_callbacks.entry_digest ^= 1;
    if (coverage.accepts(changed_token, current_input_state))
        return 45;
    if (Horse::DiagnoseRollbackNativeCallbackCoverage(
            changed_token, coverage, current_input_state)
        != Horse::RollbackNativeCallbackCoverageMismatchSimulationCollection)
        return 97;
    changed_token = exact_token;
    changed_token.input_callbacks.target_digest ^= 1;
    if (coverage.accepts(changed_token, current_input_state))
        return 49;
    if (Horse::DiagnoseRollbackNativeCallbackCoverage(
            changed_token, coverage, current_input_state)
        != Horse::RollbackNativeCallbackCoverageMismatchInputCollection)
        return 98;
    changed_token = exact_token;
    ++changed_token.input_callbacks.heap_entries;
    if (coverage.accepts(changed_token, current_input_state))
        return 50;
    auto changed_input_state = current_input_state;
    ++changed_input_state.slot_index;
    changed_input_state.digest ^= 1;
    if (!coverage.accepts(exact_token, changed_input_state)
        || Horse::DiagnoseRollbackNativeCallbackCoverage(
            exact_token, coverage, changed_input_state)
            != Horse::RollbackNativeCallbackCoverageMismatchNone)
        return 99;
    auto changed_input_identity = current_input_state;
    ++changed_input_identity.slot_table;
    changed_input_identity.digest ^= 2;
    if (coverage.accepts(exact_token, changed_input_identity)
        || Horse::DiagnoseRollbackNativeCallbackCoverage(
            exact_token, coverage, changed_input_identity)
            != Horse::RollbackNativeCallbackCoverageMismatchInputState)
        return 114;
    changed_token = exact_token;
    changed_token.pending_dispatch = 1;
    if (Horse::DiagnoseRollbackNativeCallbackCoverage(
            changed_token, coverage, current_input_state)
        != Horse::RollbackNativeCallbackCoverageMismatchBoundary)
        return 100;
    Horse::RollbackNativeCallbackCoverageMismatchEvidence evidence {};
    if (evidence.latch(0, exact_token, current_input_state)
        || evidence.valid)
        return 101;
    if (!evidence.latch(
            Horse::RollbackNativeCallbackCoverageMismatchBoundary,
            changed_token, current_input_state)
        || !evidence.valid
        || evidence.mismatch_mask
            != Horse::RollbackNativeCallbackCoverageMismatchBoundary
        || evidence.token.pending_dispatch != 1)
        return 102;
    auto later_token = changed_token;
    later_token.pending_dispatch = 2;
    if (evidence.latch(
            Horse::RollbackNativeCallbackCoverageMismatchBoundary,
            later_token, current_input_state)
        || evidence.token.pending_dispatch != 1)
        return 103;

    const auto policy_rejects = [&](const auto& input_inventory,
                                    const auto& simulation_inventory) {
        Horse::RollbackNativeCallbackCoverage rejected {};
        return !Horse::BuildRollbackNativeCallbackCoverage(
            kImageBase, exact_token, input_inventory,
            simulation_inventory, read_memory, rejected);
    };
    auto wrong_input = exact_input;
    ++wrong_input.targets[0].dispatch;
    if (!policy_rejects(wrong_input, exact_simulation)) return 51;
    wrong_input = exact_input;
    ++wrong_input.targets[0].bound_function;
    if (!policy_rejects(wrong_input, exact_simulation)) return 52;
    wrong_input = exact_input;
    ++wrong_input.targets[0].effective_target;
    if (!policy_rejects(wrong_input, exact_simulation)) return 53;
    wrong_input = exact_input;
    ++wrong_input.targets[0].vtable;
    if (!policy_rejects(wrong_input, exact_simulation)) return 54;
    wrong_input = exact_input;
    ++wrong_input.targets[0].resolved_storage[0];
    if (!policy_rejects(wrong_input, exact_simulation)) return 55;
    wrong_input = exact_input;
    wrong_input.targets[0].entry_state = 2;
    if (!policy_rejects(wrong_input, exact_simulation)) return 66;
    wrong_input = exact_input;
    wrong_input.targets[0].virtual_5f8_applies = true;
    wrong_input.targets[0].resolved_vtable = kImageBase
        + Horse::kRollbackNativeCallbackVtableRvaInput;
    wrong_input.targets[0].resolved_virtual_5f8 = kImageBase + 1;
    if (!policy_rejects(wrong_input, exact_simulation)) return 67;
    auto wrong_simulation = exact_simulation;
    std::swap(wrong_simulation.targets[0],
        wrong_simulation.targets[1]);
    if (!policy_rejects(exact_input, wrong_simulation)) return 56;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[0].dispatch;
    if (!policy_rejects(exact_input, wrong_simulation)) return 57;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[0].bound_function;
    if (!policy_rejects(exact_input, wrong_simulation)) return 58;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[0].effective_target;
    if (!policy_rejects(exact_input, wrong_simulation)) return 59;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[0].vtable;
    if (!policy_rejects(exact_input, wrong_simulation)) return 60;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[0].resolved_vtable;
    if (!policy_rejects(exact_input, wrong_simulation)) return 61;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[0].resolved_storage[0];
    if (!policy_rejects(exact_input, wrong_simulation)) return 68;
    wrong_simulation = exact_simulation;
    wrong_simulation.targets[0].virtual_5f8_applies = false;
    if (!policy_rejects(exact_input, wrong_simulation)) return 69;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[1].resolved_storage[0];
    if (!policy_rejects(exact_input, wrong_simulation)) return 62;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[1].dispatch;
    if (!policy_rejects(exact_input, wrong_simulation)) return 70;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[1].bound_function;
    if (!policy_rejects(exact_input, wrong_simulation)) return 71;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[1].effective_target;
    if (!policy_rejects(exact_input, wrong_simulation)) return 72;
    wrong_simulation = exact_simulation;
    ++wrong_simulation.targets[1].vtable;
    if (!policy_rejects(exact_input, wrong_simulation)) return 73;
    wrong_simulation = exact_simulation;
    wrong_simulation.targets[1].entry_state = 2;
    if (!policy_rejects(exact_input, wrong_simulation)) return 74;
    wrong_simulation = exact_simulation;
    wrong_simulation.targets[1].resolved_this = 0;
    if (!policy_rejects(exact_input, wrong_simulation)) return 75;
    wrong_simulation = exact_simulation;
    wrong_simulation.targets[1].virtual_5f8_applies = true;
    wrong_simulation.targets[1].resolved_vtable = kImageBase + 1;
    wrong_simulation.targets[1].resolved_virtual_5f8 = kImageBase + 2;
    if (!policy_rejects(exact_input, wrong_simulation)) return 76;
    uint32_t repair_calls = 0;
    if (!Horse::RepairRollbackNativeDerivedCallbackState(
            kImageBase, coverage,
            [&](uintptr_t target, uintptr_t object) noexcept {
                ++repair_calls;
                return target == kImageBase
                        + Horse::kRollbackNativeCallbackRvaRefreshMoveCaches
                    && object == kRefreshStateObject;
            })
        || repair_calls != 1)
        return 46;
    auto wrong_refresh = coverage;
    ++wrong_refresh.derived_cache_refresh;
    if (Horse::RepairRollbackNativeDerivedCallbackState(
            kImageBase, wrong_refresh,
            [&](uintptr_t, uintptr_t) noexcept {
                ++repair_calls;
                return true;
            })
        || repair_calls != 1)
        return 47;
    uint32_t load_repair_calls = 0;
    const auto load_repair = [&]() noexcept {
        ++load_repair_calls;
        return true;
    };
    if (Horse::CompleteRollbackNativeLoadRepair(
            false, true, load_repair)
            != Horse::RollbackNativeLoadRepairResult::RestoreRejected
        || Horse::CompleteRollbackNativeLoadRepair(
            true, false, load_repair)
            != Horse::RollbackNativeLoadRepairResult::VerificationRejected
        || load_repair_calls != 0)
        return 63;
    if (Horse::CompleteRollbackNativeLoadRepair(
            true, true, load_repair)
            != Horse::RollbackNativeLoadRepairResult::Repaired
        || Horse::CompleteRollbackNativeLoadRepair(
            true, true, load_repair)
            != Horse::RollbackNativeLoadRepairResult::Repaired
        || load_repair_calls != 2)
        return 64;
    if (Horse::CompleteRollbackNativeLoadRepair(
            true, true, []() noexcept { return false; })
            != Horse::RollbackNativeLoadRepairResult::RepairFailed)
        return 65;
    Horse::RollbackNativeInputCallbackSnapshot callback_snapshot {};
    if (!Horse::CaptureRollbackNativeInputCallbackSnapshotWith(
            kInputStateObject, read_memory, callback_snapshot)
        || !Horse::ValidateRollbackNativeInputCallbackSnapshot(
            callback_snapshot))
        return 115;
    constexpr uintptr_t kReplacementSubelements = 0x1D00;
    const int32_t replacement_count = 1;
    const int32_t replacement_capacity = 4;
    const int32_t changed_table_index = 3;
    const int32_t changed_slot_index = 7;
    const uint8_t mutable_ai_action_mode = 5;
    const int32_t changed_mode_frame_counter = 99;
    const uint8_t changed_gate = 9;
    const uint16_t changed_reserved = 0x9999;
    const int32_t changed_completion_delay = 42;
    const uint8_t changed_padding_481 = 0xB1;
    const uint8_t changed_padding_489 = 0xB9;
    const std::array<uint64_t, 2> changed_event_masks {
        UINT64_C(0xDEADBEEF), UINT64_C(0x123456789ABCDEF0),
    };
    if (!memory.write(kInputStateObject + 0x480,
            &mutable_ai_action_mode, sizeof(mutable_ai_action_mode))
        || !memory.write(kInputStateObject + 0x481,
            &changed_padding_481, sizeof(changed_padding_481))
        || !memory.write(kInputStateObject + 0x484,
            &changed_mode_frame_counter, sizeof(changed_mode_frame_counter))
        || !memory.write(kInputStateObject + 0x488,
            &changed_gate, sizeof(changed_gate))
        || !memory.write(kInputStateObject + 0x489,
            &changed_padding_489, sizeof(changed_padding_489))
        || !memory.write(kInputStateObject + 0x490,
            &changed_gate, sizeof(changed_gate))
        || !memory.write(kInputStateObject + 0x491,
            &changed_gate, sizeof(changed_gate))
        || !memory.write(kInputStateObject + 0x492,
            &changed_reserved, sizeof(changed_reserved))
        || !memory.write(kInputStateObject + 0x494,
            &changed_completion_delay, sizeof(changed_completion_delay))
        || !memory.write(kInputStateObject + 0x478,
            &changed_table_index, sizeof(changed_table_index))
        || !memory.write(kInputStateObject + 0x47C,
            &changed_slot_index, sizeof(changed_slot_index))
        || !memory.write(kInputStateObject + 0x498,
            &kReplacementSubelements, sizeof(kReplacementSubelements))
        || !memory.write(kInputStateObject + 0x4A0,
            &replacement_count, sizeof(replacement_count))
        || !memory.write(kInputStateObject + 0x4A4,
            &replacement_capacity, sizeof(replacement_capacity))
        || !memory.write(kEventMasks, changed_event_masks.data(),
            sizeof(changed_event_masks))
        || !Horse::CaptureRollbackNativeInputCallbackState(
            kInputStateObject, read_memory, current_input_state)
        || !coverage.accepts(exact_token, current_input_state)
        || !Horse::RestoreRollbackNativeInputCallbackSnapshotWith(
            callback_snapshot, read_memory,
            [&](uintptr_t address, const void* source, size_t size) noexcept {
                return memory.write(address, source, size);
            }))
        return 48;
    Horse::RollbackNativeInputCallbackSnapshot callback_verification {};
    uintptr_t live_owner = 0;
    int32_t live_capacity = 0;
    uint8_t observed_padding_481 = 0;
    uint8_t observed_padding_489 = 0;
    std::array<uint64_t, 2> observed_event_masks {};
    if (!Horse::CaptureRollbackNativeInputCallbackSnapshotWith(
            kInputStateObject, read_memory, callback_verification)
        || callback_verification.semantic_hash
            != callback_snapshot.semantic_hash
        || !memory.read(kInputStateObject + 0x498,
            &live_owner, sizeof(live_owner))
        || live_owner != kReplacementSubelements
        || !memory.read(kInputStateObject + 0x4A4,
            &live_capacity, sizeof(live_capacity))
        || live_capacity != replacement_capacity
        || !memory.read(kInputStateObject + 0x481,
            &observed_padding_481, sizeof(observed_padding_481))
        || observed_padding_481 != changed_padding_481
        || !memory.read(kInputStateObject + 0x489,
            &observed_padding_489, sizeof(observed_padding_489))
        || observed_padding_489 != changed_padding_489
        || !memory.read(kEventMasks, observed_event_masks.data(),
            sizeof(observed_event_masks))
        || observed_event_masks != event_masks)
        return 116;
    const int32_t insufficient_capacity = 1;
    if (!memory.write(kInputStateObject + 0x4A4,
            &insufficient_capacity, sizeof(insufficient_capacity))
        || Horse::RestoreRollbackNativeInputCallbackSnapshotWith(
            callback_snapshot, read_memory,
            [&](uintptr_t address, const void* source, size_t size) noexcept {
                return memory.write(address, source, size);
            }))
        return 117;
    const int32_t over_snapshot_limit = 257;
    if (!memory.write(kInputStateObject + 0x4A0,
            &over_snapshot_limit, sizeof(over_snapshot_limit))
        || !memory.write(kInputStateObject + 0x4A4,
            &over_snapshot_limit, sizeof(over_snapshot_limit))
        || Horse::CaptureRollbackNativeInputCallbackSnapshotWith(
            kInputStateObject, read_memory, callback_verification))
        return 118;
    memory = valid_callback_memory;
    constexpr uintptr_t kBattleManager = 1;
    constexpr uintptr_t kPreviousInput = 0x1800;
    constexpr uintptr_t kInputPairs = 0x1900;
    constexpr uintptr_t kPriorInputPairs = 0x1A00;
    constexpr uint32_t kTrackStateGlobal = 0x11223344;
    constexpr uint32_t kTrackStateCurrent = 0x55667788;
    constexpr int32_t kSelectedCommandPlayer = 1;
    constexpr uint32_t kTrackCompletionState = 0x99AABBCC;
    if (!memory.write(kBattleManager + 0x1498,
            &kPreviousInput, sizeof(kPreviousInput))
        || !memory.write(kBattleManager + 0x14A8,
            &kInputPairs, sizeof(kInputPairs))
        || !memory.write(kBattleManager + 0x14B8,
            &kPriorInputPairs, sizeof(kPriorInputPairs))
        || !memory.write(kBattleManager + 0x14E0,
            &kTrackStateGlobal, sizeof(kTrackStateGlobal))
        || !memory.write(kBattleManager + 0x14E4,
            &kTrackStateCurrent, sizeof(kTrackStateCurrent))
        || !memory.write(kBattleManager + 0x14E8,
            &kSelectedCommandPlayer, sizeof(kSelectedCommandPlayer))
        || !memory.write(kBattleManager + 0x14EC,
            &kTrackCompletionState, sizeof(kTrackCompletionState)))
        return 23;
    const std::array<uint64_t, 2> packed_inputs {
        UINT64_C(0x40) | (UINT64_C(0x40) << 32),
        UINT64_C(0x400),
    };
    Horse::RollbackNativeInputPairPublishReport publish_report {};
    if (Horse::PublishRollbackNativeInputPairs(
            kBattleManager, kBattleManager + 0x1210,
            kBattleManager + 0x14A8, packed_inputs,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return memory.read(address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* value, size_t size) noexcept {
                return memory.read(address, value, size);
            },
            [&](uintptr_t address, const void* value,
                size_t size) noexcept {
                return memory.write(address, value, size);
            }, publish_report)
            != Horse::RollbackNativeInputPairPublishResult::Ok)
        return 25;
    std::array<uint64_t, 2> observed_pairs {};
    std::array<uint32_t, 2> observed_previous {};
    if (!memory.read(kInputPairs, observed_pairs.data(),
            sizeof(observed_pairs))
        || !memory.read(kPreviousInput, observed_previous.data(),
            sizeof(observed_previous))
        || observed_pairs != packed_inputs
        || observed_previous[0] != 0x40
        || observed_previous[1] != 0x400
        || Horse::PublishRollbackNativeInputPairs(
            kBattleManager, kBattleManager + 0x1211,
            kBattleManager + 0x14A8, packed_inputs,
            [&](uintptr_t address, uintptr_t& value) noexcept {
                return memory.read(address, &value, sizeof(value));
            },
            [&](uintptr_t address, void* value, size_t size) noexcept {
                return memory.read(address, value, size);
            },
            [&](uintptr_t address, const void* value,
                size_t size) noexcept {
                return memory.write(address, value, size);
            }, publish_report)
            != Horse::RollbackNativeInputPairPublishResult::InvalidBoundary)
        return 26;

    // If the second publication write fails, both native arrays are restored.
    const auto original_pairs = observed_pairs;
    const auto original_previous = observed_previous;
    uint32_t publish_writes = 0;
    const auto recovered_publish = Horse::PublishRollbackNativeInputPairs(
        kBattleManager, kBattleManager + 0x1210,
        kBattleManager + 0x14A8,
        std::array<uint64_t, 2> {0x11, 0x22},
        [&](uintptr_t address, uintptr_t& value) noexcept {
            return memory.read(address, &value, sizeof(value));
        },
        [&](uintptr_t address, void* value, size_t size) noexcept {
            return memory.read(address, value, size);
        },
        [&](uintptr_t address, const void* value, size_t size) noexcept {
            ++publish_writes;
            if (publish_writes == 2) return false;
            return memory.write(address, value, size);
        }, publish_report);
    if (recovered_publish
            != Horse::RollbackNativeInputPairPublishResult::
                PreviousWriteFailedRecovered
        || publish_writes != 4 || !publish_report.pair_written
        || publish_report.previous_written
        || !publish_report.pair_recovered
        || !publish_report.previous_recovered
        || !memory.read(kInputPairs, observed_pairs.data(),
            sizeof(observed_pairs))
        || !memory.read(kPreviousInput, observed_previous.data(),
            sizeof(observed_previous))
        || observed_pairs != original_pairs
        || observed_previous != original_previous)
        return 28;
    publish_writes = 0;
    const auto failed_recovery = Horse::PublishRollbackNativeInputPairs(
        kBattleManager, kBattleManager + 0x1210,
        kBattleManager + 0x14A8,
        std::array<uint64_t, 2> {0x33, 0x44},
        [&](uintptr_t address, uintptr_t& value) noexcept {
            return memory.read(address, &value, sizeof(value));
        },
        [&](uintptr_t address, void* value, size_t size) noexcept {
            return memory.read(address, value, size);
        },
        [&](uintptr_t address, const void* value, size_t size) noexcept {
            ++publish_writes;
            if (publish_writes == 2 || publish_writes == 3) return false;
            return memory.write(address, value, size);
        }, publish_report);
    if (failed_recovery
            != Horse::RollbackNativeInputPairPublishResult::RecoveryFailed
        || publish_writes != 4 || publish_report.pair_recovered
        || !publish_report.previous_recovered)
        return 29;
    Horse::RollbackNativeSimulationState baseline {};
    if (!Horse::CaptureRollbackNativeSimulationStateWith(
            kBattleManager,
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return memory.read(address, out, size);
            }, baseline))
        return 6;
    if (baseline.track_state_global != kTrackStateGlobal
        || baseline.track_state_current != kTrackStateCurrent
        || baseline.selected_command_player != kSelectedCommandPlayer
        || baseline.track_completion_state != kTrackCompletionState)
        return 66;
    const uint64_t baseline_canonical =
        Horse::HashRollbackNativeSimulationStateCanonical(baseline);
    Horse::RollbackNativeSimulationState peer_cursor = baseline;
    ++peer_cursor.frame_advance_counter;
    peer_cursor.hash = Horse::HashRollbackNativeSimulationState(peer_cursor);
    if (peer_cursor.hash == baseline.hash
        || Horse::HashRollbackNativeSimulationStateCanonical(peer_cursor)
            != baseline_canonical)
        return 178;
    const auto changes_peer_canonical = [&](auto&& mutate) noexcept {
        Horse::RollbackNativeSimulationState changed = baseline;
        mutate(changed);
        changed.hash = Horse::HashRollbackNativeSimulationState(changed);
        return changed.hash != baseline.hash
            && Horse::HashRollbackNativeSimulationStateCanonical(changed)
                != baseline_canonical;
    };
    if (!changes_peer_canonical([](auto& s) { ++s.round_state_loop_again; })
        || !changes_peer_canonical([](auto& s) { ++s.move_state; })
        || !changes_peer_canonical([](auto& s) { ++s.pending_dispatch; })
        || !changes_peer_canonical([](auto& s) { ++s.skip_replay_catch_up; })
        || !changes_peer_canonical([](auto& s) { ++s.move_timer_masked; })
        || !changes_peer_canonical([](auto& s) { ++s.move_timer_unmasked; })
        || !changes_peer_canonical([](auto& s) { ++s.input_pair[0]; })
        || !changes_peer_canonical([](auto& s) { ++s.prior_input_pair[0]; })
        || !changes_peer_canonical([](auto& s) { ++s.previous_input[0]; })
        || !changes_peer_canonical([](auto& s) { ++s.command_input[0]; })
        || !changes_peer_canonical([](auto& s) { ++s.track_state_global; })
        || !changes_peer_canonical([](auto& s) { ++s.track_state_current; })
        || !changes_peer_canonical([](auto& s) {
            ++s.selected_command_player;
        })
        || !changes_peer_canonical([](auto& s) {
            ++s.track_completion_state;
        })
        || !changes_peer_canonical([](auto& s) {
            ++s.unpause_grace_period;
        })
        || !changes_peer_canonical([](auto& s) { ++s.battle_active_state; })
        || !changes_peer_canonical([](auto& s) { ++s.battle_world_mode; })
        || !changes_peer_canonical([](auto& s) {
            ++s.previous_battle_world_mode;
        }))
        return 179;
    memory.bytes.fill(0xA5);
    if (!memory.write(kBattleManager + 0x1498,
            &kPreviousInput, sizeof(kPreviousInput))
        || !memory.write(kBattleManager + 0x14A8,
            &kInputPairs, sizeof(kInputPairs))
        || !memory.write(kBattleManager + 0x14B8,
            &kPriorInputPairs, sizeof(kPriorInputPairs)))
        return 24;
    if (!Horse::RestoreRollbackNativeSimulationStateWith(
            kBattleManager, baseline,
            [&](uintptr_t address, const void* in, size_t size) noexcept {
                return memory.write(address, in, size);
            }))
        return 7;
    Horse::RollbackNativeSimulationState restored {};
    if (!Horse::CaptureRollbackNativeSimulationStateWith(
            kBattleManager,
            [&](uintptr_t address, void* out, size_t size) noexcept {
                return memory.read(address, out, size);
            }, restored)
        || restored.hash != baseline.hash)
        return 8;
    if (restored.track_state_global != kTrackStateGlobal
        || restored.track_state_current != kTrackStateCurrent
        || restored.selected_command_player != kSelectedCommandPlayer
        || restored.track_completion_state != kTrackCompletionState)
        return 67;
    MockOneInputDelta predicted {};
    predicted.loop_again = true;
    const MockOneInputDelta start = predicted;
    predicted.run_one_input_delta(3);
    const MockOneInputDelta predicted_end = predicted;
    MockOneInputDelta corrected = start;
    corrected.run_one_input_delta(3);
    if (predicted_end.fighter_state != corrected.fighter_state
        || predicted_end.callback_state != corrected.callback_state
        || predicted_end.round_state != corrected.round_state
        || predicted_end.active_state != corrected.active_state
        || predicted_end.input_delta_count != 1
        || predicted_end.input_pair_injections != 1
        || predicted_end.native_substeps != 2
        || predicted_end.order_count != 8
        || std::memcmp(predicted_end.order.data(), corrected.order.data(),
            predicted_end.order_count * sizeof(predicted_end.order[0])) != 0)
        return 9;
    constexpr MockOneInputDelta::Step expected[] {
        MockOneInputDelta::PerFrame,
        MockOneInputDelta::Callback,
        MockOneInputDelta::RoundState,
        MockOneInputDelta::ActiveState,
        MockOneInputDelta::PerFrame,
        MockOneInputDelta::Callback,
        MockOneInputDelta::RoundState,
        MockOneInputDelta::ActiveState,
    };
    if (std::memcmp(predicted_end.order.data(), expected,
            sizeof(expected)) != 0)
        return 10;

    using NewRoundAction =
        Horse::RollbackNativeNewRoundFinalizeAction;
    const auto new_round_action = [](bool active, bool inter_round,
            bool starting, uint64_t generation, bool restart_pending,
            bool deferred, bool readable, bool exact_state,
            bool current_mode, bool queued_clear,
            uint32_t frame, uint32_t timer) noexcept {
        return Horse::ClassifyRollbackNativeNewRoundFinalizeAction(
            active, inter_round, starting, generation, restart_pending,
            deferred, readable, exact_state, current_mode, queued_clear,
            frame, timer);
    };
    if (new_round_action(false, true, false, 0, false, false,
            true, true, true, true, 120, 120)
            != NewRoundAction::PassThrough
        || new_round_action(true, true, false, 1, false, false,
            true, true, true, true, 119, 120)
            != NewRoundAction::PassThrough
        || new_round_action(true, true, false, 1, false, false,
            true, true, true, true, 120, 120)
            != NewRoundAction::Reject
        || new_round_action(true, true, false, 1, false, false,
            true, true, true, true, 0, 0) != NewRoundAction::Reject
        || new_round_action(true, true, false, 1, false, true,
            true, true, true, true, 120, 120) != NewRoundAction::Reject
        || new_round_action(true, true, false, 0, false, false,
            true, true, true, true, 120, 120) != NewRoundAction::Reject
        || new_round_action(true, true, false, 1, true, false,
            true, true, true, true, 120, 120) != NewRoundAction::Reject
        || new_round_action(true, false, true, 1, false, false,
            true, true, true, true, 120, 120)
            != NewRoundAction::PassThrough
        || new_round_action(true, false, true, 1, false, false,
            true, true, true, true, 119, 120)
            != NewRoundAction::Reject
        || new_round_action(true, false, true, 1, true, false,
            true, true, true, true, 120, 120) != NewRoundAction::Reject
        || new_round_action(true, false, true, 1, false, true,
            true, true, true, true, 120, 120) != NewRoundAction::Reject
        || new_round_action(true, false, true, 2, true, false,
            true, true, true, true, 120, 120)
                != NewRoundAction::Reject
        || new_round_action(true, false, true, 2, false, false,
            true, true, true, true, 120, 120) != NewRoundAction::Reject
        || new_round_action(true, false, true, 2, true, true,
            true, true, true, true, 119, 120) != NewRoundAction::Reject
        || new_round_action(true, false, true, 2, true, false,
            true, true, true, true, 119, 120) != NewRoundAction::Release
        || new_round_action(true, true, false, 1, false, false,
            false, true, true, true, 120, 120) != NewRoundAction::Reject
        || new_round_action(true, true, false, 1, false, false,
            true, false, true, true, 120, 120) != NewRoundAction::Reject
        || new_round_action(true, true, false, 1, false, false,
            true, true, false, true, 120, 120) != NewRoundAction::Reject
        || new_round_action(true, true, false, 1, false, false,
            true, true, true, false, 120, 120) != NewRoundAction::Reject)
        return 119;

    bool transition_deferred = false;
    bool active_battle_queued = false;
    uint32_t native_finalize_calls = 0;
    uint32_t new_round_ticks_during_frame_zero = 0;
    uint32_t authored_inputs_consumed = 0;
    uint32_t new_round_mode_frame = 119;
    constexpr uint32_t new_round_phase_timer = 120;
    const bool outer_simulation_frozen = true;
    if (!outer_simulation_frozen
        || new_round_action(true, false, true, 2, true,
            transition_deferred, true, true, true, true,
            new_round_mode_frame, new_round_phase_timer)
            != NewRoundAction::Release)
        return 121;
    const uint32_t frozen_mode_frame = new_round_mode_frame;
    new_round_mode_frame = new_round_phase_timer;
    ++native_finalize_calls;
    active_battle_queued = true;
    new_round_mode_frame = frozen_mode_frame;
    if (active_battle_queued)
    {
        active_battle_queued = false;
        ++authored_inputs_consumed;
    }
    else
    {
        ++new_round_ticks_during_frame_zero;
    }
    if (transition_deferred || active_battle_queued
        || new_round_mode_frame != 119
        || native_finalize_calls != 1
        || new_round_ticks_during_frame_zero != 0
        || authored_inputs_consumed != 1)
        return 122;

    if (!Horse::RollbackNativeNewRoundDeferredShutdownAllowed(
            false, false)
        || Horse::RollbackNativeNewRoundDeferredShutdownAllowed(
            true, false)
        || !Horse::RollbackNativeNewRoundDeferredShutdownAllowed(
            true, true))
        return 123;

    Horse::RollbackNativeInitialNewRoundBaselineEvidence initial_evidence {};
    initial_evidence.round_generation = 1;
    initial_evidence.live_current_mode = 0x1000;
    initial_evidence.live_transition = 7;
    initial_evidence.saved_current_mode = 0x1000;
    initial_evidence.saved_transition = 7;
    initial_evidence.frame_zero_held = true;
    initial_evidence.rearm_evidence_clear = true;
    if (!Horse::ValidateRollbackNativeInitialNewRoundBaselineEvidence(
            initial_evidence, 0x1000))
        return 200;
    auto bad_initial_evidence = initial_evidence;
    bad_initial_evidence.round_generation = 2;
    if (Horse::ValidateRollbackNativeInitialNewRoundBaselineEvidence(
            bad_initial_evidence, 0x1000))
        return 201;
    bad_initial_evidence = initial_evidence;
    bad_initial_evidence.live_queued_mode = 0x2000;
    if (Horse::ValidateRollbackNativeInitialNewRoundBaselineEvidence(
            bad_initial_evidence, 0x1000))
        return 202;
    bad_initial_evidence = initial_evidence;
    bad_initial_evidence.saved_transition = 8;
    if (Horse::ValidateRollbackNativeInitialNewRoundBaselineEvidence(
            bad_initial_evidence, 0x1000))
        return 203;
    bad_initial_evidence = initial_evidence;
    bad_initial_evidence.round_restart_pending = true;
    if (Horse::ValidateRollbackNativeInitialNewRoundBaselineEvidence(
            bad_initial_evidence, 0x1000))
        return 204;
    bad_initial_evidence = initial_evidence;
    bad_initial_evidence.transition_deferred = true;
    if (Horse::ValidateRollbackNativeInitialNewRoundBaselineEvidence(
            bad_initial_evidence, 0x1000))
        return 205;
    bad_initial_evidence = initial_evidence;
    bad_initial_evidence.rearm_evidence_clear = false;
    if (Horse::ValidateRollbackNativeInitialNewRoundBaselineEvidence(
            bad_initial_evidence, 0x1000))
        return 206;
    bad_initial_evidence = initial_evidence;
    bad_initial_evidence.baseline_already_verified = true;
    if (Horse::ValidateRollbackNativeInitialNewRoundBaselineEvidence(
            bad_initial_evidence, 0x1000))
        return 207;

    struct MockInitialBaselineCommitBoundary
    {
        uint32_t arena_mutations {0};
        uint32_t slot_commits {0};
        uint32_t handle_publications {0};

        bool save(
            const Horse::RollbackNativeInitialNewRoundBaselineEvidence& e)
        {
            if (!Horse::ValidateRollbackNativeInitialNewRoundBaselineEvidence(
                    e, 0x1000))
                return false;
            ++arena_mutations;
            ++slot_commits;
            ++handle_publications;
            return true;
        }
    };
    const auto initial_rejected_without_commit = [](const auto& evidence) {
        MockInitialBaselineCommitBoundary boundary {};
        return !boundary.save(evidence)
            && boundary.arena_mutations == 0
            && boundary.slot_commits == 0
            && boundary.handle_publications == 0;
    };
    auto stale_initial_generation = initial_evidence;
    stale_initial_generation.round_generation = 2;
    if (!initial_rejected_without_commit(stale_initial_generation))
        return 220;
    auto queued_initial_baseline = initial_evidence;
    queued_initial_baseline.live_queued_mode = 0x2000;
    queued_initial_baseline.saved_queued_mode = 0x2000;
    if (!initial_rejected_without_commit(queued_initial_baseline))
        return 221;
    auto duplicate_initial_baseline = initial_evidence;
    duplicate_initial_baseline.baseline_already_verified = true;
    if (!initial_rejected_without_commit(duplicate_initial_baseline))
        return 222;
    auto released_initial_lifetime = initial_evidence;
    released_initial_lifetime.frame_zero_held = false;
    if (!initial_rejected_without_commit(released_initial_lifetime))
        return 223;
    MockInitialBaselineCommitBoundary accepted_initial_boundary {};
    if (!accepted_initial_boundary.save(initial_evidence)
        || accepted_initial_boundary.arena_mutations != 1
        || accepted_initial_boundary.slot_commits != 1
        || accepted_initial_boundary.handle_publications != 1)
        return 224;

    struct MockInitialFrameZero
    {
        uintptr_t current_mode {0x1000};
        uintptr_t queued_mode {0};
        uint32_t transition {7};
        uint64_t match_identity {0xA5A5};
        uint32_t finalize_calls {0};
        uint32_t authored_inputs {0};
        uint32_t neutral_inputs {0};
        uint32_t rearm_deferrals {0};
        uint32_t rearm_releases {0};
        uint32_t rearm_baselines {0};

        bool advance()
        {
            if (Horse::ClassifyRollbackNativeNewRoundFinalizeAction(
                    true, false, true, 1, false, false,
                    true, true, current_mode == 0x1000,
                    queued_mode == 0, 120, 120)
                != Horse::RollbackNativeNewRoundFinalizeAction::PassThrough)
                return false;
            ++finalize_calls;
            queued_mode = 0x2000;
            if (queued_mode != 0x2000) return false;
            queued_mode = 0;
            ++authored_inputs;
            return true;
        }

        void load_baseline()
        {
            current_mode = 0x1000;
            queued_mode = 0;
            transition = 7;
        }
    };
    MockInitialFrameZero initial_frame_zero {};
    const uint64_t initial_match_identity =
        initial_frame_zero.match_identity;
    if (!initial_frame_zero.advance()
        || initial_frame_zero.finalize_calls != 1
        || initial_frame_zero.authored_inputs != 1
        || initial_frame_zero.neutral_inputs != 0
        || initial_frame_zero.rearm_deferrals != 0
        || initial_frame_zero.rearm_releases != 0
        || initial_frame_zero.rearm_baselines != 0)
        return 208;
    initial_frame_zero.load_baseline();
    if (!initial_frame_zero.advance()
        || initial_frame_zero.finalize_calls != 2
        || initial_frame_zero.authored_inputs != 2
        || initial_frame_zero.neutral_inputs != 0
        || initial_frame_zero.rearm_deferrals != 0
        || initial_frame_zero.rearm_releases != 0
        || initial_frame_zero.rearm_baselines != 0
        || initial_frame_zero.match_identity != initial_match_identity)
        return 209;

    Horse::RollbackNativeNewRoundBaselineEvidence baseline_evidence {};
    baseline_evidence.round_generation = 2;
    baseline_evidence.release_generation = 2;
    baseline_evidence.save_generation = 2;
    baseline_evidence.release_serial = 1;
    baseline_evidence.save_serial = 2;
    baseline_evidence.live_current_mode = 0x1000;
    baseline_evidence.live_queued_mode = 0x2000;
    baseline_evidence.live_transition = 7;
    baseline_evidence.saved_current_mode = 0x1000;
    baseline_evidence.saved_queued_mode = 0x2000;
    baseline_evidence.saved_transition = 7;
    baseline_evidence.frame_zero_held = true;
    if (!Horse::ValidateRollbackNativeNewRoundBaselineEvidence(
            baseline_evidence, 0x1000, 0x2000))
        return 124;
    auto bad_baseline_evidence = baseline_evidence;
    bad_baseline_evidence.save_serial = 0;
    if (Horse::ValidateRollbackNativeNewRoundBaselineEvidence(
            bad_baseline_evidence, 0x1000, 0x2000))
        return 125;
    bad_baseline_evidence = baseline_evidence;
    bad_baseline_evidence.saved_transition = 8;
    if (Horse::ValidateRollbackNativeNewRoundBaselineEvidence(
            bad_baseline_evidence, 0x1000, 0x2000))
        return 126;
    bad_baseline_evidence = baseline_evidence;
    bad_baseline_evidence.transition_deferred = true;
    if (Horse::ValidateRollbackNativeNewRoundBaselineEvidence(
            bad_baseline_evidence, 0x1000, 0x2000))
        return 127;

    struct MockBaselineCommitBoundary
    {
        uint32_t arena_mutations {0};
        uint32_t slot_commits {0};
        uint32_t handle_publications {0};
        uint32_t checksum_publications {0};
        uint32_t gekko_acceptances {0};
        bool frame_zero_held {true};

        bool save(const Horse::RollbackNativeNewRoundBaselineEvidence& e)
        {
            const auto decision =
                Horse::DecideRollbackNativeNewRoundBaselineSave(
                    e, 0x1000, 0x2000);
            if (decision.action != Horse::
                    RollbackNativeNewRoundBaselineSaveAction::Commit)
            {
                frame_zero_held = decision.keep_frame_zero_held;
                return false;
            }
            ++arena_mutations;
            ++slot_commits;
            ++handle_publications;
            ++checksum_publications;
            ++gekko_acceptances;
            frame_zero_held = decision.keep_frame_zero_held;
            return true;
        }
    };
    const auto rejected_without_commit = [](const auto& evidence) {
        MockBaselineCommitBoundary boundary {};
        return !boundary.save(evidence)
            && boundary.arena_mutations == 0
            && boundary.slot_commits == 0
            && boundary.handle_publications == 0
            && boundary.checksum_publications == 0
            && boundary.gekko_acceptances == 0
            && boundary.frame_zero_held;
    };

    auto missing_release_evidence = baseline_evidence;
    missing_release_evidence.release_generation = 0;
    missing_release_evidence.release_serial = 0;
    if (!rejected_without_commit(missing_release_evidence)) return 128;
    auto stale_release_evidence = baseline_evidence;
    stale_release_evidence.release_generation = 1;
    stale_release_evidence.release_serial = 1;
    if (!rejected_without_commit(stale_release_evidence)) return 129;
    auto missing_save_evidence = baseline_evidence;
    missing_save_evidence.save_generation = 0;
    missing_save_evidence.save_serial = 0;
    if (!rejected_without_commit(missing_save_evidence)) return 130;
    auto out_of_order_save_evidence = baseline_evidence;
    out_of_order_save_evidence.save_serial = 1;
    if (!rejected_without_commit(out_of_order_save_evidence)) return 134;
    auto duplicate_save_evidence = baseline_evidence;
    duplicate_save_evidence.baseline_already_verified = true;
    if (!rejected_without_commit(duplicate_save_evidence)) return 131;
    auto released_lifetime_evidence = baseline_evidence;
    released_lifetime_evidence.frame_zero_held = false;
    if (!rejected_without_commit(released_lifetime_evidence)) return 132;
    MockBaselineCommitBoundary accepted_boundary {};
    if (!accepted_boundary.save(baseline_evidence)
        || accepted_boundary.arena_mutations != 1
        || accepted_boundary.slot_commits != 1
        || accepted_boundary.handle_publications != 1
        || accepted_boundary.checksum_publications != 1
        || accepted_boundary.gekko_acceptances != 1
        || !accepted_boundary.frame_zero_held)
        return 133;

    // Save(-1) -> Advance(0) -> Save(0), followed by both rollback paths.
    // The final NewRound countdown Tick and its deferred PostTick release have
    // already queued ActiveBattle. Advance(0) begins at that transition and
    // must not execute another NewRound Tick.
    struct MockNewRoundRollbackState
    {
        bool current_new_round {true};
        bool queued_active_battle {false};
        uint32_t native_finalize_calls {0};
        uint32_t new_round_ticks {0};
        uint32_t inputs_consumed {0};
        uint32_t input_hash {0};

        bool operator==(const MockNewRoundRollbackState& other) const noexcept
        {
            return current_new_round == other.current_new_round
                && queued_active_battle == other.queued_active_battle
                && native_finalize_calls == other.native_finalize_calls
                && new_round_ticks == other.new_round_ticks
                && inputs_consumed == other.inputs_consumed
                && input_hash == other.input_hash;
        }
    };
    const auto advance = [](MockNewRoundRollbackState& state,
            uint32_t input) {
        if (state.queued_active_battle)
        {
            state.queued_active_battle = false;
            state.current_new_round = false;
        }
        else if (state.current_new_round)
        {
            ++state.new_round_ticks;
            ++state.native_finalize_calls;
            state.queued_active_battle = true;
        }
        ++state.inputs_consumed;
        state.input_hash = state.input_hash * 16777619u ^ input;
    };
    MockNewRoundRollbackState live_new_round {};
    live_new_round.queued_active_battle = true;
    const MockNewRoundRollbackState saved_baseline = live_new_round;
    advance(live_new_round, 0x40);
    const MockNewRoundRollbackState saved_frame_zero = live_new_round;
    if (saved_frame_zero.native_finalize_calls != 0
        || saved_frame_zero.new_round_ticks != 0
        || saved_frame_zero.inputs_consumed != 1)
        return 135;

    MockNewRoundRollbackState loaded_baseline = saved_baseline;
    advance(loaded_baseline, 0x40);
    if (!(loaded_baseline == saved_frame_zero)
        || loaded_baseline.native_finalize_calls != 0)
        return 136;

    MockNewRoundRollbackState expected_frame_one = saved_frame_zero;
    advance(expected_frame_one, 0x400);
    MockNewRoundRollbackState loaded_frame_zero = saved_frame_zero;
    advance(loaded_frame_zero, 0x400);
    if (!(loaded_frame_zero == expected_frame_one)
        || loaded_frame_zero.native_finalize_calls != 0
        || loaded_frame_zero.new_round_ticks != 0
        || loaded_frame_zero.inputs_consumed != 2)
        return 137;

    if (Horse::ShouldRunRollbackOwnedWindCallback(
            true, false, false, false, false, 0))
        return 138;
    if (!Horse::ShouldRunRollbackOwnedWindCallback(
            true, false, false, true, false, 0))
        return 146;
    if (!Horse::ShouldRunRollbackOwnedWindCallback(
            false, false, true, true, true, 0))
        return 139;
    if (Horse::ShouldRunRollbackOwnedWindCallback(
            false, false, true, false, true, 0))
        return 140;
    if (Horse::ShouldRunRollbackOwnedWindCallback(
            false, false, true, true, false, 0))
        return 141;
    if (Horse::ShouldRunRollbackOwnedWindCallback(
            false, false, true, true, true, 1))
        return 142;
    if (Horse::ShouldRunRollbackOwnedWindCallback(
            false, false, false, true, true, 0))
        return 143;
    if (!Horse::ShouldRunRollbackOwnedWindCallback(
            false, true, false, false, false, 0))
        return 144;
    // RoundResult/transition ticks are not deterministic across processes.
    if (Horse::ShouldRunRollbackOwnedWindCallback(
            false, false, false, false, false, 0))
        return 145;

    std::array<uint8_t, 0x1700> battle_manager_bytes {};
    const uintptr_t battle_manager = reinterpret_cast<uintptr_t>(
        battle_manager_bytes.data());
    const Horse::RollbackNativeOnlineFrameStallCounters stall_initial {
        9u, 3u,
    };
    std::memcpy(battle_manager_bytes.data() + 0x1638,
        &stall_initial, sizeof(stall_initial));
    Horse::RollbackNativeOnlineFrameStallCounters stall_captured {};
    if (!Horse::CaptureRollbackNativeOnlineFrameStallCounters(
            battle_manager, stall_captured))
        return 147;
    const Horse::RollbackNativeOnlineFrameStallCounters stall_speculative {
        14u, 4u,
    };
    std::memcpy(battle_manager_bytes.data() + 0x1638,
        &stall_speculative, sizeof(stall_speculative));
    if (!Horse::RestoreRollbackNativeOnlineFrameStallCounters(
            battle_manager, stall_captured))
        return 148;
    Horse::RollbackNativeOnlineFrameStallCounters stall_verified {};
    std::memcpy(&stall_verified,
        battle_manager_bytes.data() + 0x1638,
        sizeof(stall_verified));
    if (stall_verified.consecutive_zero_delta_calls
            != stall_initial.consecutive_zero_delta_calls
        || stall_verified.completed_long_stalls
            != stall_initial.completed_long_stalls)
        return 149;

    std::puts("rollback native simulation iteration self-test passed");
    return 0;
}
