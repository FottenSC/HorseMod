// Fixed BattleManager state mutated by one complete native SimulationLoop
// iteration. The replay/InputLog cursor fields are intentionally excluded:
// owned rollback calls restore them before Save. Frozen peer-wait control
// progress is restored separately from its immutable boundary-clock gate
// before frame zero or any logical pass-through release.
#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    struct RollbackNativeSimulationState
    {
        uint8_t round_state_loop_again {0};
        uint8_t move_state {0};
        uint8_t pending_dispatch {0};
        uint8_t skip_replay_catch_up {0};
        int32_t move_timer_masked {0};
        int32_t move_timer_unmasked {0};
        int32_t frame_advance_counter {0};
        uintptr_t input_pair_owner {0};
        std::array<uint64_t, 2> input_pair {};
        uintptr_t prior_input_pair_owner {0};
        std::array<uint64_t, 2> prior_input_pair {};
        uintptr_t previous_input_owner {0};
        std::array<uint32_t, 2> previous_input {};
        std::array<uint32_t, 6> command_input {};
        uint32_t track_state_global {0};
        uint32_t track_state_current {0};
        int32_t selected_command_player {0};
        uint32_t track_completion_state {0};
        int32_t unpause_grace_period {0};
        uint32_t battle_active_state {0};
        uint32_t battle_world_mode {0};
        uint32_t previous_battle_world_mode {0};
        uint64_t hash {0};
        bool valid {false};

        void clear() noexcept { *this = {}; }
    };

    // UpdateLuxBattleOnlineFrameStallCounters @ 0x1403FDEC0 mutates these
    // BattleManager-local diagnostics on each complete SimulationLoop call.
    // Genuine online stalls are client-local, so they are deliberately not
    // snapshotted or peer-hashed. Extra Horse-owned resimulation iterations
    // preserve them around the native call instead.
    struct RollbackNativeOnlineFrameStallCounters
    {
        uint32_t consecutive_zero_delta_calls {0}; // BattleManager +0x1638
        uint32_t completed_long_stalls {0};        // BattleManager +0x163C
    };

    static inline bool CaptureRollbackNativeOnlineFrameStallCounters(
        uintptr_t battle_manager,
        RollbackNativeOnlineFrameStallCounters& state) noexcept
    {
        return battle_manager
            && SafeReadBytes(reinterpret_cast<const void*>(
                    battle_manager + 0x1638),
                &state, sizeof(state));
    }

    static inline bool RestoreRollbackNativeOnlineFrameStallCounters(
        uintptr_t battle_manager,
        const RollbackNativeOnlineFrameStallCounters& state) noexcept
    {
        return battle_manager
            && SafeWriteBytes(reinterpret_cast<void*>(
                    battle_manager + 0x1638),
                &state, sizeof(state));
    }

    static inline uint64_t HashRollbackNativeSimulationState(
        const RollbackNativeSimulationState& state) noexcept
    {
        if (!state.valid) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.round_state_loop_again);
        hash.add_scalar(state.move_state);
        hash.add_scalar(state.pending_dispatch);
        hash.add_scalar(state.skip_replay_catch_up);
        hash.add_scalar(state.move_timer_masked);
        hash.add_scalar(state.move_timer_unmasked);
        hash.add_scalar(state.frame_advance_counter);
        hash.add_bytes(state.input_pair.data(), sizeof(state.input_pair));
        hash.add_bytes(state.prior_input_pair.data(),
            sizeof(state.prior_input_pair));
        hash.add_bytes(state.previous_input.data(),
            sizeof(state.previous_input));
        hash.add_bytes(state.command_input.data(),
            sizeof(state.command_input));
        hash.add_scalar(state.track_state_global);
        hash.add_scalar(state.track_state_current);
        hash.add_scalar(state.selected_command_player);
        hash.add_scalar(state.track_completion_state);
        hash.add_scalar(state.unpause_grace_period);
        hash.add_scalar(state.battle_active_state);
        hash.add_scalar(state.battle_world_mode);
        hash.add_scalar(state.previous_battle_world_mode);
        return hash.value ? hash.value : 1;
    }

    // BM+0x1490 follows the process-local SimulationLoop/catch-up clock. It
    // must restore with a local snapshot, but peers can reach the same frozen
    // gameplay boundary with different native cursor values. Keep every
    // input and gameplay/control field peer-canonical; exclude only that
    // scheduling cursor.
    static inline uint64_t HashRollbackNativeSimulationStateCanonical(
        const RollbackNativeSimulationState& state) noexcept
    {
        if (!state.valid) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.round_state_loop_again);
        hash.add_scalar(state.move_state);
        hash.add_scalar(state.pending_dispatch);
        hash.add_scalar(state.skip_replay_catch_up);
        hash.add_scalar(state.move_timer_masked);
        hash.add_scalar(state.move_timer_unmasked);
        hash.add_bytes(state.input_pair.data(), sizeof(state.input_pair));
        hash.add_bytes(state.prior_input_pair.data(),
            sizeof(state.prior_input_pair));
        hash.add_bytes(state.previous_input.data(),
            sizeof(state.previous_input));
        hash.add_bytes(state.command_input.data(),
            sizeof(state.command_input));
        hash.add_scalar(state.track_state_global);
        hash.add_scalar(state.track_state_current);
        hash.add_scalar(state.selected_command_player);
        hash.add_scalar(state.track_completion_state);
        hash.add_scalar(state.unpause_grace_period);
        hash.add_scalar(state.battle_active_state);
        hash.add_scalar(state.battle_world_mode);
        hash.add_scalar(state.previous_battle_world_mode);
        return hash.value ? hash.value : 1;
    }

    template <typename ReadFn>
    static inline bool CaptureRollbackNativeSimulationStateWith(
        uintptr_t battle_manager,
        ReadFn&& read,
        RollbackNativeSimulationState& out) noexcept
    {
        out.clear();
        if (battle_manager == 0) return false;
        const bool ok =
            read(battle_manager + 0x1462,
                &out.round_state_loop_again,
                sizeof(out.round_state_loop_again))
            && read(battle_manager + 0x1463,
                &out.move_state, sizeof(out.move_state))
            && read(battle_manager + 0x1464,
                &out.pending_dispatch, sizeof(out.pending_dispatch))
            && read(battle_manager + 0x1465,
                &out.skip_replay_catch_up,
                sizeof(out.skip_replay_catch_up))
            && read(battle_manager + 0x1468,
                &out.move_timer_masked, sizeof(out.move_timer_masked))
            && read(battle_manager + 0x146C,
                &out.move_timer_unmasked, sizeof(out.move_timer_unmasked))
            && read(battle_manager + 0x1490,
                &out.frame_advance_counter,
                sizeof(out.frame_advance_counter))
            && read(battle_manager + 0x14A8,
                &out.input_pair_owner, sizeof(out.input_pair_owner))
            && out.input_pair_owner != 0
            && read(out.input_pair_owner,
                out.input_pair.data(), sizeof(out.input_pair))
            // LuxBattleChara_UpdatePlayerInputData_FromRoundCache copies the
            // old +0x14A8 pair into this second two-entry array before it
            // publishes the new pair. It is native input history, not
            // collection topology.
            && read(battle_manager + 0x14B8,
                &out.prior_input_pair_owner,
                sizeof(out.prior_input_pair_owner))
            && out.prior_input_pair_owner != 0
            && read(out.prior_input_pair_owner,
                out.prior_input_pair.data(),
                sizeof(out.prior_input_pair))
            && read(battle_manager + 0x1498,
                &out.previous_input_owner,
                sizeof(out.previous_input_owner))
            && out.previous_input_owner != 0
            && read(out.previous_input_owner,
                out.previous_input.data(), sizeof(out.previous_input))
            && read(battle_manager + 0x14C8,
                out.command_input.data(), sizeof(out.command_input))
            // LuxBattleChara_OnTrackComplete_UpdateStateAndNotifyGameFlow
            // writes this four-scalar block before notifying the external UI
            // game-flow manager. It is part of the complete SimulationLoop
            // result, not presentation-only state.
            && read(battle_manager + 0x14E0,
                &out.track_state_global,
                sizeof(out.track_state_global))
            && read(battle_manager + 0x14E4,
                &out.track_state_current,
                sizeof(out.track_state_current))
            && read(battle_manager + 0x14E8,
                &out.selected_command_player,
                sizeof(out.selected_command_player))
            && read(battle_manager + 0x14EC,
                &out.track_completion_state,
                sizeof(out.track_completion_state))
            && read(battle_manager + 0x14F0,
                &out.unpause_grace_period,
                sizeof(out.unpause_grace_period))
            && read(battle_manager + 0x1590,
                &out.battle_active_state,
                sizeof(out.battle_active_state))
            && read(battle_manager + 0x1594,
                &out.battle_world_mode, sizeof(out.battle_world_mode))
            && read(battle_manager + 0x1598,
                &out.previous_battle_world_mode,
                sizeof(out.previous_battle_world_mode));
        if (!ok) return false;
        out.valid = true;
        out.hash = HashRollbackNativeSimulationState(out);
        return out.hash != 0;
    }

    static inline bool CaptureRollbackNativeSimulationState(
        uintptr_t battle_manager,
        RollbackNativeSimulationState& out) noexcept
    {
        return CaptureRollbackNativeSimulationStateWith(
            battle_manager,
            [](uintptr_t address, void* destination, size_t size) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, size);
            },
            out);
    }

    template <typename WriteFn>
    static inline bool RestoreRollbackNativeSimulationStateWith(
        uintptr_t battle_manager,
        const RollbackNativeSimulationState& state,
        WriteFn&& write) noexcept
    {
        if (battle_manager == 0 || !state.valid
            || state.hash != HashRollbackNativeSimulationState(state))
            return false;
        if (!state.input_pair_owner || !state.prior_input_pair_owner
            || !state.previous_input_owner)
            return false;
        // The caller verifies the live owner before entering this value-only
        // restore overload. Production uses the checked overload below.
        return write(battle_manager + 0x1462,
                   &state.round_state_loop_again,
                   sizeof(state.round_state_loop_again))
            && write(battle_manager + 0x1463,
                &state.move_state, sizeof(state.move_state))
            && write(battle_manager + 0x1464,
                &state.pending_dispatch, sizeof(state.pending_dispatch))
            && write(battle_manager + 0x1465,
                &state.skip_replay_catch_up,
                sizeof(state.skip_replay_catch_up))
            && write(battle_manager + 0x1468,
                &state.move_timer_masked, sizeof(state.move_timer_masked))
            && write(battle_manager + 0x146C,
                &state.move_timer_unmasked, sizeof(state.move_timer_unmasked))
            && write(battle_manager + 0x1490,
                &state.frame_advance_counter,
                sizeof(state.frame_advance_counter))
            && write(state.input_pair_owner,
                state.input_pair.data(), sizeof(state.input_pair))
            && write(state.prior_input_pair_owner,
                state.prior_input_pair.data(),
                sizeof(state.prior_input_pair))
            && write(state.previous_input_owner,
                state.previous_input.data(), sizeof(state.previous_input))
            && write(battle_manager + 0x14C8,
                state.command_input.data(), sizeof(state.command_input))
            && write(battle_manager + 0x14E0,
                &state.track_state_global,
                sizeof(state.track_state_global))
            && write(battle_manager + 0x14E4,
                &state.track_state_current,
                sizeof(state.track_state_current))
            && write(battle_manager + 0x14E8,
                &state.selected_command_player,
                sizeof(state.selected_command_player))
            && write(battle_manager + 0x14EC,
                &state.track_completion_state,
                sizeof(state.track_completion_state))
            && write(battle_manager + 0x14F0,
                &state.unpause_grace_period,
                sizeof(state.unpause_grace_period))
            && write(battle_manager + 0x1590,
                &state.battle_active_state,
                sizeof(state.battle_active_state))
            && write(battle_manager + 0x1594,
                &state.battle_world_mode, sizeof(state.battle_world_mode))
            && write(battle_manager + 0x1598,
                &state.previous_battle_world_mode,
                sizeof(state.previous_battle_world_mode));
    }

    static inline bool RestoreRollbackNativeSimulationState(
        uintptr_t battle_manager,
        const RollbackNativeSimulationState& state) noexcept
    {
        void* live_input_pair_owner = nullptr;
        void* live_prior_input_pair_owner = nullptr;
        void* live_previous_input_owner = nullptr;
        if (!SafeReadPtr(reinterpret_cast<const void*>(
                battle_manager + 0x14A8), &live_input_pair_owner)
            || reinterpret_cast<uintptr_t>(live_input_pair_owner)
                != state.input_pair_owner
            || !SafeReadPtr(reinterpret_cast<const void*>(
                battle_manager + 0x14B8),
                &live_prior_input_pair_owner)
            || reinterpret_cast<uintptr_t>(live_prior_input_pair_owner)
                != state.prior_input_pair_owner
            || !SafeReadPtr(reinterpret_cast<const void*>(
                battle_manager + 0x1498), &live_previous_input_owner)
            || reinterpret_cast<uintptr_t>(live_previous_input_owner)
                != state.previous_input_owner)
            return false;
        return RestoreRollbackNativeSimulationStateWith(
            battle_manager, state,
            [](uintptr_t address, const void* source, size_t size) noexcept {
                return SafeWriteBytes(reinterpret_cast<void*>(address),
                    source, size);
            });
    }
}
