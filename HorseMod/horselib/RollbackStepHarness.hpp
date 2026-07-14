// ============================================================================
// Horse::RollbackStepHarness
//
// Active-round rollback validation for the lab. This module owns the smallest
// native resimulation proof: capture a stable state, step K deterministic input
// frames through LuxBattle_PerFrameTick, restore, replay the same frames, and
// compare the final state. It deliberately does not remove local input delay.
// ============================================================================

#pragma once

#include "CodeCave.hpp"
#include "NativeBinding.hpp"
#include "ReplayDebugTrace.hpp"
#include "RngTraceHook.hpp"
#include "RollbackHgCpuSnapshot.hpp"
#include "RollbackInputHistory.hpp"
#include "RollbackLifecycle.hpp"
#include "RollbackSnapshot.hpp"
#include "RollbackStageSnapshot.hpp"
#include "RollbackStageWindSnapshot.hpp"
#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"
#include "WindRngGate.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

    struct RollbackStepState
    {
        RollbackHgCpuSnapshotFrame hgcpu {};
        RollbackSnapshotFrame explicit_snapshot {};
        RollbackBreakableStageSnapshot breakable_stage {};
        RollbackStageWindSnapshot stage_wind {};
        uint32_t frame_counter {0};
        uint64_t latest_input[2] {};
        uint8_t camera_args[kRollbackCameraArgsBytes] {};
        uint64_t canonical_hash {0};
        uint64_t combined_hash {0};
    };

    struct RollbackStepStateReport
    {
        bool ok {false};
        bool frame_counter_ok {false};
        bool latest_input_ok {false};
        bool camera_args_ok {false};
        bool explicit_ok {false};
        bool hgcpu_ok {false};
        bool breakable_stage_ok {false};
        bool stage_wind_ok {false};
        bool emergency_captured {false};
        bool emergency_restored {false};
        bool verification_ok {false};
        uint32_t frame_counter {0};
        uint64_t combined_hash {0};
        RollbackSnapshotCopyReport explicit_report {};
        RollbackHgCpuSnapshotReport hgcpu_report {};
        RollbackBreakableStageReport breakable_stage_report {};
        RollbackStageWindSnapshotReport stage_wind_report {};
        RollbackHgCpuFrameCompare verification_hgcpu_compare {};
        uint64_t verification_canonical_hash {0};
        uint64_t expected_canonical_hash {0};
        uint64_t verification_explicit_hash {0};
        uint64_t expected_explicit_hash {0};
        uint64_t verification_stage_hash {0};
        uint64_t expected_stage_hash {0};
        const char* failure {"not-run"};
    };

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
        h.add_scalar(state.explicit_snapshot.hash);
        h.add_scalar(state.breakable_stage.integrity_hash);
        h.add_scalar(state.stage_wind.integrity_hash);
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
        h.add_scalar(state.explicit_snapshot.canonical_hash);
        h.add_scalar(state.breakable_stage.canonical_hash);
        h.add_scalar(state.stage_wind.canonical_hash);
        h.add_scalar(state.frame_counter);
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
        const uint64_t byte_hash = RollbackHashBytes(
            frame.bytes.data(), effective);
        const uint64_t khit_hash = RollbackHashKHitTopology(frame);
        const uint64_t motion_bank_hash =
            RollbackHashMotionBankHistory(frame.motion_banks);
        const uint64_t motion_tail_hash =
            RollbackHashMotionTailHistory(frame.motion_tail);
        const uint64_t secondary_event_stack_hash =
            RollbackHashSecondaryEventStackHistory(
                frame.secondary_event_stack);
        const uint64_t skeleton_runtime_hash =
            frame.skeleton_runtime.ok
                ? RollbackHashSkeletonRuntimeHistory(frame.skeleton_runtime)
                : 0;
        const uint64_t timer_node_hash =
            RollbackHashTimerNodeHistory(frame.timer_node);
        const uint64_t combined = RollbackHashCombine(
            RollbackHashCombine(
                RollbackHashCombine(
                    RollbackHashCombine(byte_hash, khit_hash),
                    motion_bank_hash),
                RollbackHashCombine(
                    motion_tail_hash,
                    secondary_event_stack_hash)),
            RollbackHashCombine(
                skeleton_runtime_hash, timer_node_hash));
        return byte_hash == frame.byte_hash
            && khit_hash == frame.khit_topology_hash
            && motion_bank_hash == frame.motion_bank_hash
            && motion_tail_hash == frame.motion_tail_hash
            && secondary_event_stack_hash
                == frame.secondary_event_stack_hash
            && skeleton_runtime_hash == frame.skeleton_runtime_hash
            && timer_node_hash == frame.timer_node_hash
            && combined == frame.hash
            && RollbackHashHgCpuCanonical(frame) == frame.canonical_hash;
    }

    static inline bool ValidateRollbackStepStateIntegrity(
        const RollbackStepState& state) noexcept
    {
        if (state.explicit_snapshot.integrity_hash == 0
            || state.explicit_snapshot.hash
                != state.explicit_snapshot.integrity_hash
            || HashRollbackSnapshotFrame(state.explicit_snapshot)
                != state.explicit_snapshot.integrity_hash
            || HashRollbackSnapshotCanonical(state.explicit_snapshot)
                != state.explicit_snapshot.canonical_hash
            || !ValidateRollbackHgCpuFrameIntegrity(state.hgcpu))
            return false;
        const RollbackBreakableStageHashes stage =
            ComputeRollbackBreakableStageHashes(state.breakable_stage);
        if (stage.integrity_hash != state.breakable_stage.integrity_hash
            || stage.canonical_hash != state.breakable_stage.canonical_hash
            || stage.stage_layout_digest
                != state.breakable_stage.stage_layout_digest
            || stage.actor_set_digest != state.breakable_stage.actor_set_digest)
            return false;
        if (HashRollbackStageWindIntegrity(state.stage_wind)
                != state.stage_wind.integrity_hash
            || HashRollbackStageWindCanonical(state.stage_wind)
                != state.stage_wind.canonical_hash)
            return false;
        return HashRollbackStepState(state) == state.combined_hash
            && HashRollbackStepStateCanonical(state) == state.canonical_hash;
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
        RollbackLifecycleEpoch live {};
        if (!CaptureRollbackLifecycleEpoch(image_base, live)) return false;
        live.generation = expected.generation;
        return ValidateRollbackLifecycleEpoch(expected, live, mode).ok;
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

    static inline RollbackStepStateReport CaptureRollbackStepState(
        uintptr_t image_base,
        const RollbackSnapshotManifest& manifest,
        RollbackStepState& out,
        RollbackLifecycleMode mode =
            RollbackLifecycleMode::StockOnlinePvp,
        bool replay_fork_lab = false) noexcept
    {
        RollbackStepStateReport report {};
        report.failure = "ok";
        out = {};

        if (!ValidateRollbackStepLifecycle(
                image_base, manifest.epoch, mode, replay_fork_lab))
        {
            report.failure = "capture-lifecycle-preflight-failed";
            return report;
        }

        report.explicit_report = CaptureRollbackSnapshotBytes(
            manifest, out.explicit_snapshot);
        report.explicit_ok = report.explicit_report.ok;
        if (!report.explicit_ok)
        {
            report.failure = report.explicit_report.failure;
            return report;
        }

        report.hgcpu_report = CaptureRollbackHgCpuSnapshot(
            image_base, out.hgcpu);
        report.hgcpu_ok = report.hgcpu_report.ok;

        const bool lifecycle_still_valid =
            ValidateRollbackStepLifecycle(
                image_base, manifest.epoch, mode, replay_fork_lab);
        // The native writer mutates several globals also owned by the explicit
        // snapshot. Always attempt cleanup before returning, even when the
        // native call failed or the lifecycle changed during capture.
        const RollbackSnapshotCopyReport post_hgcpu_explicit_restore =
            RestoreRollbackSnapshotBytes(out.explicit_snapshot);
        if (!post_hgcpu_explicit_restore.ok)
        {
            report.failure = "explicit-post-hgcpu-restore-failed";
            report.explicit_report = post_hgcpu_explicit_restore;
            report.explicit_ok = false;
            return report;
        }
        if (!lifecycle_still_valid)
        {
            report.failure = "capture-lifecycle-before-explicit-restore-failed";
            return report;
        }
        if (!report.hgcpu_ok)
        {
            report.failure = report.hgcpu_report.failure;
            return report;
        }

        report.breakable_stage_report =
            CaptureRollbackBreakableStageSnapshot(
                manifest.epoch.stage_actor_manager,
                out.breakable_stage);
        report.breakable_stage_ok = report.breakable_stage_report.ok;
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
            image_base, out.stage_wind);
        report.stage_wind_ok = report.stage_wind_report.ok;
        if (!report.stage_wind_ok)
        {
            report.failure = report.stage_wind_report.failure;
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

        out.canonical_hash = HashRollbackStepStateCanonical(out);
        out.combined_hash = HashRollbackStepState(out);
        if (out.canonical_hash == 0)
        {
            report.failure = "canonical-step-hash-failed";
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
        bool replay_fork_lab = false) noexcept
    {
        RollbackStepStateReport report {};
        report.failure = "ok";

        // Validate the complete source and live target epoch before emergency
        // capture or any HgCpu/KHit/native write occurs.
        if (!ValidateRollbackStepStateIntegrity(state))
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

        auto capture_current = [&](RollbackStepState& current) noexcept {
            current = {};
            if (!CaptureRollbackEmergencyFrame(
                    state.explicit_snapshot,
                    current.explicit_snapshot))
            {
                return false;
            }
            const RollbackHgCpuSnapshotReport hgcpu =
                CaptureRollbackHgCpuSnapshot(image_base, current.hgcpu);
            RollbackSnapshotCopyReport explicit_restore =
                RestoreRollbackSnapshotBytes(current.explicit_snapshot);
            if (!explicit_restore.ok)
                return false;
            if (!hgcpu.ok)
                return false;
            const RollbackBreakableStageReport stage =
                CaptureRollbackBreakableStageSnapshot(
                    state.explicit_snapshot.epoch.stage_actor_manager,
                    current.breakable_stage);
            if (!stage.ok
                || !RollbackReadFrameCounter(
                    image_base, current.frame_counter)
                || !RollbackReadLatestInputs(
                    image_base, current.latest_input)
                || !RollbackReadCameraArgs(
                    image_base, current.camera_args))
            {
                return false;
            }
            const RollbackStageWindSnapshotReport wind =
                CaptureRollbackStageWindSnapshot(
                    image_base, current.stage_wind);
            if (!wind.ok)
                return false;
            current.canonical_hash = HashRollbackStepStateCanonical(current);
            current.combined_hash = HashRollbackStepState(current);
            return true;
        };

        RollbackStepState emergency {};
        if (allow_emergency_restore)
        {
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
                        replay_fork_lab);
                report.emergency_restored = recovery.ok;
            }
            return report;
        };

        report.hgcpu_report = RestoreRollbackHgCpuSnapshot(
            image_base, state.hgcpu);
        report.hgcpu_ok = report.hgcpu_report.ok;
        if (!report.hgcpu_ok)
        {
            return recover(report.hgcpu_report.failure);
        }

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
            if (!CaptureRollbackLifecycleEpoch(image_base, live_epoch))
                return recover("lifecycle-epoch-capture-failed");
            live_epoch.generation =
                state.explicit_snapshot.epoch.generation;
        }
        report.explicit_report =
            RestoreRollbackSnapshotBytesIfEpochMatches(
                state.explicit_snapshot, live_epoch, replay_fork_lab);
        report.explicit_ok = report.explicit_report.ok;
        if (!report.explicit_ok)
        {
            return recover(report.explicit_report.failure);
        }

        report.breakable_stage_report =
            RestoreRollbackBreakableStageSnapshot(
                state.explicit_snapshot.epoch.stage_actor_manager,
                state.breakable_stage);
        report.breakable_stage_ok = report.breakable_stage_report.ok;
        if (!report.breakable_stage_ok)
            return recover(report.breakable_stage_report.failure);

        report.stage_wind_report = RestoreRollbackStageWindSnapshot(
            image_base, state.stage_wind);
        report.stage_wind_ok = report.stage_wind_report.ok;
        if (!report.stage_wind_ok)
            return recover(report.stage_wind_report.failure);

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

        // Verification is mandatory even for the non-recursive emergency
        // recovery pass. Otherwise a partial recovery write can be reported
        // as successful merely because a second recovery is disabled.
        {
            RollbackStepState verification {};
            if (!capture_current(verification))
            {
                return recover("step-post-restore-verification-failed");
            }
            const RollbackHgCpuFrameCompare hgcpu_verify =
                CompareRollbackHgCpuFrames(
                    state.hgcpu, verification.hgcpu);
            report.verification_hgcpu_compare = hgcpu_verify;
            report.verification_canonical_hash = verification.canonical_hash;
            report.expected_canonical_hash = state.canonical_hash;
            report.verification_explicit_hash =
                verification.explicit_snapshot.canonical_hash;
            report.expected_explicit_hash =
                state.explicit_snapshot.canonical_hash;
            report.verification_stage_hash =
                verification.breakable_stage.canonical_hash;
            report.expected_stage_hash =
                state.breakable_stage.canonical_hash;
            // The raw topology integrity hash contains KHit node/chara
            // addresses and list-control allocator links. A long replay-fork
            // advance can legitimately replace those process-local nodes
            // while restoring the same ordered canonical node stream. The
            // lab therefore verifies the complete canonical HgCpu digest.
            // CompareRollbackHgCpuFrames' raw offset diagnostics are not a
            // validity condition here because a changed variable-length KHit
            // record shifts the following P2/global bytes even when their
            // canonical per-record content is identical. Production retains
            // the stronger same-object topology/raw-policy check.
            const bool hgcpu_restore_match = replay_fork_lab
                ? (verification.hgcpu.canonical_hash
                        == state.hgcpu.canonical_hash)
                : hgcpu_verify.policy_match;
            if (!hgcpu_restore_match
                || verification.explicit_snapshot.canonical_hash
                    != state.explicit_snapshot.canonical_hash
                || verification.breakable_stage.canonical_hash
                    != state.breakable_stage.canonical_hash
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
        RollbackStepState start {};
        report.start_capture =
            CaptureRollbackStepState(image_base, manifest, start);
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
            CaptureRollbackStepState(image_base, manifest, baseline);
        report.baseline_ok =
            report.baseline_ok && report.baseline_capture.ok;
        report.baseline_frame = baseline.frame_counter;
        report.baseline_hash = baseline.combined_hash;
        report.baseline_explicit_hash = baseline.explicit_snapshot.hash;
        report.baseline_lfsr_index_ok = RollbackReadSnapshotLfsrIndex(
            manifest, baseline.explicit_snapshot,
            report.baseline_lfsr_index);

        RollbackStepStateReport restore_report =
            RestoreRollbackStepState(image_base, start);
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
                CaptureRollbackStepState(image_base, manifest, predicted_end);
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

            restore_report = RestoreRollbackStepState(image_base, start);
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
            CaptureRollbackStepState(image_base, manifest, corrected);
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

        restore_report = RestoreRollbackStepState(image_base, start);
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
