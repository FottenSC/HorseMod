// ============================================================================
// Horse::RollbackStepHarness
//
// Active-round rollback validation for the lab. This module owns the smallest
// native resimulation proof: capture a stable state, step K deterministic
// frames, restore, replay the same frames, and compare the final state. The
// production runtime drives SC6's complete native SimulationLoop iteration;
// replay-only harness callers may still provide a narrower tick callback.
// ============================================================================

#pragma once

#include "CodeCave.hpp"
#include "NativeBinding.hpp"
#include "ReplayDebugTrace.hpp"
#include "RngTraceHook.hpp"
#include "RollbackHgCpuSnapshot.hpp"
#include "RollbackBattleCameraSnapshot.hpp"
#include "RollbackInputHistory.hpp"
#include "RollbackLifecycle.hpp"
#include "RollbackLuxMoveCommandSnapshot.hpp"
#include "RollbackLuxMoveSystemSnapshot.hpp"
#include "RollbackLuxMoveVmSlotParamSnapshot.hpp"
#include "RollbackLuxSubVmSnapshot.hpp"
#include "RollbackMotionDecodeScratch.hpp"
#include "RollbackMotionPoseResidue.hpp"
#include "RollbackNativeInputCallbackSnapshot.hpp"
#include "RollbackNativeRoundStateSnapshot.hpp"
#include "RollbackNativeSimulationState.hpp"
#include "RollbackObserverCaptureTransaction.hpp"
#include "RollbackPaletteVariantSnapshot.hpp"
#include "RollbackPreallocatedCaptureGate.hpp"
#include "RollbackPresentationSemanticSnapshot.hpp"
#include "RollbackSnapshot.hpp"
#include "RollbackStageSnapshot.hpp"
#include "RollbackStageWindSnapshot.hpp"
#include "RollbackStepStateStorage.hpp"
#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"
#include "WindRngGate.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horse
{
    static constexpr uintptr_t kRollbackRVA_LuxBattlePerFrameTick = 0x2DBC60;
    static constexpr uintptr_t kRollbackRVA_FrameCounter = 0x470D0C4;
    static constexpr uintptr_t kRollbackRVA_PerFrameCameraArgs = 0x470D100;
    static constexpr uintptr_t kRollbackRVA_LatestEngineInput = 0x4855700;
    static constexpr size_t kRollbackCameraArgsBytes = 24;
    static constexpr uint32_t kRollbackMaxKFrameWindow = 60;

    using RollbackPerFrameTickFn = void(__fastcall*)(uintptr_t*);

    struct RollbackHgCpuFrameCompare
    {
        bool hash_match {false};
        bool policy_match {false};
        bool topology_match {false};
        bool motion_bank_match {false};
        bool motion_tail_match {false};
        bool secondary_event_stack_match {false};
        size_t bytes_compared {0};
        size_t mismatch_count {0};
        size_t ignored_mismatch_count {0};
        size_t unignored_mismatch_count {0};
        size_t first_mismatch_offset {0};
        uint8_t first_mismatch_a {0};
        uint8_t first_mismatch_b {0};
        size_t first_unignored_mismatch_offset {0};
        uint8_t first_unignored_mismatch_a {0};
        uint8_t first_unignored_mismatch_b {0};
        const char* first_ignored_reason {"none"};
        uint64_t hash_a {0};
        uint64_t hash_b {0};
        uint64_t topology_hash_a {0};
        uint64_t topology_hash_b {0};
        uint64_t motion_bank_hash_a {0};
        uint64_t motion_bank_hash_b {0};
        uint64_t motion_tail_hash_a {0};
        uint64_t motion_tail_hash_b {0};
        uint64_t secondary_event_stack_hash_a {0};
        uint64_t secondary_event_stack_hash_b {0};
        uint64_t timer_node_hash_a {0};
        uint64_t timer_node_hash_b {0};
        bool timer_node_match {false};
        uint32_t timer_indexed_nonzero_count_a {0};
        uint32_t timer_indexed_captured_count_a {0};
        uint32_t timer_indexed_object_captured_count_a {0};
        uintptr_t timer_indexed_slot0_root_a {0};
        uintptr_t timer_indexed_slot0_vtable_a {0};
        uintptr_t timer_indexed_slot0_writer_a {0};
        bool timer_indexed_slot0_captured_a {false};
        size_t p1_record_bytes_a {0};
        size_t p1_record_bytes_b {0};
        size_t p2_record_bytes_a {0};
        size_t p2_record_bytes_b {0};
        size_t p2_base_a {0};
        size_t p2_base_b {0};
        size_t first_unignored_dynamic_local {0};
        int first_unignored_dynamic_player {0};
        size_t motion_bank_mismatch_count {0};
        const char* motion_bank_first_region {"none"};
        size_t motion_bank_first_player {0};
        size_t motion_bank_first_bank {0};
        int motion_bank_first_buffer {-1};
        size_t motion_bank_first_offset {0};
        uint8_t motion_bank_first_a {0};
        uint8_t motion_bank_first_b {0};
        int motion_bank_first_slot_a {-1};
        int motion_bank_first_slot_b {-1};
        const char* khit_first_region {"none"};
        size_t khit_first_player {0};
        int khit_first_list {-1};
        int khit_first_node_index {-1};
        size_t khit_first_stream_start {0};
        size_t khit_first_stream_rel {0};
        size_t khit_first_stream_size {0};
        uintptr_t khit_first_node {0};
        uint8_t khit_first_tag {0xff};
        uintptr_t khit_first_node_source_offset {0};
        uint8_t khit_first_node_source_a {0};
        uint8_t khit_first_node_source_b {0};
        bool khit_first_node_source_match {false};
        size_t khit_first_node_stream_end {0};
        size_t khit_first_relocation_start {0};
    };

    struct RollbackStepState;

    static inline uint64_t RollbackStepStateCapacityHash(
        const RollbackStepState& state) noexcept;

    struct RollbackStepState
    {
        RollbackHgCpuSnapshotFrame hgcpu {};
        RollbackPaletteVariantSnapshot palette_variants {};
        RollbackSnapshotFrame explicit_snapshot {};
        RollbackBreakableStageSnapshot breakable_stage {};
        RollbackStageWindSnapshot stage_wind {};
        RollbackBattleCameraSnapshot battle_camera {};
        RollbackPresentationSemanticSnapshot presentation_semantic {};
        RollbackNativeRoundStateSnapshot native_round_state {};
        RollbackNativeSimulationState native_simulation_state {};
        RollbackNativeInputCallbackSnapshot native_input_callback {};
        RollbackLuxMoveCommandSnapshot lux_move_command {};
        RollbackLuxMoveSystemPumpSnapshot lux_move_pump {};
        RollbackLuxMoveVmSlotParamSnapshot lux_move_slot_params {};
        RollbackLuxSubVmSnapshot lux_subvm {};
        RollbackGameplayCrtState gameplay_crt {};
        RollbackMotionDecodeScratchSnapshot motion_decode_scratch {};
        RollbackMotionPoseResidueSnapshot motion_pose_residue {};
        uint32_t frame_counter {0};
        uint64_t latest_input[2] {};
        uint8_t camera_args[kRollbackCameraArgsBytes] {};
        uint64_t canonical_hash {0};
        uint64_t combined_hash {0};

        void preserve_capacities_from(const RollbackStepState& source)
        {
            const auto preserve = [](auto& destination, const auto& exemplar) {
                destination.reserve(exemplar.capacity());
            };
            preserve(hgcpu.bytes, source.hgcpu.bytes);
            preserve(palette_variants.payload,
                source.palette_variants.payload);
            preserve(palette_variants.writer_nodes,
                source.palette_variants.writer_nodes);
            for (size_t i = 0; i < std::size(hgcpu.khit_topology); ++i)
                preserve(hgcpu.khit_topology[i].nodes,
                    source.hgcpu.khit_topology[i].nodes);
            preserve(hgcpu.motion_banks.control_bytes,
                source.hgcpu.motion_banks.control_bytes);
            preserve(hgcpu.motion_banks.bytes,
                source.hgcpu.motion_banks.bytes);
            preserve(hgcpu.motion_tail.bytes,
                source.hgcpu.motion_tail.bytes);

            auto& skeleton = hgcpu.skeleton_runtime;
            const auto& source_skeleton = source.hgcpu.skeleton_runtime;
            preserve(skeleton.inline_bytes, source_skeleton.inline_bytes);
            preserve(skeleton.aux_nodes, source_skeleton.aux_nodes);
            for (size_t i = 0; i < skeleton.aux_nodes.size(); ++i)
                preserve(skeleton.aux_nodes[i].bytes,
                    source_skeleton.aux_nodes[i].bytes);
            preserve(skeleton.chains, source_skeleton.chains);
            preserve(skeleton.spring_nodes, source_skeleton.spring_nodes);
            for (size_t i = 0; i < skeleton.spring_nodes.size(); ++i)
                preserve(skeleton.spring_nodes[i].bytes,
                    source_skeleton.spring_nodes[i].bytes);

            auto& timer = hgcpu.timer_node;
            const auto& source_timer = source.hgcpu.timer_node;
            preserve(timer.root_bytes, source_timer.root_bytes);
            preserve(timer.backing_bytes, source_timer.backing_bytes);
            preserve(timer.nodes, source_timer.nodes);
            preserve(explicit_snapshot.bytes,
                source.explicit_snapshot.bytes);
            preserve(explicit_snapshot.ranges,
                source.explicit_snapshot.ranges);
            preserve(breakable_stage.records,
                source.breakable_stage.records);
        }

        // Terminal checkpoints are copied while rollback is active. Ordinary
        // aggregate/vector assignment is only safe when every destination
        // buffer already has enough storage and every nested vector element
        // already exists. Validate that shape first, then prove assignment did
        // not alter any capacity. This keeps terminal retention allocation-free
        // without requiring its capacity layout to equal a rolling source
        // object's incidental layout.
        bool copy_preallocated_from(const RollbackStepState& source) noexcept
        {
            const auto fits = [](const auto& destination,
                                 const auto& input) noexcept {
                return destination.capacity() >= input.size();
            };
            if (!fits(hgcpu.bytes, source.hgcpu.bytes)
                || !fits(
                    palette_variants.payload,
                    source.palette_variants.payload)
                || !fits(
                    palette_variants.writer_nodes,
                    source.palette_variants.writer_nodes)
                || !fits(
                    hgcpu.motion_banks.control_bytes,
                    source.hgcpu.motion_banks.control_bytes)
                || !fits(
                    hgcpu.motion_banks.bytes,
                    source.hgcpu.motion_banks.bytes)
                || !fits(
                    hgcpu.motion_tail.bytes,
                    source.hgcpu.motion_tail.bytes)
                || !fits(
                    hgcpu.skeleton_runtime.inline_bytes,
                    source.hgcpu.skeleton_runtime.inline_bytes)
                || !fits(
                    hgcpu.skeleton_runtime.aux_nodes,
                    source.hgcpu.skeleton_runtime.aux_nodes)
                || !fits(
                    hgcpu.skeleton_runtime.chains,
                    source.hgcpu.skeleton_runtime.chains)
                || !fits(
                    hgcpu.skeleton_runtime.spring_nodes,
                    source.hgcpu.skeleton_runtime.spring_nodes)
                || !fits(
                    hgcpu.timer_node.root_bytes,
                    source.hgcpu.timer_node.root_bytes)
                || !fits(
                    hgcpu.timer_node.backing_bytes,
                    source.hgcpu.timer_node.backing_bytes)
                || !fits(
                    hgcpu.timer_node.nodes,
                    source.hgcpu.timer_node.nodes)
                || !fits(
                    explicit_snapshot.bytes,
                    source.explicit_snapshot.bytes)
                || !fits(
                    explicit_snapshot.ranges,
                    source.explicit_snapshot.ranges)
                || !fits(
                    breakable_stage.records,
                    source.breakable_stage.records))
            {
                return false;
            }
            for (size_t i = 0; i < std::size(hgcpu.khit_topology); ++i)
            {
                if (!fits(
                        hgcpu.khit_topology[i].nodes,
                        source.hgcpu.khit_topology[i].nodes))
                {
                    return false;
                }
            }
            const auto nested_fits = [&fits](const auto& destination,
                                             const auto& input) noexcept {
                if (destination.size() < input.size())
                    return false;
                for (size_t i = 0; i < input.size(); ++i)
                {
                    if (!fits(destination[i].bytes, input[i].bytes))
                        return false;
                }
                return true;
            };
            if (!nested_fits(
                    hgcpu.skeleton_runtime.aux_nodes,
                    source.hgcpu.skeleton_runtime.aux_nodes)
                || !nested_fits(
                    hgcpu.skeleton_runtime.spring_nodes,
                    source.hgcpu.skeleton_runtime.spring_nodes))
            {
                return false;
            }

            const uint64_t capacity_before =
                RollbackStepStateCapacityHash(*this);
            try
            {
                *this = source;
            }
            catch (...)
            {
                return false;
            }
            return RollbackStepStateCapacityHash(*this) == capacity_before;
        }

        void clear() noexcept
        {
            hgcpu.clear();
            palette_variants.clear();
            explicit_snapshot.clear();
            breakable_stage.clear();
            stage_wind = {};
            battle_camera.clear();
            presentation_semantic.clear();
            native_round_state.clear();
            native_simulation_state.clear();
            native_input_callback.clear();
            lux_move_command.clear();
            lux_move_pump.clear();
            lux_move_slot_params.clear();
            lux_subvm.clear();
            gameplay_crt = {};
            motion_decode_scratch = {};
            motion_pose_residue = {};
            frame_counter = 0;
            latest_input[0] = 0;
            latest_input[1] = 0;
            std::memset(camera_args, 0, sizeof(camera_args));
            canonical_hash = 0;
            combined_hash = 0;
        }


        void recycle_for_capture() noexcept
        {
            hgcpu.recycle_for_capture();
            palette_variants.recycle_for_capture();
            explicit_snapshot.recycle_for_capture();
            breakable_stage.recycle_for_capture();
            stage_wind = {};
            battle_camera.clear();
            presentation_semantic.clear();
            native_round_state.clear();
            native_simulation_state.clear();
            native_input_callback.clear();
            lux_move_command.clear();
            lux_move_pump.clear();
            lux_move_slot_params.clear();
            lux_subvm.clear();
            gameplay_crt = {};
            motion_decode_scratch = {};
            motion_pose_residue = {};
            frame_counter = 0;
            latest_input[0] = 0;
            latest_input[1] = 0;
            std::memset(camera_args, 0, sizeof(camera_args));
            canonical_hash = 0;
            combined_hash = 0;
        }
    };

    struct RollbackStepStateReport
    {
        bool ok {false};
        bool frame_counter_ok {false};
        bool latest_input_ok {false};
        bool camera_args_ok {false};
        bool explicit_ok {false};
        bool hgcpu_ok {false};
        bool palette_variants_ok {false};
        bool breakable_stage_ok {false};
        bool stage_wind_ok {false};
        bool battle_camera_ok {false};
        bool presentation_semantic_ok {false};
        bool native_round_state_ok {false};
        bool native_simulation_state_ok {false};
        bool native_input_callback_ok {false};
        bool lux_move_command_ok {false};
        bool lux_move_pump_ok {false};
        bool lux_move_slot_params_ok {false};
        bool lux_subvm_ok {false};
        bool gameplay_crt_ok {false};
        bool motion_decode_scratch_ok {false};
        bool motion_pose_residue_ok {false};
        bool emergency_captured {false};
        bool emergency_restored {false};
        bool verification_ok {false};
        uint32_t frame_counter {0};
        uint64_t combined_hash {0};
        RollbackSnapshotCopyReport explicit_report {};
        RollbackHgCpuSnapshotReport hgcpu_report {};
        RollbackPaletteVariantSnapshotReport palette_variants_report {};
        RollbackBreakableStageReport breakable_stage_report {};
        RollbackStageWindSnapshotReport stage_wind_report {};
        RollbackHgCpuFrameCompare verification_hgcpu_compare {};
        uint64_t verification_canonical_hash {0};
        uint64_t expected_canonical_hash {0};
        uint64_t verification_explicit_hash {0};
        uint64_t expected_explicit_hash {0};
        uint64_t verification_stage_hash {0};
        uint64_t expected_stage_hash {0};
        uint64_t explicit_capture_nanoseconds {0};
        uint64_t hgcpu_capture_nanoseconds {0};
        uint64_t palette_variant_capture_nanoseconds {0};
        uint64_t explicit_cleanup_nanoseconds {0};
        uint64_t stage_capture_nanoseconds {0};
        uint64_t wind_capture_nanoseconds {0};
        uint64_t capture_finalize_nanoseconds {0};
        uint64_t restore_nanoseconds {0};
        uint64_t verification_nanoseconds {0};
        const char* failure {"not-run"};
    };

    static inline uint64_t RollbackStepStateCapacityHash(
        const RollbackStepState& state) noexcept
    {
        RollbackHash hash {};
        const auto add = [&hash](const auto& values) noexcept {
            hash.add_scalar(values.capacity());
        };
        add(state.hgcpu.bytes);
        add(state.palette_variants.payload);
        add(state.palette_variants.writer_nodes);
        for (const auto& topology : state.hgcpu.khit_topology)
            add(topology.nodes);
        add(state.hgcpu.motion_banks.control_bytes);
        add(state.hgcpu.motion_banks.bytes);
        add(state.hgcpu.motion_tail.bytes);
        const auto& skeleton = state.hgcpu.skeleton_runtime;
        add(skeleton.inline_bytes);
        add(skeleton.aux_nodes);
        for (const auto& node : skeleton.aux_nodes) add(node.bytes);
        add(skeleton.chains);
        add(skeleton.spring_nodes);
        for (const auto& node : skeleton.spring_nodes) add(node.bytes);
        const auto& timer = state.hgcpu.timer_node;
        add(timer.root_bytes);
        add(timer.backing_bytes);
        add(timer.nodes);
        add(state.explicit_snapshot.bytes);
        add(state.explicit_snapshot.ranges);
        add(state.breakable_stage.records);
        return hash.value ? hash.value : 1;
    }

    using RollbackProductionStepStateStorageIdentity =
        RollbackStepStateStorageIdentity<
            kRollbackSkeletonMaxAuxNodes,
            kRollbackSkeletonMaxSpringNodes>;

    using RollbackProductionStepStateCapacityLimits =
        RollbackStepStateCapacityLimits<
            kRollbackSkeletonMaxAuxNodes,
            kRollbackSkeletonMaxSpringNodes>;

    static inline RollbackProductionStepStateStorageIdentity
    CaptureRollbackProductionStepStateStorageIdentity(
        const RollbackStepState& state) noexcept
    {
        return CaptureRollbackStepStateStorageIdentity<
            kRollbackSkeletonMaxAuxNodes,
            kRollbackSkeletonMaxSpringNodes>(state);
    }

    static inline RollbackProductionStepStateCapacityLimits
    CaptureRollbackProductionStepStateCapacityLimits(
        const RollbackStepState& state) noexcept
    {
        return CaptureRollbackStepStateCapacityLimits<
            kRollbackSkeletonMaxAuxNodes,
            kRollbackSkeletonMaxSpringNodes>(state);
    }

    static inline bool TransferRollbackProductionStepStateStorage(
        RollbackStepState& destination,
        RollbackStepState& source) noexcept
    {
        return TransferRollbackStepStateStorage<
            kRollbackSkeletonMaxAuxNodes,
            kRollbackSkeletonMaxSpringNodes>(destination, source);
    }

    static inline std::array<uint64_t, 8>
    RollbackStepStateCapacityComponentHashes(
        const RollbackStepState& state) noexcept
    {
        std::array<uint64_t, 8> result {};
        const auto add_capacity = [](RollbackHash& hash,
                                     const auto& values) noexcept {
            hash.add_scalar(values.capacity());
        };
        RollbackHash hgcpu {};
        add_capacity(hgcpu, state.hgcpu.bytes);
        result[0] = hgcpu.value;

        RollbackHash palette {};
        add_capacity(palette, state.palette_variants.payload);
        add_capacity(palette, state.palette_variants.writer_nodes);
        result[1] = palette.value;

        RollbackHash khit {};
        for (const auto& topology : state.hgcpu.khit_topology)
            add_capacity(khit, topology.nodes);
        result[2] = khit.value;

        RollbackHash motion {};
        add_capacity(motion, state.hgcpu.motion_banks.control_bytes);
        add_capacity(motion, state.hgcpu.motion_banks.bytes);
        add_capacity(motion, state.hgcpu.motion_tail.bytes);
        result[3] = motion.value;

        RollbackHash skeleton_hash {};
        const auto& skeleton = state.hgcpu.skeleton_runtime;
        add_capacity(skeleton_hash, skeleton.inline_bytes);
        add_capacity(skeleton_hash, skeleton.aux_nodes);
        for (const auto& node : skeleton.aux_nodes)
            add_capacity(skeleton_hash, node.bytes);
        add_capacity(skeleton_hash, skeleton.chains);
        add_capacity(skeleton_hash, skeleton.spring_nodes);
        for (const auto& node : skeleton.spring_nodes)
            add_capacity(skeleton_hash, node.bytes);
        result[4] = skeleton_hash.value;

        RollbackHash timer_hash {};
        const auto& timer = state.hgcpu.timer_node;
        add_capacity(timer_hash, timer.root_bytes);
        add_capacity(timer_hash, timer.backing_bytes);
        add_capacity(timer_hash, timer.nodes);
        result[5] = timer_hash.value;

        RollbackHash explicit_hash {};
        add_capacity(explicit_hash, state.explicit_snapshot.bytes);
        add_capacity(explicit_hash, state.explicit_snapshot.ranges);
        result[6] = explicit_hash.value;

        RollbackHash stage_hash {};
        add_capacity(stage_hash, state.breakable_stage.records);
        result[7] = stage_hash.value;
        return result;
    }

    struct RollbackNativeStepReport
    {
        bool ok {false};
        bool resolved {false};
        bool input_write_ok {false};
        bool camera_write_ok {false};
        bool frame_counter_read_ok {false};
        uint32_t frame_before {0};
        uint32_t frame_after {0};
        uint64_t input_p1 {0};
        uint64_t input_p2 {0};
        uintptr_t function_address {0};
        uintptr_t trampoline_address {0};
        NativeCallFault fault {};
        const char* failure {"not-run"};
    };

    struct RollbackResimWindowReport
    {
        bool ok {false};
        bool context_ready {false};
        bool inject_fault {false};
        bool baseline_ok {false};
        bool predicted_ok {false};
        bool corrected_ok {false};
        bool restore_start_after_ok {false};
        bool corrected_matches_baseline {false};
        bool predicted_differs_from_baseline {false};
        bool explicit_match {false};
        bool hgcpu_policy_match {false};
        bool frame_counter_match {false};
        bool frame_counter_delta_ok {false};
        bool all_steps_ok {false};
        uint32_t window {0};
        uint32_t seed {0};
        uint32_t start_frame {0};
        uint32_t baseline_frame {0};
        uint32_t predicted_frame {0};
        uint32_t corrected_frame {0};
        uint32_t fault_frame_index {0};
        uint64_t baseline_hash {0};
        uint64_t predicted_hash {0};
        uint64_t corrected_hash {0};
        uint64_t baseline_explicit_hash {0};
        uint64_t predicted_explicit_hash {0};
        uint64_t corrected_explicit_hash {0};
        uint64_t post_baseline_restore_explicit_hash {0};
        uint64_t post_predicted_restore_explicit_hash {0};
        bool post_baseline_restore_explicit_ok {false};
        bool post_baseline_restore_explicit_match {false};
        bool post_predicted_restore_explicit_ok {false};
        bool post_predicted_restore_explicit_match {false};
        bool start_lfsr_index_ok {false};
        bool baseline_lfsr_index_ok {false};
        bool predicted_lfsr_index_ok {false};
        bool corrected_lfsr_index_ok {false};
        bool post_baseline_restore_lfsr_index_ok {false};
        bool post_predicted_restore_lfsr_index_ok {false};
        bool wind_rng_gate_resolved {false};
        bool wind_rng_gate_enabled {false};
        uint32_t start_lfsr_index {0xFFFFFFFFu};
        uint32_t baseline_lfsr_index {0xFFFFFFFFu};
        uint32_t predicted_lfsr_index {0xFFFFFFFFu};
        uint32_t corrected_lfsr_index {0xFFFFFFFFu};
        uint32_t post_baseline_restore_lfsr_index {0xFFFFFFFFu};
        uint32_t post_predicted_restore_lfsr_index {0xFFFFFFFFu};
        uint32_t explicit_first_mismatch_manifest_index {0xFFFFFFFFu};
        size_t explicit_first_mismatch_range_offset {0};
        uint8_t explicit_first_mismatch_a {0};
        uint8_t explicit_first_mismatch_b {0};
        uint64_t explicit_first_mismatch_hash_a {0};
        uint64_t explicit_first_mismatch_hash_b {0};
        const char* explicit_first_mismatch_name {"none"};
        const char* explicit_mismatch_reason {"none"};
        uint64_t baseline_input_p2 {0};
        uint64_t predicted_input_p2 {0};
        uint32_t steps_attempted {0};
        uint32_t steps_ok {0};
        RollbackStepStateReport start_capture {};
        RollbackStepStateReport baseline_capture {};
        RollbackStepStateReport predicted_capture {};
        RollbackStepStateReport corrected_capture {};
        RollbackStepStateReport post_baseline_restore {};
        RollbackStepStateReport post_predicted_restore {};
        RollbackStepStateReport final_restore {};
        RollbackHgCpuFrameCompare corrected_compare {};
        RollbackHgCpuFrameCompare predicted_compare {};
        const char* failure {"not-run"};
    };

    static inline uint64_t HashRollbackStepState(
        const RollbackStepState& state) noexcept
    {
        RollbackHash h {};
        h.add_scalar(state.hgcpu.hash);
        h.add_scalar(state.palette_variants.integrity_hash);
        h.add_scalar(state.explicit_snapshot.hash);
        h.add_scalar(state.breakable_stage.integrity_hash);
        h.add_scalar(state.stage_wind.integrity_hash);
        h.add_scalar(state.battle_camera.integrity_hash);
        h.add_scalar(state.presentation_semantic.integrity_hash);
        h.add_scalar(state.native_round_state.hash);
        h.add_scalar(state.native_simulation_state.hash);
        h.add_scalar(state.native_input_callback.semantic_hash);
        h.add_scalar(state.lux_move_command.integrity_hash);
        h.add_scalar(state.lux_move_pump.integrity_hash);
        h.add_scalar(state.lux_move_slot_params.integrity_hash);
        h.add_scalar(state.lux_subvm.integrity_hash);
        h.add_scalar(state.gameplay_crt.internal_state);
        h.add_scalar(state.gameplay_crt.full_round_seed);
        h.add_scalar(state.gameplay_crt.gameplay_draw_ordinal);
        h.add_scalar(state.gameplay_crt.warmup_draws);
        h.add_scalar(state.gameplay_crt.owner_thread_id);
        h.add_scalar(static_cast<uint8_t>(state.gameplay_crt.phase));
        h.add_scalar(state.gameplay_crt.native_seed_observed);
        h.add_scalar(HashRollbackMotionDecodeScratchSnapshot(
            state.motion_decode_scratch));
        h.add_scalar(HashRollbackMotionPoseResidueRollbackState(
            state.motion_pose_residue));
        h.add_scalar(state.frame_counter);
        h.add_bytes(state.latest_input, sizeof(state.latest_input));
        h.add_bytes(state.camera_args, sizeof(state.camera_args));
        return h.value;
    }

    static inline uint64_t HashRollbackStepStateCanonical(
        const RollbackStepState& state) noexcept
    {
        RollbackHash h {};
        h.add_scalar(state.hgcpu.canonical_hash);
        h.add_scalar(state.palette_variants.canonical_hash);
        h.add_scalar(state.explicit_snapshot.canonical_hash);
        h.add_scalar(state.breakable_stage.canonical_hash);
        h.add_scalar(state.stage_wind.canonical_hash);
        h.add_scalar(state.battle_camera.canonical_hash);
        h.add_scalar(state.presentation_semantic.canonical_hash);
        h.add_scalar(state.native_round_state.hash);
        h.add_scalar(HashRollbackNativeSimulationStateCanonical(
            state.native_simulation_state));
        h.add_scalar(state.lux_move_command.semantic_hash);
        h.add_scalar(state.lux_move_pump.semantic_hash);
        h.add_scalar(state.lux_move_slot_params.canonical_hash);
        h.add_scalar(state.lux_subvm.semantic_hash);
        h.add_scalar(state.gameplay_crt.internal_state);
        h.add_scalar(state.gameplay_crt.full_round_seed);
        h.add_scalar(state.gameplay_crt.gameplay_draw_ordinal);
        h.add_scalar(state.gameplay_crt.warmup_draws);
        h.add_scalar(static_cast<uint8_t>(state.gameplay_crt.phase));
        h.add_scalar(state.gameplay_crt.native_seed_observed);
        // The native decoder overwrites exactly the clip-authored signed-word
        // prefix. Unused capacity in each 0x250-byte caller buffer retains
        // process stack history and is not consumed by valid selector data.
        // Preserve the full pair for exact local rewind verification, but do
        // not compare that raw capacity tail across peers. Canonical pose,
        // matrix, motion-tail, and KHit outputs expose observable divergence.
        // sampledPoseScratch is an expired native stack object outside the
        // SolveBonePose call. Preserve its retained lanes for exact local
        // rewind, but do not compare the raw bytes across processes: final
        // corrected sampling can be identical while unconsumed lanes differ.
        // Collision-visible matrices and their HgCpu consumers remain in the
        // canonical digest and expose any observable pose divergence.
        // This callback follows the process-local native player object. Its
        // state must restore on each peer, but it is not a peer-equal digest.
        // Native SC6 frame counters are process-local telemetry. Rollback's
        // logical frame is the shared cross-peer timeline.
        // Inputs are gameplay state. Camera arguments are presentation input
        // and intentionally excluded from the cross-peer gameplay digest.
        h.add_bytes(state.latest_input, sizeof(state.latest_input));
        return h.value;
    }

    static inline bool ValidateRollbackHgCpuFrameIntegrity(
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        const size_t effective = RollbackHgCpuEffectiveBytes(frame);
        if (effective == 0 || effective > frame.bytes.size()
            || frame.byte_hash == 0 || frame.hash == 0
            || frame.canonical_hash == 0)
            return false;
        const uint64_t byte_hash = RollbackFastIntegrityHashBytes(
            frame.bytes.data(), effective);
        const uint64_t khit_hash = RollbackHashKHitTopology(frame);
        const uint64_t motion_bank_hash =
            RollbackHashMotionBankHistory(frame.motion_banks);
        const uint64_t motion_tail_hash =
            RollbackHashMotionTailHistory(frame.motion_tail);
        const uint64_t secondary_event_stack_hash =
            RollbackHashSecondaryEventStackHistory(
                frame.secondary_event_stack);
        const uint64_t chara_animation_hash =
            RollbackHashCharaAnimationIntegrity(frame.chara_animation);
        const uint64_t skeleton_runtime_hash =
            frame.skeleton_runtime.ok
                ? RollbackHashSkeletonRuntimeHistory(frame.skeleton_runtime)
                : 0;
        const uint64_t timer_node_hash =
            RollbackHashTimerNodeHistory(frame.timer_node);
        return byte_hash == frame.byte_hash
            && khit_hash == frame.khit_topology_hash
            && motion_bank_hash == frame.motion_bank_hash
            && motion_tail_hash == frame.motion_tail_hash
            && secondary_event_stack_hash
                == frame.secondary_event_stack_hash
            && RollbackCharaAnimationHistoryValid(frame.chara_animation)
            && chara_animation_hash == frame.chara_animation_hash
            && skeleton_runtime_hash == frame.skeleton_runtime_hash
            && timer_node_hash == frame.timer_node_hash
            && RollbackHashHgCpuIntegrityComponents(frame) == frame.hash
            && RollbackHashHgCpuCanonical(frame) == frame.canonical_hash;
    }

    struct RollbackStepStateIntegrityReport
    {
        bool ok {false};
        const char* failure {"not-run"};
    };

    static inline RollbackStepStateIntegrityReport
    InspectRollbackStepStateIntegrity(
        const RollbackStepState& state) noexcept
    {
        RollbackStepStateIntegrityReport report {};
        if (state.explicit_snapshot.integrity_hash == 0)
        {
            report.failure = "explicit-integrity-hash-missing";
            return report;
        }
        if (state.explicit_snapshot.hash
            != state.explicit_snapshot.integrity_hash)
        {
            report.failure = "explicit-stored-hash-mismatch";
            return report;
        }
        if (HashRollbackSnapshotFrame(state.explicit_snapshot)
            != state.explicit_snapshot.integrity_hash)
        {
            report.failure = "explicit-integrity-recompute-mismatch";
            return report;
        }
        if (HashRollbackSnapshotCanonical(state.explicit_snapshot)
            != state.explicit_snapshot.canonical_hash)
        {
            report.failure = "explicit-canonical-recompute-mismatch";
            return report;
        }
        if (!ValidateRollbackHgCpuFrameIntegrity(state.hgcpu))
        {
            report.failure = "hgcpu-integrity-mismatch";
            return report;
        }
        if (!ValidateRollbackPaletteVariantSnapshot(
                state.palette_variants))
        {
            report.failure = "palette-variant-integrity-mismatch";
            return report;
        }
        if (state.explicit_snapshot.epoch.battle_manager != 0
            && !state.native_round_state.valid)
        {
            report.failure = "native-round-state-missing";
            return report;
        }
        if (state.explicit_snapshot.epoch.battle_manager != 0
            && state.native_round_state.hash
                != HashRollbackNativeRoundStateSnapshot(
                    state.native_round_state))
        {
            report.failure = "native-round-state-integrity-mismatch";
            return report;
        }
        if (state.explicit_snapshot.epoch.battle_manager != 0
            && !state.native_simulation_state.valid)
        {
            report.failure = "native-simulation-state-missing";
            return report;
        }
        if (state.explicit_snapshot.epoch.battle_manager != 0
            && state.native_simulation_state.hash
                != HashRollbackNativeSimulationState(
                    state.native_simulation_state))
        {
            report.failure = "native-simulation-state-integrity-mismatch";
            return report;
        }
        if (state.explicit_snapshot.epoch.battle_manager != 0
            && state.native_input_callback.valid
            && !ValidateRollbackNativeInputCallbackSnapshot(
                state.native_input_callback))
        {
            report.failure = "native-input-callback-integrity-mismatch";
            return report;
        }
        if (!ValidateRollbackLuxMoveCommandSnapshot(
                state.lux_move_command))
        {
            report.failure = "lux-move-command-integrity-mismatch";
            return report;
        }
        if (!ValidateRollbackLuxMoveSystemPumpSnapshot(
                state.lux_move_pump))
        {
            report.failure = "lux-move-pump-integrity-mismatch";
            return report;
        }
        if (!ValidateRollbackLuxMoveVmSlotParamSnapshot(
                state.lux_move_slot_params))
        {
            report.failure = "lux-move-slot-param-integrity-mismatch";
            return report;
        }
        if (!ValidateRollbackLuxSubVmSnapshot(state.lux_subvm))
        {
            report.failure = "lux-subvm-integrity-mismatch";
            return report;
        }
        if (state.gameplay_crt.phase !=
                RollbackGameplayCrtPhase::Uninitialized
            && !RollbackGameplayCrtStateIsCanonical(state.gameplay_crt))
        {
            report.failure = "gameplay-crt-integrity-mismatch";
            return report;
        }
        if (!ValidateRollbackMotionDecodeScratchSnapshot(
                state.motion_decode_scratch))
        {
            report.failure = "motion-decode-scratch-integrity-mismatch";
            return report;
        }
        if (!ValidateRollbackMotionPoseResidueSnapshot(
                state.motion_pose_residue))
        {
            report.failure = "motion-pose-residue-integrity-mismatch";
            return report;
        }
        const RollbackBreakableStageHashes stage =
            ComputeRollbackBreakableStageHashes(state.breakable_stage);
        if (stage.integrity_hash != state.breakable_stage.integrity_hash)
        {
            report.failure = "breakable-stage-integrity-mismatch";
            return report;
        }
        if (stage.canonical_hash != state.breakable_stage.canonical_hash)
        {
            report.failure = "breakable-stage-canonical-mismatch";
            return report;
        }
        if (stage.stage_layout_digest
            != state.breakable_stage.stage_layout_digest)
        {
            report.failure = "breakable-stage-layout-mismatch";
            return report;
        }
        if (stage.actor_set_digest
            != state.breakable_stage.actor_set_digest)
        {
            report.failure = "breakable-stage-actor-set-mismatch";
            return report;
        }
        if (HashRollbackStageWindIntegrity(state.stage_wind)
            != state.stage_wind.integrity_hash)
        {
            report.failure = "stage-wind-integrity-mismatch";
            return report;
        }
        if (HashRollbackStageWindCanonical(state.stage_wind)
            != state.stage_wind.canonical_hash)
        {
            report.failure = "stage-wind-canonical-mismatch";
            return report;
        }
        if (!ValidateRollbackBattleCameraSnapshot(state.battle_camera))
        {
            report.failure = "battle-camera-integrity-mismatch";
            return report;
        }
        if (state.presentation_semantic.valid
            && !ValidateRollbackPresentationSemanticSnapshot(
                state.presentation_semantic))
        {
            report.failure =
                "presentation-semantic-integrity-mismatch";
            return report;
        }
        if (HashRollbackStepState(state) != state.combined_hash)
        {
            report.failure = "step-combined-hash-mismatch";
            return report;
        }
        if (HashRollbackStepStateCanonical(state) != state.canonical_hash)
        {
            report.failure = "step-canonical-hash-mismatch";
            return report;
        }
        report.ok = true;
        report.failure = "ok";
        return report;
    }

    static inline bool ValidateRollbackStepStateIntegrity(
        const RollbackStepState& state) noexcept
    {
        return InspectRollbackStepStateIntegrity(state).ok;
    }

    static inline bool ValidateRollbackStepLifecycle(
        uintptr_t image_base,
        const RollbackLifecycleEpoch& expected,
        RollbackLifecycleMode mode =
            RollbackLifecycleMode::StockOnlinePvp,
        bool replay_fork_lab = false) noexcept
    {
        if (replay_fork_lab)
        {
            if (!image_base || !expected.active_battle_common()
                || expected.presence
                    != static_cast<uint8_t>(GamePresence::Replay)
                || expected.pvp_active)
            {
                return false;
            }
            void* p1 = nullptr;
            void* p2 = nullptr;
            uint8_t main_state = 0xFF;
            uint8_t battle_status = 0xFF;
            std::array<uint8_t, 0xC0> round_start {};
            if (!SafeReadPtr(reinterpret_cast<const void*>(
                    rollback_absolute_from_image_base(
                        image_base, 0x14470DE90ull)), &p1)
                || !SafeReadPtr(reinterpret_cast<const void*>(
                    rollback_absolute_from_image_base(
                        image_base, 0x14470DE98ull)), &p2)
                || reinterpret_cast<uintptr_t>(p1) != expected.chara[0]
                || reinterpret_cast<uintptr_t>(p2) != expected.chara[1]
                || !SafeReadUInt8(reinterpret_cast<const void*>(
                    expected.battle_manager + 0x1461), &main_state)
                || !SafeReadUInt8(reinterpret_cast<const void*>(
                    expected.battle_manager + 0x1480), &battle_status)
                || main_state != expected.battle_main_state
                || battle_status != expected.battle_status
                || !SafeReadBytes(reinterpret_cast<const void*>(
                    expected.battle_manager + 0x1360), round_start.data(),
                    round_start.size())
                || RollbackHashRoundStartCanonical(
                    round_start.data(), round_start.size())
                    != expected.round_start_digest)
            {
                return false;
            }
            RollbackBreakableStageSnapshot stage {};
            const RollbackBreakableStageReport stage_report =
                CaptureRollbackBreakableStageSnapshot(
                    expected.stage_actor_manager, stage);
            return stage_report.ok
                && stage.stage_layout_digest
                    == expected.stage_layout_digest
                && stage.actor_set_digest == expected.actor_set_digest;
        }
        RollbackLiveToken live {};
        return CaptureRollbackLiveToken(image_base, expected, live)
            && RollbackLiveTokenCompatibleWithRoundTransition(
                expected, live, mode);
    }

    static inline bool RollbackReadSnapshotU32ByName(
        const RollbackSnapshotManifest& manifest,
        const RollbackSnapshotFrame& snapshot,
        const char* name,
        uint32_t& out) noexcept
    {
        out = 0xFFFFFFFFu;
        if (!name || !name[0])
            return false;

        for (const RollbackSnapshotRange& r : snapshot.ranges)
        {
            if (r.manifest_index >= manifest.entries.size())
                continue;
            const RollbackManifestEntry& entry =
                manifest.entries[r.manifest_index];
            if (!entry.name || std::strcmp(entry.name, name) != 0)
                continue;
            if (r.bytes < sizeof(uint32_t)
                || r.bytes_offset > snapshot.bytes.size()
                || sizeof(uint32_t)
                    > snapshot.bytes.size() - r.bytes_offset)
            {
                return false;
            }
            std::memcpy(
                &out, snapshot.bytes.data() + r.bytes_offset,
                sizeof(out));
            return true;
        }
        return false;
    }

    static inline bool RollbackReadSnapshotLfsrIndex(
        const RollbackSnapshotManifest& manifest,
        const RollbackSnapshotFrame& snapshot,
        uint32_t& out) noexcept
    {
        return RollbackReadSnapshotU32ByName(
            manifest, snapshot, "g_dwLuxBattleLfsrIndex", out);
    }

    class RollbackScopedWindRngGate
    {
    public:
        RollbackScopedWindRngGate() noexcept
        {
            static WindRngGate s_gate;
            m_gate = &s_gate;
            m_resolved = m_gate->resolve();
            m_enabled = m_resolved && m_gate->enable();
        }

        ~RollbackScopedWindRngGate()
        {
            if (m_enabled && m_gate)
                m_gate->disable();
        }

        bool resolved() const noexcept { return m_resolved; }
        bool enabled() const noexcept { return m_enabled; }

    private:
        WindRngGate* m_gate {nullptr};
        bool m_resolved {false};
        bool m_enabled {false};
    };

    static inline void DescribeRollbackExplicitMismatch(
        const RollbackSnapshotManifest& manifest,
        const RollbackSnapshotFrame& a,
        const RollbackSnapshotFrame& b,
        RollbackResimWindowReport& report) noexcept
    {
        report.explicit_mismatch_reason = "none";
        report.explicit_first_mismatch_name = "none";
        report.explicit_first_mismatch_manifest_index = 0xFFFFFFFFu;
        report.explicit_first_mismatch_range_offset = 0;
        report.explicit_first_mismatch_a = 0;
        report.explicit_first_mismatch_b = 0;
        report.explicit_first_mismatch_hash_a = 0;
        report.explicit_first_mismatch_hash_b = 0;

        if (a.ranges.size() != b.ranges.size())
        {
            report.explicit_mismatch_reason = "range-count";
            return;
        }

        for (size_t i = 0; i < a.ranges.size(); ++i)
        {
            const RollbackSnapshotRange& ar = a.ranges[i];
            const RollbackSnapshotRange& br = b.ranges[i];
            if (ar.manifest_index != br.manifest_index
                || ar.bytes != br.bytes)
            {
                report.explicit_mismatch_reason = "range-metadata";
                report.explicit_first_mismatch_manifest_index =
                    ar.manifest_index;
                if (ar.manifest_index < manifest.entries.size())
                {
                    report.explicit_first_mismatch_name =
                        manifest.entries[ar.manifest_index].name;
                }
                return;
            }
            if (ar.hash == br.hash)
                continue;

            report.explicit_mismatch_reason = "range-bytes";
            report.explicit_first_mismatch_manifest_index = ar.manifest_index;
            report.explicit_first_mismatch_hash_a = ar.hash;
            report.explicit_first_mismatch_hash_b = br.hash;
            if (ar.manifest_index < manifest.entries.size())
            {
                report.explicit_first_mismatch_name =
                    manifest.entries[ar.manifest_index].name;
            }

            if (ar.bytes_offset > a.bytes.size()
                || br.bytes_offset > b.bytes.size()
                || static_cast<size_t>(ar.bytes)
                    > a.bytes.size() - ar.bytes_offset
                || static_cast<size_t>(br.bytes)
                    > b.bytes.size() - br.bytes_offset)
            {
                report.explicit_mismatch_reason = "range-out-of-bounds";
                return;
            }

            for (size_t j = 0; j < ar.bytes; ++j)
            {
                const uint8_t av = a.bytes[ar.bytes_offset + j];
                const uint8_t bv = b.bytes[br.bytes_offset + j];
                if (av == bv)
                    continue;
                report.explicit_first_mismatch_range_offset = j;
                report.explicit_first_mismatch_a = av;
                report.explicit_first_mismatch_b = bv;
                return;
            }
            return;
        }
    }

    static inline bool RollbackReadFrameCounter(
        uintptr_t image_base,
        uint32_t& out) noexcept
    {
        out = 0;
        return image_base
            && SafeReadUInt32(
                reinterpret_cast<const void*>(
                    image_base + kRollbackRVA_FrameCounter),
                &out);
    }

    static inline bool RollbackWriteFrameCounter(
        uintptr_t image_base,
        uint32_t value) noexcept
    {
        return image_base
            && SafeWriteUInt32(
                reinterpret_cast<void*>(
                    image_base + kRollbackRVA_FrameCounter),
                value);
    }

    static inline bool RollbackReadLatestInputs(
        uintptr_t image_base,
        uint64_t (&out)[2]) noexcept
    {
        out[0] = 0;
        out[1] = 0;
        return image_base
            && SafeReadBytes(
                reinterpret_cast<const void*>(
                    image_base + kRollbackRVA_LatestEngineInput),
                out, sizeof(out));
    }

    static inline bool RollbackWriteLatestInputs(
        uintptr_t image_base,
        const uint64_t (&input)[2]) noexcept
    {
        return image_base
            && SafeWriteBytes(
                reinterpret_cast<void*>(
                    image_base + kRollbackRVA_LatestEngineInput),
                input, sizeof(input));
    }

    static inline bool RollbackReadCameraArgs(
        uintptr_t image_base,
        uint8_t (&out)[kRollbackCameraArgsBytes]) noexcept
    {
        std::memset(out, 0, kRollbackCameraArgsBytes);
        return image_base
            && SafeReadBytes(
                reinterpret_cast<const void*>(
                    image_base + kRollbackRVA_PerFrameCameraArgs),
                out, kRollbackCameraArgsBytes);
    }

    static inline bool RollbackWriteCameraArgs(
        uintptr_t image_base,
        const uint8_t (&camera_args)[kRollbackCameraArgsBytes]) noexcept
    {
        return image_base
            && SafeWriteBytes(
                reinterpret_cast<void*>(
                    image_base + kRollbackRVA_PerFrameCameraArgs),
                camera_args, kRollbackCameraArgsBytes);
    }

    static inline bool RollbackEpochMatchesLiveCharaPointers(
        uintptr_t image_base,
        const RollbackLifecycleEpoch& epoch) noexcept
    {
        RollbackLifecycleEpoch live {};
        if (!CaptureRollbackLifecycleEpoch(image_base, live))
            return false;
        live.generation = epoch.generation;
        return ValidateRollbackLifecycleEpoch(epoch, live).ok;
    }

    static inline bool RollbackDescribeMotionBankByteOffset(
        size_t absolute_offset,
        size_t& player_out,
        size_t& bank_out,
        size_t& buffer_out,
        size_t& offset_out) noexcept
    {
        const size_t per_player =
            RollbackMotionBankTotalBytes() / kRollbackMotionBankPlayerCount;
        if (per_player == 0)
            return false;

        player_out = absolute_offset / per_player;
        if (player_out >= kRollbackMotionBankPlayerCount)
            return false;

        size_t rel = absolute_offset % per_player;
        for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
        {
            const size_t bank_bytes =
                kRollbackMotionBankSpecs[bank].bytes
                * kRollbackMotionBankBufferCount;
            if (rel >= bank_bytes)
            {
                rel -= bank_bytes;
                continue;
            }

            buffer_out = rel / kRollbackMotionBankSpecs[bank].bytes;
            if (buffer_out >= kRollbackMotionBankBufferCount)
                return false;
            bank_out = bank;
            offset_out = rel % kRollbackMotionBankSpecs[bank].bytes;
            return true;
        }

        return false;
    }

    static inline void CompareRollbackMotionBankHistories(
        const RollbackHgCpuSnapshotFrame& a,
        const RollbackHgCpuSnapshotFrame& b,
        RollbackHgCpuFrameCompare& report) noexcept
    {
        const auto& ah = a.motion_banks;
        const auto& bh = b.motion_banks;
        if (!ah.ok || !bh.ok)
            return;

        auto record_first = [&](const char* region,
                                size_t player,
                                size_t bank,
                                int buffer,
                                size_t offset,
                                uint8_t av,
                                uint8_t bv) noexcept {
            if (report.motion_bank_mismatch_count != 0)
                return;
            report.motion_bank_first_region = region;
            report.motion_bank_first_player = player + 1;
            report.motion_bank_first_bank = bank;
            report.motion_bank_first_buffer = buffer;
            report.motion_bank_first_offset = offset;
            report.motion_bank_first_a = av;
            report.motion_bank_first_b = bv;
            if (player < kRollbackMotionBankPlayerCount
                && bank < kRollbackMotionBankCount)
            {
                report.motion_bank_first_slot_a =
                    ah.provider_slot[player][bank];
                report.motion_bank_first_slot_b =
                    bh.provider_slot[player][bank];
            }
        };

        const size_t control_compared =
            (std::min)(ah.control_bytes.size(), bh.control_bytes.size());
        for (size_t i = 0; i < control_compared; ++i)
        {
            if (ah.control_bytes[i] == bh.control_bytes[i])
                continue;
            const size_t control_index =
                i / kRollbackMotionBankControlBytes;
            const size_t player =
                control_index / kRollbackMotionBankCount;
            const size_t bank =
                control_index % kRollbackMotionBankCount;
            record_first("control",
                         player,
                         bank,
                         -1,
                         i % kRollbackMotionBankControlBytes,
                         ah.control_bytes[i],
                         bh.control_bytes[i]);
            ++report.motion_bank_mismatch_count;
        }
        if (ah.control_bytes.size() != bh.control_bytes.size())
            ++report.motion_bank_mismatch_count;

    }

    static inline void DescribeRollbackKHitMismatchOffset(
        const RollbackHgCpuSnapshotFrame& a,
        const RollbackHgCpuSnapshotFrame& b,
        size_t snapshot_offset,
        RollbackHgCpuFrameCompare& report) noexcept
    {
        size_t player = 0;
        size_t base = 0;
        if (RollbackHgCpuSnapshotCharaBase(a, 0, base)
            && snapshot_offset >= base
            && snapshot_offset < base + RollbackHgCpuCharaRecordBytes(&a, 0))
        {
            player = 0;
            report.first_unignored_dynamic_player = 1;
            report.first_unignored_dynamic_local = snapshot_offset - base;
        }
        else if (RollbackHgCpuSnapshotCharaBase(a, 1, base)
                 && snapshot_offset >= base
                 && snapshot_offset
                    < base + RollbackHgCpuCharaRecordBytes(&a, 1))
        {
            player = 1;
            report.first_unignored_dynamic_player = 2;
            report.first_unignored_dynamic_local = snapshot_offset - base;
        }
        else
        {
            report.khit_first_region = "outside-dynamic-chara-records";
            return;
        }

        const size_t local = snapshot_offset - base;
        if (local < kRollbackHgCpuHitAreaLocalStart)
            return;

        const auto& at = a.khit_topology[player];
        const auto& bt = b.khit_topology[player];
        if (!at.ok || !bt.ok)
            return;

        const size_t node_start = kRollbackHgCpuHitAreaLocalStart
            + kRollbackHgCpuHitAreaFixedBytes;
        const size_t node_end = node_start + at.node_stream_bytes;
        report.khit_first_player = player + 1;
        report.khit_first_node_stream_end = node_end;
        report.khit_first_relocation_start = node_end;

        if (local < node_start)
        {
            report.khit_first_region = "fixed";
            return;
        }
        if (local >= node_end)
        {
            report.khit_first_region =
                local < node_end + kRollbackHgCpuHitAreaRelocBytes
                    ? "relocation"
                    : "after-relocation";
            return;
        }

        report.khit_first_region = "node";
        for (size_t i = 0; i < at.nodes.size(); ++i)
        {
            const auto& node_a = at.nodes[i];
            if (local < node_a.stream_start_local
                || local >= node_a.stream_start_local + node_a.writer_bytes)
            {
                continue;
            }

            report.khit_first_list = node_a.list_index;
            report.khit_first_node_index = node_a.node_index;
            report.khit_first_stream_start = node_a.stream_start_local;
            report.khit_first_stream_rel = local - node_a.stream_start_local;
            report.khit_first_stream_size = node_a.writer_bytes;
            report.khit_first_node = node_a.address;
            report.khit_first_tag = node_a.writer_tag;

            uintptr_t node_offset = 0;
            size_t contiguous = 0;
            if (!RollbackKHitSourceOffsetForSerializedOffset(
                    node_a.writer_tag,
                    report.khit_first_stream_rel,
                    &node_offset,
                    &contiguous)
                || node_offset >= node_a.bytes.size())
            {
                return;
            }

            report.khit_first_node_source_offset = node_offset;
            report.khit_first_node_source_a = node_a.bytes[node_offset];
            if (i < bt.nodes.size()
                && node_offset < bt.nodes[i].bytes.size())
            {
                report.khit_first_node_source_b =
                    bt.nodes[i].bytes[node_offset];
                report.khit_first_node_source_match =
                    report.khit_first_node_source_a
                    == report.khit_first_node_source_b;
            }
            return;
        }

        report.khit_first_region = "node-unmapped";
    }

    static inline RollbackHgCpuFrameCompare CompareRollbackHgCpuFrames(
        const RollbackHgCpuSnapshotFrame& a,
        const RollbackHgCpuSnapshotFrame& b) noexcept
    {
        RollbackHgCpuFrameCompare report {};
        report.hash_a = a.hash;
        report.hash_b = b.hash;
        report.topology_hash_a = a.khit_topology_hash;
        report.topology_hash_b = b.khit_topology_hash;
        report.motion_bank_hash_a = a.motion_bank_hash;
        report.motion_bank_hash_b = b.motion_bank_hash;
        report.motion_tail_hash_a = a.motion_tail_hash;
        report.motion_tail_hash_b = b.motion_tail_hash;
        report.secondary_event_stack_hash_a =
            a.secondary_event_stack_hash;
        report.secondary_event_stack_hash_b =
            b.secondary_event_stack_hash;
        report.secondary_event_stack_match =
            a.secondary_event_stack_hash != 0
            && a.secondary_event_stack_hash
                == b.secondary_event_stack_hash;
        report.timer_node_hash_a = a.timer_node_hash;
        report.timer_node_hash_b = b.timer_node_hash;
        report.timer_node_match =
            a.timer_node_hash != 0
            && a.timer_node_hash == b.timer_node_hash;
        report.timer_indexed_nonzero_count_a =
            a.timer_node.indexed_nonzero_count;
        report.timer_indexed_captured_count_a =
            a.timer_node.indexed_captured_count;
        report.timer_indexed_object_captured_count_a =
            a.timer_node.indexed_object_captured_count;
        report.timer_indexed_slot0_root_a = a.timer_node.indexed_root[0];
        report.timer_indexed_slot0_vtable_a = a.timer_node.indexed_vtable[0];
        report.timer_indexed_slot0_writer_a = a.timer_node.indexed_writer[0];
        report.timer_indexed_slot0_captured_a =
            a.timer_node.indexed_captured[0];
        report.p1_record_bytes_a = RollbackHgCpuCharaRecordBytes(&a, 0);
        report.p1_record_bytes_b = RollbackHgCpuCharaRecordBytes(&b, 0);
        report.p2_record_bytes_a = RollbackHgCpuCharaRecordBytes(&a, 1);
        report.p2_record_bytes_b = RollbackHgCpuCharaRecordBytes(&b, 1);
        (void)RollbackHgCpuSnapshotCharaBase(a, 1, report.p2_base_a);
        (void)RollbackHgCpuSnapshotCharaBase(b, 1, report.p2_base_b);
        report.hash_match = a.hash != 0 && a.hash == b.hash;
        report.topology_match =
            a.khit_topology_hash != 0
            && a.khit_topology_hash == b.khit_topology_hash;
        report.motion_bank_match =
            a.motion_bank_hash != 0
            && a.motion_bank_hash == b.motion_bank_hash;
        report.motion_tail_match =
            a.motion_tail_hash != 0
            && a.motion_tail_hash == b.motion_tail_hash;
        CompareRollbackMotionBankHistories(a, b, report);
        report.bytes_compared = (std::min)(
            RollbackHgCpuEffectiveBytes(a),
            RollbackHgCpuEffectiveBytes(b));
        bool first_set = false;
        bool first_unignored_set = false;
        for (size_t i = 0; i < report.bytes_compared; ++i)
        {
            if (a.bytes[i] == b.bytes[i])
                continue;
            ++report.mismatch_count;
            if (!first_set)
            {
                first_set = true;
                report.first_mismatch_offset = i;
                report.first_mismatch_a = a.bytes[i];
                report.first_mismatch_b = b.bytes[i];
            }

            const char* ignore_reason = nullptr;
            if (RollbackHgCpuRoundTripOffsetIgnored(i, &ignore_reason, &a))
            {
                ++report.ignored_mismatch_count;
                if (std::strcmp(report.first_ignored_reason, "none") == 0
                    && ignore_reason)
                {
                    report.first_ignored_reason = ignore_reason;
                }
                continue;
            }

            ++report.unignored_mismatch_count;
            if (!first_unignored_set)
            {
                first_unignored_set = true;
                report.first_unignored_mismatch_offset = i;
                report.first_unignored_mismatch_a = a.bytes[i];
                report.first_unignored_mismatch_b = b.bytes[i];
                DescribeRollbackKHitMismatchOffset(a, b, i, report);
            }
        }

        const size_t a_effective = RollbackHgCpuEffectiveBytes(a);
        const size_t b_effective = RollbackHgCpuEffectiveBytes(b);
        const size_t size_mismatch =
            a_effective > report.bytes_compared
            ? a_effective - report.bytes_compared
            : b_effective - report.bytes_compared;
        report.mismatch_count += size_mismatch;
        report.unignored_mismatch_count += size_mismatch;
        const bool motion_bank_captured =
            a.motion_banks.ok
            && b.motion_banks.ok
            && a.motion_bank_hash != 0
            && b.motion_bank_hash != 0;
        report.policy_match =
            a_effective == b_effective
            && report.topology_match
            && motion_bank_captured
            && report.motion_bank_match
            && report.motion_bank_mismatch_count == 0
            && report.motion_tail_match
            && report.secondary_event_stack_match
            && report.timer_node_match
            && report.unignored_mismatch_count == 0;
        return report;
    }

    static inline bool RollbackHgCpuCanonicalRestoreMatch(
        const RollbackHgCpuSnapshotFrame& expected,
        const RollbackHgCpuSnapshotFrame& observed,
        const RollbackHgCpuFrameCompare& compare) noexcept
    {
        // KHit node addresses and allocator list links are process-local
        // topology. Real attack-heavy rollback can retire and recreate those
        // nodes while preserving the complete ordered canonical KHit stream.
        // Reject every gameplay/history mismatch, but do not require raw node
        // addresses to survive a restore.
        return RollbackHgCpuCanonicalRestoreEvidenceMatches(
            expected.canonical_hash,
            observed.canonical_hash,
            compare.motion_bank_match,
            compare.motion_bank_mismatch_count,
            compare.motion_tail_match,
            compare.secondary_event_stack_match,
            compare.timer_node_match,
            compare.unignored_mismatch_count);
    }

    static inline RollbackPreallocatedCapturePreflightReport
    ValidateRollbackStepStatePreallocatedCapture(
        const RollbackSnapshotManifest& manifest,
        const RollbackStepState& out,
        const RollbackBreakableStageSnapshot* accepted_stage,
        const RollbackHgCpuSnapshotFrame* hgcpu_emergency_scratch,
        const RollbackProductionStepStateCapacityLimits* capacity_limits)
        noexcept
    {
        return ValidateRollbackPreallocatedCaptureGate(
            out, accepted_stage, hgcpu_emergency_scratch, capacity_limits,
            [](const RollbackStepState& state,
               const RollbackProductionStepStateCapacityLimits& limits)
                noexcept {
                return CaptureRollbackProductionStepStateCapacityLimits(state)
                    == limits;
            },
            [&manifest](const RollbackStepState& state) noexcept {
                return RollbackSnapshotPreallocatedCaptureReady(
                    manifest, state.explicit_snapshot);
            },
            [](const RollbackStepState& state) noexcept {
                return RollbackHgCpuPreallocatedCaptureFailure(state.hgcpu);
            },
            [](const RollbackHgCpuSnapshotFrame& scratch_state) noexcept {
                return RollbackHgCpuPreallocatedCaptureFailure(scratch_state);
            },
            [](const RollbackBreakableStageSnapshot& stage,
               const RollbackStepState& state) noexcept {
                return RollbackBreakableStagePreallocatedCaptureReady(
                    stage, state.breakable_stage);
            });
    }

    static inline RollbackStepStateReport CaptureRollbackStepState(
        uintptr_t image_base,
        const RollbackSnapshotManifest& manifest,
        RollbackStepState& out,
        RollbackLifecycleMode mode =
            RollbackLifecycleMode::StockOnlinePvp,
        bool replay_fork_lab = false,
        const RollbackBreakableStageSnapshot* accepted_stage = nullptr,
        RollbackHgCpuSnapshotFrame* hgcpu_emergency_scratch = nullptr,
        const RollbackProductionStepStateCapacityLimits* capacity_limits =
            nullptr,
        uintptr_t native_input_callback_object = 0,
        RollbackStageWindAllocationPool* stage_wind_pool = nullptr,
        RollbackPaletteVariantWriterRegistry*
            palette_writer_registry = nullptr,
        const RollbackPresentationSemanticIdentity*
            presentation_semantic_identity = nullptr) noexcept
    {
        RollbackStepStateReport report {};
        report.failure = "ok";
        if (accepted_stage)
        {
            const auto preflight =
                ValidateRollbackStepStatePreallocatedCapture(
                    manifest, out, accepted_stage,
                    hgcpu_emergency_scratch, capacity_limits);
            if (!preflight.ok)
            {
                report.failure = preflight.failure;
                return report;
            }
        }
        const uint64_t capacity_hash_before = accepted_stage
            ? RollbackStepStateCapacityHash(out) : 0;
        const auto component_capacities_before = accepted_stage
            ? RollbackStepStateCapacityComponentHashes(out)
            : std::array<uint64_t, 8> {};
        out.recycle_for_capture();
        auto phase_started = std::chrono::steady_clock::now();
        const auto finish_phase = [&phase_started]() noexcept {
            const auto now = std::chrono::steady_clock::now();
            const uint64_t elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now - phase_started).count());
            phase_started = now;
            return elapsed;
        };

        if (!ValidateRollbackStepLifecycle(
                image_base, manifest.epoch, mode, replay_fork_lab))
        {
            report.failure = "capture-lifecycle-preflight-failed";
            return report;
        }

        report.lux_move_command_ok =
            CaptureRollbackLuxMoveCommandSnapshot(
                image_base, out.lux_move_command);
        if (!report.lux_move_command_ok)
        {
            report.failure = "lux-move-command-capture-failed";
            return report;
        }
        report.lux_move_pump_ok =
            CaptureRollbackLuxMoveSystemPumpSnapshot(
                image_base, out.lux_move_pump);
        if (!report.lux_move_pump_ok)
        {
            report.failure = "lux-move-pump-capture-failed";
            return report;
        }
        report.lux_move_slot_params_ok =
            CaptureRollbackLuxMoveVmSlotParamSnapshot(
                image_base, out.lux_move_slot_params);
        if (!report.lux_move_slot_params_ok)
        {
            report.failure = "lux-move-slot-param-capture-failed";
            return report;
        }
        report.lux_subvm_ok = CaptureRollbackLuxSubVmSnapshot(
            image_base, out.lux_subvm);
        if (!report.lux_subvm_ok)
        {
            report.failure = "lux-subvm-capture-or-class-unsupported";
            return report;
        }

        RngTraceHook& rng_trace = RngTraceHook::instance();
        const bool gameplay_crt_enabled = rng_trace.gameplay_crt_enabled();
        report.gameplay_crt_ok = !gameplay_crt_enabled
            || rng_trace.capture_gameplay_crt_state(out.gameplay_crt);
        if (!report.gameplay_crt_ok)
        {
            report.failure = "gameplay-crt-state-capture-failed";
            return report;
        }

        report.explicit_report = CaptureRollbackSnapshotBytes(
            manifest, out.explicit_snapshot, accepted_stage != nullptr);
        report.explicit_capture_nanoseconds = finish_phase();
        report.explicit_ok = report.explicit_report.ok;
        if (!report.explicit_ok)
        {
            report.failure = report.explicit_report.failure;
            return report;
        }

        report.hgcpu_report = CaptureRollbackHgCpuSnapshot(
            image_base, out.hgcpu, hgcpu_emergency_scratch,
            accepted_stage != nullptr);
        report.hgcpu_capture_nanoseconds = finish_phase();
        report.hgcpu_ok = report.hgcpu_report.ok;

        // The native writer mutates several globals also owned by the explicit
        // snapshot. Always attempt cleanup before returning, even when the
        // native call failed. The second lifecycle token is checked after the
        // complete capture below.
        const RollbackSnapshotCopyReport post_hgcpu_explicit_restore =
            RestoreFreshRollbackSnapshotBytesOnce(out.explicit_snapshot);
        report.explicit_cleanup_nanoseconds = finish_phase();
        if (!post_hgcpu_explicit_restore.ok)
        {
            report.failure = "explicit-post-hgcpu-restore-failed";
            report.explicit_report = post_hgcpu_explicit_restore;
            report.explicit_ok = false;
            return report;
        }
        if (!report.hgcpu_ok)
        {
            report.failure = report.hgcpu_report.failure;
            return report;
        }

        if (!palette_writer_registry)
        {
            report.failure =
                "palette-variant-writer-registry-required";
            return report;
        }
        report.palette_variants_report =
            CaptureRollbackPaletteVariantSnapshot(
                image_base, out.palette_variants,
                *palette_writer_registry,
                accepted_stage != nullptr);
        report.palette_variants_ok =
            report.palette_variants_report.ok;
        report.palette_variant_capture_nanoseconds = finish_phase();
        if (!report.palette_variants_ok)
        {
            report.failure = report.palette_variants_report.failure;
            return report;
        }

        report.breakable_stage_report = accepted_stage
            ? CaptureRollbackBreakableStageScalars(
                *accepted_stage, out.breakable_stage)
            : CaptureRollbackBreakableStageSnapshot(
                manifest.epoch.stage_actor_manager, out.breakable_stage);
        report.breakable_stage_ok = report.breakable_stage_report.ok;
        report.stage_capture_nanoseconds = finish_phase();
        if (!report.breakable_stage_ok
            || out.breakable_stage.stage_layout_digest
                != manifest.epoch.stage_layout_digest
            || out.breakable_stage.actor_set_digest
                != manifest.epoch.actor_set_digest)
        {
            report.failure = report.breakable_stage_ok
                ? "breakable-stage-epoch-digest-mismatch"
                : report.breakable_stage_report.failure;
            return report;
        }

        report.stage_wind_report = CaptureRollbackStageWindSnapshot(
            image_base, out.stage_wind, stage_wind_pool);
        report.stage_wind_ok = report.stage_wind_report.ok;
        report.wind_capture_nanoseconds = finish_phase();
        if (!report.stage_wind_ok)
        {
            report.failure = report.stage_wind_report.failure;
            return report;
        }

        report.native_round_state_ok =
            CaptureRollbackNativeRoundStateSnapshot(
                manifest.epoch.battle_manager, out.native_round_state);
        if (!report.native_round_state_ok)
        {
            report.failure = "native-round-state-capture-failed";
            return report;
        }
        report.native_simulation_state_ok =
            CaptureRollbackNativeSimulationState(
                manifest.epoch.battle_manager,
                out.native_simulation_state);
        if (!report.native_simulation_state_ok)
        {
            report.failure = "native-simulation-state-capture-failed";
            return report;
        }
        report.native_input_callback_ok = native_input_callback_object == 0
            || CaptureRollbackNativeInputCallbackSnapshot(
                native_input_callback_object, out.native_input_callback);
        if (!report.native_input_callback_ok)
        {
            report.failure = "native-input-callback-state-capture-failed";
            return report;
        }
        report.battle_camera_ok = CaptureRollbackBattleCameraSnapshot(
            image_base, out.battle_camera);
        if (!report.battle_camera_ok)
        {
            report.failure = "battle-camera-capture-failed";
            return report;
        }
        report.presentation_semantic_ok =
            presentation_semantic_identity == nullptr
            || CaptureRollbackPresentationSemanticSnapshot(
                *presentation_semantic_identity,
                out.presentation_semantic);
        if (!report.presentation_semantic_ok)
        {
            report.failure =
                "presentation-semantic-capture-failed";
            return report;
        }

        report.frame_counter_ok =
            RollbackReadFrameCounter(image_base, out.frame_counter);
        report.latest_input_ok =
            RollbackReadLatestInputs(image_base, out.latest_input);
        report.camera_args_ok =
            RollbackReadCameraArgs(image_base, out.camera_args);
        if (!report.frame_counter_ok)
        {
            report.failure = "frame-counter-read-failed";
            return report;
        }
        if (!report.latest_input_ok)
        {
            report.failure = "latest-input-read-failed";
            return report;
        }
        if (!report.camera_args_ok)
        {
            report.failure = "camera-args-read-failed";
            return report;
        }

        out.motion_decode_scratch =
            rollback_motion_decode_scratch().capture();
        report.motion_decode_scratch_ok =
            ValidateRollbackMotionDecodeScratchSnapshot(
                out.motion_decode_scratch);
        if (!report.motion_decode_scratch_ok)
        {
            report.failure = "motion-decode-scratch-capture-failed";
            return report;
        }
        out.motion_pose_residue =
            rollback_motion_pose_residue().capture();
        report.motion_pose_residue_ok =
            ValidateRollbackMotionPoseResidueSnapshot(
                out.motion_pose_residue);
        if (!report.motion_pose_residue_ok)
        {
            report.failure = "motion-pose-residue-capture-failed";
            return report;
        }

        // Several component capture adapters invoke native readers after the
        // HgCpu cleanup above. Those readers are observers, but some of them
        // consume battle RNG internally. Reapply the captured explicit image
        // as the final live-memory operation so snapshot construction cannot
        // advance simulation state. This also makes the cleanup independent
        // of component ordering.
        const RollbackSnapshotCopyReport final_explicit_restore =
            RestoreFreshRollbackSnapshotBytesOnce(out.explicit_snapshot);
        report.explicit_cleanup_nanoseconds += finish_phase();
        if (!final_explicit_restore.ok)
        {
            report.failure = "explicit-final-capture-restore-failed";
            report.explicit_report = final_explicit_restore;
            report.explicit_ok = false;
            return report;
        }
        if (gameplay_crt_enabled
            && !rng_trace.restore_gameplay_crt_state(out.gameplay_crt))
        {
            report.gameplay_crt_ok = false;
            report.failure = "gameplay-crt-final-capture-restore-failed";
            return report;
        }

        out.canonical_hash = HashRollbackStepStateCanonical(out);
        out.combined_hash = HashRollbackStepState(out);
        report.capture_finalize_nanoseconds = finish_phase();
        if (out.canonical_hash == 0)
        {
            report.failure = "canonical-step-hash-failed";
            return report;
        }
        if (accepted_stage
            && RollbackStepStateCapacityHash(out) != capacity_hash_before)
        {
            static constexpr const char* failures[] = {
                "rollback-capacity-growth-hgcpu",
                "rollback-capacity-growth-palette-variants",
                "rollback-capacity-growth-khit",
                "rollback-capacity-growth-motion",
                "rollback-capacity-growth-skeleton",
                "rollback-capacity-growth-timer",
                "rollback-capacity-growth-explicit",
                "rollback-capacity-growth-stage",
            };
            const auto after = RollbackStepStateCapacityComponentHashes(out);
            report.failure = "rollback-snapshot-capacity-growth";
            for (size_t i = 0; i < after.size(); ++i)
            {
                if (after[i] != component_capacities_before[i])
                {
                    report.failure = failures[i];
                    break;
                }
            }
            return report;
        }
        if (!ValidateRollbackStepLifecycle(
                image_base, manifest.epoch, mode, replay_fork_lab))
        {
            report.failure = "capture-lifecycle-postflight-failed";
            return report;
        }
        report.frame_counter = out.frame_counter;
        report.combined_hash = out.combined_hash;
        report.ok = true;
        return report;
    }

    static inline RollbackStepStateReport RestoreRollbackStepState(
        uintptr_t image_base,
        const RollbackStepState& state,
        bool allow_emergency_restore = true,
        RollbackLifecycleMode mode =
            RollbackLifecycleMode::StockOnlinePvp,
        bool replay_fork_lab = false,
        bool validate_integrity = true,
        RollbackStepState* verification_storage = nullptr,
        RollbackHgCpuSnapshotFrame* verification_hgcpu_scratch = nullptr,
        RollbackStageWindAllocationPool* stage_wind_pool = nullptr,
        RollbackPaletteVariantWriterRegistry*
            palette_writer_registry = nullptr,
        RollbackStepState* emergency_storage = nullptr,
        const RollbackPresentationSemanticIdentity*
            presentation_semantic_identity = nullptr)
        noexcept
    {
        RollbackStepStateReport report {};
        report.failure = "ok";

        // Validate the complete source and live target epoch before emergency
        // capture or any HgCpu/KHit/native write occurs.
        if (validate_integrity
            && !ValidateRollbackStepStateIntegrity(state))
        {
            report.failure = "step-state-integrity-preflight-failed";
            return report;
        }
        if (!ValidateRollbackStepLifecycle(
                image_base, state.explicit_snapshot.epoch, mode,
                replay_fork_lab))
        {
            report.failure = "lifecycle-epoch-preflight-failed";
            return report;
        }
        if (state.presentation_semantic.valid
            && (!presentation_semantic_identity
                || !presentation_semantic_identity->same_identity_as(
                    state.presentation_semantic.identity)
                || !RollbackPresentationSemanticIdentityLive(
                    *presentation_semantic_identity)))
        {
            report.failure =
                "presentation-semantic-identity-preflight-failed";
            return report;
        }

        const char* verification_capture_failure =
            "step-post-restore-verification-failed";
        const bool require_preallocated = verification_storage != nullptr;
        auto capture_current = [&](RollbackStepState& current) noexcept {
            current.recycle_for_capture();
            RngTraceHook& rng_trace = RngTraceHook::instance();
            const bool gameplay_crt_enabled =
                rng_trace.gameplay_crt_enabled();
            if (gameplay_crt_enabled
                && !rng_trace.capture_gameplay_crt_state(
                    current.gameplay_crt))
            {
                verification_capture_failure =
                    "verification-gameplay-crt-capture-failed";
                return false;
            }
            if (!CaptureRollbackEmergencyFrame(
                    state.explicit_snapshot,
                    current.explicit_snapshot,
                    require_preallocated))
            {
                verification_capture_failure =
                    "verification-explicit-capture-failed";
                return false;
            }
            const auto transaction =
                RunRollbackObserverCaptureTransaction(
                    [&]() noexcept {
                        const RollbackHgCpuSnapshotReport hgcpu =
                            CaptureRollbackHgCpuSnapshot(
                                image_base, current.hgcpu,
                                verification_hgcpu_scratch,
                                require_preallocated);
                        RollbackSnapshotCopyReport explicit_restore =
                            RestoreRollbackSnapshotBytes(
                                current.explicit_snapshot);
                        if (!explicit_restore.ok)
                        {
                            verification_capture_failure =
                                "verification-explicit-cleanup-failed";
                            return false;
                        }
                        if (!hgcpu.ok)
                        {
                            verification_capture_failure = hgcpu.failure;
                            return false;
                        }
                        if (!palette_writer_registry)
                        {
                            verification_capture_failure =
                                "palette-variant-writer-registry-required";
                            return false;
                        }
                        const RollbackPaletteVariantSnapshotReport palette =
                            CaptureRollbackPaletteVariantSnapshot(
                                image_base, current.palette_variants,
                                *palette_writer_registry,
                                require_preallocated);
                        if (!palette.ok)
                        {
                            verification_capture_failure = palette.failure;
                            return false;
                        }
                        const RollbackBreakableStageReport stage =
                            CaptureRollbackBreakableStageScalars(
                                state.breakable_stage,
                                current.breakable_stage);
                        if (!stage.ok)
                        {
                            verification_capture_failure = stage.failure;
                            return false;
                        }
                        if (!RollbackReadFrameCounter(
                                image_base, current.frame_counter))
                        {
                            verification_capture_failure =
                                "verification-frame-counter-read-failed";
                            return false;
                        }
                        if (!RollbackReadLatestInputs(
                                image_base, current.latest_input))
                        {
                            verification_capture_failure =
                                "verification-latest-input-read-failed";
                            return false;
                        }
                        if (!RollbackReadCameraArgs(
                                image_base, current.camera_args))
                        {
                            verification_capture_failure =
                                "verification-camera-args-read-failed";
                            return false;
                        }
                        const RollbackStageWindSnapshotReport wind =
                            CaptureRollbackStageWindSnapshot(
                                image_base, current.stage_wind,
                                stage_wind_pool);
                        if (!wind.ok)
                        {
                            verification_capture_failure = wind.failure;
                            return false;
                        }
                        if (!CaptureRollbackNativeRoundStateSnapshot(
                                state.explicit_snapshot.epoch.battle_manager,
                                current.native_round_state))
                        {
                            verification_capture_failure =
                                "verification-native-round-state-capture-failed";
                            return false;
                        }
                        if (!CaptureRollbackNativeSimulationState(
                                state.explicit_snapshot.epoch.battle_manager,
                                current.native_simulation_state))
                        {
                            verification_capture_failure =
                                "verification-native-simulation-state-capture-failed";
                            return false;
                        }
                        if (state.native_input_callback.valid
                            && !CaptureRollbackNativeInputCallbackSnapshot(
                                state.native_input_callback.object,
                                current.native_input_callback))
                        {
                            verification_capture_failure =
                                "verification-native-input-callback-capture-failed";
                            return false;
                        }
                        if (!CaptureRollbackBattleCameraSnapshot(
                                image_base, current.battle_camera))
                        {
                            verification_capture_failure =
                                "verification-battle-camera-capture-failed";
                            return false;
                        }
                        if (state.presentation_semantic.valid
                            && !CaptureRollbackPresentationSemanticSnapshot(
                                state.presentation_semantic.identity,
                                current.presentation_semantic))
                        {
                            verification_capture_failure =
                                "verification-presentation-semantic-capture-failed";
                            return false;
                        }
                        if (!CaptureRollbackLuxMoveSystemPumpSnapshot(
                                image_base, current.lux_move_pump))
                        {
                            verification_capture_failure =
                                "verification-lux-move-pump-capture-failed";
                            return false;
                        }
                        if (!CaptureRollbackLuxMoveVmSlotParamSnapshot(
                                image_base,
                                current.lux_move_slot_params))
                        {
                            verification_capture_failure =
                                "verification-lux-move-slot-param-capture-failed";
                            return false;
                        }
                        if (!CaptureRollbackLuxMoveCommandSnapshot(
                                image_base, current.lux_move_command))
                        {
                            verification_capture_failure =
                                "verification-lux-move-command-capture-failed";
                            return false;
                        }
                        if (!CaptureRollbackLuxSubVmSnapshot(
                                image_base, current.lux_subvm))
                        {
                            verification_capture_failure =
                                "verification-lux-subvm-capture-failed";
                            return false;
                        }
                        current.motion_decode_scratch =
                            rollback_motion_decode_scratch().capture();
                        if (!ValidateRollbackMotionDecodeScratchSnapshot(
                                current.motion_decode_scratch))
                        {
                            verification_capture_failure =
                                "verification-motion-decode-scratch-capture-failed";
                            return false;
                        }
                        current.motion_pose_residue =
                            rollback_motion_pose_residue().capture();
                        if (!ValidateRollbackMotionPoseResidueSnapshot(
                                current.motion_pose_residue))
                        {
                            verification_capture_failure =
                                "verification-motion-pose-residue-capture-failed";
                            return false;
                        }
                        current.canonical_hash =
                            HashRollbackStepStateCanonical(current);
                        current.combined_hash =
                            HashRollbackStepState(current);
                        return true;
                    },
                    [&]() noexcept {
                        const RollbackSnapshotCopyReport final_restore =
                            RestoreRollbackSnapshotBytes(
                                current.explicit_snapshot);
                        if (!final_restore.ok)
                        {
                            verification_capture_failure =
                                "verification-final-explicit-cleanup-failed";
                        }
                        const bool gameplay_crt_restored =
                            !gameplay_crt_enabled
                            || rng_trace.restore_gameplay_crt_state(
                                current.gameplay_crt);
                        if (!gameplay_crt_restored)
                        {
                            verification_capture_failure =
                                "verification-gameplay-crt-cleanup-failed";
                        }
                        return final_restore.ok && gameplay_crt_restored;
                    });
            return transaction.ok;
        };

        RollbackStepState local_emergency {};
        RollbackStepState& emergency = emergency_storage
            ? *emergency_storage : local_emergency;
        if (allow_emergency_restore)
        {
            if (&emergency == &state
                || &emergency == verification_storage)
            {
                report.failure = "step-emergency-storage-alias";
                return report;
            }
            if (!capture_current(emergency))
            {
                report.failure = "step-emergency-capture-failed";
                return report;
            }
            report.emergency_captured = true;
        }

        auto recover = [&](const char* failure) noexcept {
            report.failure = failure;
            if (allow_emergency_restore && report.emergency_captured)
            {
                const RollbackStepStateReport recovery =
                    RestoreRollbackStepState(
                        image_base, emergency, false, mode,
                        replay_fork_lab, validate_integrity,
                        nullptr, nullptr, stage_wind_pool,
                        palette_writer_registry, nullptr,
                        presentation_semantic_identity);
                report.emergency_restored = recovery.ok;
            }
            if (!report.emergency_restored)
            {
                rollback_motion_decode_scratch()
                    .cancel_pending_restore();
                rollback_motion_pose_residue()
                    .cancel_pending_restore();
            }
            return report;
        };

        const auto restore_started = std::chrono::steady_clock::now();
        if (!palette_writer_registry)
            return recover("palette-variant-writer-registry-required");

        // Lifecycle admission is part of restore preflight, not a condition
        // that may be discovered after HgCpu/camera/palette state has already
        // been mutated.
        RollbackLifecycleEpoch live_epoch {};
        if (replay_fork_lab)
        {
            if (!ValidateRollbackStepLifecycle(
                    image_base, state.explicit_snapshot.epoch,
                    mode, true))
            {
                return recover("replay-fork-lifecycle-epoch-drift");
            }
            live_epoch = state.explicit_snapshot.epoch;
        }
        else
        {
            RollbackLiveToken live_token {};
            if (!CaptureRollbackLiveToken(
                    image_base, state.explicit_snapshot.epoch,
                    live_token)
                || !RollbackLiveTokenCompatibleWithRoundTransition(
                    state.explicit_snapshot.epoch, live_token, mode))
            {
                return recover("lifecycle-token-mismatch");
            }
            live_epoch = state.explicit_snapshot.epoch;
            live_epoch.input_log_frame = live_token.input_log_frame;
        }

        // Identity-generating native objects must be checked before any
        // rollback component mutates the process. A stale scheduler snapshot
        // must never republish a deleted SubVM address.
        if (!ValidateRollbackMoveSchedGenerations(state.explicit_snapshot))
            return recover("lux-move-sched-generation-mismatch");
        if (!RollbackLuxMoveCommandGenerationMatches(
                state.lux_move_command))
            return recover("lux-move-command-generation-mismatch");
        if (!RollbackLuxMoveSystemPumpGenerationMatches(
                state.lux_move_pump))
            return recover("lux-move-pump-generation-mismatch");
        if (!RollbackLuxSubVmGenerationMatches(state.lux_subvm))
            return recover("lux-subvm-generation-mismatch");
        if (!RollbackBattleCameraGenerationMatches(state.battle_camera))
            return recover("battle-camera-generation-mismatch");
        report.hgcpu_report = RestoreRollbackHgCpuSnapshot(
            image_base, state.hgcpu);
        report.hgcpu_ok = report.hgcpu_report.ok;
        if (!report.hgcpu_ok)
        {
            return recover(report.hgcpu_report.failure);
        }
        report.battle_camera_ok = RestoreRollbackBattleCameraSnapshot(
            image_base, state.battle_camera);
        if (!report.battle_camera_ok)
            return recover("battle-camera-restore-failed");

        report.palette_variants_report =
            RestoreRollbackPaletteVariantSnapshot(
                image_base, state.palette_variants,
                *palette_writer_registry);
        report.palette_variants_ok =
            report.palette_variants_report.ok;
        if (!report.palette_variants_ok)
        {
            return recover(report.palette_variants_report.failure);
        }

        report.explicit_report = allow_emergency_restore
            ? RestoreRollbackSnapshotBytesIfEpochMatches(
                state.explicit_snapshot, live_epoch, replay_fork_lab)
            : RestoreRollbackSnapshotBytesOnceIfEpochMatches(
                state.explicit_snapshot, live_epoch, replay_fork_lab);
        report.explicit_ok = report.explicit_report.ok;
        if (!report.explicit_ok)
        {
            return recover(report.explicit_report.failure);
        }
        report.presentation_semantic_ok =
            !state.presentation_semantic.valid
            || RestoreRollbackPresentationSemanticSnapshot(
                state.presentation_semantic);
        if (!report.presentation_semantic_ok)
            return recover("presentation-semantic-restore-failed");
        report.lux_move_command_ok =
            RestoreRollbackLuxMoveCommandSnapshot(
                state.lux_move_command);
        if (!report.lux_move_command_ok)
            return recover("lux-move-command-restore-failed");
        report.lux_move_pump_ok =
            RestoreRollbackLuxMoveSystemPumpSnapshot(state.lux_move_pump);
        if (!report.lux_move_pump_ok)
            return recover("lux-move-pump-restore-failed");
        report.lux_move_slot_params_ok =
            RestoreRollbackLuxMoveVmSlotParamSnapshot(
                state.lux_move_slot_params);
        if (!report.lux_move_slot_params_ok)
            return recover("lux-move-slot-param-restore-failed");
        report.lux_subvm_ok = RestoreRollbackLuxSubVmSnapshot(
            state.lux_subvm);
        if (!report.lux_subvm_ok)
            return recover("lux-subvm-restore-failed");

        report.breakable_stage_report =
            RestoreRollbackBreakableStageSnapshot(
                state.explicit_snapshot.epoch.stage_actor_manager,
                state.breakable_stage);
        report.breakable_stage_ok = report.breakable_stage_report.ok;
        if (!report.breakable_stage_ok)
            return recover(report.breakable_stage_report.failure);

        report.stage_wind_report = RestoreRollbackStageWindSnapshot(
            image_base, state.stage_wind, stage_wind_pool);
        report.stage_wind_ok = report.stage_wind_report.ok;
        if (!report.stage_wind_ok)
            return recover(report.stage_wind_report.failure);

        report.native_round_state_ok =
            RestoreRollbackNativeRoundStateSnapshot(
                state.explicit_snapshot.epoch.battle_manager,
                state.native_round_state);
        if (!report.native_round_state_ok)
            return recover("native-round-state-restore-failed");
        report.native_simulation_state_ok =
            RestoreRollbackNativeSimulationState(
                state.explicit_snapshot.epoch.battle_manager,
                state.native_simulation_state);
        if (!report.native_simulation_state_ok)
            return recover("native-simulation-state-restore-failed");
        report.native_input_callback_ok = !state.native_input_callback.valid
            || RestoreRollbackNativeInputCallbackSnapshot(
                state.native_input_callback);
        if (!report.native_input_callback_ok)
            return recover("native-input-callback-state-restore-failed");
        report.gameplay_crt_ok = state.gameplay_crt.phase
                == RollbackGameplayCrtPhase::Uninitialized
            ? !RngTraceHook::instance().gameplay_crt_enabled()
            : RngTraceHook::instance().restore_gameplay_crt_state(
                state.gameplay_crt);
        if (!report.gameplay_crt_ok)
            return recover("gameplay-crt-state-restore-failed");

        report.frame_counter_ok =
            RollbackWriteFrameCounter(image_base, state.frame_counter);
        report.latest_input_ok =
            RollbackWriteLatestInputs(image_base, state.latest_input);
        report.camera_args_ok =
            RollbackWriteCameraArgs(image_base, state.camera_args);
        if (!report.frame_counter_ok)
        {
            return recover("frame-counter-write-failed");
        }
        if (!report.latest_input_ok)
        {
            return recover("latest-input-write-failed");
        }
        if (!report.camera_args_ok)
        {
            return recover("camera-args-write-failed");
        }

        report.motion_decode_scratch_ok =
            rollback_motion_decode_scratch().restore(
                state.motion_decode_scratch);
        if (!report.motion_decode_scratch_ok)
            return recover("motion-decode-scratch-restore-failed");
        report.motion_pose_residue_ok =
            rollback_motion_pose_residue().restore(
                state.motion_pose_residue);
        if (!report.motion_pose_residue_ok)
            return recover("motion-pose-residue-restore-failed");

        // Verification is mandatory even for the non-recursive emergency
        // recovery pass. Otherwise a partial recovery write can be reported
        // as successful merely because a second recovery is disabled.
        report.restore_nanoseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - restore_started).count());
        {
            const auto verification_started =
                std::chrono::steady_clock::now();
            RollbackStepState local_verification {};
            RollbackStepState& verification = verification_storage
                ? *verification_storage : local_verification;
            if (!capture_current(verification))
            {
                return recover(verification_capture_failure);
            }
            const RollbackHgCpuFrameCompare hgcpu_verify =
                CompareRollbackHgCpuFrames(
                    state.hgcpu, verification.hgcpu);
            report.verification_hgcpu_compare = hgcpu_verify;
            report.verification_canonical_hash = verification.canonical_hash;
            report.expected_canonical_hash = state.canonical_hash;
            report.verification_explicit_hash =
                verification.explicit_snapshot.integrity_hash;
            report.expected_explicit_hash =
                state.explicit_snapshot.integrity_hash;
            report.verification_stage_hash =
                verification.breakable_stage.canonical_hash;
            report.expected_stage_hash =
                state.breakable_stage.canonical_hash;
            const bool hgcpu_restore_match =
                RollbackHgCpuCanonicalRestoreMatch(
                    state.hgcpu, verification.hgcpu, hgcpu_verify);
            if (!hgcpu_restore_match
                || verification.explicit_snapshot.integrity_hash
                    != state.explicit_snapshot.integrity_hash
                || verification.palette_variants.integrity_hash
                    != state.palette_variants.integrity_hash
                || verification.breakable_stage.canonical_hash
                    != state.breakable_stage.canonical_hash
                || verification.native_round_state.hash
                    != state.native_round_state.hash
                || verification.native_simulation_state.hash
                    != state.native_simulation_state.hash
                || verification.native_input_callback.semantic_hash
                    != state.native_input_callback.semantic_hash
                || verification.battle_camera.integrity_hash
                    != state.battle_camera.integrity_hash
                || verification.lux_move_command.semantic_hash
                    != state.lux_move_command.semantic_hash
                || verification.lux_move_pump.semantic_hash
                    != state.lux_move_pump.semantic_hash
                || verification.lux_move_slot_params.canonical_hash
                    != state.lux_move_slot_params.canonical_hash
                || verification.lux_subvm.semantic_hash
                    != state.lux_subvm.semantic_hash
                || !RollbackGameplayCrtCanonicalEqual(
                    verification.gameplay_crt, state.gameplay_crt)
                || HashRollbackMotionDecodeScratchSnapshot(
                    verification.motion_decode_scratch)
                    != HashRollbackMotionDecodeScratchSnapshot(
                        state.motion_decode_scratch)
                || HashRollbackMotionPoseResidueRollbackState(
                    verification.motion_pose_residue)
                    != HashRollbackMotionPoseResidueRollbackState(
                        state.motion_pose_residue)
                || verification.canonical_hash != state.canonical_hash
                || verification.frame_counter != state.frame_counter
                || std::memcmp(
                    verification.latest_input,
                    state.latest_input,
                    sizeof(state.latest_input)) != 0
                || std::memcmp(
                    verification.camera_args,
                    state.camera_args,
                    sizeof(state.camera_args)) != 0)
            {
                return recover("step-post-restore-verification-failed");
            }
            report.verification_ok = true;
            report.verification_nanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now()
                        - verification_started).count());
        }

        report.frame_counter = state.frame_counter;
        report.combined_hash = state.combined_hash;
        report.ok = true;
        return report;
    }

    static inline bool RollbackSafeInvokePerFrameTick(
        RollbackPerFrameTickFn fn,
        uintptr_t* args,
        NativeCallFault* fault = nullptr) noexcept
    {
        if (fault) *fault = NativeCallFault {};
        if (!fn || !args) return false;
        __try
        {
            fn(args);
            return true;
        }
        __except (CaptureNativeCallFault(
            GetExceptionCode(), GetExceptionInformation(), fault))
        {
            return false;
        }
    }

    class RollbackNativeStepper
    {
    public:
        bool resolve(uintptr_t image_base) noexcept
        {
            if (m_bypass) return true;
            if (!image_base) return false;

            void* site = reinterpret_cast<void*>(
                image_base + kRollbackRVA_LuxBattlePerFrameTick);
            constexpr size_t kTrampSize = 12;
            uint8_t* tramp = static_cast<uint8_t*>(
                CodeCave::allocate(kTrampSize));
            if (!tramp) return false;

            size_t off = 0;
            const uint8_t prologue[7] =
                {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x10};
            std::memcpy(tramp + off, prologue, sizeof(prologue));
            off += sizeof(prologue);

            uint8_t jmp[5] {};
            if (!encode_jmp_rel32(
                    tramp + off, static_cast<uint8_t*>(site) + 7, jmp))
                return false;
            std::memcpy(tramp + off, jmp, sizeof(jmp));
            off += sizeof(jmp);
            ::FlushInstructionCache(::GetCurrentProcess(), tramp, off);

            m_bypass = reinterpret_cast<RollbackPerFrameTickFn>(tramp);
            m_trampoline_address = reinterpret_cast<uintptr_t>(tramp);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[RollbackLab] direct PerFrameTick bypass ready "
                "tramp=0x{:X} target=0x{:X}\n"),
                static_cast<unsigned long long>(m_trampoline_address),
                static_cast<unsigned long long>(
                    image_base + kRollbackRVA_LuxBattlePerFrameTick));
            return true;
        }

        RollbackNativeStepReport step(
            uintptr_t image_base,
            const RollbackInputPair& input,
            const uint8_t (&camera_args)[kRollbackCameraArgsBytes]) noexcept
        {
            RollbackNativeStepReport report {};
            report.failure = "ok";
            report.resolved = resolve(image_base);
            report.function_address =
                image_base + kRollbackRVA_LuxBattlePerFrameTick;
            report.trampoline_address = m_trampoline_address;
            report.input_p1 = input.p1;
            report.input_p2 = input.p2;
            if (!report.resolved)
            {
                report.failure = "per-frame-bypass-resolve-failed";
                return report;
            }

            uint64_t input_pair[2] = {input.p1, input.p2};
            report.input_write_ok =
                RollbackWriteLatestInputs(image_base, input_pair);
            report.camera_write_ok =
                RollbackWriteCameraArgs(image_base, camera_args);
            report.frame_counter_read_ok =
                RollbackReadFrameCounter(image_base, report.frame_before);
            if (!report.input_write_ok)
            {
                report.failure = "latest-input-write-failed";
                return report;
            }
            if (!report.camera_write_ok)
            {
                report.failure = "camera-args-write-failed";
                return report;
            }
            if (!report.frame_counter_read_ok)
            {
                report.failure = "frame-counter-read-before-failed";
                return report;
            }

            uintptr_t args[3] = {
                reinterpret_cast<uintptr_t>(&input_pair[0]),
                reinterpret_cast<uintptr_t>(&input_pair[1]),
                reinterpret_cast<uintptr_t>(camera_args),
            };
            if (!RollbackSafeInvokePerFrameTick(
                    m_bypass, args, &report.fault))
            {
                report.failure = "per-frame-tick-faulted";
                return report;
            }
            report.frame_counter_read_ok =
                RollbackReadFrameCounter(image_base, report.frame_after);
            if (!report.frame_counter_read_ok)
            {
                report.failure = "frame-counter-read-after-failed";
                return report;
            }
            if (report.frame_after != report.frame_before + 1)
            {
                report.failure = "frame-counter-delta-not-one";
                return report;
            }

            report.ok = true;
            return report;
        }

    private:
        RollbackPerFrameTickFn m_bypass {nullptr};
        uintptr_t m_trampoline_address {0};
    };

    static inline uint32_t RollbackNextPrng(uint32_t& state) noexcept
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    static inline void BuildRollbackInputSequence(
        uint32_t seed,
        uint32_t window,
        const uint64_t (&base_input)[2],
        RollbackInputPair (&out)[kRollbackMaxKFrameWindow]) noexcept
    {
        uint32_t s = seed ? seed : 0x5C6B0001u;
        for (uint32_t i = 0; i < window; ++i)
        {
            const uint8_t p1_bits =
                static_cast<uint8_t>(RollbackNextPrng(s) >> 24);
            const uint8_t p2_bits =
                static_cast<uint8_t>(RollbackNextPrng(s) >> 24);
            out[i].p1 = (base_input[0] & ~0xFFull) | p1_bits;
            out[i].p2 = (base_input[1] & ~0xFFull) | p2_bits;
        }
    }

    static inline bool RunRollbackStepSequence(
        RollbackNativeStepper& stepper,
        uintptr_t image_base,
        const RollbackInputPair (&inputs)[kRollbackMaxKFrameWindow],
        uint32_t window,
        const uint8_t (&camera_args)[kRollbackCameraArgsBytes],
        RollbackResimWindowReport& report,
        std::vector<RollbackHgCpuSnapshotFrame>* hgcpu_timeline = nullptr)
        noexcept
    {
        bool ok = true;
        for (uint32_t i = 0; i < window; ++i)
        {
            ++report.steps_attempted;
            RollbackNativeStepReport step =
                stepper.step(image_base, inputs[i], camera_args);
            if (step.ok)
            {
                ++report.steps_ok;
                if (hgcpu_timeline)
                {
                    RollbackHgCpuSnapshotFrame frame {};
                    if (!RollbackCaptureMotionBankHistory(image_base, frame))
                    {
                        ok = false;
                        report.failure =
                            "motion-bank-timeline-capture-failed";
                        break;
                    }
                    try
                    {
                        hgcpu_timeline->push_back(std::move(frame));
                    }
                    catch (const std::bad_alloc&)
                    {
                        ok = false;
                        report.failure = "hgcpu-timeline-alloc-failed";
                        break;
                    }
                }
                continue;
            }
            ok = false;
            report.failure = step.failure;
            break;
        }
        return ok;
    }

    static inline RollbackResimWindowReport RunRollbackResimWindowProbe(
        const RollbackSnapshotManifest& manifest,
        uint32_t requested_window,
        uint32_t seed,
        bool inject_fault) noexcept
    {
        RollbackResimWindowReport report {};
        report.failure = "ok";
        report.inject_fault = inject_fault;
        report.seed = seed;
        report.window = (std::min)(
            requested_window == 0 ? 1u : requested_window,
            kRollbackMaxKFrameWindow);
        report.fault_frame_index = report.window / 2u;

        const uintptr_t image_base = NativeBinding::imageBase();
        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        report.context_ready = RollbackReadCharaPointers(image_base, p1, p2);
        if (!report.context_ready)
        {
            report.failure = "battle-context-not-ready";
            return report;
        }
        if (manifest.epoch.chara[0] != p1
            || manifest.epoch.chara[1] != p2
            || !manifest.epoch.active_pvp())
        {
            report.failure = "lifecycle-epoch-not-current";
            return report;
        }

        RollbackNativeStepper stepper {};
        RollbackPaletteVariantWriterRegistry
            palette_writer_registry {};
        RollbackStepState start {};
        report.start_capture =
            CaptureRollbackStepState(
                image_base, manifest, start,
                RollbackLifecycleMode::StockOnlinePvp, false,
                nullptr, nullptr, nullptr, 0, nullptr,
                &palette_writer_registry);
        if (!report.start_capture.ok)
        {
            report.failure = report.start_capture.failure;
            return report;
        }
        report.start_frame = start.frame_counter;
        report.start_lfsr_index_ok = RollbackReadSnapshotLfsrIndex(
            manifest, start.explicit_snapshot, report.start_lfsr_index);
        RollbackScopedWindRngGate wind_rng_gate {};
        report.wind_rng_gate_resolved = wind_rng_gate.resolved();
        report.wind_rng_gate_enabled = wind_rng_gate.enabled();
        if (!report.wind_rng_gate_enabled)
        {
            report.failure = "wind-rng-gate-enable-failed";
            return report;
        }
        RngTraceHook& rng_trace = RngTraceHook::instance();
        const bool trace_rng_callers =
            ReplayDebugTrace::instance().enabled() && rng_trace.install();
        auto begin_rng_trace = [&]() noexcept {
            if (trace_rng_callers) rng_trace.begin_window();
        };
        auto end_rng_trace = [&](const char* label) noexcept {
            if (!trace_rng_callers) return;
            uint32_t current_frame = 0;
            (void)RollbackReadFrameCounter(image_base, current_frame);
            rng_trace.end_window_and_emit(
                "rollback_resim",
                label,
                static_cast<int32_t>(start.frame_counter),
                static_cast<int32_t>(current_frame),
                static_cast<int32_t>(report.window));
        };

        RollbackInputPair confirmed[kRollbackMaxKFrameWindow] {};
        RollbackInputPair predicted[kRollbackMaxKFrameWindow] {};
        BuildRollbackInputSequence(
            seed, report.window, start.latest_input, confirmed);
        std::memcpy(predicted, confirmed, sizeof(predicted));
        if (inject_fault && report.window > 0)
        {
            const uint32_t f = report.fault_frame_index;
            report.baseline_input_p2 = confirmed[f].p2;
            for (uint32_t i = f; i < report.window; ++i)
            {
                const uint64_t low =
                    (~confirmed[i].p2) & 0xFFull;
                predicted[i].p2 = (confirmed[i].p2 & ~0xFFull) | low;
                if (predicted[i].p2 == confirmed[i].p2)
                    predicted[i].p2 ^= 0xFFull;
            }
            report.predicted_input_p2 = predicted[f].p2;
        }

        auto reserve_motion_timeline =
            [&](std::vector<RollbackHgCpuSnapshotFrame>& timeline) noexcept
                -> bool {
            try
            {
                timeline.reserve(report.window);
                return true;
            }
            catch (const std::bad_alloc&)
            {
                report.failure = "motion-bank-timeline-alloc-failed";
                return false;
            }
        };
        auto restore_motion_timeline =
            [&](const std::vector<RollbackHgCpuSnapshotFrame>& timeline,
                const char* failure) noexcept -> bool {
            if (timeline.empty())
                return true;
            const bool ok = RollbackRestoreMotionBankHistoryFromTimeline(
                image_base,
                timeline.data(),
                timeline.size(),
                timeline.size() - 1);
            if (!ok)
                report.failure = failure;
            return ok;
        };

        std::vector<RollbackHgCpuSnapshotFrame> baseline_motion_timeline;
        std::vector<RollbackHgCpuSnapshotFrame> predicted_motion_timeline;
        std::vector<RollbackHgCpuSnapshotFrame> corrected_motion_timeline;
        if (!reserve_motion_timeline(baseline_motion_timeline)
            || (inject_fault
                && !reserve_motion_timeline(predicted_motion_timeline))
            || !reserve_motion_timeline(corrected_motion_timeline))
        {
            return report;
        }

        begin_rng_trace();
        report.baseline_ok = RunRollbackStepSequence(
            stepper, image_base, confirmed, report.window, start.camera_args,
            report, &baseline_motion_timeline);
        end_rng_trace("baseline");
        if (report.baseline_ok)
        {
            report.baseline_ok = restore_motion_timeline(
                baseline_motion_timeline,
                "baseline-motion-bank-history-restore-failed");
        }
        RollbackStepState baseline {};
        report.baseline_capture =
            CaptureRollbackStepState(
                image_base, manifest, baseline,
                RollbackLifecycleMode::StockOnlinePvp, false,
                nullptr, nullptr, nullptr, 0, nullptr,
                &palette_writer_registry);
        report.baseline_ok =
            report.baseline_ok && report.baseline_capture.ok;
        report.baseline_frame = baseline.frame_counter;
        report.baseline_hash = baseline.combined_hash;
        report.baseline_explicit_hash = baseline.explicit_snapshot.hash;
        report.baseline_lfsr_index_ok = RollbackReadSnapshotLfsrIndex(
            manifest, baseline.explicit_snapshot,
            report.baseline_lfsr_index);

        RollbackStepStateReport restore_report =
            RestoreRollbackStepState(
                image_base, start, true,
                RollbackLifecycleMode::StockOnlinePvp, false, true,
                nullptr, nullptr, nullptr, &palette_writer_registry);
        report.post_baseline_restore = restore_report;
        if (!restore_report.ok)
        {
            report.failure = restore_report.failure;
            return report;
        }
        RollbackSnapshotFrame post_baseline_restore_explicit {};
        RollbackSnapshotCopyReport post_baseline_restore_capture =
            CaptureRollbackSnapshotBytes(
                manifest, post_baseline_restore_explicit);
        report.post_baseline_restore_explicit_ok =
            post_baseline_restore_capture.ok;
        report.post_baseline_restore_explicit_hash =
            post_baseline_restore_explicit.hash;
        report.post_baseline_restore_explicit_match =
            post_baseline_restore_capture.ok
            && start.explicit_snapshot.hash != 0
            && post_baseline_restore_explicit.hash
                == start.explicit_snapshot.hash;
        report.post_baseline_restore_lfsr_index_ok =
            RollbackReadSnapshotLfsrIndex(
                manifest,
                post_baseline_restore_explicit,
                report.post_baseline_restore_lfsr_index);

        RollbackStepState predicted_end {};
        if (inject_fault)
        {
            begin_rng_trace();
            report.predicted_ok = RunRollbackStepSequence(
                stepper, image_base, predicted, report.window,
                start.camera_args, report, &predicted_motion_timeline);
            end_rng_trace("predicted");
            if (report.predicted_ok)
            {
                report.predicted_ok = restore_motion_timeline(
                    predicted_motion_timeline,
                    "predicted-motion-bank-history-restore-failed");
            }
            report.predicted_capture =
                CaptureRollbackStepState(
                    image_base, manifest, predicted_end,
                    RollbackLifecycleMode::StockOnlinePvp, false,
                    nullptr, nullptr, nullptr, 0, nullptr,
                    &palette_writer_registry);
            report.predicted_ok =
                report.predicted_ok && report.predicted_capture.ok;
            report.predicted_frame = predicted_end.frame_counter;
            report.predicted_hash = predicted_end.combined_hash;
            report.predicted_explicit_hash =
                predicted_end.explicit_snapshot.hash;
            report.predicted_lfsr_index_ok = RollbackReadSnapshotLfsrIndex(
                manifest, predicted_end.explicit_snapshot,
                report.predicted_lfsr_index);
            report.predicted_compare =
                CompareRollbackHgCpuFrames(baseline.hgcpu, predicted_end.hgcpu);
            report.predicted_differs_from_baseline =
                !report.predicted_compare.policy_match
                || baseline.explicit_snapshot.hash
                    != predicted_end.explicit_snapshot.hash
                || baseline.frame_counter != predicted_end.frame_counter;

            restore_report = RestoreRollbackStepState(
                image_base, start, true,
                RollbackLifecycleMode::StockOnlinePvp, false, true,
                nullptr, nullptr, nullptr, &palette_writer_registry);
            report.post_predicted_restore = restore_report;
            if (!restore_report.ok)
            {
                report.failure = restore_report.failure;
                return report;
            }
            RollbackSnapshotFrame post_predicted_restore_explicit {};
            RollbackSnapshotCopyReport post_predicted_restore_capture =
                CaptureRollbackSnapshotBytes(
                    manifest, post_predicted_restore_explicit);
            report.post_predicted_restore_explicit_ok =
                post_predicted_restore_capture.ok;
            report.post_predicted_restore_explicit_hash =
                post_predicted_restore_explicit.hash;
            report.post_predicted_restore_explicit_match =
                post_predicted_restore_capture.ok
                && start.explicit_snapshot.hash != 0
                && post_predicted_restore_explicit.hash
                    == start.explicit_snapshot.hash;
            report.post_predicted_restore_lfsr_index_ok =
                RollbackReadSnapshotLfsrIndex(
                    manifest,
                    post_predicted_restore_explicit,
                    report.post_predicted_restore_lfsr_index);
        }
        else
        {
            report.predicted_ok = true;
            report.post_predicted_restore = report.post_baseline_restore;
            report.post_predicted_restore_explicit_ok =
                report.post_baseline_restore_explicit_ok;
            report.post_predicted_restore_explicit_match =
                report.post_baseline_restore_explicit_match;
            report.post_predicted_restore_explicit_hash =
                report.post_baseline_restore_explicit_hash;
            report.post_predicted_restore_lfsr_index_ok =
                report.post_baseline_restore_lfsr_index_ok;
            report.post_predicted_restore_lfsr_index =
                report.post_baseline_restore_lfsr_index;
        }

        begin_rng_trace();
        report.corrected_ok = RunRollbackStepSequence(
            stepper, image_base, confirmed, report.window, start.camera_args,
            report, &corrected_motion_timeline);
        end_rng_trace("corrected");
        if (report.corrected_ok)
        {
            report.corrected_ok = restore_motion_timeline(
                corrected_motion_timeline,
                "corrected-motion-bank-history-restore-failed");
        }
        RollbackStepState corrected {};
        report.corrected_capture =
            CaptureRollbackStepState(
                image_base, manifest, corrected,
                RollbackLifecycleMode::StockOnlinePvp, false,
                nullptr, nullptr, nullptr, 0, nullptr,
                &palette_writer_registry);
        report.corrected_ok =
            report.corrected_ok && report.corrected_capture.ok;
        report.corrected_frame = corrected.frame_counter;
        report.corrected_hash = corrected.combined_hash;
        report.corrected_explicit_hash = corrected.explicit_snapshot.hash;
        report.corrected_lfsr_index_ok = RollbackReadSnapshotLfsrIndex(
            manifest, corrected.explicit_snapshot,
            report.corrected_lfsr_index);
        report.corrected_compare =
            CompareRollbackHgCpuFrames(baseline.hgcpu, corrected.hgcpu);

        report.explicit_match =
            baseline.explicit_snapshot.hash != 0
            && baseline.explicit_snapshot.hash
                == corrected.explicit_snapshot.hash;
        if (!report.explicit_match)
        {
            DescribeRollbackExplicitMismatch(
                manifest,
                baseline.explicit_snapshot,
                corrected.explicit_snapshot,
                report);
        }
        report.hgcpu_policy_match = report.corrected_compare.policy_match;
        report.frame_counter_match =
            baseline.frame_counter == corrected.frame_counter;
        report.frame_counter_delta_ok =
            baseline.frame_counter == report.start_frame + report.window
            && corrected.frame_counter == report.start_frame + report.window;
        report.corrected_matches_baseline =
            report.explicit_match
            && report.hgcpu_policy_match
            && report.frame_counter_match;
        report.all_steps_ok = report.steps_attempted == report.steps_ok;

        restore_report = RestoreRollbackStepState(
            image_base, start, true,
            RollbackLifecycleMode::StockOnlinePvp, false, true,
            nullptr, nullptr, nullptr, &palette_writer_registry);
        report.final_restore = restore_report;
        report.restore_start_after_ok = restore_report.ok;
        if (!report.restore_start_after_ok)
            report.failure = restore_report.failure;

        report.ok =
            report.baseline_ok
            && report.predicted_ok
            && report.corrected_ok
            && report.restore_start_after_ok
            && report.post_baseline_restore_explicit_ok
            && report.post_baseline_restore_explicit_match
            && report.post_predicted_restore_explicit_ok
            && report.post_predicted_restore_explicit_match
            && report.corrected_matches_baseline
            && report.frame_counter_delta_ok
            && report.all_steps_ok
            && (!report.inject_fault
                || report.predicted_differs_from_baseline);
        if (!report.ok && std::strcmp(report.failure, "ok") == 0)
        {
            report.failure =
                report.inject_fault
                    && !report.predicted_differs_from_baseline
                ? "prediction-did-not-diverge"
                : "resim-compare-failed";
        }
        return report;
    }
}
