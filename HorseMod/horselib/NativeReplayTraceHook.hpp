// ============================================================================
// Horse::NativeReplayTraceHook
//
// Lightweight PolyHook probes for SC6's native replay launch path.  This is
// the in-process equivalent of the x64dbg breakpoint plan: it records the
// order and key arguments/return values while the user drives the stock replay
// UI, without changing game behavior.
// ============================================================================

#pragma once

#include "RollbackScheduledBarrier.hpp"
#include "RollbackMotionDecodeScratch.hpp"
#include "RollbackMotionPoseResidue.hpp"
#include "RollbackMotionDecodeTraceWindow.hpp"
#include "ReplayInputPairAuthority.hpp"

#include "GameMode.hpp"
#include "NativeBinding.hpp"
#include "ReplayDebugTrace.hpp"
#include "ReplayScrubDiag.hpp"
#include "RngTraceHook.hpp"
#include "RollbackNativeSimulationIteration.hpp"
#include "RollbackCharaAnimationState.hpp"
#include "RollbackPoseProducerTraceContract.hpp"
#include "RollbackReplayTraceDiagnostics.hpp"
#include "RollbackRootMotionTraceContract.hpp"
#include "RollbackStageWindSnapshot.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <polyhook2/Detour/x64Detour.hpp>

#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <intrin.h>
#include <memory>
#include <mutex>
#include <string>

#ifndef HORSEMOD_ENABLE_EXTENDED_NATIVE_REPLAY_TRACE
#define HORSEMOD_ENABLE_EXTENDED_NATIVE_REPLAY_TRACE 0
#endif

#ifndef HORSEMOD_ENABLE_REPLAY_INPUT_STAGE_TRACE
#define HORSEMOD_ENABLE_REPLAY_INPUT_STAGE_TRACE 1
#endif

#ifndef HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
#define HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE 1
#endif

#ifndef HORSEMOD_ENABLE_REPLAY_MOVEVM_TRANSITION_TRACE
// LuxMoveVM_TransitionToMove consumes live XMM3 state not represented by the
// current C++ detour signature. Keep this off until it has an assembly thunk.
#define HORSEMOD_ENABLE_REPLAY_MOVEVM_TRANSITION_TRACE 0
#endif

namespace Horse
{
    ReplayInputPairRepairResult
    replay_scrub_repair_latest_engine_input_before_chara_input(
        void* chara) noexcept;
    extern std::atomic<bool>
        g_replay_scrub_generation_diagnostics_suppressed;
    void replay_scrub_append_secondary_action_stack_push_trace_context(
        ReplayTraceFields& f) noexcept;
    bool replay_scrub_read_native_trace_position(
        int32_t& sequence,
        int32_t& round,
        int32_t& master) noexcept;
    bool replay_scrub_repair_secondary_action_stack_last_variant_before_random_push(
        void* stack_base) noexcept;
    bool replay_scrub_repair_secondary_action_stack_last_variant_before_chara_input(
        void* chara) noexcept;
    void replay_scrub_note_tick_chara_main_simulation_exit(
        void* chara) noexcept;
    void replay_scrub_note_hit_resolution_exit() noexcept;
    bool replay_scrub_capture_native_replay_entry_payload(
        void* container,
        uint64_t request_id,
        const void* payload_data,
        size_t payload_size) noexcept;

    class NativeReplayTraceHook
    {
    public:
        using StockBattleAssetReleaseCallback = bool (*)(void*) noexcept;

        enum class ReplayLifecycleTraceOwner : uint32_t
        {
            Legacy = 1u << 0,
            TimelineGeneration = 1u << 1,
            SeekTest = 1u << 2,
            RollbackDiagnostics = 1u << 3,
            FrameInputLogDiagnostics = 1u << 4,
        };

        static NativeReplayTraceHook& instance()
        {
            static NativeReplayTraceHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base)
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[NativeReplayTraceHook] NativeBinding image base is null; "
                    "cannot install replay trace hooks\n"));
                return false;
            }

            // These two hooks participate in replay/rollback state, so reject
            // an unknown executable before installing any detour.
            static constexpr std::array<uint8_t, 12>
                kSolveBonePosePrefix {
                    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56,
                    0x57, 0x41, 0x54, 0x41, 0x55,
                };
            static constexpr std::array<uint8_t, 13>
                kSampleKeyframeTransformsPrefix {
                    0x4C, 0x8B, 0xDC, 0x53, 0x55, 0x41, 0x54,
                    0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                };
            static constexpr std::array<uint8_t, 14>
                kWritebackScaledBoneTransformsPrefix {
                    0x4C, 0x8B, 0xDC, 0x55, 0x57, 0x41, 0x55,
                    0x48, 0x81, 0xEC, 0xB0, 0x08, 0x00, 0x00,
                };
            // Verified against LuxBattleChara_UpdateRootMotionDeltasFromBone1
            // @ 0x1403043D0. This is executable-byte evidence for the
            // reverse-engineered target, unlike a static assertion that
            // merely repeats the RVA.
            static constexpr std::array<uint8_t, 15>
                kUpdateRootMotionDeltasPrefix {
                    0x48, 0x89, 0x5C, 0x24, 0x08,
                    0x48, 0x89, 0x74, 0x24, 0x10,
                    0x57, 0x48, 0x83, 0xEC, 0x20,
                };
            // Verified native producer chain:
            // TickCharaMainSimulation -> FinalizeTickPoseAndState ->
            // optional EvaluateBonePose -> collision/root-motion extraction.
            static constexpr std::array<uint8_t, 14>
                kTickCharaMainSimulationPrefix {
                    0x48, 0x8B, 0xC4, 0x57,
                    0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00,
                    0x48, 0x8B, 0xF9,
                };
            static constexpr std::array<uint8_t, 18>
                kFinalizeTickPoseAndStatePrefix {
                    0x48, 0x89, 0x5C, 0x24, 0x10,
                    0x48, 0x89, 0x6C, 0x24, 0x18,
                    0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
                };
            static constexpr std::array<uint8_t, 14>
                kEvaluateBonePosePrefix {
                    0x40, 0x55, 0x53, 0x57, 0x41, 0x55,
                    0x48, 0x8D, 0xAC, 0x24, 0xA8, 0xC7, 0xFF, 0xFF,
                };
            static constexpr std::array<uint8_t, 12>
                kTickHitResolutionPrefix {
                    0x48, 0x8B, 0xC4, 0x55, 0x41, 0x54,
                    0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                };
            static constexpr std::array<uint8_t, 15>
                kSolvePhysBodyCollisionPrefix {
                    0x48, 0x8B, 0xC4, 0x53, 0x55, 0x56, 0x57,
                    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                };
            static constexpr std::array<uint8_t, 17>
                kTickBothCharaCollisionPhysicsPrefix {
                    0x48, 0x89, 0x5C, 0x24, 0x08,
                    0x48, 0x89, 0x74, 0x24, 0x10,
                    0x48, 0x89, 0x7C, 0x24, 0x18,
                    0x41, 0x56,
                };
            std::array<uint8_t, kSolveBonePosePrefix.size()> live_solve {};
            std::array<uint8_t, kSampleKeyframeTransformsPrefix.size()>
                live_sample {};
            std::array<uint8_t,
                       kWritebackScaledBoneTransformsPrefix.size()>
                live_pose_writeback {};
            std::array<uint8_t, kUpdateRootMotionDeltasPrefix.size()>
                live_root_motion {};
            std::array<uint8_t, kTickCharaMainSimulationPrefix.size()>
                live_tick_chara_main {};
            std::array<uint8_t, kFinalizeTickPoseAndStatePrefix.size()>
                live_finalize_pose {};
            std::array<uint8_t, kEvaluateBonePosePrefix.size()>
                live_evaluate_pose {};
            std::array<uint8_t, kTickHitResolutionPrefix.size()>
                live_hit_resolution {};
            std::array<uint8_t, kSolvePhysBodyCollisionPrefix.size()>
                live_body_collision {};
            std::array<uint8_t,
                       kTickBothCharaCollisionPhysicsPrefix.size()>
                live_chara_collision {};
            if (!safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kSolveBonePoseRVA),
                    live_solve.data(), live_solve.size())
                || live_solve != kSolveBonePosePrefix
                || !safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kSampleKeyframeTransformsRVA),
                    live_sample.data(), live_sample.size())
                || live_sample != kSampleKeyframeTransformsPrefix
                || !safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kWritebackScaledBoneTransformsRVA),
                    live_pose_writeback.data(),
                    live_pose_writeback.size())
                || live_pose_writeback
                    != kWritebackScaledBoneTransformsPrefix
                || !safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kUpdateRootMotionDeltasFromBone1RVA),
                    live_root_motion.data(), live_root_motion.size())
                || live_root_motion != kUpdateRootMotionDeltasPrefix
                || !safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kTickCharaMainSimulationRVA),
                    live_tick_chara_main.data(),
                    live_tick_chara_main.size())
                || live_tick_chara_main
                    != kTickCharaMainSimulationPrefix
                || !safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kFinalizeTickPoseAndStateRVA),
                    live_finalize_pose.data(),
                    live_finalize_pose.size())
                || live_finalize_pose != kFinalizeTickPoseAndStatePrefix
                || !safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kEvaluateBonePoseRVA),
                    live_evaluate_pose.data(),
                    live_evaluate_pose.size())
                || live_evaluate_pose != kEvaluateBonePosePrefix
                || !safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kTickHitResolutionAndBodyCollisionRVA),
                    live_hit_resolution.data(),
                    live_hit_resolution.size())
                || live_hit_resolution != kTickHitResolutionPrefix
                || !safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kSolvePhysBodyCollisionRVA),
                    live_body_collision.data(),
                    live_body_collision.size())
                || live_body_collision != kSolvePhysBodyCollisionPrefix
                || !safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kTickBothCharaCollisionPhysicsRVA),
                    live_chara_collision.data(),
                    live_chara_collision.size())
                || live_chara_collision
                    != kTickBothCharaCollisionPhysicsPrefix)
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[NativeReplayTraceHook] motion/root target signature "
                    "mismatch; refusing replay trace hooks\n"));
                return false;
            }

            bool ok = true;
            ok &= hook(Slot::LoadReplaySaveExec,
                       base + kLoadReplaySaveExecRVA,
                       &NativeReplayTraceHook::detour_load_replay_save_exec);
            ok &= hook(Slot::DoesReplaySaveExistExec,
                       base + kDoesReplaySaveExistExecRVA,
                       &NativeReplayTraceHook::detour_does_replay_save_exist_exec);
            ok &= hook(Slot::SaveReplayToSlotExec,
                       base + kSaveReplayToSlotExecRVA,
                       &NativeReplayTraceHook::detour_save_replay_to_slot_exec);
            ok &= hook(Slot::GetReplaySaveManager,
                       base + kGetReplaySaveManagerRVA,
                       &NativeReplayTraceHook::detour_get_replay_save_manager);
            ok &= hook(Slot::ApplyBattleSettings,
                       base + kApplyBattleSettingsRVA,
                       &NativeReplayTraceHook::detour_apply_battle_settings);
            ok &= hook(Slot::ApplyReplayToBattleSetup,
                       base + kApplyReplayToBattleSetupRVA,
                       &NativeReplayTraceHook::detour_apply_replay_to_battle_setup);
            ok &= hook(Slot::RequestBattleAsset,
                       base + kRequestBattleAssetRVA,
                       &NativeReplayTraceHook::detour_request_battle_asset);
            ok &= hook(Slot::HasAnyBattleRequest,
                       base + kHasAnyBattleRequestRVA,
                       &NativeReplayTraceHook::detour_has_any_battle_request);
            ok &= hook(Slot::CanLaunchBattleManually,
                       base + kCanLaunchBattleManuallyRVA,
                       &NativeReplayTraceHook::detour_can_launch_battle_manually);
            ok &= hook(Slot::ManualLaunchBattle,
                       base + kManualLaunchBattleRVA,
                       &NativeReplayTraceHook::detour_manual_launch_battle);
            ok &= hook(Slot::QuickBattleRequestExec,
                       base + kQuickBattleRequestExecRVA,
                       &NativeReplayTraceHook::detour_quick_battle_request_exec);
            ok &= hook(Slot::SetActiveStageMapPath,
                       base + kSetActiveStageMapPathRVA,
                       &NativeReplayTraceHook::detour_set_active_stage_map_path);
            ok &= hook(Slot::DeferredStageMapPathCallback,
                       base + kDeferredStageMapPathCallbackRVA,
                       &NativeReplayTraceHook::detour_deferred_stage_map_path_callback);

#if HORSEMOD_ENABLE_EXTENDED_NATIVE_REPLAY_TRACE
            ok &= hook(Slot::ReplayListRequestReplayFileExec,
                       base + kReplayListRequestReplayFileExecRVA,
                       &NativeReplayTraceHook::detour_replay_list_request_replay_file_exec);
            ok &= hook(Slot::ReplayListRequestReadyReplayExec,
                       base + kReplayListRequestReadyReplayExecRVA,
                       &NativeReplayTraceHook::detour_replay_list_request_ready_replay_exec);
            ok &= hook(Slot::ReplayListOnRequestPlayExec,
                       base + kReplayListOnRequestPlayExecRVA,
                       &NativeReplayTraceHook::detour_replay_list_on_request_play_exec);
            ok &= hook(Slot::ReplayListApplyTemporaryDataExec,
                       base + kReplayListApplyTemporaryDataExecRVA,
                       &NativeReplayTraceHook::detour_replay_list_apply_temporary_data_exec);
            ok &= hook(Slot::HandleReplayFileRequest,
                       base + kHandleReplayFileRequestRVA,
                       &NativeReplayTraceHook::detour_handle_replay_file_request);
            ok &= hook(Slot::DecompressUlx1EntryBlob,
                       base + kDecompressUlx1EntryBlobRVA,
                       &NativeReplayTraceHook::detour_decompress_ulx1_entry_blob);
            ok &= hook(Slot::DeserializeEntryPayloadToListItem,
                       base + kDeserializeEntryPayloadToListItemRVA,
                       &NativeReplayTraceHook::detour_deserialize_entry_payload_to_list_item);
#endif
            ok &= hook(Slot::HandleReplayFileRequestComplete,
                       base + kHandleReplayFileRequestCompleteRVA,
                       &NativeReplayTraceHook::detour_handle_replay_file_request_complete);

#if HORSEMOD_ENABLE_REPLAY_INPUT_STAGE_TRACE
            bool input_stage_trace_ok = true;
            input_stage_trace_ok &= hook(
                Slot::ProcessReplayDecodedInputPackets,
                base + kProcessReplayDecodedInputPacketsRVA,
                &NativeReplayTraceHook::detour_process_replay_decoded_input_packets);
            input_stage_trace_ok &= hook(
                Slot::ReplayPlaybackPushInputs,
                base + kReplayPlaybackPushInputsRVA,
                &NativeReplayTraceHook::detour_replay_playback_push_inputs);
            if (!input_stage_trace_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[NativeReplayTraceHook] replay input-stage trace hooks "
                    "partially failed; continuing with launch trace hooks\n"));
            }
#endif

            // The native pose sampler reuses caller-owned decoded-word
            // scratch whose unwritten tail survives across frames. Keep these
            // two hooks installed with the launch hooks so captured seek and
            // rollback state do not depend on optional lifecycle tracing.
            ok &= hook(
                Slot::SolveBonePose,
                base + kSolveBonePoseRVA,
                &NativeReplayTraceHook::detour_solve_bone_pose);
            ok &= hook(
                Slot::SampleKeyframeTransforms,
                base + kSampleKeyframeTransformsRVA,
                &NativeReplayTraceHook::detour_sample_keyframe_transforms);
            ok &= hook(
                Slot::WritebackScaledBoneTransforms,
                base + kWritebackScaledBoneTransformsRVA,
                &NativeReplayTraceHook::
                    detour_writeback_scaled_bone_transforms);
            ok &= hook(
                Slot::UpdateRootMotionDeltasFromBone1,
                base + kUpdateRootMotionDeltasFromBone1RVA,
                &NativeReplayTraceHook::
                    detour_update_root_motion_deltas_from_bone1);
            ok &= hook(
                Slot::TickCharaMainSimulation,
                base + kTickCharaMainSimulationRVA,
                &NativeReplayTraceHook::
                    detour_tick_chara_main_simulation);
            ok &= hook(
                Slot::FinalizeTickPoseAndState,
                base + kFinalizeTickPoseAndStateRVA,
                &NativeReplayTraceHook::
                    detour_finalize_tick_pose_and_state);
            ok &= hook(
                Slot::EvaluateBonePose,
                base + kEvaluateBonePoseRVA,
                &NativeReplayTraceHook::detour_evaluate_bone_pose);
            ok &= hook(
                Slot::TickHitResolutionAndBodyCollision,
                base + kTickHitResolutionAndBodyCollisionRVA,
                &NativeReplayTraceHook::
                    detour_tick_hit_resolution_and_body_collision);
            ok &= hook(
                Slot::SolvePhysBodyCollision,
                base + kSolvePhysBodyCollisionRVA,
                &NativeReplayTraceHook::detour_solve_phys_body_collision);
            ok &= hook(
                Slot::TickBothCharaCollisionPhysics,
                base + kTickBothCharaCollisionPhysicsRVA,
                &NativeReplayTraceHook::
                    detour_tick_both_chara_collision_physics);

            // Lifecycle probes are diagnostic-only. Keep them disarmed by
            // default; Replay presence can include menus/setup scenes with
            // transient chara objects before the battle runtime is stable.

            if (!ok)
            {
                uninstall();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            ReplayTraceFields f;
            f.hex("image_base", base)
             .integer("hook_count", static_cast<int64_t>(installed_hook_count()))
             .boolean("extended_hooks",
#if HORSEMOD_ENABLE_EXTENDED_NATIVE_REPLAY_TRACE
                      true
#else
                      false
#endif
             )
             .boolean("lifecycle_probe_trace_available",
#if HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
                      true
#else
                      false
#endif
             )
             .boolean("lifecycle_probe_trace_active",
                      replay_lifecycle_trace_active());
            emit("native_replay_trace_hooks_installed", f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTraceHook] installed replay native trace hooks "
                "(image_base=0x{:X})\n"), base);
            return true;
        }

        bool set_replay_lifecycle_trace_active(bool active)
        {
            if (active)
                return acquire_replay_lifecycle_trace(
                    ReplayLifecycleTraceOwner::Legacy);
            release_replay_lifecycle_trace(
                ReplayLifecycleTraceOwner::Legacy);
            return true;
        }

        bool acquire_replay_lifecycle_trace(
            ReplayLifecycleTraceOwner owner)
        {
#if HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            const uint32_t owner_bit = static_cast<uint32_t>(owner);
            std::scoped_lock lock(m_lifecycle_owner_mutex);
            const uint32_t previous = m_lifecycle_owner_mask.load(
                std::memory_order_acquire);
            if ((previous & owner_bit) != 0)
                return m_lifecycle_installed.load(
                    std::memory_order_acquire);
            const bool frame_input_log_only =
                owner == ReplayLifecycleTraceOwner::
                    FrameInputLogDiagnostics;
            if (!install_lifecycle_hooks(frame_input_log_only))
                return false;
            const uint32_t current = previous | owner_bit;
            m_lifecycle_owner_mask.store(current, std::memory_order_release);
            ReplayTraceFields f;
            f.uinteger("owner", owner_bit)
             .hex("owner_mask", current)
             .string("operation", "acquire");
            emit("native_replay_lifecycle_trace_lease", f);
            return true;
#else
            (void)owner;
            return false;
#endif
        }

        void release_replay_lifecycle_trace(
            ReplayLifecycleTraceOwner owner)
        {
#if HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            const uint32_t owner_bit = static_cast<uint32_t>(owner);
            std::scoped_lock lock(m_lifecycle_owner_mutex);
            const uint32_t previous = m_lifecycle_owner_mask.load(
                std::memory_order_acquire);
            if ((previous & owner_bit) == 0)
                return;
            const uint32_t current = previous & ~owner_bit;
            m_lifecycle_owner_mask.store(current, std::memory_order_release);
            ReplayTraceFields f;
            f.uinteger("owner", owner_bit)
             .hex("owner_mask", current)
             .string("operation", "release");
            emit("native_replay_lifecycle_trace_lease", f);
            if (current == 0)
                uninstall_lifecycle_hooks();
#else
            (void)owner;
#endif
        }

        uint32_t replay_lifecycle_trace_owner_mask() const noexcept
        {
            return m_lifecycle_owner_mask.load(std::memory_order_acquire);
        }

        bool replay_lifecycle_trace_active() const noexcept
        {
            return m_lifecycle_installed.load(std::memory_order_acquire);
        }

        bool set_replay_resume_phase_signals_active(bool active)
        {
            if (active)
                return install_resume_phase_signal_hooks();
            uninstall_resume_phase_signal_hooks();
            return true;
        }

        bool replay_resume_phase_signals_active() const noexcept
        {
            return m_resume_phase_signal_installed.load(
                std::memory_order_acquire);
        }

        void uninstall()
        {
            std::scoped_lock lock(m_lifecycle_owner_mutex);
            // A failed install can leave populated slots before m_installed is
            // published. Always drain the slot table so partial installation
            // and module teardown cannot leave a live detour behind.
            m_installed.store(false, std::memory_order_release);
            m_lifecycle_installed.store(false, std::memory_order_release);
            m_lifecycle_owner_mask.store(0, std::memory_order_release);
            m_resume_phase_signal_installed.store(
                false, std::memory_order_release);
            for (size_t i = 0; i < m_slots.size(); ++i)
                unhook_slot_index(i);
        }

        bool installed() const noexcept
        {
            return m_installed.load(std::memory_order_acquire);
        }

        uintptr_t latest_replay_input_overlay() const noexcept
        {
            return m_latest_replay_input_overlay.load(
                std::memory_order_acquire);
        }

        void clear_replay_input_stage_cache() noexcept
        {
            m_latest_replay_input_overlay.store(
                0, std::memory_order_release);
            for (auto& entry : m_replay_ring_cache)
                entry = ReplayRingCacheEntry{};
            s_stage2_enter_count.store(0, std::memory_order_release);
            s_stage2_exit_count.store(0, std::memory_order_release);
            s_stage3_enter_count.store(0, std::memory_order_release);
            s_stage3_exit_count.store(0, std::memory_order_release);
            s_input_stage_non_replay_suppress_count.store(
                0, std::memory_order_release);
        }

        bool lookup_replay_ring_entry(
            int32_t frame_id,
            uint32_t cursor,
            size_t slot,
            void* out_entry,
            size_t out_bytes) const noexcept
        {
            if (!out_entry || out_bytes < kCachedReplayRingEntryBytes)
                return false;
            if (frame_id < 0 || slot >= kCachedReplayRingSlots)
                return false;
            const ReplayRingCacheEntry& entry = m_replay_ring_cache[
                replay_ring_cache_index(frame_id, cursor, slot)];
            if (!entry.valid || entry.frame_id != frame_id
                || entry.cursor != cursor || entry.slot != slot)
                return false;
            std::memcpy(out_entry, entry.bytes,
                        kCachedReplayRingEntryBytes);
            return true;
        }

        void set_stock_battle_asset_barrier(
            bool armed, bool ready, bool released,
            StockBattleAssetReleaseCallback callback = nullptr,
            void* callback_context = nullptr) noexcept
        {
            m_stock_battle_asset_barrier_armed.store(
                armed, std::memory_order_release);
            if (!armed)
            {
                m_stock_battle_asset_release_claim.reset();
                m_stock_battle_asset_completion_withheld.store(
                    false, std::memory_order_release);
                m_stock_battle_asset_release_emitted.store(
                    false, std::memory_order_release);
                m_stock_battle_ready_emitted.store(
                    false, std::memory_order_release);
                m_stock_battle_ready_callback_ok.store(
                    false, std::memory_order_release);
                m_stock_battle_asset_barrier_ready.store(
                    false, std::memory_order_release);
                m_stock_battle_asset_release_callback.store(
                    nullptr, std::memory_order_release);
                m_stock_battle_asset_release_context.store(
                    nullptr, std::memory_order_release);
            }
            else
            {
                m_stock_battle_asset_release_callback.store(
                    callback, std::memory_order_release);
                m_stock_battle_asset_release_context.store(
                    callback_context, std::memory_order_release);
                if (ready)
                {
                    m_stock_battle_asset_barrier_ready.store(
                        true, std::memory_order_release);
                }
                if (released)
                {
                    // Release is one-way for this battle transition. The
                    // online scene may clear IsMatchConnecting at launch.
                    m_stock_battle_asset_release_claim.set_released();
                }
            }
        }

        void set_stock_battle_asset_release_schedule(
            uint64_t request_generation, uint64_t target_qpc) noexcept
        {
            m_stock_battle_asset_release_claim.publish(
                request_generation, target_qpc);
        }

        uint64_t stock_battle_asset_release_actual_qpc() const noexcept
        {
            return m_stock_battle_asset_release_claim.actual_qpc();
        }

        uint64_t stock_battle_asset_release_generation() const noexcept
        { return m_stock_battle_asset_release_claim.generation(); }

        uint64_t stock_battle_asset_release_target_qpc() const noexcept
        { return m_stock_battle_asset_release_claim.target_qpc(); }

        RollbackScheduledReleaseObservation
        stock_battle_asset_release_observation() noexcept
        { return m_stock_battle_asset_release_claim.observe(); }

        bool release_stock_battle_asset_barrier_if_ready(
            uint64_t request_generation,
            uint64_t expected_generation,
            bool target_latched,
            uint64_t target_qpc,
            bool release_applied,
            bool native_battle_started,
            uint64_t now_qpc) noexcept
        {
            const bool armed = m_stock_battle_asset_barrier_armed.load(
                std::memory_order_acquire);
            const bool ready = m_stock_battle_asset_barrier_ready.load(
                std::memory_order_acquire);
            const bool withheld = m_stock_battle_asset_completion_withheld.load(
                std::memory_order_acquire);
            const bool callback_ok = m_stock_battle_ready_callback_ok.load(
                std::memory_order_acquire);
            return target_latched
                && m_stock_battle_asset_release_claim.try_release(
                    request_generation, expected_generation, target_qpc,
                    release_applied, native_battle_started, now_qpc,
                    armed, ready, withheld, callback_ok);
        }

        bool last_stock_battle_asset_native_result() const noexcept
        {
            return m_stock_battle_asset_native_result.load(
                std::memory_order_acquire);
        }

    private:
        enum class Slot : size_t
        {
            LoadReplaySaveExec,
            DoesReplaySaveExistExec,
            SaveReplayToSlotExec,
            GetReplaySaveManager,
            ApplyBattleSettings,
            ApplyReplayToBattleSetup,
            RequestBattleAsset,
            HasAnyBattleRequest,
            CanLaunchBattleManually,
            ManualLaunchBattle,
            QuickBattleRequestExec,
            SetActiveStageMapPath,
            DeferredStageMapPathCallback,
            ReplayListRequestReplayFileExec,
            ReplayListRequestReadyReplayExec,
            ReplayListOnRequestPlayExec,
            ReplayListApplyTemporaryDataExec,
            HandleReplayFileRequest,
            HandleReplayFileRequestComplete,
            DecompressUlx1EntryBlob,
            DeserializeEntryPayloadToListItem,
            ProcessReplayDecodedInputPackets,
            ReplayPlaybackPushInputs,
            SolveBonePose,
            SampleKeyframeTransforms,
            WritebackScaledBoneTransforms,
            UpdateRootMotionDeltasFromBone1,
            MoveSystemPumpVMSlots,
            BattlePerFrameTick,
            CharaPerTickAdvanceAll,
            FrameInputLogTickControl,
            TickCharaInput,
            AdvanceCharaAnimClipPlayer,
            TickCharaEventCueScheduler,
            PreTickStateSnapshotAndRoundDecision,
            TickCharaMainSimulation,
            UpdateOpponentRelativeAngles,
            FinalizeTickPoseAndState,
            EvaluateBonePose,
            TickHitResolutionAndBodyCollision,
            UpdateProximityBlendWeight,
            UpdateStanceCategory,
            TickHitStateStateMachine,
            IntegratePhysicsPerTick,
            EvaluateDefenseMode,
            UpdateBlockStateStochastic,
            TickDamageAndBehaviorLock,
            TickCharaTerrainContactBlend,
            MoveVMExecuteOpStream,
            MoveVMCheckTransitionTiming,
            MoveVMRunBytecodeScript,
            MoveVMTransitionToMove,
            MoveVMExecuteBankSlotScript,
            MoveVMDecodeVariadicStreamArgs,
            MoveVMPushAnimNotifyOntoSecondaryStack,
            CpuSubVMSwap,
            CpuSubVMFactory,
            CpuSubVMSpecialFactory,
            SolvePhysBodyCollision,
            TickBothCharaCollisionPhysics,
            Count,
        };

        struct HookSlot
        {
            uintptr_t target {0};
            uint64_t trampoline {0};
            std::unique_ptr<PLH::x64Detour> detour;
        };

        struct ReplayRingCacheEntry
        {
            int32_t frame_id {-1};
            uint32_t cursor {0};
            size_t slot {0};
            bool valid {false};
            uint8_t bytes[16] {};
        };

        NativeReplayTraceHook() = default;
        ~NativeReplayTraceHook() { uninstall(); }
        NativeReplayTraceHook(const NativeReplayTraceHook&) = delete;
        NativeReplayTraceHook& operator=(const NativeReplayTraceHook&) = delete;

        static constexpr uintptr_t kDoesReplaySaveExistExecRVA = 0x9A5120;
        static constexpr uintptr_t kLoadReplaySaveExecRVA      = 0x9A5560;
        static constexpr uintptr_t kSaveReplayToSlotExecRVA    = 0x9A57C0;
        static constexpr uintptr_t kGetReplaySaveManagerRVA    = 0x50BDA0;
        static constexpr uintptr_t kApplyBattleSettingsRVA     = 0x594EB0;
        static constexpr uintptr_t kApplyReplayToBattleSetupRVA = 0x53C700;
        static constexpr uintptr_t kRequestBattleAssetRVA      = 0x551BB0;
        static constexpr uintptr_t kHasAnyBattleRequestRVA     = 0x549E50;
        static constexpr uintptr_t kCanLaunchBattleManuallyRVA = 0x53D990;
        static constexpr uintptr_t kManualLaunchBattleRVA      = 0x54DE10;
        static constexpr uintptr_t kQuickBattleRequestExecRVA  = 0xA90FB0;
        static constexpr uintptr_t kSetActiveStageMapPathRVA   = 0x550D70;
        static constexpr uintptr_t kDeferredStageMapPathCallbackRVA = 0x53D940;
        static constexpr uintptr_t kReplayListRequestReplayFileExecRVA = 0xB9F510;
        static constexpr uintptr_t kReplayListRequestReadyReplayExecRVA = 0x30060E0;
        static constexpr uintptr_t kReplayListOnRequestPlayExecRVA = 0xB9F2C0;
        static constexpr uintptr_t kReplayListApplyTemporaryDataExecRVA = 0x30069F0;
        static constexpr uintptr_t kHandleReplayFileRequestRVA = 0x5EA1F0;
        static constexpr uintptr_t kHandleReplayFileRequestCompleteRVA = 0x5E34D0;
        static constexpr uintptr_t kDecompressUlx1EntryBlobRVA = 0x2DCE6F0;
        static constexpr uintptr_t kDeserializeEntryPayloadToListItemRVA = 0x5B17F0;
        static constexpr uintptr_t kProcessReplayDecodedInputPacketsRVA = 0x3F63B0;
        static constexpr uintptr_t kReplayPlaybackPushInputsRVA = 0x3F6600;
        static constexpr uintptr_t kMoveSystemPumpVMSlotsRVA = 0x31D460;
        // Observe the stock vtable dispatch thunks instead of the native
        // function entries.  ReplayScrub and RollbackProductionRuntime may
        // already own the native entries by the time these optional probes
        // arm; the thunks remain an unambiguous, stock-only admission point.
        static constexpr uintptr_t kBattlePerFrameTickDispatchThunkRVA =
            0x3D2A20;
        static constexpr uintptr_t kCharaPerTickAdvanceAllDispatchThunkRVA =
            0x3D3180;
        static constexpr uintptr_t kFrameInputLogTickControlRVA = 0x3F5D20;
        static constexpr uintptr_t kTickCharaInputRVA = 0x312510;
        static constexpr uintptr_t kAdvanceCharaAnimClipPlayerRVA = 0x37C2F0;
        static constexpr uintptr_t kTickCharaEventCueSchedulerRVA = 0x38BD60;
        static constexpr uintptr_t
            kPreTickStateSnapshotAndRoundDecisionRVA = 0x34FCE0;
        static constexpr uintptr_t kTickCharaMainSimulationRVA = 0x34DA70;
        static constexpr uintptr_t kUpdateOpponentRelativeAnglesRVA = 0x305E50;
        static constexpr uintptr_t kFinalizeTickPoseAndStateRVA = 0x305B50;
        static constexpr uintptr_t kEvaluateBonePoseRVA = 0x2F0F20;
        static constexpr uintptr_t kSolveBonePoseRVA = 0x2EDB90;
        static constexpr uintptr_t kSampleKeyframeTransformsRVA = 0x2E7780;
        static constexpr uintptr_t
            kWritebackScaledBoneTransformsRVA = 0x2F3690;
        static constexpr uintptr_t kTickHitResolutionAndBodyCollisionRVA = 0x33CCA0;
        static constexpr uintptr_t kSolvePhysBodyCollisionRVA = 0x30CCF0;
        static constexpr uintptr_t
            kTickBothCharaCollisionPhysicsRVA = 0x317800;
        static constexpr uintptr_t
            kUpdateRootMotionDeltasFromBone1RVA = 0x3043D0;
        static constexpr uintptr_t kUpdateProximityBlendWeightRVA = 0x306A50;
        static constexpr uintptr_t kUpdateStanceCategoryRVA = 0x308CA0;
        static constexpr uintptr_t kTickHitStateStateMachineRVA = 0x308EC0;
        static constexpr uintptr_t kIntegratePhysicsPerTickRVA = 0x306BB0;
        static constexpr uintptr_t kEvaluateDefenseModeRVA = 0x34EA60;
        static constexpr uintptr_t kUpdateBlockStateStochasticRVA = 0x34E820;
        static constexpr uintptr_t kTickDamageAndBehaviorLockRVA = 0x34E900;
        static constexpr uintptr_t kTickCharaTerrainContactBlendRVA = 0x308620;
        static constexpr uintptr_t kMoveVMExecuteOpStreamRVA = 0x2FDEA0;
        static constexpr uintptr_t kMoveVMCheckTransitionTimingRVA = 0x2FDD70;
        static constexpr uintptr_t kMoveVMRunBytecodeScriptRVA = 0x2E67B0;
        static constexpr uintptr_t kMoveVMTransitionToMoveRVA = 0x2FE350;
        static constexpr uintptr_t kMoveVMExecuteBankSlotScriptRVA = 0x2FCC30;
        static constexpr uintptr_t kMoveVMDecodeVariadicStreamArgsRVA = 0x2FC930;
        static constexpr uintptr_t kMoveVMPushAnimNotifyOntoSecondaryStackRVA = 0x38F180;
        static constexpr uintptr_t kCpuSubVMSwapRVA = 0x2E57D0;
        static constexpr uintptr_t kCpuSubVMFactoryRVA = 0x2E26A0;
        static constexpr uintptr_t kCpuSubVMSpecialFactoryRVA = 0x2E5220;
        static constexpr uintptr_t kMoveVMGlobalVarBankBaseRVA = 0x470D200;
        static constexpr uintptr_t kMoveVMTransitionThresholdNowFlagRVA = 0x470DE64;
        static constexpr uintptr_t kMoveVMDeferredTransitionScheduleFrameRVA = 0x470DE68;
        static constexpr uintptr_t kMoveVMDeferredTransitionScheduleFlagRVA = 0x470DE6C;
        static constexpr uintptr_t kMoveVMDeferredTransitionCommitFlagRVA = 0x470DEC0;
        static constexpr uintptr_t kMoveVMCommandPlayerArrayRVA = 0x470F390;
        static constexpr uintptr_t kMoveSystemPumpStateRVA = 0x4100C70;
        static constexpr uintptr_t kRVA_LatestEngineInput = 0x4855700;
        static constexpr uintptr_t kRVA_InputRingBaseOffset = 0x470DED0;
        static constexpr uintptr_t kRVA_PerPlayerInputRing = 0x485E750;
        static constexpr uintptr_t kRVA_PerPlayerInputCursor = 0x485EB20;
        static constexpr uintptr_t kRVA_CharaSlotP1 = 0x470DE90;
        static constexpr uintptr_t kRVA_CharaSlotP2 = 0x470DE98;
        static constexpr uintptr_t kRVA_VMFreezeOutBlendW0 = 0x48462F0;
        static constexpr uintptr_t kRVA_VMFreezeOutModeTag = 0x4846300;
        static constexpr uintptr_t kCharaVfxEffectAnchorOffset = 0x95FA0;

        static constexpr size_t kCachedReplayRingRounds = 16;
        static constexpr size_t kCachedReplayRingMasters = 8192;
        static constexpr size_t kCachedReplayRingSlots = 2;
        static constexpr size_t kCachedReplayRingEntryBytes = 16;
        static constexpr uint32_t kReplayRingBucketMask = 0x1FF;
        static constexpr int32_t kCachedStage3RepairLookahead = 16;

        static constexpr size_t slot_index(Slot s) noexcept
        {
            return static_cast<size_t>(s);
        }
        static constexpr size_t kSlotCount = static_cast<size_t>(Slot::Count);
        static constexpr size_t kLifecycleFirstSlot =
            static_cast<size_t>(Slot::MoveSystemPumpVMSlots);
        static constexpr size_t kLifecycleLastSlot =
            static_cast<size_t>(Slot::CpuSubVMSpecialFactory);
        static constexpr size_t kLifecycleHookCount =
            kLifecycleLastSlot - kLifecycleFirstSlot + 1
            - 1 // EvaluateBonePose is permanent, not a lifecycle-only hook.
            - 1 // TickHitResolution is permanent collision evidence.
#if !HORSEMOD_ENABLE_REPLAY_MOVEVM_TRANSITION_TRACE
            - 1
#endif
            ;
        // TickCharaMainSimulation and TickHitResolution are permanent
        // artifact-bound diagnostics. Resume signaling adds the five
        // existing subordinate hooks plus the two native scheduler roots.
        static constexpr size_t kResumePhaseHookCount = 7;

        static constexpr size_t installed_hook_count() noexcept
        {
            size_t count =
#if HORSEMOD_ENABLE_EXTENDED_NATIVE_REPLAY_TRACE
                slot_index(Slot::DeserializeEntryPayloadToListItem) + 1;
#else
                slot_index(Slot::DeferredStageMapPathCallback) + 1;
#endif
#if !HORSEMOD_ENABLE_EXTENDED_NATIVE_REPLAY_TRACE
            ++count; // HandleReplayFileRequestComplete payload capture.
#endif
#if HORSEMOD_ENABLE_REPLAY_INPUT_STAGE_TRACE
            count += 2;
#endif
            // Pose solve/sample scratch, root-motion extraction, the three
            // upstream pose-producer boundaries, and three collision
            // transaction boundaries.
            count += 10;
            return count;
        }

        bool hook(Slot slot, uintptr_t target, void* detour_fn)
        {
            HookSlot& s = m_slots[slot_index(slot)];
            if (s.detour)
                return true;
            s.target = target;
            s.trampoline = 0;
            s.detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(detour_fn),
                &s.trampoline);
            if (!s.detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[NativeReplayTraceHook] failed to hook slot={} "
                    "target=0x{:X}\n"), slot_index(slot), target);
                s.detour.reset();
                return false;
            }
            return true;
        }

        void unhook_slot_index(size_t idx)
        {
            if (idx >= m_slots.size()) return;
            HookSlot& s = m_slots[idx];
            if (s.detour)
            {
                s.detour->unHook();
                s.detour.reset();
            }
            s.trampoline = 0;
            s.target = 0;
        }

        static bool is_resume_phase_slot(size_t i) noexcept
        {
            return i == static_cast<size_t>(Slot::BattlePerFrameTick)
                || i == static_cast<size_t>(Slot::CharaPerTickAdvanceAll)
                || i == static_cast<size_t>(Slot::TickCharaInput)
                || i == static_cast<size_t>(Slot::TickCharaMainSimulation)
                || i == static_cast<size_t>(
                    Slot::TickHitResolutionAndBodyCollision)
                || i == static_cast<size_t>(
                    Slot::MoveVMRunBytecodeScript)
                || i == static_cast<size_t>(
                    Slot::MoveVMExecuteBankSlotScript)
                || i == static_cast<size_t>(
                    Slot::MoveVMDecodeVariadicStreamArgs)
                || i == static_cast<size_t>(
                    Slot::MoveVMPushAnimNotifyOntoSecondaryStack);
        }

        static bool validate_native_tick_dispatch_thunk_signatures(
            uintptr_t base) noexcept
        {
            static constexpr std::array<uint8_t, 8>
                kBattlePerFrameTickDispatchThunkBytes {
                    0x48, 0x8B, 0xCA, 0xE9,
                    0x38, 0x92, 0xF0, 0xFF,
                };
            static constexpr std::array<uint8_t, 8>
                kCharaPerTickAdvanceAllDispatchThunkBytes {
                    0xE9, 0xAB, 0x99, 0xF0,
                    0xFF, 0xCC, 0xCC, 0xCC,
                };
            // ProcessFrameInputLogTickAndAdvanceClockUnlessHeld
            // @ 0x1403F5D20. This wrapper always executes +0x620, asks
            // +0x628 whether the tick is held, and tail-dispatches +0x630
            // only when the hold gate is clear.
            static constexpr std::array<uint8_t, 10>
                kFrameInputLogTickControlBytes {
                    0x40, 0x53, 0x48, 0x83, 0xEC,
                    0x20, 0x48, 0x8B, 0x01, 0x48,
                };
            std::array<uint8_t,
                       kBattlePerFrameTickDispatchThunkBytes.size()>
                live_per_frame {};
            std::array<uint8_t,
                       kCharaPerTickAdvanceAllDispatchThunkBytes.size()>
                live_per_tick {};
            std::array<uint8_t,
                       kFrameInputLogTickControlBytes.size()>
                live_input_log_tick_control {};
            return base != 0
                && safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kBattlePerFrameTickDispatchThunkRVA),
                    live_per_frame.data(), live_per_frame.size())
                && live_per_frame
                    == kBattlePerFrameTickDispatchThunkBytes
                && safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kCharaPerTickAdvanceAllDispatchThunkRVA),
                    live_per_tick.data(), live_per_tick.size())
                && live_per_tick
                    == kCharaPerTickAdvanceAllDispatchThunkBytes
                && safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kFrameInputLogTickControlRVA),
                    live_input_log_tick_control.data(),
                    live_input_log_tick_control.size())
                && live_input_log_tick_control
                    == kFrameInputLogTickControlBytes;
        }

        static bool validate_cpu_subvm_lifecycle_signatures(
            uintptr_t base) noexcept
        {
            // SoulcaliburVI.exe @ 0x1402E57D0. Validate the live native
            // publish-before-delete helper before installing this probe.
            static constexpr std::array<uint8_t, 16> kSwapExpected {
                0x48, 0x83, 0xEC, 0x38,
                0x48, 0xC7, 0x44, 0x24,
                0x20, 0xFE, 0xFF, 0xFF,
                0xFF, 0x4C, 0x8B, 0x01,
            };
            // SoulcaliburVI.exe @ 0x1402E26A0. The factory contains several
            // inline publish/delete paths which never call the helper above,
            // so its entry is also an authoritative replacement boundary.
            static constexpr std::array<uint8_t, 16> kFactoryExpected {
                0x40, 0x57, 0x48, 0x83,
                0xEC, 0x30, 0x48, 0xC7,
                0x44, 0x24, 0x20, 0xFE,
                0xFF, 0xFF, 0xFF, 0x48,
            };
            std::array<uint8_t, kSwapExpected.size()> live_swap {};
            std::array<uint8_t, kFactoryExpected.size()> live_factory {};
            std::array<uint8_t, kFactoryExpected.size()> live_special_factory {};
            return base != 0
                && safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kCpuSubVMSwapRVA),
                    live_swap.data(), live_swap.size())
                && live_swap == kSwapExpected
                && safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kCpuSubVMFactoryRVA),
                    live_factory.data(), live_factory.size())
                && live_factory == kFactoryExpected
                && safe_read_bytes(
                    reinterpret_cast<const void*>(
                        base + kCpuSubVMSpecialFactoryRVA),
                    live_special_factory.data(), live_special_factory.size())
                && live_special_factory == kFactoryExpected;
        }

        void unhook_lifecycle_slots()
        {
            const bool keep_resume_phase_signals =
                m_resume_phase_signal_installed.load(
                    std::memory_order_acquire);
            for (size_t i = kLifecycleFirstSlot;
                 i <= kLifecycleLastSlot;
                 ++i)
            {
#if !HORSEMOD_ENABLE_REPLAY_MOVEVM_TRANSITION_TRACE
                if (i == static_cast<size_t>(Slot::MoveVMTransitionToMove))
                    continue;
#endif
                if (keep_resume_phase_signals && is_resume_phase_slot(i))
                {
                    continue;
                }
                // These two slots are also immutable producer-boundary
                // diagnostics installed by install(). Lifecycle tracing may
                // add fields to their events, but disabling lifecycle probes
                // must not remove the artifact-bound pose evidence.
                if (i == static_cast<size_t>(
                        Slot::TickCharaMainSimulation)
                    || i == static_cast<size_t>(
                        Slot::FinalizeTickPoseAndState)
                    || i == static_cast<size_t>(
                        Slot::TickHitResolutionAndBodyCollision))
                {
                    continue;
                }
                unhook_slot_index(i);
            }
        }

        bool install_resume_phase_signal_hooks()
        {
            if (m_resume_phase_signal_installed.load(
                    std::memory_order_acquire))
            {
                return true;
            }
            if (!m_installed.load(std::memory_order_acquire) && !install())
                return false;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;

            if (!validate_native_tick_dispatch_thunk_signatures(base))
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[NativeReplayTraceHook] native tick-root signature "
                    "mismatch; refusing resume diagnostics\n"));
                return false;
            }

            bool ok = true;
            ok &= hook(Slot::BattlePerFrameTick,
                       base + kBattlePerFrameTickDispatchThunkRVA,
                       &NativeReplayTraceHook::detour_battle_per_frame_tick);
            ok &= hook(Slot::CharaPerTickAdvanceAll,
                       base + kCharaPerTickAdvanceAllDispatchThunkRVA,
                       &NativeReplayTraceHook::
                           detour_chara_per_tick_advance_all);
            ok &= hook(Slot::TickCharaMainSimulation,
                       base + kTickCharaMainSimulationRVA,
                       &NativeReplayTraceHook::
                           detour_tick_chara_main_simulation);
            ok &= hook(Slot::TickHitResolutionAndBodyCollision,
                       base + kTickHitResolutionAndBodyCollisionRVA,
                       &NativeReplayTraceHook::
                           detour_tick_hit_resolution_and_body_collision);
            ok &= hook(Slot::TickCharaInput,
                       base + kTickCharaInputRVA,
                       &NativeReplayTraceHook::detour_tick_chara_input);
            ok &= hook(Slot::MoveVMRunBytecodeScript,
                       base + kMoveVMRunBytecodeScriptRVA,
                       &NativeReplayTraceHook::
                           detour_movevm_run_bytecode_script);
            ok &= hook(Slot::MoveVMExecuteBankSlotScript,
                       base + kMoveVMExecuteBankSlotScriptRVA,
                       &NativeReplayTraceHook::
                           detour_movevm_execute_bank_slot_script);
            ok &= hook(Slot::MoveVMDecodeVariadicStreamArgs,
                       base + kMoveVMDecodeVariadicStreamArgsRVA,
                       &NativeReplayTraceHook::
                           detour_movevm_decode_variadic_stream_args);
            ok &= hook(Slot::MoveVMPushAnimNotifyOntoSecondaryStack,
                       base + kMoveVMPushAnimNotifyOntoSecondaryStackRVA,
                       &NativeReplayTraceHook::
                           detour_movevm_push_anim_notify_onto_secondary_stack);
            if (!ok)
            {
                for (size_t i = kLifecycleFirstSlot;
                     i <= kLifecycleLastSlot;
                     ++i)
                {
                    if (is_resume_phase_slot(i))
                        unhook_slot_index(i);
                }
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[NativeReplayTraceHook] resume phase signal hooks "
                    "failed; replay seek resume-state validation may be "
                    "deferred\n"));
                return false;
            }

            m_resume_phase_signal_installed.store(
                true, std::memory_order_release);
            ReplayTraceFields f;
            f.hex("image_base", base)
             .integer("hook_count", kResumePhaseHookCount)
             .boolean("secondary_action_repair_hooks", true);
            emit("native_replay_resume_phase_signal_hooks_installed", f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTraceHook] replay resume phase signal hooks "
                "armed\n"));
            return true;
        }

        void uninstall_resume_phase_signal_hooks()
        {
            if (!m_resume_phase_signal_installed.exchange(
                    false, std::memory_order_acq_rel))
            {
                return;
            }
            if (!m_lifecycle_installed.load(std::memory_order_acquire))
            {
                for (size_t i = kLifecycleFirstSlot;
                     i <= kLifecycleLastSlot;
                     ++i)
                {
                    if (is_resume_phase_slot(i)
                        && i != static_cast<size_t>(
                            Slot::TickCharaMainSimulation)
                        && i != static_cast<size_t>(
                            Slot::TickHitResolutionAndBodyCollision))
                    {
                        unhook_slot_index(i);
                    }
                }
            }
            ReplayTraceFields f;
            f.integer("hook_count", kResumePhaseHookCount)
             .boolean("secondary_action_repair_hooks", true);
            emit("native_replay_resume_phase_signal_hooks_uninstalled", f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTraceHook] replay resume phase signal hooks "
                "disarmed\n"));
        }

        bool install_lifecycle_hooks(bool frame_input_log_only = false)
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            return false;
#else
            if (m_lifecycle_installed.load(std::memory_order_acquire))
            {
                // A full installation is a superset of the focused probe.
                // Upgrading a focused live detour set in place would race
                // native callbacks, so reject that transition until the
                // focused owner releases and the slots are detached.
                if (!frame_input_log_only
                    && m_lifecycle_frame_input_log_only.load(
                        std::memory_order_acquire))
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[NativeReplayTraceHook] refusing live upgrade "
                        "from focused FrameInputLog trace to full lifecycle "
                        "trace\n"));
                    return false;
                }
                return true;
            }
            if (!m_installed.load(std::memory_order_acquire) && !install())
                return false;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base)
                return false;
            if (!validate_native_tick_dispatch_thunk_signatures(base))
                return false;
            if (!frame_input_log_only
                && !validate_cpu_subvm_lifecycle_signatures(base))
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[NativeReplayTraceHook] CPU SubVM lifecycle signature "
                    "mismatch; refusing lifecycle diagnostics\n"));
                return false;
            }

            bool lifecycle_trace_ok = true;
            if (frame_input_log_only)
            {
                lifecycle_trace_ok &= hook(
                    Slot::FrameInputLogTickControl,
                    base + kFrameInputLogTickControlRVA,
                    &NativeReplayTraceHook::
                        detour_frame_input_log_tick_control);
            }
            else
            {
            lifecycle_trace_ok &= hook(
                Slot::MoveSystemPumpVMSlots,
                base + kMoveSystemPumpVMSlotsRVA,
                &NativeReplayTraceHook::detour_move_system_pump_vm_slots);
            lifecycle_trace_ok &= hook(
                Slot::BattlePerFrameTick,
                base + kBattlePerFrameTickDispatchThunkRVA,
                &NativeReplayTraceHook::detour_battle_per_frame_tick);
            lifecycle_trace_ok &= hook(
                Slot::CharaPerTickAdvanceAll,
                base + kCharaPerTickAdvanceAllDispatchThunkRVA,
                &NativeReplayTraceHook::
                    detour_chara_per_tick_advance_all);
            lifecycle_trace_ok &= hook(
                Slot::FrameInputLogTickControl,
                base + kFrameInputLogTickControlRVA,
                &NativeReplayTraceHook::
                    detour_frame_input_log_tick_control);
            lifecycle_trace_ok &= hook(
                Slot::TickCharaInput,
                base + kTickCharaInputRVA,
                &NativeReplayTraceHook::detour_tick_chara_input);
            lifecycle_trace_ok &= hook(
                Slot::AdvanceCharaAnimClipPlayer,
                base + kAdvanceCharaAnimClipPlayerRVA,
                &NativeReplayTraceHook::
                    detour_advance_chara_anim_clip_player);
            lifecycle_trace_ok &= hook(
                Slot::TickCharaEventCueScheduler,
                base + kTickCharaEventCueSchedulerRVA,
                &NativeReplayTraceHook::
                    detour_tick_chara_event_cue_scheduler);
            lifecycle_trace_ok &= hook(
                Slot::PreTickStateSnapshotAndRoundDecision,
                base + kPreTickStateSnapshotAndRoundDecisionRVA,
                &NativeReplayTraceHook::
                    detour_pre_tick_state_snapshot_and_round_decision);
            lifecycle_trace_ok &= hook(
                Slot::TickCharaMainSimulation,
                base + kTickCharaMainSimulationRVA,
                &NativeReplayTraceHook::detour_tick_chara_main_simulation);
            lifecycle_trace_ok &= hook(
                Slot::UpdateOpponentRelativeAngles,
                base + kUpdateOpponentRelativeAnglesRVA,
                &NativeReplayTraceHook::detour_update_opponent_relative_angles);
            lifecycle_trace_ok &= hook(
                Slot::FinalizeTickPoseAndState,
                base + kFinalizeTickPoseAndStateRVA,
                &NativeReplayTraceHook::detour_finalize_tick_pose_and_state);
            lifecycle_trace_ok &= hook(
                Slot::TickHitResolutionAndBodyCollision,
                base + kTickHitResolutionAndBodyCollisionRVA,
                &NativeReplayTraceHook::detour_tick_hit_resolution_and_body_collision);
            lifecycle_trace_ok &= hook(
                Slot::UpdateProximityBlendWeight,
                base + kUpdateProximityBlendWeightRVA,
                &NativeReplayTraceHook::detour_update_proximity_blend_weight);
            lifecycle_trace_ok &= hook(
                Slot::UpdateStanceCategory,
                base + kUpdateStanceCategoryRVA,
                &NativeReplayTraceHook::detour_update_stance_category);
            lifecycle_trace_ok &= hook(
                Slot::TickHitStateStateMachine,
                base + kTickHitStateStateMachineRVA,
                &NativeReplayTraceHook::detour_tick_hit_state_state_machine);
            lifecycle_trace_ok &= hook(
                Slot::IntegratePhysicsPerTick,
                base + kIntegratePhysicsPerTickRVA,
                &NativeReplayTraceHook::detour_integrate_physics_per_tick);
            lifecycle_trace_ok &= hook(
                Slot::EvaluateDefenseMode,
                base + kEvaluateDefenseModeRVA,
                &NativeReplayTraceHook::detour_evaluate_defense_mode);
            lifecycle_trace_ok &= hook(
                Slot::UpdateBlockStateStochastic,
                base + kUpdateBlockStateStochasticRVA,
                &NativeReplayTraceHook::detour_update_block_state_stochastic);
            lifecycle_trace_ok &= hook(
                Slot::TickDamageAndBehaviorLock,
                base + kTickDamageAndBehaviorLockRVA,
                &NativeReplayTraceHook::detour_tick_damage_and_behavior_lock);
            lifecycle_trace_ok &= hook(
                Slot::TickCharaTerrainContactBlend,
                base + kTickCharaTerrainContactBlendRVA,
                &NativeReplayTraceHook::detour_tick_chara_terrain_contact_blend);
            lifecycle_trace_ok &= hook(
                Slot::MoveVMExecuteOpStream,
                base + kMoveVMExecuteOpStreamRVA,
                &NativeReplayTraceHook::detour_movevm_execute_op_stream);
            lifecycle_trace_ok &= hook(
                Slot::MoveVMCheckTransitionTiming,
                base + kMoveVMCheckTransitionTimingRVA,
                &NativeReplayTraceHook::detour_movevm_check_transition_timing);
            lifecycle_trace_ok &= hook(
                Slot::MoveVMRunBytecodeScript,
                base + kMoveVMRunBytecodeScriptRVA,
                &NativeReplayTraceHook::detour_movevm_run_bytecode_script);
#if HORSEMOD_ENABLE_REPLAY_MOVEVM_TRANSITION_TRACE
            lifecycle_trace_ok &= hook(
                Slot::MoveVMTransitionToMove,
                base + kMoveVMTransitionToMoveRVA,
                &NativeReplayTraceHook::detour_movevm_transition_to_move);
#endif
            lifecycle_trace_ok &= hook(
                Slot::MoveVMExecuteBankSlotScript,
                base + kMoveVMExecuteBankSlotScriptRVA,
                &NativeReplayTraceHook::detour_movevm_execute_bank_slot_script);
            lifecycle_trace_ok &= hook(
                Slot::MoveVMDecodeVariadicStreamArgs,
                base + kMoveVMDecodeVariadicStreamArgsRVA,
                &NativeReplayTraceHook::detour_movevm_decode_variadic_stream_args);
            lifecycle_trace_ok &= hook(
                Slot::MoveVMPushAnimNotifyOntoSecondaryStack,
                base + kMoveVMPushAnimNotifyOntoSecondaryStackRVA,
                &NativeReplayTraceHook::detour_movevm_push_anim_notify_onto_secondary_stack);
            lifecycle_trace_ok &= hook(
                Slot::CpuSubVMSwap,
                base + kCpuSubVMSwapRVA,
                &NativeReplayTraceHook::detour_cpu_subvm_swap);
            lifecycle_trace_ok &= hook(
                Slot::CpuSubVMFactory,
                base + kCpuSubVMFactoryRVA,
                &NativeReplayTraceHook::detour_cpu_subvm_factory);
            lifecycle_trace_ok &= hook(
                Slot::CpuSubVMSpecialFactory,
                base + kCpuSubVMSpecialFactoryRVA,
                &NativeReplayTraceHook::detour_cpu_subvm_special_factory);
            }
            if (!lifecycle_trace_ok)
            {
                unhook_lifecycle_slots();
                m_lifecycle_installed.store(false, std::memory_order_release);
                m_lifecycle_frame_input_log_only.store(
                    false, std::memory_order_release);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[NativeReplayTraceHook] lifecycle trace hooks "
                    "partially failed; continuing with replay launch hooks\n"));
                return false;
            }

            m_lifecycle_frame_input_log_only.store(
                frame_input_log_only, std::memory_order_release);
            m_lifecycle_installed.store(true, std::memory_order_release);
            ReplayTraceFields f;
            f.hex("image_base", base)
             .integer("hook_count",
                      static_cast<int64_t>(
                          installed_hook_count()
                            + (frame_input_log_only
                                ? 1 : kLifecycleHookCount)))
             .boolean("frame_input_log_only", frame_input_log_only)
             .boolean("movevm_transition_hook",
#if HORSEMOD_ENABLE_REPLAY_MOVEVM_TRANSITION_TRACE
                      true
#else
                      false
#endif
             );
            emit("native_replay_lifecycle_trace_hooks_installed", f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTraceHook] replay lifecycle trace hooks armed "
                "(image_base=0x{:X}, hooks={}, frame_input_log_only={})\n"),
                base,
                static_cast<int>(frame_input_log_only
                    ? 1 : kLifecycleHookCount),
                frame_input_log_only ? 1 : 0);
            return true;
#endif
        }

        void uninstall_lifecycle_hooks()
        {
#if HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            if (!m_lifecycle_installed.exchange(false))
                return;
            unhook_lifecycle_slots();
            m_lifecycle_frame_input_log_only.store(
                false, std::memory_order_release);
            ReplayTraceFields f;
            f.integer("hook_count",
                      static_cast<int64_t>(installed_hook_count()));
            emit("native_replay_lifecycle_trace_hooks_uninstalled", f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTraceHook] replay lifecycle trace hooks "
                "disarmed\n"));
#endif
        }

        template <typename Fn>
        static Fn orig(Slot slot)
        {
            return reinterpret_cast<Fn>(
                instance().m_slots[slot_index(slot)].trampoline);
        }

        static uint64_t next_seq() noexcept
        {
            return s_seq.fetch_add(1, std::memory_order_acq_rel) + 1;
        }

        static void emit(const char* name, ReplayTraceFields& fields)
        {
            fields.uinteger("trace_seq", next_seq());
            ReplayDebugTrace::instance().event(name, fields);
        }

        static void log0(const char* name)
        {
            ReplayTraceFields f;
            emit(name, f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] {}\n"), RC::to_generic_string(name));
        }

        static void log_ptrs(const char* name,
                             uintptr_t a = 0,
                             uintptr_t b = 0,
                             uintptr_t c = 0)
        {
            ReplayTraceFields f;
            f.hex("arg0", a).hex("arg1", b).hex("arg2", c);
            emit(name, f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] {} arg0=0x{:X} arg1=0x{:X} arg2=0x{:X}\n"),
                RC::to_generic_string(name), a, b, c);
        }

        static void log_bool_result(const char* name,
                                    uintptr_t self,
                                    bool result)
        {
            ReplayTraceFields f;
            f.hex("self", self).boolean("result", result);
            emit(name, f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] {} self=0x{:X} result={}\n"),
                RC::to_generic_string(name), self, result ? 1 : 0);
        }

        using Exec3VoidFn = void(__fastcall*)(void*, void*, void*);
        using VoidPtrFn = void(__fastcall*)(void*);
        using VoidPtrIntFn = void(__fastcall*)(void*, int);
        using VoidPtr2U16Fn = void(__fastcall*)(
            void*, uint16_t, uint16_t);
        using Void2PtrFn = void(__fastcall*)(void*, void*);
        using Void3PtrFn = void(__fastcall*)(void*, void*, void*);
        using TickCharaInputFn = void(__fastcall*)(void*, void*, void*);
        using VoidU64PtrFn = void(__fastcall*)(uint64_t, void*);
        using MoveVMExecuteOpStreamFn = void(__fastcall*)(void*, int, uint64_t, void*);
        using MoveVMCheckTransitionTimingFn = void(__fastcall*)(void*, void*, uint64_t, uint32_t);
        using MoveVMRunBytecodeScriptFn = int16_t(__fastcall*)(void*, void*, uint16_t, uint16_t*);
        using MoveVMTransitionToMoveFn = int(__fastcall*)(void*, int, uint32_t, uint32_t, uint32_t, uint64_t, int, int, int, void*);
        using MoveVMExecuteBankSlotScriptFn = uint64_t(__fastcall*)(void*, int, int16_t*);
        using MoveVMDecodeVariadicStreamArgsFn = uint64_t(__fastcall*)(void*, int, uint16_t*, int);
        using MoveVMPushAnimNotifyOntoSecondaryStackFn = uint32_t(__fastcall*)(void*, void*, int, uint32_t, void*, uint32_t);
        using BoolPtrFn = bool(__fastcall*)(void*);
        using IntPtrFn = int(__fastcall*)(void*);
        using ReplaySaveManagerFn = void*(__fastcall*)(bool);
        using QuickBattleExecFn = void(__fastcall*)(void*, void*);
        using SetActiveStageMapPathFn = void(__fastcall*)(void*, uint8_t, uint8_t, int32_t);
        using DeferredStageMapPathCallbackFn = uint64_t(__fastcall*)(void*);
        using ReplayFileRequestFn = bool(__fastcall*)(void*, void*, int32_t);
        using ReplayFileRequestCompleteFn = void(__fastcall*)(void*, uint64_t, bool, void*);
        using Bool2PtrFn = bool(__fastcall*)(void*, void*);
        using VoidNoArgFn = void(__fastcall*)();
        using UpdateRootMotionDeltasFn = void(__fastcall*)(void*);
        using SolvePhysBodyCollisionFn = uint8_t(__fastcall*)(
            void*, void*, void*, void*, float);
        using TickBothCharaCollisionPhysicsFn = void(__fastcall*)(
            void*, void*, uint8_t, uint32_t*, uint32_t*);
        using SolveBonePoseFn = void(__fastcall*)(void*, float*, uint32_t);
        using SampleKeyframeTransformsFn = uint64_t(__fastcall*)(
            void*, void*, void*, uint8_t*, void*, uint64_t, float, float);
        using WritebackScaledBoneTransformsFn = void(__fastcall*)(
            void*, void*, void*, float*);

        struct FStringNative
        {
            const wchar_t* data;
            int32_t num;
            int32_t max;
        };

        struct TArrayByteNative
        {
            const uint8_t* data;
            int32_t num;
            int32_t max;
        };

        static uintptr_t safe_read_uintptr(const void* p) noexcept
        {
            uintptr_t out = 0;
            if (!p) return 0;
            __try { out = *static_cast<const uintptr_t*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; }
            return out;
        }

        static bool safe_read_bool(const void* p) noexcept
        {
            bool out = false;
            if (!p) return false;
            __try { out = *static_cast<const bool*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = false; }
            return out;
        }

        static int32_t safe_read_int32(const void* p) noexcept
        {
            int32_t out = 0;
            if (!p) return 0;
            __try { out = *static_cast<const int32_t*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; }
            return out;
        }

        static uint32_t safe_read_uint32(const void* p) noexcept
        {
            uint32_t out = 0;
            if (!p) return 0;
            __try { out = *static_cast<const uint32_t*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; }
            return out;
        }

        static uint64_t safe_read_uint64(const void* p) noexcept
        {
            uint64_t out = 0;
            if (!p) return 0;
            __try { out = *static_cast<const uint64_t*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; }
            return out;
        }

        static uint16_t safe_read_uint16(const void* p) noexcept
        {
            uint16_t out = 0;
            if (!p) return 0;
            __try { out = *static_cast<const uint16_t*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; }
            return out;
        }

        static int16_t safe_read_int16(const void* p) noexcept
        {
            int16_t out = 0;
            if (!p) return 0;
            __try { out = *static_cast<const int16_t*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; }
            return out;
        }

        static uint8_t safe_read_uint8(const void* p) noexcept
        {
            uint8_t out = 0;
            if (!p) return 0;
            __try { out = *static_cast<const uint8_t*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; }
            return out;
        }

        static float safe_read_float(const void* p) noexcept
        {
            float out = 0.0f;
            if (!p) return 0.0f;
            __try { out = *static_cast<const float*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0.0f; }
            return out;
        }

        static bool safe_read_bytes(
            const void* src,
            void* dst,
            size_t bytes) noexcept
        {
            if (!src || !dst) return false;
            __try { std::memcpy(dst, src, bytes); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return true;
        }

        static bool safe_write_bytes(
            void* dst,
            const void* src,
            size_t bytes) noexcept
        {
            if (!src || !dst) return false;
            __try { std::memcpy(dst, src, bytes); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return true;
        }

        static std::string safe_read_hex_string(const void* src, size_t bytes)
        {
            if (!src || bytes == 0) return {};
            if (bytes > 64) bytes = 64;
            uint8_t buf[64]{};
            if (!safe_read_bytes(src, buf, bytes)) return {};
            char out[64 * 3]{};
            size_t pos = 0;
            for (size_t i = 0; i < bytes && pos + 3 < sizeof(out); ++i)
            {
                pos += static_cast<size_t>(std::snprintf(
                    out + pos, sizeof(out) - pos,
                    i == 0 ? "%02X" : " %02X", buf[i]));
            }
            return std::string(out);
        }

        static size_t replay_ring_cache_index(
            int32_t frame_id,
            uint32_t cursor,
            size_t slot) noexcept
        {
            const size_t round = static_cast<size_t>(frame_id)
                & (kCachedReplayRingRounds - 1);
            const size_t master = static_cast<size_t>(cursor)
                & (kCachedReplayRingMasters - 1);
            return ((round * kCachedReplayRingMasters) + master)
                * kCachedReplayRingSlots + slot;
        }

        static void* chara_slot_from_global(size_t player_index) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base || player_index > 1) return nullptr;
            const uintptr_t rva = player_index == 0
                ? kRVA_CharaSlotP1 : kRVA_CharaSlotP2;
            return reinterpret_cast<void*>(
                safe_read_uintptr(reinterpret_cast<const void*>(base + rva)));
        }

        static int player_index_for_chara(void* chara) noexcept
        {
            if (!chara) return -1;
            for (size_t pi = 0; pi < 2; ++pi)
            {
                if (chara_slot_from_global(pi) == chara)
                    return static_cast<int>(pi);
            }
            return -1;
        }

        static void* safe_provider_index(void* provider, int index) noexcept
        {
            void* vtable = nullptr;
            void* fn_raw = nullptr;
            if (!provider || !safe_read_bytes(provider, &vtable, sizeof(vtable))
                || !vtable
                || !safe_read_bytes(static_cast<uint8_t*>(vtable) + 0x28,
                                    &fn_raw, sizeof(fn_raw))
                || !fn_raw)
                return nullptr;

            using Fn = void*(__fastcall*)(void*, int);
            void* result = nullptr;
            __try
            {
                result = reinterpret_cast<Fn>(fn_raw)(provider, index);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                result = nullptr;
            }
            return result;
        }

        static uint64_t hash_live_bytes(const void* src,
                                        size_t bytes,
                                        bool* ok) noexcept
        {
            if (ok) *ok = false;
            if (!src || bytes == 0 || bytes > 0x100) return 0;
            uint8_t buf[0x100]{};
            if (!safe_read_bytes(src, buf, bytes)) return 0;
            if (ok) *ok = true;
            return ReplayTraceFields::fnv1a64(buf, bytes);
        }

        static constexpr size_t kMotionDecodeTransformBytes = 0x30;
        static constexpr size_t kMotionDecodeTransformCount = 32;
        static constexpr size_t kMotionDecodeMaxSelectorCount = 96;

        struct MotionDecodeSampleEvidence
        {
            bool header_readable {false};
            bool selector_walk_complete {false};
            bool scratch_readable {false};
            bool out_pose_readable {false};
            int32_t clip_index {-1};
            uint32_t clip_flags {0};
            uint64_t authored_channel_mask {0};
            uint32_t decoded_word_count {0};
            uint32_t consumed_word_count {0};
            uint32_t selector_count {0};
            uint8_t first_unknown_selector {0};
            uint64_t scratch_pair_hash {0};
            uint64_t primary_decoded_prefix_hash {0};
            uint64_t secondary_decoded_prefix_hash {0};
            uint64_t primary_consumed_prefix_hash {0};
            uint64_t secondary_consumed_prefix_hash {0};
            uint64_t primary_retained_tail_hash {0};
            uint64_t secondary_retained_tail_hash {0};
            std::array<uint64_t, kMotionDecodeTransformCount>
                out_transform_hash {};
            std::array<uint32_t,
                kMotionDecodeTransformBytes / sizeof(uint32_t)>
                root_transform_words {};
        };

        struct MotionDecodeTraceWindow
        {
            bool enabled {false};
            const char* coordinate_source {"none"};
            int32_t sequence {-1};
            int32_t round {-1};
            int32_t master {-1};
            int32_t logical_frame {-1};
        };

        // Diagnostic capture is deliberately bounded and is always available
        // inside an owned rollback iteration. The deep-diagnostics lease is
        // needed only for additional lifecycle probes; requiring it here made
        // the useful pose window unavailable in ordinary beta evidence.
        // The first proven
        // normal-versus-rollback primitive mismatch is replay round 0,
        // source index 871, logical frame 755.  Capture a small symmetric
        // window around each coordinate, plus bootstrap frames 0..3.  An
        // unbounded record for every motion sample generated multi-gigabyte
        // traces and materially perturbed the validation workload.
        static MotionDecodeTraceWindow
        motion_decode_trace_window() noexcept
        {
            MotionDecodeTraceWindow out {};
            (void)replay_scrub_read_native_trace_position(
                out.sequence, out.round, out.master);
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            if (native_scope.active)
                out.logical_frame =
                    static_cast<int32_t>(native_scope.logical_frame);
            const MotionDecodeTraceWindowDecision decision =
                EvaluateMotionDecodeTraceWindow({
                    out.sequence,
                    out.round,
                    native_scope.active,
                    out.logical_frame,
                    (instance().replay_lifecycle_trace_owner_mask()
                        & (static_cast<uint32_t>(
                                ReplayLifecycleTraceOwner::
                                    RollbackDiagnostics)
                            | static_cast<uint32_t>(
                                ReplayLifecycleTraceOwner::
                                    FrameInputLogDiagnostics))) != 0});
            out.enabled = decision.enabled;
            out.coordinate_source =
                MotionDecodeTraceCoordinateSourceName(decision.source);
            return out;
        }

        static bool detailed_lifecycle_trace_enabled() noexcept
        {
            const uint32_t owner_mask =
                instance().replay_lifecycle_trace_owner_mask();
            const uint32_t rollback_diagnostics_bit =
                static_cast<uint32_t>(
                    ReplayLifecycleTraceOwner::RollbackDiagnostics);
            const uint32_t full_trace_owner_mask =
                static_cast<uint32_t>(ReplayLifecycleTraceOwner::Legacy)
                | static_cast<uint32_t>(
                    ReplayLifecycleTraceOwner::TimelineGeneration)
                | static_cast<uint32_t>(
                    ReplayLifecycleTraceOwner::SeekTest);

            const auto& native_scope =
                g_rollback_native_simulation_scope;
            return EvaluateDetailedReplayLifecycleTrace(
                owner_mask, full_trace_owner_mask,
                rollback_diagnostics_bit,
                native_scope.active,
                native_scope.active
                    ? native_scope.logical_frame : 0);
        }

        static uint32_t motion_decode_selector_bytes(
            uint8_t selector,
            uint32_t clip_flags,
            bool& known) noexcept
        {
            known = true;
            switch (selector)
            {
            case 0x02:
            case 0x03:
            case 0x0D:
            case 0x13:
            case 0x14:
            case 0x19:
            case 0x1A:
                return 6;
            case 0x06:
            case 0x10:
                return 2;
            case 0x0E:
            case 0x0F:
                return 12;
            case 0x16:
                return (clip_flags & (1u << 28)) != 0 ? 14 : 8;
            case 0x17:
            case 0x18:
                return (clip_flags & (1u << 28)) != 0 ? 12 : 6;
            case 0x00:
            case 0x15:
            case 0x1B:
            case 0x1C:
                return 0;
            default:
                known = false;
                return 0;
            }
        }

        static bool measure_motion_decode_selector_consumption(
            const uint8_t* selector_stream,
            uint64_t authored_mask,
            uint32_t clip_flags,
            uint32_t& consumed_word_count,
            uint32_t& selector_count,
            uint8_t& first_unknown_selector) noexcept
        {
            consumed_word_count = 0;
            selector_count = 0;
            first_unknown_selector = 0;
            if (!selector_stream) return false;

            for (size_t index = 0;
                 index < kMotionDecodeMaxSelectorCount
                    && authored_mask != 0;
                 ++index)
            {
                uint8_t selector = 0;
                if (!safe_read_bytes(
                        selector_stream + index,
                        &selector, sizeof(selector)))
                {
                    return false;
                }
                ++selector_count;
                if ((authored_mask & 1ull) != 0)
                {
                    bool known = false;
                    const uint32_t bytes = motion_decode_selector_bytes(
                        selector, clip_flags, known);
                    if (!known)
                    {
                        first_unknown_selector = selector;
                        return false;
                    }
                    consumed_word_count += bytes / sizeof(int16_t);
                }

                if (selector == 0x00)
                    return true;
                // Selectors 0x1B and 0x1C alias the following selector onto
                // the current logical channel. Native multiplies the mask by
                // two before the common right shift, so the mask does not
                // advance for these opcodes.
                if (selector != 0x1B && selector != 0x1C)
                    authored_mask >>= 1;
            }
            return authored_mask == 0;
        }

        static MotionDecodeSampleEvidence
        capture_motion_decode_sample_evidence(
            void* playback_state,
            void* out_pose,
            const uint8_t* selector_stream,
            void* scratch_pair) noexcept
        {
            MotionDecodeSampleEvidence out {};
            const auto* playback =
                static_cast<const uint8_t*>(playback_state);
            const uintptr_t bank = playback
                ? safe_read_uintptr(playback + 0x08) : 0;
            out.clip_index = playback
                ? safe_read_int32(playback + 0x10) : -1;
            const uint32_t motion_count = bank
                ? safe_read_uint32(
                    reinterpret_cast<const void*>(bank)) : 0;
            if (bank && out.clip_index >= 0
                && static_cast<uint32_t>(out.clip_index) < motion_count)
            {
                const uintptr_t offset_address = bank + 0x08
                    + static_cast<uintptr_t>(out.clip_index)
                        * sizeof(uint32_t);
                const uint32_t clip_offset = safe_read_uint32(
                    reinterpret_cast<const void*>(offset_address));
                const uintptr_t clip = bank + clip_offset;
                std::array<uint8_t, 0x10> header {};
                if (clip_offset >= 0x08
                    && safe_read_bytes(
                        reinterpret_cast<const void*>(clip),
                        header.data(), header.size()))
                {
                    uint16_t packed_word_count = 0;
                    std::memcpy(
                        &packed_word_count, header.data() + 0x02,
                        sizeof(packed_word_count));
                    std::memcpy(
                        &out.clip_flags, header.data() + 0x04,
                        sizeof(out.clip_flags));
                    std::memcpy(
                        &out.authored_channel_mask,
                        header.data() + 0x08,
                        sizeof(out.authored_channel_mask));
                    out.decoded_word_count =
                        static_cast<uint32_t>(packed_word_count >> 1);
                    out.header_readable = true;
                    out.selector_walk_complete =
                        measure_motion_decode_selector_consumption(
                            selector_stream,
                            out.authored_channel_mask,
                            out.clip_flags,
                            out.consumed_word_count,
                            out.selector_count,
                            out.first_unknown_selector);
                }
            }

            std::array<uint8_t,
                kRollbackMotionDecodeScratchPairBytes> scratch {};
            out.scratch_readable = scratch_pair
                && safe_read_bytes(
                    scratch_pair, scratch.data(), scratch.size());
            if (out.scratch_readable)
            {
                out.scratch_pair_hash = ReplayTraceFields::fnv1a64(
                    scratch.data(), scratch.size());
                const size_t decoded_bytes = (std::min)(
                    static_cast<size_t>(out.decoded_word_count)
                        * sizeof(int16_t),
                    kRollbackMotionDecodedWordBufferBytes);
                const size_t consumed_bytes = (std::min)(
                    static_cast<size_t>(out.consumed_word_count)
                        * sizeof(int16_t),
                    kRollbackMotionDecodedWordBufferBytes);
                const uint8_t* primary = scratch.data();
                const uint8_t* secondary = scratch.data()
                    + kRollbackMotionDecodedWordBufferBytes;
                out.primary_decoded_prefix_hash =
                    ReplayTraceFields::fnv1a64(
                        primary, decoded_bytes);
                out.secondary_decoded_prefix_hash =
                    ReplayTraceFields::fnv1a64(
                        secondary, decoded_bytes);
                out.primary_consumed_prefix_hash =
                    ReplayTraceFields::fnv1a64(
                        primary, consumed_bytes);
                out.secondary_consumed_prefix_hash =
                    ReplayTraceFields::fnv1a64(
                        secondary, consumed_bytes);
                out.primary_retained_tail_hash =
                    ReplayTraceFields::fnv1a64(
                        primary + decoded_bytes,
                        kRollbackMotionDecodedWordBufferBytes
                            - decoded_bytes);
                out.secondary_retained_tail_hash =
                    ReplayTraceFields::fnv1a64(
                        secondary + decoded_bytes,
                        kRollbackMotionDecodedWordBufferBytes
                            - decoded_bytes);
            }

            std::array<uint8_t,
                kMotionDecodeTransformCount
                    * kMotionDecodeTransformBytes> pose {};
            out.out_pose_readable = out_pose
                && safe_read_bytes(
                    out_pose, pose.data(), pose.size());
            if (out.out_pose_readable)
            {
                for (size_t transform = 0;
                     transform < kMotionDecodeTransformCount;
                     ++transform)
                {
                    const uint8_t* bytes = pose.data()
                        + transform * kMotionDecodeTransformBytes;
                    out.out_transform_hash[transform] =
                        ReplayTraceFields::fnv1a64(
                            bytes, kMotionDecodeTransformBytes);
                }
                std::memcpy(
                    out.root_transform_words.data(),
                    pose.data() + kMotionDecodeTransformBytes,
                    kMotionDecodeTransformBytes);
            }
            return out;
        }

        static void add_motion_decode_sample_evidence_fields(
            ReplayTraceFields& fields,
            const char* prefix,
            const MotionDecodeSampleEvidence& evidence) noexcept
        {
            char key[112] {};
            const auto add_bool = [&](const char* suffix, bool value) {
                std::snprintf(
                    key, sizeof(key), "%s_%s", prefix, suffix);
                fields.boolean(key, value);
            };
            const auto add_uint = [&](const char* suffix, uint64_t value) {
                std::snprintf(
                    key, sizeof(key), "%s_%s", prefix, suffix);
                fields.uinteger(key, value);
            };
            const auto add_hex = [&](const char* suffix, uint64_t value) {
                std::snprintf(
                    key, sizeof(key), "%s_%s", prefix, suffix);
                fields.hex(key, static_cast<uintptr_t>(value));
            };

            add_bool("header_readable", evidence.header_readable);
            add_bool(
                "selector_walk_complete",
                evidence.selector_walk_complete);
            add_bool("scratch_readable", evidence.scratch_readable);
            add_bool("out_pose_readable", evidence.out_pose_readable);
            add_uint(
                "clip_index",
                evidence.clip_index >= 0
                    ? static_cast<uint32_t>(evidence.clip_index)
                    : UINT32_MAX);
            add_hex("clip_flags", evidence.clip_flags);
            add_hex(
                "authored_channel_mask",
                evidence.authored_channel_mask);
            add_uint(
                "decoded_word_count",
                evidence.decoded_word_count);
            add_uint(
                "consumed_word_count",
                evidence.consumed_word_count);
            add_bool(
                "consumption_within_decoded_prefix",
                evidence.selector_walk_complete
                    && evidence.consumed_word_count
                        <= evidence.decoded_word_count);
            add_uint("selector_count", evidence.selector_count);
            add_hex(
                "first_unknown_selector",
                evidence.first_unknown_selector);
            add_hex("scratch_pair_hash", evidence.scratch_pair_hash);
            add_hex(
                "primary_decoded_prefix_hash",
                evidence.primary_decoded_prefix_hash);
            add_hex(
                "secondary_decoded_prefix_hash",
                evidence.secondary_decoded_prefix_hash);
            add_hex(
                "primary_consumed_prefix_hash",
                evidence.primary_consumed_prefix_hash);
            add_hex(
                "secondary_consumed_prefix_hash",
                evidence.secondary_consumed_prefix_hash);
            add_hex(
                "primary_retained_tail_hash",
                evidence.primary_retained_tail_hash);
            add_hex(
                "secondary_retained_tail_hash",
                evidence.secondary_retained_tail_hash);
            for (size_t transform = 0;
                 transform < evidence.out_transform_hash.size();
                 ++transform)
            {
                std::snprintf(
                    key, sizeof(key),
                    "%s_out_transform_%02zu_hash",
                    prefix, transform);
                fields.hex(
                    key,
                    static_cast<uintptr_t>(
                        evidence.out_transform_hash[transform]));
            }
            for (size_t word = 0;
                 word < evidence.root_transform_words.size();
                 ++word)
            {
                std::snprintf(
                    key, sizeof(key),
                    "%s_root_transform_word_%02zu_bits",
                    prefix, word);
                fields.hex(
                    key,
                    evidence.root_transform_words[word]);
            }
        }

        static void add_live_hash(ReplayTraceFields& f,
                                  const char* prefix,
                                  const char* suffix,
                                  const void* ptr,
                                  size_t bytes) noexcept
        {
            bool ok = false;
            const uint64_t h = hash_live_bytes(ptr, bytes, &ok);
            char key[96]{};
            std::snprintf(key, sizeof(key), "%s_%s_ok", prefix, suffix);
            f.boolean(key, ok);
            std::snprintf(key, sizeof(key), "%s_%s_hash", prefix, suffix);
            f.hex(key, static_cast<uintptr_t>(h));
        }

        static void add_matrix_bank_ring_state(ReplayTraceFields& f,
                                               const char* prefix,
                                               const char* bank_name,
                                               uint8_t* bank) noexcept
        {
            if (!prefix || !bank_name || !bank) return;

            const uint32_t ring_index = safe_read_uint32(bank + 0x20);
            const uintptr_t buffers[3] = {
                safe_read_uintptr(bank + 0x08),
                safe_read_uintptr(bank + 0x10),
                safe_read_uintptr(bank + 0x18),
            };
            const uintptr_t current = safe_read_uintptr(bank + 0x28);
            const uintptr_t previous = safe_read_uintptr(bank + 0x30);

            auto slot_for_ptr = [&](uintptr_t ptr) noexcept -> int {
                for (int i = 0; i < 3; ++i)
                {
                    if (ptr != 0 && ptr == buffers[i]) return i;
                }
                return -1;
            };

            char key[112]{};
            std::snprintf(key, sizeof(key), "%s_%s_ring_index",
                          prefix, bank_name);
            f.uinteger(key, ring_index);
            std::snprintf(key, sizeof(key), "%s_%s_current_slot",
                          prefix, bank_name);
            f.integer(key, slot_for_ptr(current));
            std::snprintf(key, sizeof(key), "%s_%s_previous_slot",
                          prefix, bank_name);
            f.integer(key, slot_for_ptr(previous));
            std::snprintf(key, sizeof(key), "%s_%s_current_ptr",
                          prefix, bank_name);
            f.hex(key, current);
            std::snprintf(key, sizeof(key), "%s_%s_previous_ptr",
                          prefix, bank_name);
            f.hex(key, previous);

            for (int i = 0; i < 3; ++i)
            {
                const uint8_t* buf = reinterpret_cast<const uint8_t*>(
                    buffers[i]);
                std::snprintf(key, sizeof(key),
                              "%s_%s_buffer%d_ptr", prefix, bank_name, i);
                f.hex(key, buffers[i]);
                std::snprintf(key, sizeof(key),
                              "%s_%s_buffer%d_sub_de4_ok", prefix,
                              bank_name, i);
                bool ok = false;
                const uint64_t h = hash_live_bytes(
                    buf ? buf + 0xDE4 : nullptr, 0x40, &ok);
                f.boolean(key, ok);
                std::snprintf(key, sizeof(key),
                              "%s_%s_buffer%d_sub_de4_hash", prefix,
                              bank_name, i);
                f.hex(key, static_cast<uintptr_t>(h));
            }
        }

        static void add_provider_hashes(ReplayTraceFields& f,
                                        const char* prefix,
                                        uint8_t* chara) noexcept
        {
            if (!prefix || !chara) return;
            add_matrix_bank_ring_state(f, prefix, "primary", chara + 0x35A0);
            add_matrix_bank_ring_state(f, prefix, "secondary", chara + 0x27760);

            void* primary_root = safe_provider_index(chara + 0x35A0, 0);
            void* primary_bone1 = safe_provider_index(chara + 0x35A0, 1);
            add_live_hash(f, prefix, "primary_root0_matrix",
                          primary_root, 0x40);
            add_live_hash(f, prefix, "primary_root1_matrix",
                          primary_bone1, 0x40);
            add_live_hash(
                f, prefix, "primary_root1_translation",
                primary_bone1 ? static_cast<uint8_t*>(primary_bone1) + 0x30
                              : nullptr,
                0x10);
            add_live_hash(
                f, prefix, "primary_delta_5f0",
                primary_root ? static_cast<uint8_t*>(primary_root) + 0x5F0
                             : nullptr,
                0x40);
            add_live_hash(
                f, prefix, "primary_sub_de4",
                primary_root ? static_cast<uint8_t*>(primary_root) + 0xDE4
                             : nullptr,
                0x40);

            void* secondary_root = safe_provider_index(chara + 0x27760, 0);
            void* secondary_bone1 = safe_provider_index(chara + 0x27760, 1);
            add_live_hash(f, prefix, "secondary_root1_matrix",
                          secondary_bone1, 0x40);
            add_live_hash(
                f, prefix, "secondary_delta_5f0",
                secondary_root ? static_cast<uint8_t*>(secondary_root) + 0x5F0
                               : nullptr,
                0x40);
        }

        struct RootMotionDeltaTraceSample
        {
            std::array<float, 16> current {};
            std::array<float, 16> previous {};
            bool current_readable {false};
            bool previous_readable {false};
            uint64_t current_hash {0};
            uint64_t previous_hash {0};
            float raw_x {0.0f};
            float raw_y {0.0f};
            float raw_z {0.0f};
            float smoothed_x {0.0f};
            float smoothed_y {0.0f};
            float smoothed_z {0.0f};
            float carry_x {0.0f};
            float carry_z {0.0f};
            uint32_t carry_mode {0};
        };

        static constexpr size_t kCollisionMatrixCount = 32;
        static constexpr size_t kCollisionMatrixBytes = 0x40;
        static constexpr size_t kCollisionBodyNodeLimit = 256;
        static constexpr std::array<size_t, 3>
            kCollisionDiagnosticBones {9, 11, 12};

        struct CollisionMatrixBankTraceSample
        {
            std::array<uint64_t, kCollisionMatrixCount>
                current_bone_hash {};
            std::array<uint64_t, kCollisionMatrixCount>
                previous_bone_hash {};
            std::array<
                std::array<uint32_t, kCollisionMatrixBytes
                    / sizeof(uint32_t)>,
                kCollisionDiagnosticBones.size()>
                    current_diagnostic_bone_bits {};
            std::array<
                std::array<uint32_t, kCollisionMatrixBytes
                    / sizeof(uint32_t)>,
                kCollisionDiagnosticBones.size()>
                    previous_diagnostic_bone_bits {};
            bool current_readable {false};
            bool previous_readable {false};
            bool explicit_current_matches_bank {false};
            uint64_t current_hash {0};
            uint64_t previous_hash {0};
        };

        struct CollisionBodyTraceSample
        {
            struct Node
            {
                uint64_t slot_bit_mask {0};
                uint32_t flags {0};
                uint16_t active_gate {0};
                uint8_t stream_type {0xff};
                uint8_t slot_or_kind {0};
                std::array<uint32_t, 4> live_local_center_bits {};
                std::array<uint32_t, 4> authored_local_center_bits {};
                std::array<uint32_t, 4> world_center_current_bits {};
                std::array<uint32_t, 4> world_center_previous_bits {};
                uint32_t live_radius_bits {0};
                uint32_t authored_radius_bits {0};
                uint32_t contact_impulse_scale_bits {0};
                uint32_t bone_index {0};
                bool readable {false};
            };

            std::array<uint64_t, kCollisionBodyNodeLimit>
                node_semantic_hash {};
            std::array<Node, kCollisionBodyNodeLimit> node {};
            uint32_t node_hash_count {0};
            uint32_t declared_node_count {0};
            int32_t scratch_bytes {0};
            int32_t max_slot_exclusive {0};
            uint32_t phys_body_type {0};
            float phys_body_radius {0.0f};
            float separation_scale {0.0f};
            float upper_offset {0.0f};
            float lower_offset {0.0f};
            float impact_force_scale {0.0f};
            uint64_t list_semantic_hash {0};
            bool runtime_readable {false};
            bool list_readable {false};
            bool list_complete {false};
            bool list_cycle {false};
        };

        struct PoseProducerTraceContext
        {
            uint64_t transaction_id {0};
            void* chara {nullptr};
            int player_index {-1};
            RollbackPoseProducerTraceLedger ledger {};
        };

        static PoseProducerTraceContext&
        pose_producer_trace_transaction() noexcept
        {
            static thread_local PoseProducerTraceContext context {};
            return context;
        }

        static RollbackRootMotionTraceLedger&
        root_motion_delta_trace_transaction() noexcept
        {
            static thread_local RollbackRootMotionTraceLedger context {};
            return context;
        }

        static RootMotionDeltaTraceSample capture_root_motion_delta_sample(
            void* chara) noexcept
        {
            RootMotionDeltaTraceSample out {};
            if (!chara) return out;
            const auto* c = static_cast<const uint8_t*>(chara);
            const auto* bank =
                c + ReplayScrubDiag::kChara_BoneMatrixBank_Off;
            const uintptr_t current_base = safe_read_uintptr(
                bank + ReplayScrubDiag::kMatrixBank_pCurrent_Off);
            const uintptr_t previous_base = safe_read_uintptr(
                bank + ReplayScrubDiag::kMatrixBank_pPrevious_Off);
            const uintptr_t bone_bytes =
                ReplayScrubDiag::kRootMotionBoneIndex
                * ReplayScrubDiag::kBoneMatrixBytes;
            const void* current = current_base
                ? reinterpret_cast<const void*>(current_base + bone_bytes)
                : nullptr;
            const void* previous = previous_base
                ? reinterpret_cast<const void*>(previous_base + bone_bytes)
                : nullptr;
            out.current_readable = safe_read_bytes(
                current, out.current.data(),
                ReplayScrubDiag::kBoneMatrixBytes);
            out.previous_readable = safe_read_bytes(
                previous, out.previous.data(),
                ReplayScrubDiag::kBoneMatrixBytes);
            if (out.current_readable)
                out.current_hash = ReplayTraceFields::fnv1a64(
                    out.current.data(), ReplayScrubDiag::kBoneMatrixBytes);
            if (out.previous_readable)
                out.previous_hash = ReplayTraceFields::fnv1a64(
                    out.previous.data(), ReplayScrubDiag::kBoneMatrixBytes);
            out.raw_x = safe_read_float(
                c + ReplayScrubDiag::kChara_flRawRootMotion_X_Off);
            out.raw_y = safe_read_float(
                c + ReplayScrubDiag::kChara_flRawRootMotion_X_Off
                    + sizeof(float));
            out.raw_z = safe_read_float(
                c + ReplayScrubDiag::kChara_flRawRootMotion_Z_Off);
            out.smoothed_x = safe_read_float(
                c + ReplayScrubDiag::kChara_flSmoothedRootMotion_X_Off);
            out.smoothed_y = safe_read_float(
                c + ReplayScrubDiag::kChara_flSmoothedRootMotion_X_Off
                    + sizeof(float));
            out.smoothed_z = safe_read_float(
                c + ReplayScrubDiag::kChara_flSmoothedRootMotion_Z_Off);
            out.carry_x = safe_read_float(
                c + ReplayScrubDiag::kChara_flRootMotionCarry_X_Off);
            out.carry_z = safe_read_float(
                c + ReplayScrubDiag::kChara_flRootMotionCarry_Z_Off);
            out.carry_mode = safe_read_uint32(
                c + ReplayScrubDiag::kChara_dwRootMotionCarryMode_Off);
            return out;
        }

        static CollisionMatrixBankTraceSample
        capture_collision_matrix_bank_sample(
            void* chara,
            const void* explicit_current = nullptr) noexcept
        {
            CollisionMatrixBankTraceSample out {};
            if (!chara) return out;
            const auto* c = static_cast<const uint8_t*>(chara);
            const auto* bank =
                c + ReplayScrubDiag::kChara_BoneMatrixBank_Off;
            const uintptr_t bank_current = safe_read_uintptr(
                bank + ReplayScrubDiag::kMatrixBank_pCurrent_Off);
            const uintptr_t bank_previous = safe_read_uintptr(
                bank + ReplayScrubDiag::kMatrixBank_pPrevious_Off);
            const uintptr_t current = explicit_current
                ? reinterpret_cast<uintptr_t>(explicit_current)
                : bank_current;
            out.explicit_current_matches_bank =
                explicit_current == nullptr
                || reinterpret_cast<uintptr_t>(explicit_current)
                    == bank_current;

            RollbackHash current_hash {};
            RollbackHash previous_hash {};
            std::array<uint8_t, kCollisionMatrixBytes> matrix {};
            out.current_readable = current != 0;
            out.previous_readable = bank_previous != 0;
            for (size_t bone = 0; bone < kCollisionMatrixCount; ++bone)
            {
                const uintptr_t offset =
                    bone * kCollisionMatrixBytes;
                const bool current_ok = current != 0
                    && safe_read_bytes(
                        reinterpret_cast<const void*>(current + offset),
                        matrix.data(), matrix.size());
                out.current_readable =
                    out.current_readable && current_ok;
                out.current_bone_hash[bone] = current_ok
                    ? RollbackHashBytes(matrix.data(), matrix.size()) : 0;
                if (current_ok)
                {
                    for (size_t diagnostic_index = 0;
                         diagnostic_index
                            < kCollisionDiagnosticBones.size();
                         ++diagnostic_index)
                    {
                        if (kCollisionDiagnosticBones[diagnostic_index]
                            == bone)
                        {
                            std::memcpy(
                                out.current_diagnostic_bone_bits[
                                    diagnostic_index].data(),
                                matrix.data(), matrix.size());
                        }
                    }
                }
                current_hash.add_scalar(current_ok);
                current_hash.add_scalar(out.current_bone_hash[bone]);

                const bool previous_ok = bank_previous != 0
                    && safe_read_bytes(
                        reinterpret_cast<const void*>(
                            bank_previous + offset),
                        matrix.data(), matrix.size());
                out.previous_readable =
                    out.previous_readable && previous_ok;
                out.previous_bone_hash[bone] = previous_ok
                    ? RollbackHashBytes(matrix.data(), matrix.size()) : 0;
                if (previous_ok)
                {
                    for (size_t diagnostic_index = 0;
                         diagnostic_index
                            < kCollisionDiagnosticBones.size();
                         ++diagnostic_index)
                    {
                        if (kCollisionDiagnosticBones[diagnostic_index]
                            == bone)
                        {
                            std::memcpy(
                                out.previous_diagnostic_bone_bits[
                                    diagnostic_index].data(),
                                matrix.data(), matrix.size());
                        }
                    }
                }
                previous_hash.add_scalar(previous_ok);
                previous_hash.add_scalar(out.previous_bone_hash[bone]);
            }
            out.current_hash = current_hash.value;
            out.previous_hash = previous_hash.value;
            return out;
        }

        static void add_collision_matrix_bank_fields(
            ReplayTraceFields& f,
            const char* prefix,
            const CollisionMatrixBankTraceSample& sample,
            bool detailed) noexcept
        {
            char key[96] {};
            const auto add_bool =
                [&f, &key, prefix](
                    const char* suffix, bool value) noexcept {
                    std::snprintf(
                        key, sizeof(key), "%s_%s", prefix, suffix);
                    f.boolean(key, value);
                };
            const auto add_hash =
                [&f, &key, prefix](
                    const char* suffix, uint64_t value) noexcept {
                    std::snprintf(
                        key, sizeof(key), "%s_%s", prefix, suffix);
                    f.hex(key, static_cast<uintptr_t>(value));
                };
            add_bool("matrix_bank_current_readable",
                     sample.current_readable);
            add_bool("matrix_bank_previous_readable",
                     sample.previous_readable);
            add_bool("solver_current_matches_matrix_bank",
                     sample.explicit_current_matches_bank);
            add_hash("matrix_bank_current_hash", sample.current_hash);
            add_hash("matrix_bank_previous_hash", sample.previous_hash);
            if (!detailed) return;
            for (size_t bone = 0; bone < kCollisionMatrixCount; ++bone)
            {
                std::snprintf(
                    key, sizeof(key),
                    "%s_matrix_bank_current_bone_%02zu_hash",
                    prefix, bone);
                f.hex(
                    key,
                    static_cast<uintptr_t>(
                        sample.current_bone_hash[bone]));
                std::snprintf(
                    key, sizeof(key),
                    "%s_matrix_bank_previous_bone_%02zu_hash",
                    prefix, bone);
                f.hex(
                    key,
                    static_cast<uintptr_t>(
                        sample.previous_bone_hash[bone]));
            }
            for (size_t diagnostic_index = 0;
                 diagnostic_index < kCollisionDiagnosticBones.size();
                 ++diagnostic_index)
            {
                const size_t bone =
                    kCollisionDiagnosticBones[diagnostic_index];
                for (size_t word = 0;
                     word < kCollisionMatrixBytes / sizeof(uint32_t);
                     ++word)
                {
                    const size_t row = word / 4;
                    const size_t column = word % 4;
                    std::snprintf(
                        key, sizeof(key),
                        "%s_matrix_bank_current_bone_%02zu_m%zu%zu_bits",
                        prefix, bone, row, column);
                    f.hex(
                        key,
                        static_cast<uintptr_t>(
                            sample.current_diagnostic_bone_bits[
                                diagnostic_index][word]));
                    std::snprintf(
                        key, sizeof(key),
                        "%s_matrix_bank_previous_bone_%02zu_m%zu%zu_bits",
                        prefix, bone, row, column);
                    f.hex(
                        key,
                        static_cast<uintptr_t>(
                            sample.previous_diagnostic_bone_bits[
                                diagnostic_index][word]));
                }
            }
        }

        struct PreMainCharaMotionTraceSample
        {
            CollisionMatrixBankTraceSample matrix_bank {};
            uint64_t linked_motion_hash {0};
            uint64_t pose_publication_hash {0};
            uint64_t clip_player_scalar_hash {0};
            uint64_t clip_runtime_scalar_hash {0};
            uint32_t current_move_id {0};
            uint32_t pose_finalize_tick {0};
            uint32_t clip_cached_frame_bits {0};
            uint32_t clip_owner_frame_bits {0};
            bool linked_motion_readable {false};
            bool pose_publication_readable {false};
            bool clip_player_readable {false};
            bool clip_runtime_readable {false};
            bool clip_runtime_present {false};
        };

        static PreMainCharaMotionTraceSample
        capture_pre_main_chara_motion_sample(void* chara) noexcept
        {
            PreMainCharaMotionTraceSample out {};
            if (!chara) return out;
            const auto* c = static_cast<const uint8_t*>(chara);
            out.matrix_bank = capture_collision_matrix_bank_sample(chara);
            out.current_move_id = safe_read_uint32(c + 0x324);
            out.pose_finalize_tick = safe_read_uint32(c + 0x20C);

            constexpr uintptr_t kMoveLaneOffset = 0x44958;
            constexpr size_t kMoveLaneBytes = 0x468;
            constexpr size_t kMoveLanePrefixBytes = 0xD8;
            constexpr size_t kMoveLaneTailOffset = 0x438;
            constexpr size_t kMoveLaneTailBytes = 0x30;
            constexpr uintptr_t kPosePrefixOffset = 0x90;
            constexpr size_t kPosePrefixBytes = 0x80;
            constexpr uintptr_t kWindSampleOffset = 0x29310;
            constexpr size_t kWindSampleBytes = 0x10;
            constexpr uintptr_t kPoseYawOffset = 0x9613C;

            RollbackHash linked {};
            std::array<uint8_t, kMoveLanePrefixBytes> lane_prefix {};
            std::array<uint8_t, kMoveLaneTailBytes> lane_tail {};
            out.linked_motion_readable = true;
            for (size_t lane = 0; lane < 2; ++lane)
            {
                const auto* lane_base =
                    c + kMoveLaneOffset + lane * kMoveLaneBytes;
                const bool lane_ok = safe_read_bytes(
                        lane_base, lane_prefix.data(), lane_prefix.size())
                    && safe_read_bytes(
                        lane_base + kMoveLaneTailOffset,
                        lane_tail.data(), lane_tail.size());
                out.linked_motion_readable =
                    out.linked_motion_readable && lane_ok;
                if (lane_ok)
                {
                    linked.add_bytes(
                        lane_prefix.data(), lane_prefix.size());
                    linked.add_bytes(lane_tail.data(), lane_tail.size());
                }
            }
            if (out.linked_motion_readable)
                out.linked_motion_hash = linked.value;

            RollbackHash pose {};
            std::array<uint8_t, kPosePrefixBytes> pose_prefix {};
            std::array<uint8_t, kWindSampleBytes> wind_sample {};
            uint32_t pose_yaw_bits = 0;
            out.pose_publication_readable = safe_read_bytes(
                    c + kPosePrefixOffset,
                    pose_prefix.data(), pose_prefix.size())
                && safe_read_bytes(
                    c + kWindSampleOffset,
                    wind_sample.data(), wind_sample.size())
                && safe_read_bytes(
                    c + kPoseYawOffset,
                    &pose_yaw_bits, sizeof(pose_yaw_bits));
            if (out.pose_publication_readable)
            {
                pose.add_bytes(pose_prefix.data(), pose_prefix.size());
                pose.add_bytes(wind_sample.data(), wind_sample.size());
                pose.add_scalar(pose_yaw_bits);
                out.pose_publication_hash = pose.value;
            }

            out.clip_player_scalar_hash = hash_live_bytes(
                c + kRollbackCharaAnimClipPlayerOffset
                    + kRollbackCharaAnimClipPlayerScalarOffset,
                kRollbackCharaAnimClipPlayerScalarBytes,
                &out.clip_player_readable);
            out.clip_runtime_scalar_hash = hash_live_bytes(
                c + kRollbackCharaAnimRuntimeOffset
                    + kRollbackCharaAnimRuntimeScalarOffset,
                kRollbackCharaAnimRuntimeScalarBytes,
                &out.clip_runtime_readable);
            out.clip_runtime_present = safe_read_uintptr(
                c + kRollbackCharaAnimRuntimeOffset) != 0;
            (void)safe_read_bytes(
                c + kRollbackCharaAnimClipPlayerOffset + 0x10,
                &out.clip_cached_frame_bits,
                sizeof(out.clip_cached_frame_bits));
            (void)safe_read_bytes(
                c + kRollbackCharaAnimRuntimeOffset + 0x0C,
                &out.clip_owner_frame_bits,
                sizeof(out.clip_owner_frame_bits));
            return out;
        }

        struct NativeTickRootTraceSample
        {
            struct CpuSubVM
            {
                uintptr_t sched_state {0};
                uintptr_t chara {0};
                uintptr_t subvm {0};
                uintptr_t subvm_vtable_rva {0};
                uintptr_t subvm_chara {0};
                uintptr_t subvm_opponent {0};
                uintptr_t subvm_owner_sched {0};
                uint64_t sched_semantic_hash {0};
                uint64_t subvm_common_semantic_hash {0};
                uint32_t selected_slot {0};
                uint32_t active_slot {0};
                uint32_t input_command {0};
                uint32_t transition_state {0};
                uint32_t reaction_timer_bits {0};
                bool sched_readable {false};
                bool subvm_readable {false};
                bool identity_links_valid {false};
            };

            PreMainCharaMotionTraceSample p1 {};
            PreMainCharaMotionTraceSample p2 {};
            CpuSubVM cpu_subvm[2] {};
            RngTraceHook::HistogramSnapshot lfsr {};
            RngTraceHook::HistogramSnapshot xorshift96 {};
            uint64_t stage_wind_semantic_hash {0};
            uint32_t stage_wind_emitter_count {0};
            int32_t replay_sequence {-1};
            int32_t replay_round {-1};
            int32_t replay_master {-1};
            bool stage_wind_readable {false};
        };

        struct FrameInputLogTickTraceSample
        {
            uintptr_t object {0};
            uintptr_t vtable_rva {0};
            int32_t last_frame_id {-1};
            int32_t master_clock {-1};
            int32_t hold_frame_count {0};
            int32_t tick_sequence_counter {0};
            int32_t primary_cursor {-1};
            int32_t secondary_cursor {-1};
            int32_t drain_cursor {-1};
            int32_t dormant_cursor {-1};
            uint32_t active_slot_count {0};
            uint32_t active_slot_mask {0};
            uint32_t current_input[2] {};
            uint8_t tick_mode {0};
            uint8_t online_state {0};
            uint8_t hold_request {0};
            bool readable {false};
        };

        static FrameInputLogTickTraceSample
        capture_frame_input_log_tick_trace_sample(void* input_log) noexcept
        {
            FrameInputLogTickTraceSample out {};
            out.object = reinterpret_cast<uintptr_t>(input_log);
            if (!input_log) return out;

            const auto* bytes = static_cast<const uint8_t*>(input_log);
            const uintptr_t image_base = NativeBinding::imageBase();
            const uintptr_t vtable = safe_read_uintptr(bytes);
            out.vtable_rva = image_base != 0 && vtable >= image_base
                ? vtable - image_base : 0;

            bool ok = vtable != 0;
            ok &= safe_read_bytes(
                bytes + 0x398, &out.active_slot_count,
                sizeof(out.active_slot_count));
            ok &= safe_read_bytes(
                bytes + 0x39C, &out.active_slot_mask,
                sizeof(out.active_slot_mask));
            ok &= safe_read_bytes(
                bytes + 0x3A0, &out.last_frame_id,
                sizeof(out.last_frame_id));
            ok &= safe_read_bytes(
                bytes + 0x3A4, &out.master_clock,
                sizeof(out.master_clock));
            ok &= safe_read_bytes(
                bytes + 0x3A8, &out.hold_frame_count,
                sizeof(out.hold_frame_count));
            ok &= safe_read_bytes(
                bytes + 0x3AC, &out.tick_sequence_counter,
                sizeof(out.tick_sequence_counter));
            ok &= safe_read_bytes(
                bytes + 0x3B0, &out.primary_cursor,
                sizeof(out.primary_cursor));
            ok &= safe_read_bytes(
                bytes + 0x3B4, &out.secondary_cursor,
                sizeof(out.secondary_cursor));
            ok &= safe_read_bytes(
                bytes + 0x3B8, out.current_input,
                sizeof(out.current_input));
            ok &= safe_read_bytes(
                bytes + 0x4404, &out.tick_mode,
                sizeof(out.tick_mode));
            ok &= safe_read_bytes(
                bytes + 0x4410, &out.drain_cursor,
                sizeof(out.drain_cursor));
            ok &= safe_read_bytes(
                bytes + 0x4414, &out.dormant_cursor,
                sizeof(out.dormant_cursor));
            ok &= safe_read_bytes(
                bytes + 0x4424, &out.online_state,
                sizeof(out.online_state));
            ok &= safe_read_bytes(
                bytes + 0x4425, &out.hold_request,
                sizeof(out.hold_request));
            out.readable = ok;
            return out;
        }

        static void add_frame_input_log_tick_sample_fields(
            ReplayTraceFields& fields,
            const char* prefix,
            const FrameInputLogTickTraceSample& sample) noexcept
        {
            char key[96] {};
            const auto add_bool = [&fields, &key, prefix](
                const char* suffix, bool value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.boolean(key, value);
            };
            const auto add_int = [&fields, &key, prefix](
                const char* suffix, int64_t value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.integer(key, value);
            };
            const auto add_uint = [&fields, &key, prefix](
                const char* suffix, uint64_t value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.uinteger(key, value);
            };
            const auto add_hex = [&fields, &key, prefix](
                const char* suffix, uint64_t value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.hex(key, static_cast<uintptr_t>(value));
            };
            add_bool("readable", sample.readable);
            add_hex("object", sample.object);
            add_hex("vtable_rva", sample.vtable_rva);
            add_int("last_frame_id", sample.last_frame_id);
            add_int("master_clock", sample.master_clock);
            add_int("hold_frame_count", sample.hold_frame_count);
            add_int("tick_sequence_counter",
                    sample.tick_sequence_counter);
            add_int("primary_cursor", sample.primary_cursor);
            add_int("secondary_cursor", sample.secondary_cursor);
            add_int("drain_cursor", sample.drain_cursor);
            add_int("dormant_cursor", sample.dormant_cursor);
            add_uint("active_slot_count", sample.active_slot_count);
            add_hex("active_slot_mask", sample.active_slot_mask);
            add_hex("current_input_p1", sample.current_input[0]);
            add_hex("current_input_p2", sample.current_input[1]);
            add_uint("tick_mode", sample.tick_mode);
            add_uint("online_state", sample.online_state);
            add_uint("hold_request", sample.hold_request);
        }

        static NativeTickRootTraceSample::CpuSubVM
        capture_cpu_subvm_trace_sample(
            uintptr_t image_base,
            size_t player_index) noexcept
        {
            NativeTickRootTraceSample::CpuSubVM out {};
            if (image_base == 0 || player_index >= 2) return out;

            constexpr uintptr_t kCpuSchedArrayRva = 0x4715400;
            constexpr size_t kCpuSchedStride = 0x60;
            constexpr size_t kCpuSchedSemanticBytesA = 0x08;
            constexpr size_t kCpuSchedSemanticOffsetB = 0x30;
            constexpr size_t kCpuSchedSemanticBytesB = 0x20;
            constexpr size_t kSubVMSemanticBytesA = 0x08;
            constexpr size_t kSubVMSemanticOffsetB = 0x20;
            constexpr size_t kSubVMSemanticBytesB = 0x40;

            out.sched_state = image_base + kCpuSchedArrayRva
                + player_index * kCpuSchedStride;
            std::array<uint8_t, kCpuSchedSemanticBytesA> sched_a {};
            std::array<uint8_t, kCpuSchedSemanticBytesB> sched_b {};
            out.sched_readable = safe_read_bytes(
                    reinterpret_cast<const void*>(out.sched_state),
                    sched_a.data(), sched_a.size())
                && safe_read_bytes(
                    reinterpret_cast<const void*>(
                        out.sched_state + kCpuSchedSemanticOffsetB),
                    sched_b.data(), sched_b.size())
                && safe_read_bytes(
                    reinterpret_cast<const void*>(out.sched_state + 0x08),
                    &out.selected_slot, sizeof(out.selected_slot))
                && safe_read_bytes(
                    reinterpret_cast<const void*>(out.sched_state + 0x58),
                    &out.active_slot, sizeof(out.active_slot));
            out.chara = safe_read_uintptr(
                reinterpret_cast<const void*>(out.sched_state + 0x10));
            out.subvm = safe_read_uintptr(
                reinterpret_cast<const void*>(out.sched_state + 0x50));
            if (out.sched_readable)
            {
                RollbackHash hash {};
                hash.add_bytes(sched_a.data(), sched_a.size());
                hash.add_bytes(sched_b.data(), sched_b.size());
                hash.add_scalar(out.selected_slot);
                hash.add_scalar(out.active_slot);
                out.sched_semantic_hash = hash.value;
            }

            if (out.subvm == 0) return out;
            const uintptr_t vtable = safe_read_uintptr(
                reinterpret_cast<const void*>(out.subvm));
            out.subvm_vtable_rva = vtable >= image_base
                ? vtable - image_base : 0;
            out.subvm_chara = safe_read_uintptr(
                reinterpret_cast<const void*>(out.subvm + 0x10));
            out.subvm_opponent = safe_read_uintptr(
                reinterpret_cast<const void*>(out.subvm + 0x18));
            out.subvm_owner_sched = safe_read_uintptr(
                reinterpret_cast<const void*>(out.subvm + 0x60));

            std::array<uint8_t, kSubVMSemanticBytesA> subvm_a {};
            std::array<uint8_t, kSubVMSemanticBytesB> subvm_b {};
            out.subvm_readable = vtable != 0
                && safe_read_bytes(
                    reinterpret_cast<const void*>(out.subvm + 0x08),
                    subvm_a.data(), subvm_a.size())
                && safe_read_bytes(
                    reinterpret_cast<const void*>(
                        out.subvm + kSubVMSemanticOffsetB),
                    subvm_b.data(), subvm_b.size())
                && safe_read_bytes(
                    reinterpret_cast<const void*>(out.subvm + 0x08),
                    &out.input_command, sizeof(out.input_command))
                && safe_read_bytes(
                    reinterpret_cast<const void*>(out.subvm + 0x54),
                    &out.transition_state, sizeof(out.transition_state))
                && safe_read_bytes(
                    reinterpret_cast<const void*>(out.subvm + 0x58),
                    &out.reaction_timer_bits,
                    sizeof(out.reaction_timer_bits));
            if (out.subvm_readable)
            {
                RollbackHash hash {};
                hash.add_scalar(out.subvm_vtable_rva);
                hash.add_bytes(subvm_a.data(), subvm_a.size());
                hash.add_bytes(subvm_b.data(), subvm_b.size());
                out.subvm_common_semantic_hash = hash.value;
            }
            out.identity_links_valid = out.sched_readable
                && out.subvm_readable
                && out.subvm_chara == out.chara
                && out.subvm_owner_sched == out.sched_state;
            return out;
        }

        static NativeTickRootTraceSample
        capture_native_tick_root_trace_sample() noexcept
        {
            NativeTickRootTraceSample out {};
            out.p1 = capture_pre_main_chara_motion_sample(
                chara_slot_from_global(0));
            out.p2 = capture_pre_main_chara_motion_sample(
                chara_slot_from_global(1));
            out.lfsr = RngTraceHook::instance().snapshot_histogram();
            out.xorshift96 =
                RngTraceHook::instance().snapshot_xorshift_histogram();
            (void)replay_scrub_read_native_trace_position(
                out.replay_sequence, out.replay_round,
                out.replay_master);

            static thread_local RollbackStageWindAllocationPool
                diagnostic_wind_pool {};
            static thread_local RollbackStageWindSnapshot
                diagnostic_wind_snapshot {};
            const uintptr_t image_base = NativeBinding::imageBase();
            if (image_base != 0)
            {
                out.cpu_subvm[0] =
                    capture_cpu_subvm_trace_sample(image_base, 0);
                out.cpu_subvm[1] =
                    capture_cpu_subvm_trace_sample(image_base, 1);
                const RollbackStageWindSnapshotReport report =
                    CaptureRollbackStageWindSnapshotForDiagnostics(
                        image_base, diagnostic_wind_snapshot,
                        diagnostic_wind_pool);
                out.stage_wind_readable = report.ok;
                out.stage_wind_emitter_count = report.count;
                if (report.ok)
                {
                    out.stage_wind_semantic_hash =
                        diagnostic_wind_snapshot.canonical_hash;
                }
            }
            return out;
        }

        static void add_compact_chara_motion_fields(
            ReplayTraceFields& fields,
            const char* prefix,
            const PreMainCharaMotionTraceSample& sample) noexcept
        {
            char key[96] {};
            const auto add_bool = [&fields, &key, prefix](
                const char* suffix, bool value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.boolean(key, value);
            };
            const auto add_uint = [&fields, &key, prefix](
                const char* suffix, uint64_t value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.uinteger(key, value);
            };
            const auto add_hex = [&fields, &key, prefix](
                const char* suffix, uint64_t value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.hex(key, static_cast<uintptr_t>(value));
            };
            add_uint("pose_finalize_tick", sample.pose_finalize_tick);
            add_bool("linked_motion_readable",
                     sample.linked_motion_readable);
            add_hex("linked_motion_hash", sample.linked_motion_hash);
            add_bool("pose_publication_readable",
                     sample.pose_publication_readable);
            add_hex("pose_publication_hash",
                    sample.pose_publication_hash);
            add_bool("clip_player_readable", sample.clip_player_readable);
            add_hex("clip_player_scalar_hash",
                    sample.clip_player_scalar_hash);
            add_bool("clip_runtime_readable", sample.clip_runtime_readable);
            add_bool("clip_runtime_present", sample.clip_runtime_present);
            add_hex("clip_runtime_scalar_hash",
                    sample.clip_runtime_scalar_hash);
            add_hex("clip_cached_frame_bits",
                    sample.clip_cached_frame_bits);
            add_hex("clip_owner_frame_bits",
                    sample.clip_owner_frame_bits);
            add_bool("matrix_bank_current_readable",
                     sample.matrix_bank.current_readable);
            add_hex("matrix_bank_current_hash",
                    sample.matrix_bank.current_hash);
            add_bool("matrix_bank_previous_readable",
                     sample.matrix_bank.previous_readable);
            add_hex("matrix_bank_previous_hash",
                    sample.matrix_bank.previous_hash);
        }

        static void add_native_tick_root_sample_fields(
            ReplayTraceFields& fields,
            const char* prefix,
            const NativeTickRootTraceSample& sample) noexcept
        {
            char key[96] {};
            const auto add_bool = [&fields, &key, prefix](
                const char* suffix, bool value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.boolean(key, value);
            };
            const auto add_int = [&fields, &key, prefix](
                const char* suffix, int64_t value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.integer(key, value);
            };
            const auto add_uint = [&fields, &key, prefix](
                const char* suffix, uint64_t value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.uinteger(key, value);
            };
            const auto add_hex = [&fields, &key, prefix](
                const char* suffix, uint64_t value) noexcept {
                std::snprintf(key, sizeof(key), "%s_%s", prefix, suffix);
                fields.hex(key, static_cast<uintptr_t>(value));
            };
            add_int("replay_sequence", sample.replay_sequence);
            add_int("replay_round", sample.replay_round);
            add_int("replay_master", sample.replay_master);
            add_bool("stage_wind_readable", sample.stage_wind_readable);
            add_uint("stage_wind_emitter_count",
                     sample.stage_wind_emitter_count);
            add_hex("stage_wind_semantic_hash",
                    sample.stage_wind_semantic_hash);
            for (size_t player_index = 0; player_index < 2;
                 ++player_index)
            {
                const auto& cpu = sample.cpu_subvm[player_index];
                char cpu_prefix[96] {};
                std::snprintf(
                    cpu_prefix, sizeof(cpu_prefix), "%s_cpu_p%zu",
                    prefix, player_index + 1);
                const auto add_cpu_bool = [&fields, &key, &cpu_prefix](
                    const char* suffix, bool value) noexcept {
                    std::snprintf(
                        key, sizeof(key), "%s_%s", cpu_prefix, suffix);
                    fields.boolean(key, value);
                };
                const auto add_cpu_uint = [&fields, &key, &cpu_prefix](
                    const char* suffix, uint64_t value) noexcept {
                    std::snprintf(
                        key, sizeof(key), "%s_%s", cpu_prefix, suffix);
                    fields.uinteger(key, value);
                };
                const auto add_cpu_hex = [&fields, &key, &cpu_prefix](
                    const char* suffix, uint64_t value) noexcept {
                    std::snprintf(
                        key, sizeof(key), "%s_%s", cpu_prefix, suffix);
                    fields.hex(key, static_cast<uintptr_t>(value));
                };
                add_cpu_bool("sched_readable", cpu.sched_readable);
                add_cpu_bool("subvm_readable", cpu.subvm_readable);
                add_cpu_bool(
                    "identity_links_valid", cpu.identity_links_valid);
                add_cpu_hex("sched_state", cpu.sched_state);
                add_cpu_hex("chara", cpu.chara);
                add_cpu_hex("subvm", cpu.subvm);
                add_cpu_hex("subvm_vtable_rva", cpu.subvm_vtable_rva);
                add_cpu_hex("subvm_chara", cpu.subvm_chara);
                add_cpu_hex("subvm_opponent", cpu.subvm_opponent);
                add_cpu_hex(
                    "subvm_owner_sched", cpu.subvm_owner_sched);
                add_cpu_hex(
                    "sched_semantic_hash", cpu.sched_semantic_hash);
                add_cpu_hex(
                    "subvm_common_semantic_hash",
                    cpu.subvm_common_semantic_hash);
                add_cpu_uint("selected_slot", cpu.selected_slot);
                add_cpu_uint("active_slot", cpu.active_slot);
                add_cpu_hex("input_command", cpu.input_command);
                add_cpu_uint(
                    "transition_state", cpu.transition_state);
                add_cpu_hex(
                    "reaction_timer_bits", cpu.reaction_timer_bits);
            }
            add_uint("rng_lfsr_total", sample.lfsr.total_calls);
            add_hex("rng_lfsr_histogram_hash",
                    hash_rng_histogram_snapshot(sample.lfsr));
            add_uint("rng_xorshift96_total",
                     sample.xorshift96.total_calls);
            add_hex("rng_xorshift96_histogram_hash",
                    hash_rng_histogram_snapshot(sample.xorshift96));

            char nested[96] {};
            std::snprintf(nested, sizeof(nested), "%s_p1", prefix);
            add_compact_chara_motion_fields(
                fields, nested, sample.p1);
            std::snprintf(nested, sizeof(nested), "%s_p2", prefix);
            add_compact_chara_motion_fields(
                fields, nested, sample.p2);
        }

        static uint32_t& native_tick_root_depth() noexcept
        {
            static thread_local uint32_t depth = 0;
            return depth;
        }

        static void emit_native_tick_root_transaction(
            const char* root,
            uint64_t entry_sequence,
            uint32_t entry_depth,
            uintptr_t return_address,
            bool original_called,
            const NativeTickRootTraceSample& before,
            const NativeTickRootTraceSample& after) noexcept
        {
            if (!detailed_lifecycle_trace_enabled()) return;
            const uintptr_t image_base = NativeBinding::imageBase();
            const uintptr_t return_rva = image_base != 0
                    && return_address >= image_base
                ? return_address - image_base : 0;
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            ReplayTraceFields fields;
            fields.string("tick_root", root ? root : "?")
                .uinteger("entry_sequence", entry_sequence)
                .uinteger("entry_depth", entry_depth)
                .uinteger("thread_id", GetCurrentThreadId())
                .hex("return_address", return_address)
                .hex("return_rva", return_rva)
                .boolean("original_called", original_called)
                .boolean("rollback_session_active", native_scope.active)
                .boolean("native_scope_owned", native_scope.active)
                .boolean(
                    "rolling_back",
                    native_scope.active && native_scope.rolling_back)
                .string(
                    "simulation_ownership",
                    native_scope.active ? "rollback-owned" : "stock")
                .boolean("round_epoch_available", false)
                .hex("round_epoch", 0)
                .integer(
                    "native_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_frame)
                        : int64_t{-1})
                .integer(
                    "native_applied_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_applied)
                        : int64_t{-1})
                .integer(
                    "logical_frame",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.logical_frame)
                        : int64_t{-1})
                .boolean(
                    "p1_subvm_replaced",
                    before.cpu_subvm[0].subvm
                        != after.cpu_subvm[0].subvm)
                .boolean(
                    "p2_subvm_replaced",
                    before.cpu_subvm[1].subvm
                        != after.cpu_subvm[1].subvm);
            AddRollbackFloatingPointEnvironmentTraceFields(fields);
            add_native_tick_root_sample_fields(fields, "before", before);
            add_native_tick_root_sample_fields(fields, "after", after);
            add_rng_histogram_snapshot_fields(
                fields, "before_rng_lfsr_callers", before.lfsr);
            add_rng_histogram_snapshot_fields(
                fields, "after_rng_lfsr_callers", after.lfsr);
            add_rng_histogram_snapshot_fields(
                fields, "before_rng_xorshift96_callers",
                before.xorshift96);
            add_rng_histogram_snapshot_fields(
                fields, "after_rng_xorshift96_callers",
                after.xorshift96);
            emit("native_battle_tick_root_transaction", fields);
        }

        static void emit_frame_input_log_tick_transaction(
            uint64_t call_sequence,
            uintptr_t return_address,
            bool original_called,
            const FrameInputLogTickTraceSample& before,
            const FrameInputLogTickTraceSample& after) noexcept
        {
            const uintptr_t image_base = NativeBinding::imageBase();
            const uintptr_t return_rva = image_base != 0
                    && return_address >= image_base
                ? return_address - image_base : 0;
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            const uint32_t lifecycle_owner_mask =
                instance().replay_lifecycle_trace_owner_mask();
            const uint32_t rollback_diagnostics_bit =
                static_cast<uint32_t>(
                    ReplayLifecycleTraceOwner::RollbackDiagnostics);
            const bool rollback_diagnostics_lease_active =
                (lifecycle_owner_mask & rollback_diagnostics_bit) != 0;
            const bool master_advanced = before.readable && after.readable
                && after.master_clock != before.master_clock;
            const bool tick_sequence_advanced =
                before.readable && after.readable
                && after.tick_sequence_counter
                    != before.tick_sequence_counter;

            ReplayTraceFields fields;
            fields.uinteger("call_sequence", call_sequence)
                .uinteger("thread_id", GetCurrentThreadId())
                .hex("return_address", return_address)
                .hex("return_rva", return_rva)
                .boolean("original_called", original_called)
                .hex("lifecycle_owner_mask", lifecycle_owner_mask)
                .boolean(
                    "rollback_diagnostics_lease_active",
                    rollback_diagnostics_lease_active)
                .boolean("native_scope_owned", native_scope.active)
                .boolean(
                    "rolling_back",
                    native_scope.active && native_scope.rolling_back)
                .boolean("master_advanced", master_advanced)
                .boolean(
                    "tick_sequence_advanced", tick_sequence_advanced)
                .boolean(
                    "hold_observed",
                    tick_sequence_advanced && !master_advanced)
                .integer(
                    "native_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_frame)
                        : int64_t{-1})
                .integer(
                    "native_applied_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_applied)
                        : int64_t{-1})
                .integer(
                    "logical_frame",
                    native_scope.active
                        ? static_cast<int64_t>(native_scope.logical_frame)
                        : int64_t{-1});
            add_frame_input_log_tick_sample_fields(
                fields, "before", before);
            add_frame_input_log_tick_sample_fields(
                fields, "after", after);
            emit("native_frame_input_log_tick_control", fields);
        }

        static uint64_t hash_rng_histogram_snapshot(
            const RngTraceHook::HistogramSnapshot& snapshot) noexcept
        {
            RollbackHash hash {};
            hash.add_scalar(snapshot.total_calls);
            hash.add_scalar(snapshot.overflow_calls);
            hash.add_scalar(snapshot.caller_count);
            for (uint32_t index = 0;
                 index < snapshot.caller_count
                    && index < snapshot.callers.size();
                 ++index)
            {
                hash.add_scalar(snapshot.callers[index].rva);
                hash.add_scalar(snapshot.callers[index].count);
            }
            return hash.value;
        }

        static void add_rng_histogram_snapshot_fields(
            ReplayTraceFields& f,
            const char* prefix,
            const RngTraceHook::HistogramSnapshot& snapshot) noexcept
        {
            char key[96] {};
            std::snprintf(key, sizeof(key), "%s_total", prefix);
            f.uinteger(key, snapshot.total_calls);
            std::snprintf(key, sizeof(key), "%s_overflow", prefix);
            f.uinteger(key, snapshot.overflow_calls);
            std::snprintf(key, sizeof(key), "%s_caller_count", prefix);
            f.uinteger(key, snapshot.caller_count);
            std::snprintf(key, sizeof(key), "%s_histogram_hash", prefix);
            f.hex(key, static_cast<uintptr_t>(
                hash_rng_histogram_snapshot(snapshot)));
            for (uint32_t index = 0;
                 index < snapshot.caller_count
                    && index < snapshot.callers.size();
                 ++index)
            {
                std::snprintf(
                    key, sizeof(key), "%s_caller_%02u_rva", prefix, index);
                f.hex(key, snapshot.callers[index].rva);
                std::snprintf(
                    key, sizeof(key), "%s_caller_%02u_count", prefix, index);
                f.uinteger(key, snapshot.callers[index].count);
            }
        }

        static void add_crt_rng_snapshot_fields(
            ReplayTraceFields& f,
            const RngTraceHook::CrtSnapshot& snapshot) noexcept
        {
            f.uinteger("rng_crt_total", snapshot.callers.total_calls)
             .uinteger("rng_crt_caller_overflow",
                       snapshot.callers.overflow_calls)
             .hex("rng_crt_caller_histogram_hash",
                  static_cast<uintptr_t>(
                      hash_rng_histogram_snapshot(snapshot.callers)))
             .uinteger("rng_crt_seed_calls", snapshot.seed_calls)
             .uinteger("rng_crt_last_seed", snapshot.last_seed)
             .uinteger("rng_crt_event_count", snapshot.event_count)
             .uinteger("rng_crt_overflow_events",
                       snapshot.overflow_events)
             .hex("rng_crt_sequence_hash",
                  static_cast<uintptr_t>(snapshot.sequence_hash))
             .hex("rng_crt_execution_hash",
                  static_cast<uintptr_t>(snapshot.execution_hash))
             .uinteger("rng_crt_thread_count", snapshot.thread_count)
             .boolean("rng_crt_thread_observed",
                      snapshot.persistent_thread_observed)
             .boolean("rng_crt_thread_seed_known",
                      snapshot.persistent_seed_known)
             .uinteger("rng_crt_thread_last_seed",
                       snapshot.persistent_last_seed)
             .hex("rng_crt_thread_predicted_state",
                  snapshot.persistent_predicted_state)
             .uinteger("rng_crt_thread_expected_next",
                       snapshot.persistent_expected_next)
             .uinteger("rng_crt_thread_draws_since_seed",
                       snapshot.persistent_draws_since_seed)
             .uinteger("rng_crt_thread_seed_generation",
                       snapshot.persistent_seed_generation)
             .uinteger("rng_crt_thread_prediction_mismatches",
                       snapshot.persistent_prediction_mismatches);
            for (uint32_t index = 0;
                 index < snapshot.thread_count
                    && index < snapshot.threads.size();
                 ++index)
            {
                char key[64] {};
                std::snprintf(
                    key, sizeof(key), "rng_crt_thread_%02u_id", index);
                f.uinteger(key, static_cast<uint32_t>(
                    snapshot.threads[index].address));
                std::snprintf(
                    key, sizeof(key), "rng_crt_thread_%02u_count", index);
                f.uinteger(key, snapshot.threads[index].count);
            }
        }

        static void add_pre_main_chara_motion_fields(
            ReplayTraceFields& f,
            const char* prefix,
            void* chara,
            const PreMainCharaMotionTraceSample& sample) noexcept
        {
            char key[112] {};
            const auto add_bool = [&f, &key, prefix](
                const char* suffix, bool value) noexcept {
                std::snprintf(
                    key, sizeof(key), "%s_%s", prefix, suffix);
                f.boolean(key, value);
            };
            const auto add_uint = [&f, &key, prefix](
                const char* suffix, uint64_t value) noexcept {
                std::snprintf(
                    key, sizeof(key), "%s_%s", prefix, suffix);
                f.uinteger(key, value);
            };
            const auto add_hex = [&f, &key, prefix](
                const char* suffix, uint64_t value) noexcept {
                std::snprintf(
                    key, sizeof(key), "%s_%s", prefix, suffix);
                f.hex(key, static_cast<uintptr_t>(value));
            };
            add_hex("chara", reinterpret_cast<uintptr_t>(chara));
            add_uint("current_move_id", sample.current_move_id);
            add_uint("pose_finalize_tick", sample.pose_finalize_tick);
            add_bool("linked_motion_readable",
                     sample.linked_motion_readable);
            add_hex("linked_motion_hash", sample.linked_motion_hash);
            add_bool("pose_publication_readable",
                     sample.pose_publication_readable);
            add_hex("pose_publication_hash",
                    sample.pose_publication_hash);
            add_bool("clip_player_readable", sample.clip_player_readable);
            add_hex("clip_player_scalar_hash",
                    sample.clip_player_scalar_hash);
            add_bool("clip_runtime_readable", sample.clip_runtime_readable);
            add_bool("clip_runtime_present", sample.clip_runtime_present);
            add_hex("clip_runtime_scalar_hash",
                    sample.clip_runtime_scalar_hash);
            add_hex("clip_cached_frame_bits",
                    sample.clip_cached_frame_bits);
            add_hex("clip_owner_frame_bits",
                    sample.clip_owner_frame_bits);
            add_bool("matrix_bank_current_readable",
                     sample.matrix_bank.current_readable);
            add_bool("matrix_bank_previous_readable",
                     sample.matrix_bank.previous_readable);
            add_hex("matrix_bank_current_hash",
                    sample.matrix_bank.current_hash);
            add_hex("matrix_bank_previous_hash",
                    sample.matrix_bank.previous_hash);
            constexpr size_t kBone11 = 11;
            constexpr size_t kMatrixM30Word = 12;
            size_t bone11_diagnostic_index =
                kCollisionDiagnosticBones.size();
            for (size_t index = 0;
                 index < kCollisionDiagnosticBones.size(); ++index)
            {
                if (kCollisionDiagnosticBones[index] == kBone11)
                {
                    bone11_diagnostic_index = index;
                    break;
                }
            }
            add_hex("matrix_bank_current_bone_11_hash",
                    sample.matrix_bank.current_bone_hash[kBone11]);
            add_hex("matrix_bank_previous_bone_11_hash",
                    sample.matrix_bank.previous_bone_hash[kBone11]);
            add_hex("matrix_bank_current_bone_11_m30_bits",
                    bone11_diagnostic_index
                            < kCollisionDiagnosticBones.size()
                        ? sample.matrix_bank.current_diagnostic_bone_bits[
                            bone11_diagnostic_index][kMatrixM30Word]
                        : 0);
            add_hex("matrix_bank_previous_bone_11_m30_bits",
                    bone11_diagnostic_index
                            < kCollisionDiagnosticBones.size()
                        ? sample.matrix_bank.previous_diagnostic_bone_bits[
                            bone11_diagnostic_index][kMatrixM30Word]
                        : 0);
        }

        static void emit_pre_main_motion_checkpoint(
            const char* stage,
            const char* phase,
            void* subject = nullptr,
            void* subject_chara = nullptr) noexcept
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)stage;
            (void)phase;
            (void)subject;
            (void)subject_chara;
            return;
#else
            if (!detailed_lifecycle_trace_enabled()) return;
            void* p1 = chara_slot_from_global(0);
            void* p2 = chara_slot_from_global(1);
            const PreMainCharaMotionTraceSample p1_sample =
                capture_pre_main_chara_motion_sample(p1);
            const PreMainCharaMotionTraceSample p2_sample =
                capture_pre_main_chara_motion_sample(p2);
            const uint64_t causal_p1_transaction =
                s_pose_producer_transaction_seq.load(
                    std::memory_order_acquire) + 1;
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            const RngTraceHook::HistogramSnapshot lfsr =
                RngTraceHook::instance().snapshot_histogram();
            const RngTraceHook::HistogramSnapshot xorshift =
                RngTraceHook::instance().snapshot_xorshift_histogram();
            const RngTraceHook::CrtSnapshot crt =
                RngTraceHook::instance().snapshot_crt();

            ReplayTraceFields f;
            f.uinteger(
                    "checkpoint_sequence",
                    s_pre_main_motion_checkpoint_seq.fetch_add(
                        1, std::memory_order_acq_rel) + 1)
             .string("stage", stage ? stage : "?")
             .string("phase", phase ? phase : "?")
             .hex("subject", reinterpret_cast<uintptr_t>(subject))
             .hex("subject_chara",
                  reinterpret_cast<uintptr_t>(subject_chara))
             .integer("subject_player",
                      player_index_for_chara(subject_chara) + 1)
             .uinteger("causal_p1_transaction_id",
                       causal_p1_transaction)
             .uinteger("causal_p2_transaction_id",
                       causal_p1_transaction + 1)
             .boolean("native_scope_owned", native_scope.active)
             .boolean("rolling_back",
                      native_scope.active && native_scope.rolling_back)
             .string("simulation_ownership",
                     native_scope.active ? "rollback-owned" : "stock")
             .integer("native_coordinate",
                      native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_frame)
                        : int64_t{-1})
             .integer("native_applied_coordinate",
                      native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_applied)
                        : int64_t{-1})
             .integer("logical_frame",
                      native_scope.active
                        ? static_cast<int64_t>(native_scope.logical_frame)
                        : int64_t{-1});
            add_pre_main_chara_motion_fields(f, "p1", p1, p1_sample);
            add_pre_main_chara_motion_fields(f, "p2", p2, p2_sample);
            add_rng_histogram_snapshot_fields(f, "rng_lfsr", lfsr);
            add_rng_histogram_snapshot_fields(
                f, "rng_xorshift96", xorshift);
            add_crt_rng_snapshot_fields(f, crt);
            emit("native_pre_main_motion_checkpoint", f);
#endif
        }

        static CollisionBodyTraceSample
        capture_collision_body_sample(const void* runtime) noexcept
        {
            CollisionBodyTraceSample out {};
            if (!runtime) return out;
            const auto* bytes = static_cast<const uint8_t*>(runtime);
            uintptr_t head = 0;
            uintptr_t scratch_tail = 0;
            out.runtime_readable =
                safe_read_bytes(
                    bytes + 0x400, &head, sizeof(head))
                && safe_read_bytes(
                    bytes + 0x408,
                    &out.scratch_bytes, sizeof(out.scratch_bytes))
                && safe_read_bytes(
                    bytes + 0x40C,
                    &out.max_slot_exclusive,
                    sizeof(out.max_slot_exclusive))
                && safe_read_bytes(
                    bytes + 0x410,
                    &scratch_tail, sizeof(scratch_tail))
                && safe_read_bytes(
                    bytes + 0x418,
                    &out.declared_node_count,
                    sizeof(out.declared_node_count))
                && safe_read_bytes(
                    bytes + 0x45C,
                    &out.phys_body_type, sizeof(out.phys_body_type))
                && safe_read_bytes(
                    bytes + 0x460,
                    &out.phys_body_radius, sizeof(out.phys_body_radius))
                && safe_read_bytes(
                    bytes + 0x464,
                    &out.separation_scale, sizeof(out.separation_scale))
                && safe_read_bytes(
                    bytes + 0x468,
                    &out.upper_offset, sizeof(out.upper_offset))
                && safe_read_bytes(
                    bytes + 0x46C,
                    &out.lower_offset, sizeof(out.lower_offset))
                && safe_read_bytes(
                    bytes + 0x470,
                    &out.impact_force_scale,
                    sizeof(out.impact_force_scale));
            (void)scratch_tail;
            if (!out.runtime_readable) return out;

            RollbackHash list_hash {};
            list_hash.add_scalar(out.declared_node_count);
            std::array<uintptr_t, kCollisionBodyNodeLimit> visited {};
            uintptr_t node = head;
            out.list_readable = true;
            while (node != 0
                   && out.node_hash_count < kCollisionBodyNodeLimit)
            {
                for (uint32_t index = 0;
                     index < out.node_hash_count; ++index)
                {
                    if (visited[index] == node)
                    {
                        out.list_cycle = true;
                        out.list_readable = false;
                        node = 0;
                        break;
                    }
                }
                if (node == 0) break;
                visited[out.node_hash_count] = node;

                std::array<uint8_t, 0x80> node_bytes {};
                std::array<uint8_t, 0x60> semantic {};
                uintptr_t next = 0;
                const bool node_ok =
                    safe_read_bytes(
                        reinterpret_cast<const void*>(node),
                        node_bytes.data(), node_bytes.size());
                if (!node_ok)
                {
                    out.list_readable = false;
                    break;
                }
                std::memcpy(
                    semantic.data(), node_bytes.data() + 0x08, 0x10);
                std::memcpy(
                    semantic.data() + 0x10,
                    node_bytes.data() + 0x30, 0x50);
                std::memcpy(
                    &next, node_bytes.data() + 0x18, sizeof(next));
                auto& typed = out.node[out.node_hash_count];
                std::memcpy(
                    &typed.slot_bit_mask,
                    node_bytes.data() + 0x08,
                    sizeof(typed.slot_bit_mask));
                std::memcpy(
                    &typed.flags,
                    node_bytes.data() + 0x10,
                    sizeof(typed.flags));
                std::memcpy(
                    &typed.active_gate,
                    node_bytes.data() + 0x14,
                    sizeof(typed.active_gate));
                std::memcpy(
                    &typed.stream_type,
                    node_bytes.data() + 0x16,
                    sizeof(typed.stream_type));
                std::memcpy(
                    &typed.slot_or_kind,
                    node_bytes.data() + 0x17,
                    sizeof(typed.slot_or_kind));
                std::memcpy(
                    typed.live_local_center_bits.data(),
                    node_bytes.data() + 0x30,
                    sizeof(typed.live_local_center_bits));
                std::memcpy(
                    typed.authored_local_center_bits.data(),
                    node_bytes.data() + 0x40,
                    sizeof(typed.authored_local_center_bits));
                std::memcpy(
                    typed.world_center_current_bits.data(),
                    node_bytes.data() + 0x50,
                    sizeof(typed.world_center_current_bits));
                std::memcpy(
                    typed.world_center_previous_bits.data(),
                    node_bytes.data() + 0x60,
                    sizeof(typed.world_center_previous_bits));
                std::memcpy(
                    &typed.live_radius_bits,
                    node_bytes.data() + 0x70,
                    sizeof(typed.live_radius_bits));
                std::memcpy(
                    &typed.authored_radius_bits,
                    node_bytes.data() + 0x74,
                    sizeof(typed.authored_radius_bits));
                std::memcpy(
                    &typed.contact_impulse_scale_bits,
                    node_bytes.data() + 0x78,
                    sizeof(typed.contact_impulse_scale_bits));
                std::memcpy(
                    &typed.bone_index,
                    node_bytes.data() + 0x7C,
                    sizeof(typed.bone_index));
                typed.readable = true;
                const uint64_t node_hash =
                    RollbackHashBytes(semantic.data(), semantic.size());
                out.node_semantic_hash[out.node_hash_count++] =
                    node_hash;
                list_hash.add_scalar(node_hash);
                node = next;
            }
            if (node != 0)
                out.list_readable = false;
            out.list_complete =
                out.list_readable
                && !out.list_cycle
                && node == 0
                && out.node_hash_count == out.declared_node_count;
            list_hash.add_scalar(out.node_hash_count);
            list_hash.add_scalar(out.list_complete);
            out.list_semantic_hash = list_hash.value;
            return out;
        }

        static void add_collision_body_fields(
            ReplayTraceFields& f,
            const char* prefix,
            const CollisionBodyTraceSample& sample,
            bool detailed) noexcept
        {
            char key[96] {};
            const auto key_for = [&key, prefix](const char* suffix) {
                std::snprintf(
                    key, sizeof(key), "%s_%s", prefix, suffix);
                return key;
            };
            f.boolean(key_for("khit_runtime_readable"),
                      sample.runtime_readable)
                .boolean(key_for("khit_body_list_readable"),
                         sample.list_readable)
                .boolean(key_for("khit_body_list_complete"),
                         sample.list_complete)
                .boolean(key_for("khit_body_list_cycle"),
                         sample.list_cycle)
                .uinteger(key_for("khit_body_node_count"),
                          sample.declared_node_count)
                .uinteger(key_for("khit_body_node_hash_count"),
                          sample.node_hash_count)
                .integer(key_for("khit_body_scratch_bytes"),
                         sample.scratch_bytes)
                .integer(key_for("khit_body_max_slot_exclusive"),
                         sample.max_slot_exclusive)
                .uinteger(key_for("khit_phys_body_type"),
                          sample.phys_body_type)
                .real(key_for("khit_phys_body_radius"),
                      sample.phys_body_radius)
                .real(key_for("khit_phys_body_separation_scale"),
                      sample.separation_scale)
                .real(key_for("khit_phys_body_upper_offset"),
                      sample.upper_offset)
                .real(key_for("khit_phys_body_lower_offset"),
                      sample.lower_offset)
                .real(key_for("khit_phys_body_impact_force_scale"),
                      sample.impact_force_scale)
                .hex(key_for("khit_body_list_semantic_hash"),
                     static_cast<uintptr_t>(
                         sample.list_semantic_hash));
            for (uint32_t index = 0;
                 index < sample.node_hash_count; ++index)
            {
                std::snprintf(
                    key, sizeof(key),
                    "%s_khit_body_node_%03u_semantic_hash",
                    prefix, index);
                f.hex(
                    key,
                    static_cast<uintptr_t>(
                        sample.node_semantic_hash[index]));
                if (!detailed) continue;
                const auto& node = sample.node[index];
                const auto add_node_uint =
                    [&f, &key, prefix, index](
                        const char* suffix, uint64_t value) noexcept {
                        std::snprintf(
                            key, sizeof(key),
                            "%s_khit_body_node_%03u_%s",
                            prefix, index, suffix);
                        f.uinteger(key, value);
                    };
                const auto add_node_hex =
                    [&f, &key, prefix, index](
                        const char* suffix, uint64_t value) noexcept {
                        std::snprintf(
                            key, sizeof(key),
                            "%s_khit_body_node_%03u_%s",
                            prefix, index, suffix);
                        f.hex(
                            key,
                            static_cast<uintptr_t>(value));
                    };
                add_node_uint("readable", node.readable);
                add_node_hex("slot_bit_mask", node.slot_bit_mask);
                add_node_hex("flags", node.flags);
                add_node_uint("active_gate", node.active_gate);
                add_node_uint("stream_type", node.stream_type);
                add_node_uint("slot_or_kind", node.slot_or_kind);
                static constexpr const char* kComponents[] = {
                    "x", "y", "z", "w",
                };
                for (size_t component = 0; component < 4; ++component)
                {
                    char suffix[64] {};
                    std::snprintf(
                        suffix, sizeof(suffix),
                        "live_local_center_%s_bits",
                        kComponents[component]);
                    add_node_hex(
                        suffix,
                        node.live_local_center_bits[component]);
                    std::snprintf(
                        suffix, sizeof(suffix),
                        "authored_local_center_%s_bits",
                        kComponents[component]);
                    add_node_hex(
                        suffix,
                        node.authored_local_center_bits[component]);
                    std::snprintf(
                        suffix, sizeof(suffix),
                        "world_center_current_%s_bits",
                        kComponents[component]);
                    add_node_hex(
                        suffix,
                        node.world_center_current_bits[component]);
                    std::snprintf(
                        suffix, sizeof(suffix),
                        "world_center_previous_%s_bits",
                        kComponents[component]);
                    add_node_hex(
                        suffix,
                        node.world_center_previous_bits[component]);
                }
                add_node_hex(
                    "live_radius_bits", node.live_radius_bits);
                add_node_hex(
                    "authored_radius_bits", node.authored_radius_bits);
                add_node_hex(
                    "contact_impulse_scale_bits",
                    node.contact_impulse_scale_bits);
                add_node_uint("bone_index", node.bone_index);
            }
        }

        static void add_root_motion_delta_sample_fields(
            ReplayTraceFields& f,
            const char* prefix,
            const RootMotionDeltaTraceSample& sample) noexcept
        {
            char key[96] {};
            std::snprintf(
                key, sizeof(key), "%s_current_readable", prefix);
            f.boolean(key, sample.current_readable);
            std::snprintf(
                key, sizeof(key), "%s_previous_readable", prefix);
            f.boolean(key, sample.previous_readable);
            std::snprintf(key, sizeof(key), "%s_current_hash", prefix);
            f.hex(key, static_cast<uintptr_t>(sample.current_hash));
            std::snprintf(key, sizeof(key), "%s_previous_hash", prefix);
            f.hex(key, static_cast<uintptr_t>(sample.previous_hash));
            for (size_t i = 0; i < sample.current.size(); ++i)
            {
                const size_t row = i / 4u;
                const size_t column = i % 4u;
                std::snprintf(
                    key, sizeof(key), "%s_current_m%zu%zu",
                    prefix, row, column);
                f.real(key, sample.current[i]);
                std::snprintf(
                    key, sizeof(key), "%s_previous_m%zu%zu",
                    prefix, row, column);
                f.real(key, sample.previous[i]);
            }
            std::snprintf(key, sizeof(key), "%s_raw_x", prefix);
            f.real(key, sample.raw_x);
            std::snprintf(key, sizeof(key), "%s_raw_y", prefix);
            f.real(key, sample.raw_y);
            std::snprintf(key, sizeof(key), "%s_raw_z", prefix);
            f.real(key, sample.raw_z);
            std::snprintf(key, sizeof(key), "%s_smoothed_x", prefix);
            f.real(key, sample.smoothed_x);
            std::snprintf(key, sizeof(key), "%s_smoothed_y", prefix);
            f.real(key, sample.smoothed_y);
            std::snprintf(key, sizeof(key), "%s_smoothed_z", prefix);
            f.real(key, sample.smoothed_z);
            std::snprintf(key, sizeof(key), "%s_carry_x", prefix);
            f.real(key, sample.carry_x);
            std::snprintf(key, sizeof(key), "%s_carry_z", prefix);
            f.real(key, sample.carry_z);
            std::snprintf(key, sizeof(key), "%s_carry_mode", prefix);
            f.uinteger(key, sample.carry_mode);
        }

        static void emit_collision_checkpoint(
            const char* stage,
            const char* phase,
            int body_solve_result = -1,
            int p1_collision_result = -1,
            int p2_collision_result = -1,
            const void* p1_khit = nullptr,
            const void* p1_current_matrices = nullptr,
            const void* p2_khit = nullptr,
            const void* p2_current_matrices = nullptr,
            float push_angle_turns = 0.0f,
            bool has_solver_inputs = false) noexcept
        {
            // Collision evidence is large and can be emitted several times
            // per character/frame. Keep it on the same tested, focused
            // coordinate window as the motion producer evidence.
            if (!motion_decode_trace_window().enabled) return;
            const uintptr_t image_base = NativeBinding::imageBase();
            if (!image_base) return;
            void* p1 = reinterpret_cast<void*>(safe_read_uintptr(
                reinterpret_cast<const void*>(
                    image_base + kRVA_CharaSlotP1)));
            void* p2 = reinterpret_cast<void*>(safe_read_uintptr(
                reinterpret_cast<const void*>(
                    image_base + kRVA_CharaSlotP2)));
            const auto& transaction =
                root_motion_delta_trace_transaction();
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            const RootMotionDeltaTraceSample p1_sample =
                capture_root_motion_delta_sample(p1);
            const RootMotionDeltaTraceSample p2_sample =
                capture_root_motion_delta_sample(p2);
            const CollisionMatrixBankTraceSample p1_matrix_bank =
                capture_collision_matrix_bank_sample(
                    p1, p1_current_matrices);
            const CollisionMatrixBankTraceSample p2_matrix_bank =
                capture_collision_matrix_bank_sample(
                    p2, p2_current_matrices);
            const CollisionBodyTraceSample p1_body =
                capture_collision_body_sample(p1_khit);
            const CollisionBodyTraceSample p2_body =
                capture_collision_body_sample(p2_khit);
            const bool detailed_matrix_bank =
                stage
                && phase
                && std::strcmp(stage, "SolvePhysBodyCollision") == 0;
            const bool detailed_khit = detailed_matrix_bank
                && has_solver_inputs;

            ReplayTraceFields f;
            f.uinteger(
                    "transaction_id", transaction.transaction_id())
                .boolean("transaction_active", transaction.active())
                .string("stage", stage ? stage : "?")
                .string("phase", phase ? phase : "?")
                .boolean("rollback_session_active", native_scope.active)
                .boolean("native_scope_owned", native_scope.active)
                .boolean(
                    "rolling_back",
                    native_scope.active && native_scope.rolling_back)
                .string(
                    "simulation_ownership",
                    native_scope.active ? "rollback-owned" : "stock")
                .integer(
                    "native_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_frame)
                        : int64_t{-1})
                .integer(
                    "native_applied_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_applied)
                        : int64_t{-1})
                .integer(
                    "logical_frame",
                    native_scope.active
                        ? static_cast<int64_t>(native_scope.logical_frame)
                        : int64_t{-1})
                .integer("body_solve_result", body_solve_result)
                .integer(
                    "p1_collision_result", p1_collision_result)
                .integer(
                    "p2_collision_result", p2_collision_result)
                .boolean("has_solver_inputs", has_solver_inputs)
                .real("push_angle_turns", push_angle_turns)
                .hex("p1_chara", reinterpret_cast<uintptr_t>(p1))
                .hex("p2_chara", reinterpret_cast<uintptr_t>(p2));
            AddRollbackFloatingPointEnvironmentTraceFields(f);

            const auto add_chara_scalars =
                [&f](const char* prefix, void* chara) noexcept {
                    char key[96] {};
                    const auto* c =
                        static_cast<const uint8_t*>(chara);
                    const auto add_real =
                        [&f, &key, prefix](
                            const char* suffix, float value) noexcept {
                            std::snprintf(
                                key, sizeof(key), "%s_%s",
                                prefix, suffix);
                            f.real(key, value);
                        };
                    const auto add_uint =
                        [&f, &key, prefix](
                            const char* suffix,
                            uint64_t value) noexcept {
                            std::snprintf(
                                key, sizeof(key), "%s_%s",
                                prefix, suffix);
                            f.uinteger(key, value);
                        };
                    add_real("sim_pos_x", safe_read_float(c + 0xA0));
                    add_real("sim_pos_z", safe_read_float(c + 0xA8));
                    add_real("facing", safe_read_float(c + 0x94));
                    add_real(
                        "impact_force_scale",
                        safe_read_float(c + 0x444E8));
                    add_uint(
                        "collision_result",
                        safe_read_uint8(c + 0x16EE));
                    add_uint(
                        "orientation_preserve",
                        safe_read_uint8(c + 0x16E6));
                    add_uint(
                        "pose_finalize_tick",
                        safe_read_uint32(c + 0x20C));
                };
            if (p1) add_chara_scalars("p1", p1);
            if (p2) add_chara_scalars("p2", p2);
            add_root_motion_delta_sample_fields(
                f, "p1_bone1", p1_sample);
            add_root_motion_delta_sample_fields(
                f, "p2_bone1", p2_sample);
            add_collision_matrix_bank_fields(
                f, "p1", p1_matrix_bank, detailed_matrix_bank);
            add_collision_matrix_bank_fields(
                f, "p2", p2_matrix_bank, detailed_matrix_bank);
            add_collision_body_fields(
                f, "p1", p1_body, detailed_khit);
            add_collision_body_fields(
                f, "p2", p2_body, detailed_khit);
            emit("native_collision_checkpoint", f);
        }

        static void emit_pose_producer_checkpoint(
            const char* stage,
            const char* phase,
            void* chara) noexcept
        {
            if (!chara) return;
            if (!motion_decode_trace_window().enabled) return;
            const auto* c = static_cast<const uint8_t*>(chara);
            const auto* anchor = c + kCharaVfxEffectAnchorOffset;
            const auto& transaction = pose_producer_trace_transaction();
            const auto& native_scope = g_rollback_native_simulation_scope;
            const RootMotionDeltaTraceSample sample =
                capture_root_motion_delta_sample(chara);
            const bool detailed_matrix_bank =
                phase && stage
                && (
                    std::strcmp(
                        stage, "TickCharaMainSimulation") == 0
                    || (
                        std::strcmp(phase, "exit") == 0
                        && (
                            std::strcmp(
                                stage,
                                "FinalizeTickPoseAndState") == 0
                            || std::strcmp(
                                stage, "EvaluateBonePose") == 0
                        )
                    )
                );
            const CollisionMatrixBankTraceSample matrix_bank =
                capture_collision_matrix_bank_sample(chara);
            bool pose_root_ok = false;
            bool pose_translation_ok = false;
            bool playback_ok = false;
            const uint64_t pose_root_hash = hash_live_bytes(
                anchor + 0x4F0, 0x0C, &pose_root_ok);
            const uint64_t pose_translation_hash = hash_live_bytes(
                anchor + 0x500, 0x0C, &pose_translation_ok);
            // This 0xC0 record is diagnostic only. It may contain authored
            // pointers, so analyzers must compare its explicit scalar fields
            // before using the aggregate hash to localize a producer.
            const uint64_t playback_hash = hash_live_bytes(
                anchor + 0xB00, 0xC0, &playback_ok);

            ReplayTraceFields f;
            f.uinteger("producer_transaction_id",
                       transaction.transaction_id)
             .uinteger("producer_checkpoint_ordinal",
                       static_cast<uint64_t>(transaction.ledger.count))
             .boolean("producer_sequence_valid",
                      transaction.ledger.valid)
             .boolean("producer_sequence_complete",
                      transaction.ledger.complete())
             .string("stage", stage ? stage : "?")
             .string("phase", phase ? phase : "?")
             .integer("player",
                      transaction.player_index >= 0
                        ? transaction.player_index + 1
                        : player_index_for_chara(chara) + 1)
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .integer("replay_frame", safe_read_int32(c + 0x3A0))
             .integer("replay_master", safe_read_int32(c + 0x3A4))
             .uinteger("move_id", safe_read_uint32(c + 0x324))
             .real("clip_frame", safe_read_float(
                 c + ReplayScrubDiag::kChara_flCurrentClipFrame_Off))
             .uinteger("pose_finalize_tick", safe_read_uint32(c + 0x20C))
             .uinteger("post_finalize_pose_mode",
                       safe_read_uint16(c + 0x19E6))
             .boolean("rollback_session_active", native_scope.active)
             .boolean("native_scope_owned", native_scope.active)
             .boolean("rolling_back",
                      native_scope.active && native_scope.rolling_back)
             .string("simulation_ownership",
                     native_scope.active ? "rollback-owned" : "stock")
             .integer("native_coordinate",
                      native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_frame)
                        : int64_t{-1})
             .integer("native_applied_coordinate",
                      native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_applied)
                        : int64_t{-1})
             .integer("logical_frame",
                      native_scope.active
                        ? static_cast<int64_t>(native_scope.logical_frame)
                        : int64_t{-1})
             .boolean("pose_root_readable", pose_root_ok)
             .hex("pose_root_hash",
                  static_cast<uintptr_t>(pose_root_hash))
             .real("pose_root_pitch", safe_read_float(anchor + 0x4F0))
             .real("pose_root_yaw", safe_read_float(anchor + 0x4F4))
             .real("pose_root_roll", safe_read_float(anchor + 0x4F8))
             .boolean("pose_translation_readable", pose_translation_ok)
             .hex("pose_translation_hash",
                  static_cast<uintptr_t>(pose_translation_hash))
             .real("pose_translation_x", safe_read_float(anchor + 0x500))
             .real("pose_translation_y", safe_read_float(anchor + 0x504))
             .real("pose_translation_z", safe_read_float(anchor + 0x508))
             .hex("active_motion_identity",
                  safe_read_uintptr(anchor + 0xB18))
             .boolean("playback_record_readable", playback_ok)
             .hex("playback_record_diagnostic_hash",
                  static_cast<uintptr_t>(playback_hash));
            add_root_motion_delta_sample_fields(f, "bone1", sample);
            add_collision_matrix_bank_fields(
                f, "pose", matrix_bank, detailed_matrix_bank);
            emit("native_pose_producer_checkpoint", f);
        }

        static void emit_chara_lifecycle(const char* stage,
                                         const char* phase,
                                         void* chara) noexcept
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)stage;
            (void)phase;
            (void)chara;
            return;
#else
            if (!detailed_lifecycle_trace_enabled()) return;
            if (!chara) return;
            uint8_t* c = static_cast<uint8_t*>(chara);
            const int pi = player_index_for_chara(chara);

            ReplayTraceFields f;
            f.string("stage", stage ? stage : "?")
             .string("phase", phase ? phase : "?")
             .integer("player", pi >= 0 ? pi + 1 : 0)
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .integer("replay_frame", safe_read_int32(c + 0x3A0))
             .integer("replay_master", safe_read_int32(c + 0x3A4))
             .uinteger("move_id", safe_read_uint32(c + 0x324))
             .real("clip_frame", safe_read_float(
                 c + ReplayScrubDiag::kChara_flCurrentClipFrame_Off))
             .real("pos_x", safe_read_float(c + 0xA0))
             .real("pos_z", safe_read_float(c + 0xA8))
             .real("step_x", safe_read_float(c + 0xC0))
             .real("step_z", safe_read_float(c + 0xC8))
             .real("facing", safe_read_float(c + 0x94))
             .real("opponent_distance", safe_read_float(c + 0x15A0))
             .real("opponent_angle", safe_read_float(c + 0x15A4))
             .real("ground_vel_x", safe_read_float(c + 0x140))
              .real("ground_vel_z", safe_read_float(c + 0x148))
              .real("one_shot_x", safe_read_float(c + 0x150))
              .real("one_shot_z", safe_read_float(c + 0x158))
              .uinteger("hitstun_16db", safe_read_uint8(c + 0x16DB))
              .uinteger("input_freeze_16e7", safe_read_uint8(c + 0x16E7))
              .uinteger("move_transition_state", safe_read_uint16(c + 0x198C))
              .uinteger("hit_slide_slot", safe_read_uint32(c + 0x1E0))
              .hex("latched_hit_slide_input_dir", safe_read_uint32(c + 0x1E4))
              .uinteger("hit_slide_state", safe_read_uint16(c + 0x1994))
              .integer("hit_slide_frame_timer", safe_read_int32(c + 0x43D90))
              .integer("hit_slide_frame_timer_mirror", safe_read_int32(c + 0x43D94))
              .hex("hit_reaction_result", safe_read_uint32(c + 0x43DA0))
              .real("root_delta_x", safe_read_float(c + 0x180))
              .real("root_delta_z", safe_read_float(c + 0x188));
            add_provider_hashes(f, "provider", c);
            emit("native_chara_lifecycle", f);
#endif
        }

        static void emit_chara_state_probe(const char* stage,
                                           const char* phase,
                                           void* chara) noexcept
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)stage;
            (void)phase;
            (void)chara;
            return;
#else
            if (!detailed_lifecycle_trace_enabled()) return;
            if (!chara) return;
            uint8_t* c = static_cast<uint8_t*>(chara);
            const int pi = player_index_for_chara(chara);

            ReplayTraceFields f;
            f.string("stage", stage ? stage : "?")
             .string("phase", phase ? phase : "?")
             .integer("player", pi >= 0 ? pi + 1 : 0)
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .real("pos_x", safe_read_float(c + 0xA0))
             .real("step_x", safe_read_float(c + 0xC0))
             .real("root_motion_carry_x", safe_read_float(c + 0x1C0))
             .real("expected_motion_x", safe_read_float(c + 0x1C0))
             .real("root_delta_x", safe_read_float(c + 0x180))
             .real("ground_vel_x", safe_read_float(c + 0x140))
             .real("one_shot_x", safe_read_float(c + 0x150))
             .uinteger("hitstun_16db", safe_read_uint8(c + 0x16DB))
             .uinteger("blockstun_16dc", safe_read_uint8(c + 0x16DC))
             .uinteger("input_freeze_16e7", safe_read_uint8(c + 0x16E7))
             .uinteger("move_transition_state", safe_read_uint16(c + 0x198C))
             .uinteger("hit_slide_slot", safe_read_uint32(c + 0x1E0))
             .hex("latched_hit_slide_input_dir", safe_read_uint32(c + 0x1E4))
             .uinteger("hit_slide_state", safe_read_uint16(c + 0x1994))
             .integer("hit_slide_frame_timer", safe_read_int32(c + 0x43D90))
             .integer("hit_slide_frame_timer_mirror",
                      safe_read_int32(c + 0x43D94))
             .hex("hit_reaction_result", safe_read_uint32(c + 0x43DA0));
            emit("native_chara_state_probe", f);
#endif
        }

        static void emit_movevm_lane_probe(const char* stage,
                                           const char* phase,
                                           void* chara,
                                           int lane) noexcept
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)stage;
            (void)phase;
            (void)chara;
            (void)lane;
            return;
#else
            if (!detailed_lifecycle_trace_enabled()) return;
            if (!chara) return;
            uint8_t* c = static_cast<uint8_t*>(chara);
            const int pi = player_index_for_chara(chara);
            const bool lane_valid = lane >= 0 && lane < 3;
            uint8_t* l = lane_valid
                ? c + 0x444F0 + static_cast<uintptr_t>(lane) * 0x468
                : nullptr;
            const int other_lane_index = l ? safe_read_int16(l + 0x56) : -1;
            uint8_t* other_l = (other_lane_index >= 0 && other_lane_index < 3)
                ? c + 0x444F0 + static_cast<uintptr_t>(other_lane_index) * 0x468
                : nullptr;
            const bool uses_other_prev_cursor =
                l && other_l && lane == 1 && safe_read_int16(other_l + 0x00) == 0;
            const float other_transition_cursor = other_l
                ? safe_read_float(other_l + (uses_other_prev_cursor ? 0x20 : 0x08))
                : -1.0f;

            ReplayTraceFields f;
            f.string("stage", stage ? stage : "MoveVM")
             .string("phase", phase ? phase : "?")
             .integer("player", pi >= 0 ? pi + 1 : 0)
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .integer("lane_arg", lane)
             .boolean("lane_valid", lane_valid)
             .real("pos_x", safe_read_float(c + 0xA0))
             .real("step_x", safe_read_float(c + 0xC0))
             .real("root_motion_carry_x", safe_read_float(c + 0x1C0))
             .real("expected_motion_x", safe_read_float(c + 0x1C0))
             .real("root_delta_x", safe_read_float(c + 0x180))
             .uinteger("hitstun_16db", safe_read_uint8(c + 0x16DB))
             .uinteger("flag_16eb", safe_read_uint8(c + 0x16EB))
             .uinteger("flag_16fe", safe_read_uint8(c + 0x16FE))
             .uinteger("move_transition_state", safe_read_uint16(c + 0x198C))
             .integer("lane_packed_move", l ? safe_read_int16(l + 0x02) : -1)
             .integer("lane_tick_counter", l ? safe_read_int32(l + 0x04) : -1)
             .real("lane_current_frame", l ? safe_read_float(l + 0x08) : -1.0f)
             .real("lane_prev_frame", l ? safe_read_float(l + 0x0C) : -1.0f)
             .real("lane_length_frames", l ? safe_read_float(l + 0x10) : -1.0f)
             .uinteger("lane_at_end", l ? safe_read_uint16(l + 0x1A) : 0)
             .uinteger("lane_frame_step_finished",
                       l ? safe_read_uint16(l + 0x24) : 0)
             .uinteger("lane_in_transition", l ? safe_read_uint16(l + 0x26) : 0)
             .uinteger("lane_transition_fired_marker",
                       l ? safe_read_uint16(l + 0x2C) : 0)
             .real("lane_playback_speed", l ? safe_read_float(l + 0x30) : -1.0f)
             .integer("lane_transition_target_5a",
                      l ? safe_read_int16(l + 0x5A) : -1)
             .integer("lane_transition_target_16fe_5c",
                      l ? safe_read_int16(l + 0x5C) : -1)
             .integer("lane_transition_target_16eb_5e",
                      l ? safe_read_int16(l + 0x5E) : -1)
             .integer("lane_transition_target_mode2_60",
                      l ? safe_read_int16(l + 0x60) : -1)
             .real("lane_transition_start_frame_64",
                   l ? safe_read_float(l + 0x64) : -1.0f)
             .integer("lane_other_lane_index_56", other_lane_index)
             .real("lane_transition_threshold_frame_68",
                   l ? safe_read_float(l + 0x68) : -1.0f)
             .uinteger("lane_script_arg_count_6c",
                       l ? safe_read_uint16(l + 0x6C) : 0)
             .uinteger("lane_pvar_count_90",
                       l ? safe_read_uint32(l + 0x90) : 0)
             .integer("other_lane_packed_move",
                      other_l ? safe_read_int16(other_l + 0x02) : -1)
             .integer("other_lane_tick_counter",
                      other_l ? safe_read_int32(other_l + 0x04) : -1)
             .real("other_lane_current_frame",
                   other_l ? safe_read_float(other_l + 0x08) : -1.0f)
             .real("other_lane_prev_frame_dup_20",
                   other_l ? safe_read_float(other_l + 0x20) : -1.0f)
             .boolean("uses_other_prev_cursor", uses_other_prev_cursor)
             .real("other_transition_cursor", other_transition_cursor)
             .uinteger("lane_anim_variant", l ? safe_read_uint32(l + 0x460) : 0);
            emit("native_movevm_lane_probe", f);
#endif
        }

        static uint8_t* movevm_lane_for_index(void* chara, int lane) noexcept
        {
            if (!chara || lane < 0 || lane >= 3) return nullptr;
            return static_cast<uint8_t*>(chara) + 0x444F0
                + static_cast<uintptr_t>(lane) * 0x468;
        }

        static uint8_t* movevm_executing_lane(void* chara) noexcept
        {
            if (!chara) return nullptr;
            const uintptr_t p = safe_read_uintptr(
                static_cast<uint8_t*>(chara) + 0x455A0);
            return reinterpret_cast<uint8_t*>(p);
        }

        static uint64_t safe_hash_bytes(const void* source,
                                        size_t bytes) noexcept
        {
            if (!source || bytes == 0) return 0;
            uint64_t hash = 1469598103934665603ull;
            __try
            {
                const uint8_t* data =
                    static_cast<const uint8_t*>(source);
                for (size_t i = 0; i < bytes; ++i)
                {
                    hash ^= data[i];
                    hash *= 1099511628211ull;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0;
            }
            return hash;
        }

        static bool should_emit_movevm_inner_probe(void* chara,
                                                   uint8_t* lane) noexcept
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)chara;
            (void)lane;
            return false;
#else
            if (!chara || !lane) return false;
            const int player = player_index_for_chara(chara);
            if (safe_read_int16(lane + 0x00) != 1) return false;
            const int move = safe_read_int16(lane + 0x02);
            const int target = safe_read_int16(lane + 0x5A);
            const float frame = safe_read_float(lane + 0x08);
            if (player == 1 && move == 562) return true;
            if (player != 0) return false;
            if (target == 146) return true;
            return move == 8843 && frame >= 35.0f && frame <= 50.0f;
#endif
        }

        static void append_movevm_global_flags(ReplayTraceFields& f) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return;
            f.integer("global_transition_threshold_now",
                      safe_read_int32(reinterpret_cast<const void*>(
                          base + kMoveVMTransitionThresholdNowFlagRVA)))
             .integer("global_deferred_schedule_flag",
                      safe_read_int16(reinterpret_cast<const void*>(
                          base + kMoveVMDeferredTransitionScheduleFlagRVA)))
             .real("global_deferred_schedule_frame",
                   safe_read_float(reinterpret_cast<const void*>(
                       base + kMoveVMDeferredTransitionScheduleFrameRVA)))
             .integer("global_deferred_commit_flag",
                       safe_read_int16(reinterpret_cast<const void*>(
                           base + kMoveVMDeferredTransitionCommitFlagRVA)));
        }

        static const uint8_t* movevm_global_var_bank_for_chara(
            const uint8_t* chara) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base || !chara) return nullptr;
            uint32_t slot = safe_read_uint8(chara + 0x23C);
            if (slot > 1)
            {
                const int pi = player_index_for_chara(
                    const_cast<uint8_t*>(chara));
                if (pi < 0 || pi > 1) return nullptr;
                slot = static_cast<uint32_t>(pi);
            }
            return reinterpret_cast<const uint8_t*>(
                base + kMoveVMGlobalVarBankBaseRVA
                + static_cast<uintptr_t>(slot) * 0x1E0);
        }

        static void append_movevm_predicate_state(ReplayTraceFields& f,
                                                   const uint8_t* chara,
                                                   const uint8_t* lane) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            const uint8_t* globals = movevm_global_var_bank_for_chara(chara);
            uint32_t slot = chara ? safe_read_uint8(chara + 0x23C) : 0;
            if (slot > 1)
            {
                const int pi = player_index_for_chara(
                    const_cast<uint8_t*>(chara));
                slot = (pi >= 0 && pi <= 1) ? static_cast<uint32_t>(pi) : 0;
            }
            const uint64_t latest_p1 = base
                ? safe_read_uint64(reinterpret_cast<const void*>(
                    base + kRVA_LatestEngineInput)) : 0;
            const uint64_t latest_p2 = base
                ? safe_read_uint64(reinterpret_cast<const void*>(
                    base + kRVA_LatestEngineInput + sizeof(uint64_t))) : 0;
            const uint32_t cursor_p1 = base
                ? safe_read_uint32(reinterpret_cast<const void*>(
                    base + kRVA_PerPlayerInputCursor)) : 0;
            const uint32_t cursor_p2 = base
                ? safe_read_uint32(reinterpret_cast<const void*>(
                    base + kRVA_PerPlayerInputCursor + sizeof(uint32_t))) : 0;
            const uint32_t ring_base_p1 = base
                ? safe_read_uint32(reinterpret_cast<const void*>(
                    base + kRVA_InputRingBaseOffset)) : 0;
            const uint32_t ring_base_p2 = base
                ? safe_read_uint32(reinterpret_cast<const void*>(
                    base + kRVA_InputRingBaseOffset + sizeof(uint32_t))) : 0;
            const uint32_t slot_cursor = slot == 0 ? cursor_p1 : cursor_p2;
            const uint32_t slot_base = slot == 0 ? ring_base_p1 : ring_base_p2;
            const uintptr_t slot_ring_base = base
                ? base + kRVA_PerPlayerInputRing
                    + static_cast<uintptr_t>(slot) * 0x3D * sizeof(uint64_t)
                : 0;
            const uint64_t slot_ring_read_qword = slot_ring_base
                ? safe_read_uint64(reinterpret_cast<const void*>(
                    slot_ring_base
                    + static_cast<uintptr_t>(slot_cursor % 0x3D)
                        * sizeof(uint64_t))) : 0;
            const uint64_t slot_ring_write_qword = slot_ring_base
                ? safe_read_uint64(reinterpret_cast<const void*>(
                    slot_ring_base
                    + static_cast<uintptr_t>((slot_base + slot_cursor) % 0x3D)
                        * sizeof(uint64_t))) : 0;
            const uintptr_t command_player = base
                ? base + kMoveVMCommandPlayerArrayRVA
                    + static_cast<uintptr_t>(slot) * 0x3038
                : 0;
            const uintptr_t move_system_pump = base
                ? base + kMoveSystemPumpStateRVA : 0;
            f.integer("lane_frame_delta_this_tick_1c",
                      lane ? safe_read_int16(lane + 0x1C) : 0)
             .integer("input_source_move_id_324",
                      chara ? safe_read_int32(chara + 0x324) : 0)
             .uinteger("input_side_flag_16e4",
                       chara ? safe_read_uint8(chara + 0x16E4) : 0)
             .hex("input_word_2150", chara ? safe_read_uint32(chara + 0x2150) : 0)
             .hex("input_word_2154", chara ? safe_read_uint32(chara + 0x2154) : 0)
             .hex("input_word_2158", chara ? safe_read_uint32(chara + 0x2158) : 0)
             .hex("input_word_215c", chara ? safe_read_uint32(chara + 0x215C) : 0)
             .hex("input_word_2160", chara ? safe_read_uint32(chara + 0x2160) : 0)
             .hex("input_word_2164", chara ? safe_read_uint32(chara + 0x2164) : 0)
             .hex("input_word_2168", chara ? safe_read_uint32(chara + 0x2168) : 0)
             .hex("input_word_216c", chara ? safe_read_uint32(chara + 0x216C) : 0)
             .hex("input_word_2170", chara ? safe_read_uint32(chara + 0x2170) : 0)
             .hex("input_word_2174", chara ? safe_read_uint32(chara + 0x2174) : 0)
             .hex("input_word_2178", chara ? safe_read_uint32(chara + 0x2178) : 0)
             .hex("input_word_217c", chara ? safe_read_uint32(chara + 0x217C) : 0)
             .hex("input_word_2180", chara ? safe_read_uint32(chara + 0x2180) : 0)
             .hex("input_word_2184", chara ? safe_read_uint32(chara + 0x2184) : 0)
             .uinteger("input_history_cursor_2188",
                       chara ? safe_read_uint32(chara + 0x2188) : 0)
             .hex("latest_engine_input_p1", latest_p1)
             .hex("latest_engine_input_p2", latest_p2)
             .hex("latest_engine_input_for_chara", slot == 0 ? latest_p1 : latest_p2)
             .uinteger("input_ring_cursor_global_p1", cursor_p1)
             .uinteger("input_ring_cursor_global_p2", cursor_p2)
             .uinteger("input_ring_base_global_p1", ring_base_p1)
             .uinteger("input_ring_base_global_p2", ring_base_p2)
             .hex("input_ring_read_qword_for_chara", slot_ring_read_qword)
             .hex("input_ring_write_qword_for_chara", slot_ring_write_qword)
             .hex("input_history_hash",
                  chara ? safe_hash_bytes(chara + 0x2190, 0xF00) : 0)
             .hex("transition_cache_hash",
                  chara ? safe_hash_bytes(chara + 0x3090, 0x30C) : 0)
             .hex("command_player_hash",
                  command_player
                      ? safe_hash_bytes(
                            reinterpret_cast<const void*>(command_player),
                            0x3038)
                      : 0)
             .hex("move_system_pump_hash",
                  move_system_pump
                      ? safe_hash_bytes(
                            reinterpret_cast<const void*>(move_system_pump),
                            0x88)
                      : 0)
             .hex("secondary_action_stack_hash",
                  chara ? safe_hash_bytes(chara + 0x95788, 0x260) : 0)
             .hex("movevm_global_var_bank_hash",
                  globals ? safe_hash_bytes(globals, 0x1E0) : 0)
             .uinteger("flag_16e1", chara ? safe_read_uint8(chara + 0x16E1) : 0)
             .uinteger("input_freeze_16e7",
                        chara ? safe_read_uint8(chara + 0x16E7) : 0)
             .uinteger("flag_16f9", chara ? safe_read_uint8(chara + 0x16F9) : 0)
             .integer("movevm_global_003e",
                      globals ? safe_read_int16(globals + 0x3E * 2) : 0)
             .integer("movevm_global_0041",
                      globals ? safe_read_int16(globals + 0x41 * 2) : 0);
        }

        static void emit_movevm_bytecode_probe(const char* phase,
                                               void* chara,
                                               void* bytecode,
                                               uint16_t arg_count,
                                               uint16_t* args,
                                               int result) noexcept
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)phase;
            (void)chara;
            (void)bytecode;
            (void)arg_count;
            (void)args;
            (void)result;
            return;
#else
            uint8_t* lane = movevm_executing_lane(chara);
            if (!should_emit_movevm_inner_probe(chara, lane)) return;

            const uintptr_t base = NativeBinding::imageBase();
            uint8_t* c = static_cast<uint8_t*>(chara);
            const uintptr_t bytecode_addr = reinterpret_cast<uintptr_t>(bytecode);
            const uintptr_t bytecode_rva =
                (base && bytecode_addr >= base) ? bytecode_addr - base : 0;
            const uintptr_t bank = safe_read_uintptr(c + 0x455C0);
            const uintptr_t bytecode_bank_offset =
                (bank && bytecode_addr >= bank) ? bytecode_addr - bank : 0;
            ReplayTraceFields f;
            f.string("stage", "MoveVMRunBytecodeScript")
             .string("phase", phase ? phase : "?")
             .integer("player", player_index_for_chara(chara) + 1)
             .integer("lane_arg", safe_read_int16(lane + 0x00))
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .hex("bytecode", bytecode_addr)
             .hex("bytecode_rva", bytecode_rva)
             .hex("move_bank", bank)
             .hex("bytecode_bank_offset", bytecode_bank_offset)
             .string("bytecode_head", safe_read_hex_string(bytecode, 48))
             .uinteger("active_slot_context", safe_read_uint16(c + 0x1C68))
             .uinteger("arg_count", arg_count)
             .hex("args", reinterpret_cast<uintptr_t>(args))
             .integer("result", result)
             .integer("lane_packed_move", safe_read_int16(lane + 0x02))
             .integer("lane_tick_counter", safe_read_int32(lane + 0x04))
             .real("lane_current_frame", safe_read_float(lane + 0x08))
              .integer("lane_transition_target_5a", safe_read_int16(lane + 0x5A))
              .real("lane_transition_threshold_frame_68",
                    safe_read_float(lane + 0x68))
              .uinteger("hitstun_16db", safe_read_uint8(c + 0x16DB))
              .uinteger("move_transition_state",
                        safe_read_uint16(c + 0x198C));
            for (int i = 0; i < 8; ++i)
            {
                char name[16];
                std::snprintf(name, sizeof(name), "arg%d", i);
                f.integer(name, (args && i < arg_count)
                    ? safe_read_int16(args + i) : 0);
            }
            append_movevm_global_flags(f);
            append_movevm_predicate_state(f, c, lane);
            emit("native_movevm_bytecode_probe", f);
#endif
        }

        static void emit_movevm_transition_call_probe(const char* phase,
                                                      void* chara,
                                                      int lane,
                                                      uint32_t packed_move,
                                                      uint32_t start_time,
                                                      int result) noexcept
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)phase;
            (void)chara;
            (void)lane;
            (void)packed_move;
            (void)start_time;
            (void)result;
            return;
#else
            uint8_t* lane_ptr = movevm_lane_for_index(chara, lane);
            const bool interesting = should_emit_movevm_inner_probe(chara, lane_ptr)
                || (player_index_for_chara(chara) == 0 && lane == 1
                    && static_cast<int32_t>(packed_move) == 146);
            if (!interesting) return;

            ReplayTraceFields f;
            f.string("stage", "MoveVMTransitionToMove")
             .string("phase", phase ? phase : "?")
             .integer("player", player_index_for_chara(chara) + 1)
             .integer("lane_arg", lane)
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .integer("packed_move_arg", static_cast<int32_t>(packed_move))
             .uinteger("start_time_arg", start_time)
             .integer("result", result)
             .integer("lane_packed_move",
                      lane_ptr ? safe_read_int16(lane_ptr + 0x02) : -1)
             .integer("lane_tick_counter",
                      lane_ptr ? safe_read_int32(lane_ptr + 0x04) : -1)
             .real("lane_current_frame",
                   lane_ptr ? safe_read_float(lane_ptr + 0x08) : -1.0f)
              .integer("lane_transition_target_5a",
                       lane_ptr ? safe_read_int16(lane_ptr + 0x5A) : -1)
              .real("lane_transition_threshold_frame_68",
                    lane_ptr ? safe_read_float(lane_ptr + 0x68) : -1.0f)
              .uinteger("hitstun_16db",
                        safe_read_uint8(static_cast<uint8_t*>(chara) + 0x16DB))
              .uinteger("move_transition_state",
                        safe_read_uint16(static_cast<uint8_t*>(chara) + 0x198C));
            append_movevm_global_flags(f);
            append_movevm_predicate_state(
                f, static_cast<uint8_t*>(chara), lane_ptr);
            emit("native_movevm_transition_call", f);
#endif
        }

        static void emit_movevm_bank_slot_script_probe(const char* phase,
                                                       void* chara,
                                                       int arg_count,
                                                       int16_t* args,
                                                       uint64_t result,
                                                       uintptr_t return_address)
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)phase;
            (void)chara;
            (void)arg_count;
            (void)args;
            (void)result;
            (void)return_address;
            return;
#else
            uint8_t* lane = movevm_executing_lane(chara);
            const int slot_arg = args ? safe_read_int16(args) : -1;
            const bool interesting = should_emit_movevm_inner_probe(chara, lane)
                || (player_index_for_chara(chara) == 0
                    && (slot_arg == 100 || slot_arg == 101));
            if (!interesting) return;

            uint8_t* c = static_cast<uint8_t*>(chara);
            ReplayTraceFields f;
            f.string("stage", "MoveVMExecuteBankSlotScript")
             .string("phase", phase ? phase : "?")
             .integer("player", player_index_for_chara(chara) + 1)
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .hex("return_address", return_address)
             .integer("arg_count", arg_count)
             .integer("slot_arg", slot_arg)
             .integer("result", static_cast<int16_t>(result & 0xffff))
             .uinteger("active_slot_context", safe_read_uint16(c + 0x1C68))
             .integer("lane_arg", lane ? safe_read_int16(lane + 0x00) : -1)
             .integer("lane_packed_move", lane ? safe_read_int16(lane + 0x02) : -1)
             .integer("lane_tick_counter", lane ? safe_read_int32(lane + 0x04) : -1)
             .real("lane_current_frame", lane ? safe_read_float(lane + 0x08) : -1.0f)
              .integer("lane_transition_target_5a",
                       lane ? safe_read_int16(lane + 0x5A) : -1)
              .real("lane_transition_threshold_frame_68",
                    lane ? safe_read_float(lane + 0x68) : -1.0f)
              .uinteger("hitstun_16db", safe_read_uint8(c + 0x16DB))
              .uinteger("move_transition_state", safe_read_uint16(c + 0x198C));
            for (int i = 0; i < 8; ++i)
            {
                char name[16];
                std::snprintf(name, sizeof(name), "arg%d", i);
                f.integer(name, (args && i < arg_count)
                    ? safe_read_int16(args + i) : 0);
            }
            append_movevm_global_flags(f);
            append_movevm_predicate_state(f, c, lane);
            emit("native_movevm_bank_slot_script", f);
#endif
        }

        static void emit_movevm_transition_author_probe(const char* phase,
                                                        void* chara,
                                                        int arg_count,
                                                        uint16_t* args,
                                                        int lane_idx,
                                                        uint64_t result,
                                                        uintptr_t return_address)
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)phase;
            (void)chara;
            (void)arg_count;
            (void)args;
            (void)lane_idx;
            (void)result;
            (void)return_address;
            return;
#else
            uint8_t* lane = movevm_lane_for_index(chara, lane_idx);
            const int next_move = args ? safe_read_int16(args) : -1;
            const bool interesting = should_emit_movevm_inner_probe(chara, lane)
                || (player_index_for_chara(chara) == 0 && next_move == 146);
            if (!interesting) return;

            uint8_t* c = static_cast<uint8_t*>(chara);
            ReplayTraceFields f;
            f.string("stage", "MoveVMDecodeVariadicStreamArgs")
             .string("phase", phase ? phase : "?")
             .integer("player", player_index_for_chara(chara) + 1)
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .hex("return_address", return_address)
             .integer("arg_count", arg_count)
             .integer("lane_arg", lane_idx)
             .integer("next_move_arg", next_move)
             .integer("result", static_cast<int16_t>(result & 0xffff))
             .uinteger("active_slot_context", safe_read_uint16(c + 0x1C68))
             .integer("lane_packed_move", lane ? safe_read_int16(lane + 0x02) : -1)
             .integer("lane_tick_counter", lane ? safe_read_int32(lane + 0x04) : -1)
             .real("lane_current_frame", lane ? safe_read_float(lane + 0x08) : -1.0f)
             .integer("lane_transition_target_5a",
                      lane ? safe_read_int16(lane + 0x5A) : -1)
             .real("lane_transition_start_frame_64",
                   lane ? safe_read_float(lane + 0x64) : -1.0f)
              .real("lane_transition_threshold_frame_68",
                    lane ? safe_read_float(lane + 0x68) : -1.0f)
              .uinteger("hitstun_16db", safe_read_uint8(c + 0x16DB))
              .uinteger("move_transition_state", safe_read_uint16(c + 0x198C));
            for (int i = 0; i < 8; ++i)
            {
                char name[16];
                std::snprintf(name, sizeof(name), "arg%d", i);
                f.integer(name, (args && i < arg_count)
                    ? safe_read_int16(args + i) : 0);
            }
            append_movevm_global_flags(f);
            append_movevm_predicate_state(f, c, lane);
            emit("native_movevm_transition_author", f);
#endif
        }

        static void emit_secondary_action_stack_push_probe(
            void* stack_base,
            void* chara_back_ptr,
            int event_id,
            uint32_t sub_id_mode,
            void* payload,
            uint32_t extra,
            uint32_t result,
            uint32_t variant_before,
            uint32_t variant_after,
            uint64_t rng_calls_before,
            uint64_t rng_calls_after,
            uintptr_t return_address) noexcept
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)stack_base;
            (void)chara_back_ptr;
            (void)event_id;
            (void)sub_id_mode;
            (void)payload;
            (void)extra;
            (void)result;
            (void)variant_before;
            (void)variant_after;
            (void)rng_calls_before;
            (void)rng_calls_after;
            (void)return_address;
            return;
#else
            static constexpr uintptr_t kCharaSecondaryActionStackOff = 0x95788;
            static constexpr uintptr_t kStackHeaderTableOff = 0x248;
            static constexpr uintptr_t kStackEnabledCountOff = 0x258;

            if (!ReplayDebugTrace::instance().enabled()) return;
            if (!RngTraceHook::instance().active()) return;

            const uintptr_t base = NativeBinding::imageBase();
            const uintptr_t stack_addr = reinterpret_cast<uintptr_t>(stack_base);
            const uintptr_t chara_from_stack = stack_addr >= kCharaSecondaryActionStackOff
                ? stack_addr - kCharaSecondaryActionStackOff : 0;
            const uintptr_t caller_rva = (base && return_address >= base)
                ? return_address - base : 0;
            const std::string caller_fn =
                ReplayDebugTrace::instance().format_absolute_rip(return_address);

            const auto* stack = static_cast<const uint8_t*>(stack_base);
            const uintptr_t header_table = stack
                ? safe_read_uintptr(stack + kStackHeaderTableOff) : 0;
            const int32_t enabled_count = stack
                ? safe_read_int32(stack + kStackEnabledCountOff) : 0;
            const int32_t variant_count =
                (header_table && event_id >= 0)
                    ? static_cast<int32_t>(safe_read_int16(
                        reinterpret_cast<const void*>(
                            header_table + static_cast<uintptr_t>(event_id) * 8 + 4)))
                    : -1;
            const uint64_t rng_delta = rng_calls_after >= rng_calls_before
                ? rng_calls_after - rng_calls_before : 0;

            ReplayTraceFields f;
            replay_scrub_append_secondary_action_stack_push_trace_context(f);
            f.string("stage", "MoveVMPushAnimNotifyOntoSecondaryStack")
             .integer("player", chara_from_stack
                      ? player_index_for_chara(reinterpret_cast<void*>(chara_from_stack)) + 1
                      : 0)
             .hex("stack_base", stack_addr)
             .hex("chara_from_stack", chara_from_stack)
             .hex("chara_back_ptr", reinterpret_cast<uintptr_t>(chara_back_ptr))
             .hex("payload", reinterpret_cast<uintptr_t>(payload))
             .integer("event_id", event_id)
             .hex("sub_id_mode", sub_id_mode)
             .hex("extra", extra)
             .integer("enabled_count", enabled_count)
             .hex("header_table", header_table)
             .integer("variant_count", variant_count)
             .hex("variant_before", variant_before)
             .hex("variant_after", variant_after)
             .hex("result_variant", result)
             .uinteger("rng_calls_before", rng_calls_before)
             .uinteger("rng_calls_after", rng_calls_after)
             .uinteger("rng_calls_delta", rng_delta)
             .hex("return_address", return_address)
             .hex("return_rva", caller_rva)
             .boolean("variant_changed", variant_before != variant_after);
            if (!caller_fn.empty()) f.string("return_fn", caller_fn.c_str());
            emit(sub_id_mode == 0xfffffffeu
                     ? "secondary_action_stack_push_random_variant"
                     : "secondary_action_stack_push_variant",
                 f);
#endif
        }

        static void emit_lifecycle_slots(const char* stage,
                                         const char* phase) noexcept
        {
            emit_chara_lifecycle(stage, phase, chara_slot_from_global(0));
            emit_chara_lifecycle(stage, phase, chara_slot_from_global(1));
        }

        void cache_replay_ring_entries(void* chara) noexcept
        {
            if (!chara) return;
            const auto* c = static_cast<const uint8_t*>(chara);
            const int32_t frame_id = safe_read_int32(c + 0x3A0);
            const int32_t input_cursor = safe_read_int32(c + 0x3B0);
            const int32_t active_slots = safe_read_int32(c + 0x398);
            if (frame_id < 0 || input_cursor < 0 || active_slots <= 0)
                return;
            const uint32_t cursor = static_cast<uint32_t>(input_cursor);
            const size_t ring_index = static_cast<size_t>(cursor)
                & kReplayRingBucketMask;
            size_t slot_count = static_cast<size_t>(active_slots);
            if (slot_count > kCachedReplayRingSlots)
                slot_count = kCachedReplayRingSlots;
            for (size_t slot = 0; slot < slot_count; ++slot)
            {
                const uint8_t* ring = c + 0x3C0
                    + slot * 0x2000 + ring_index * 0x10;
                const int32_t entry_frame = safe_read_int32(ring + 0x0);
                const uint32_t entry_cursor = safe_read_uint32(ring + 0x4);
                const uint8_t filled = safe_read_uint8(ring + 0x0C);
                if (entry_frame != frame_id || entry_cursor != cursor
                    || filled == 0)
                    continue;
                uint8_t bytes[kCachedReplayRingEntryBytes] {};
                if (!safe_read_bytes(ring, bytes, sizeof(bytes)))
                    continue;
                ReplayRingCacheEntry& entry = m_replay_ring_cache[
                    replay_ring_cache_index(frame_id, cursor, slot)];
                if (entry.valid && entry.frame_id == frame_id
                    && entry.cursor == cursor && entry.slot == slot)
                {
                    continue;
                }
                std::memcpy(entry.bytes, bytes, sizeof(bytes));
                entry.frame_id = frame_id;
                entry.cursor = cursor;
                entry.slot = slot;
                entry.valid = true;
            }
        }

        void restore_cached_replay_ring_entries_for_stage3(
            void* chara,
            uint32_t call_index) noexcept
        {
            if (!chara) return;
            auto* c = static_cast<uint8_t*>(chara);
            const int32_t frame_id = safe_read_int32(c + 0x3A0);
            const int32_t input_cursor = safe_read_int32(c + 0x3B0);
            const int32_t active_slots = safe_read_int32(c + 0x398);
            const int32_t master = safe_read_int32(c + 0x3A4);
            const int32_t frame_count = safe_read_int32(c + 0x3B4);
            if (frame_id < 0 || input_cursor < 0 || active_slots <= 0)
                return;

            int32_t repair_end = input_cursor + 1;
            if (master >= input_cursor)
                repair_end = master + 1;
            if (frame_count >= 0 && repair_end > frame_count)
                repair_end = frame_count;
            if (repair_end <= input_cursor)
                repair_end = input_cursor + 1;
            const int32_t max_end =
                input_cursor + kCachedStage3RepairLookahead;
            if (repair_end > max_end)
                repair_end = max_end;

            size_t slot_count = static_cast<size_t>(active_slots);
            if (slot_count > kCachedReplayRingSlots)
                slot_count = kCachedReplayRingSlots;

            int restored_entries = 0;
            int already_valid_entries = 0;
            int cache_missing_entries = 0;
            int write_failures = 0;
            for (int32_t cursor_i = input_cursor;
                 cursor_i < repair_end;
                 ++cursor_i)
            {
                const uint32_t cursor = static_cast<uint32_t>(cursor_i);
                const size_t ring_index = static_cast<size_t>(cursor)
                    & kReplayRingBucketMask;
                for (size_t slot = 0; slot < slot_count; ++slot)
                {
                    uint8_t* ring = c + 0x3C0
                        + slot * 0x2000 + ring_index * 0x10;
                    const int32_t live_frame = safe_read_int32(ring + 0x0);
                    const uint32_t live_cursor = safe_read_uint32(ring + 0x4);
                    const uint8_t live_filled = safe_read_uint8(ring + 0x0C);
                    const bool live_valid =
                        live_frame == frame_id && live_cursor == cursor
                        && live_filled != 0;

                    const ReplayRingCacheEntry& entry = m_replay_ring_cache[
                        replay_ring_cache_index(frame_id, cursor, slot)];
                    if (!entry.valid || entry.frame_id != frame_id
                        || entry.cursor != cursor || entry.slot != slot)
                    {
                        if (live_valid)
                            ++already_valid_entries;
                        else
                            ++cache_missing_entries;
                        continue;
                    }

                    uint8_t live_bytes[kCachedReplayRingEntryBytes] {};
                    if (live_valid
                        && safe_read_bytes(ring, live_bytes,
                                           sizeof(live_bytes))
                        && std::memcmp(live_bytes, entry.bytes,
                                       sizeof(live_bytes)) == 0)
                    {
                        ++already_valid_entries;
                        continue;
                    }

                    if (safe_write_bytes(ring, entry.bytes,
                                         kCachedReplayRingEntryBytes))
                    {
                        ++restored_entries;
                    }
                    else
                    {
                        ++write_failures;
                    }
                }
            }

            if (restored_entries || cache_missing_entries || write_failures)
            {
                ReplayTraceFields f;
                f.uinteger("call_index", call_index)
                 .hex("chara", reinterpret_cast<uintptr_t>(chara))
                 .integer("static_chara_slot", static_chara_slot_index(chara))
                 .integer("frame_id", frame_id)
                 .integer("input_cursor", input_cursor)
                 .integer("master", master)
                 .integer("frame_count", frame_count)
                 .integer("repair_begin", input_cursor)
                 .integer("repair_end", repair_end)
                 .integer("slot_count", static_cast<int64_t>(slot_count))
                 .integer("restored_entries", restored_entries)
                 .integer("already_valid_entries", already_valid_entries)
                 .integer("cache_missing_entries", cache_missing_entries)
                 .integer("write_failures", write_failures)
                 .boolean("ok", write_failures == 0)
                 .string("reason", "stage3-current-ring-cache-restore");
                emit("replay_ring_cached_stage3_restore", f);
            }
        }

        static int static_chara_slot_index(void* chara) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base || !chara) return -1;
            void* p1 = nullptr;
            void* p2 = nullptr;
            __try
            {
                p1 = *reinterpret_cast<void**>(base + kRVA_CharaSlotP1);
                p2 = *reinterpret_cast<void**>(base + kRVA_CharaSlotP2);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
            if (chara == p1) return 0;
            if (chara == p2) return 1;
            return -1;
        }

        static bool replay_input_stage_trace_active() noexcept
        {
            return GameMode::instance().current_presence()
                == GamePresence::Replay;
        }

        static void note_replay_input_stage_suppressed(
            const char* stage) noexcept
        {
            const uint32_t count =
                s_input_stage_non_replay_suppress_count.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            if (count != 1 && count != 2 && count != 4
                && (count % 4096) != 0)
            {
                return;
            }

            const auto presence = GameMode::instance().current_presence();
            ReplayTraceFields f;
            f.string("stage", stage ? stage : "")
             .uinteger("suppress_count", count)
             .integer("presence", static_cast<int64_t>(
                          static_cast<uint8_t>(presence)))
             .boolean("replay_presence", presence == GamePresence::Replay);
            emit("native_replay_input_stage_suppressed", f);

            if (count == 1)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[NativeReplayTraceHook] replay input-stage repair "
                    "suppressed outside Replay presence\n"));
            }
        }

        static void add_replay_input_overlay_fields(
            ReplayTraceFields& f,
            void* chara) noexcept
        {
            const auto* c = static_cast<const uint8_t*>(chara);
            const int32_t input_cursor = safe_read_int32(c + 0x3B0);
            const size_t ring_index = static_cast<size_t>(
                static_cast<uint32_t>(input_cursor))
                & kReplayRingBucketMask;
            const uint8_t* ring0 = c + 0x3C0 + ring_index * 0x10;
            const uint8_t* ring1 = c + 0x3C0 + 0x2000 + ring_index * 0x10;
            f.hex("chara", reinterpret_cast<uintptr_t>(chara))
             .hex("vtable", safe_read_uintptr(chara))
             .hex("resolver_component", safe_read_uintptr(c + 0x388))
             .integer("static_chara_slot", static_chara_slot_index(chara))
             .integer("frame_base", safe_read_int32(c + 0x390))
             .integer("active_slots", safe_read_int32(c + 0x398))
             .integer("playback_cursor", safe_read_int32(c + 0x39C))
             .integer("last_frame_id", safe_read_int32(c + 0x3A0))
             .integer("master", safe_read_int32(c + 0x3A4))
             .integer("input_cursor", input_cursor)
             .integer("frame_count", safe_read_int32(c + 0x3B4))
             .integer("ring_index", static_cast<int64_t>(ring_index))
             .integer("ring0_frame_id", safe_read_int32(ring0 + 0x0))
             .uinteger("ring0_cursor", safe_read_uint32(ring0 + 0x4))
             .hex("ring0_input", safe_read_uint32(ring0 + 0x8))
             .uinteger("ring0_filled", safe_read_uint8(ring0 + 0xC))
             .integer("ring1_frame_id", safe_read_int32(ring1 + 0x0))
             .uinteger("ring1_cursor", safe_read_uint32(ring1 + 0x4))
             .hex("ring1_input", safe_read_uint32(ring1 + 0x8))
             .uinteger("ring1_filled", safe_read_uint8(ring1 + 0xC))
             .hex("lookup_key", safe_read_uint32(c + 0x43F4))
             .integer("enable_flag", safe_read_int32(c + 0x4400))
             .integer("frame_offset", safe_read_int32(c + 0x440C))
             .integer("frame_total", safe_read_int32(c + 0x4410))
             .integer("frame_target", safe_read_int32(c + 0x4414))
             .integer("consumer_cursor", safe_read_int32(c + 0x4420))
             .uinteger("chara_mode", safe_read_uint8(c + 0x4424));
        }

        static void emit_replay_input_stage(
            const char* name,
            void* chara,
            uint32_t call_index) noexcept
        {
            ReplayTraceFields f;
            f.uinteger("call_index", call_index);
            add_replay_input_overlay_fields(f, chara);
            emit(name, f);
        }

        static FStringNative safe_read_fstring_header(const void* p) noexcept
        {
            FStringNative out{};
            if (!p) return out;
            __try { out = *static_cast<const FStringNative*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = {}; }
            return out;
        }

        static TArrayByteNative safe_read_tarray_header(const void* p) noexcept
        {
            TArrayByteNative out{};
            if (!p) return out;
            __try { out = *static_cast<const TArrayByteNative*>(p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = {}; }
            return out;
        }

        static bool safe_read_wchar_at(const wchar_t* p,
                                       int32_t index,
                                       wchar_t& out) noexcept
        {
            out = L'\0';
            if (!p || index < 0) return false;
            __try { out = p[index]; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return true;
        }

        static std::string narrow_utf8(const std::wstring& wide)
        {
            if (wide.empty()) return {};
            const int need = WideCharToMultiByte(
                CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                nullptr, 0, nullptr, nullptr);
            if (need <= 0) return {};
            std::string out(static_cast<size_t>(need), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                static_cast<int>(wide.size()), out.data(),
                                need, nullptr, nullptr);
            return out;
        }

        static std::string safe_read_fstring_utf8(const void* p,
                                                  int32_t max_chars = 512)
        {
            const FStringNative s = safe_read_fstring_header(p);
            if (!s.data || s.num <= 0 || s.num > max_chars || s.max < s.num)
                return {};

            std::wstring wide;
            wide.reserve(static_cast<size_t>(s.num));
            for (int32_t i = 0; i < s.num && i < max_chars; ++i)
            {
                wchar_t ch = L'\0';
                if (!safe_read_wchar_at(s.data, i, ch)) break;
                if (ch == L'\0') break;
                wide.push_back(ch);
            }
            return narrow_utf8(wide);
        }

        static int32_t replay_list_item_version(void* item) noexcept
        {
            return item ? safe_read_int32(static_cast<uint8_t*>(item) + 0x18)
                        : 0;
        }

        static int32_t replay_container_current_version(void* container) noexcept
        {
            return container
                ? safe_read_int32(static_cast<uint8_t*>(container) + 0x98)
                : 0;
        }

        static void add_tarray_fields(ReplayTraceFields& f,
                                       const char* prefix,
                                       const void* tarray) noexcept
        {
            const TArrayByteNative a = safe_read_tarray_header(tarray);
            char key[64]{};
            std::snprintf(key, sizeof(key), "%s_ptr", prefix);
            f.hex(key, reinterpret_cast<uintptr_t>(a.data));
            std::snprintf(key, sizeof(key), "%s_num", prefix);
            f.integer(key, a.num);
            std::snprintf(key, sizeof(key), "%s_max", prefix);
            f.integer(key, a.max);
        }

        static void add_battle_request_queue_fields(ReplayTraceFields& f,
                                                    void* gi) noexcept
        {
            const auto* bytes = static_cast<uint8_t*>(gi);
            f.hex("battle_request_entries",
                  gi ? safe_read_uintptr(bytes + 0x168) : 0)
             .integer("battle_request_count",
                      gi ? safe_read_int32(bytes + 0x170) : -1)
             .integer("battle_request_capacity",
                      gi ? safe_read_int32(bytes + 0x174) : -1);
        }

        static void __fastcall detour_load_replay_save_exec(
            void* self, void* stack, void* out_replay_save)
        {
            log_ptrs("native_replay_load_save_exec_enter",
                     reinterpret_cast<uintptr_t>(self),
                     reinterpret_cast<uintptr_t>(stack),
                     reinterpret_cast<uintptr_t>(out_replay_save));
            if (auto fn = orig<Exec3VoidFn>(Slot::LoadReplaySaveExec))
                fn(self, stack, out_replay_save);
            const uintptr_t result = safe_read_uintptr(out_replay_save);
            ReplayTraceFields f;
            f.hex("self", reinterpret_cast<uintptr_t>(self))
             .hex("out_replay_save", reinterpret_cast<uintptr_t>(out_replay_save))
             .hex("result", result);
            emit("native_replay_load_save_exec_exit", f);
        }

        static void __fastcall detour_does_replay_save_exist_exec(
            void* self, void* stack, void* out_bool)
        {
            log_ptrs("native_replay_save_exists_exec_enter",
                     reinterpret_cast<uintptr_t>(self),
                     reinterpret_cast<uintptr_t>(stack),
                     reinterpret_cast<uintptr_t>(out_bool));
            if (auto fn = orig<Exec3VoidFn>(Slot::DoesReplaySaveExistExec))
                fn(self, stack, out_bool);
            const bool result = safe_read_bool(out_bool);
            log_bool_result("native_replay_save_exists_exec_exit",
                            reinterpret_cast<uintptr_t>(self), result);
        }

        static void __fastcall detour_save_replay_to_slot_exec(
            void* self, void* stack, void* out_bool)
        {
            log_ptrs("native_replay_save_to_slot_exec_enter",
                     reinterpret_cast<uintptr_t>(self),
                     reinterpret_cast<uintptr_t>(stack),
                     reinterpret_cast<uintptr_t>(out_bool));
            if (auto fn = orig<Exec3VoidFn>(Slot::SaveReplayToSlotExec))
                fn(self, stack, out_bool);
            const bool result = safe_read_bool(out_bool);
            log_bool_result("native_replay_save_to_slot_exec_exit",
                            reinterpret_cast<uintptr_t>(self), result);
        }

        static void* __fastcall detour_get_replay_save_manager(bool writable)
        {
            void* result = nullptr;
            if (auto fn = orig<ReplaySaveManagerFn>(Slot::GetReplaySaveManager))
                result = fn(writable);
            ReplayTraceFields f;
            f.boolean("writable", writable)
             .hex("result", reinterpret_cast<uintptr_t>(result));
            emit("native_replay_get_save_manager", f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] get_save_manager writable={} result=0x{:X}\n"),
                writable ? 1 : 0, reinterpret_cast<uintptr_t>(result));
            return result;
        }

        static void __fastcall detour_apply_battle_settings(
            void* launcher, void* settings)
        {
            log_ptrs("native_replay_apply_battle_settings_enter",
                     reinterpret_cast<uintptr_t>(launcher),
                     reinterpret_cast<uintptr_t>(settings));
            if (auto fn = orig<Void2PtrFn>(Slot::ApplyBattleSettings))
                fn(launcher, settings);
            log_ptrs("native_replay_apply_battle_settings_exit",
                     reinterpret_cast<uintptr_t>(launcher),
                     reinterpret_cast<uintptr_t>(settings));
        }

        static void __fastcall detour_apply_replay_to_battle_setup(void* gi)
        {
            log_ptrs("native_replay_apply_to_battle_setup_enter",
                     reinterpret_cast<uintptr_t>(gi));
            if (auto fn = orig<VoidPtrFn>(Slot::ApplyReplayToBattleSetup))
                fn(gi);
            log_ptrs("native_replay_apply_to_battle_setup_exit",
                     reinterpret_cast<uintptr_t>(gi));
        }

        static void __fastcall detour_request_battle_asset(void* gi)
        {
            ReplayTraceFields in;
            in.hex("game_instance", reinterpret_cast<uintptr_t>(gi));
            add_battle_request_queue_fields(in, gi);
            emit("native_replay_request_battle_asset_enter", in);
            if (auto fn = orig<VoidPtrFn>(Slot::RequestBattleAsset))
                fn(gi);
            ReplayTraceFields out;
            out.hex("game_instance", reinterpret_cast<uintptr_t>(gi));
            add_battle_request_queue_fields(out, gi);
            emit("native_replay_request_battle_asset_exit", out);
        }

        static bool __fastcall detour_has_any_battle_request(void* gi)
        {
            bool native_result = false;
            if (auto fn = orig<BoolPtrFn>(Slot::HasAnyBattleRequest))
                native_result = fn(gi);
            auto& self = instance();
            self.m_stock_battle_asset_native_result.store(
                native_result, std::memory_order_release);
            const uint64_t schedule_generation =
                self.m_stock_battle_asset_release_claim.generation();
            const uint64_t schedule_target =
                self.m_stock_battle_asset_release_claim.target_qpc();
            LARGE_INTEGER schedule_now {};
            if (schedule_generation != 0 && schedule_target != 0
                && QueryPerformanceCounter(&schedule_now) != 0)
            {
                // This native predicate is the stock transition's actual poll
                // edge. Releasing here avoids depending on an unrelated UE
                // engine-tick callback that can pause briefly during loading.
                (void)self.release_stock_battle_asset_barrier_if_ready(
                    schedule_generation, schedule_generation, true,
                    schedule_target, false, native_result,
                    static_cast<uint64_t>(schedule_now.QuadPart));
            }
            const bool barrier_armed =
                self.m_stock_battle_asset_barrier_armed.load(
                    std::memory_order_acquire);
            const bool barrier_ready =
                self.m_stock_battle_asset_barrier_ready.load(
                    std::memory_order_acquire);
            const bool barrier_released =
                self.m_stock_battle_asset_release_claim.released();
            const bool completion_withheld =
                barrier_armed && !barrier_released && !native_result;
            if (completion_withheld)
            {
                self.m_stock_battle_asset_completion_withheld.store(
                    true, std::memory_order_release);
            }
            const bool result = native_result || completion_withheld;
            ReplayTraceFields f;
            f.hex("game_instance", reinterpret_cast<uintptr_t>(gi))
             .boolean("native_result", native_result)
             .boolean("completion_withheld", completion_withheld)
             .boolean("result", result);
            add_battle_request_queue_fields(f, gi);
            emit("native_replay_has_any_battle_request", f);
            if (barrier_armed && barrier_ready && !native_result &&
                self.m_stock_battle_asset_completion_withheld.load(
                    std::memory_order_acquire) &&
                !self.m_stock_battle_ready_emitted.exchange(
                    true, std::memory_order_acq_rel))
            {
                bool callback_ok = false;
                const auto callback =
                    self.m_stock_battle_asset_release_callback.load(
                        std::memory_order_acquire);
                void* const callback_context =
                    self.m_stock_battle_asset_release_context.load(
                        std::memory_order_acquire);
                if (callback)
                    callback_ok = callback(callback_context);
                self.m_stock_battle_ready_callback_ok.store(
                    callback_ok, std::memory_order_release);
                ReplayTraceFields ready;
                ready.hex("game_instance", reinterpret_cast<uintptr_t>(gi))
                    .boolean("callback_ok", callback_ok)
                    .hex("context", reinterpret_cast<uintptr_t>(
                        callback_context));
                emit("native_replay_stock_battle_ready", ready);
            }
            if (barrier_armed && barrier_released && !native_result &&
                self.m_stock_battle_ready_callback_ok.load(
                    std::memory_order_acquire) &&
                !self.m_stock_battle_asset_release_emitted.exchange(
                    true, std::memory_order_acq_rel))
            {
                ReplayTraceFields released;
                released.hex("game_instance", reinterpret_cast<uintptr_t>(gi));
                emit("native_replay_stock_battle_asset_release", released);
            }
            return result;
        }

        static bool __fastcall detour_can_launch_battle_manually(void* gi)
        {
            bool result = false;
            if (auto fn = orig<BoolPtrFn>(Slot::CanLaunchBattleManually))
                result = fn(gi);
            log_bool_result("native_replay_can_launch_battle_manually",
                            reinterpret_cast<uintptr_t>(gi), result);
            return result;
        }

        static void __fastcall detour_manual_launch_battle(void* gi)
        {
            log_ptrs("native_replay_manual_launch_battle_enter",
                     reinterpret_cast<uintptr_t>(gi));
            if (auto fn = orig<VoidPtrFn>(Slot::ManualLaunchBattle))
                fn(gi);
            log_ptrs("native_replay_manual_launch_battle_exit",
                     reinterpret_cast<uintptr_t>(gi));
        }

        static void __fastcall detour_quick_battle_request_exec(
            void* self, void* stack)
        {
            ReplayTraceFields in;
            in.hex("game_instance", reinterpret_cast<uintptr_t>(self))
              .hex("stack", reinterpret_cast<uintptr_t>(stack));
            add_battle_request_queue_fields(in, self);
            emit("native_replay_quick_battle_request_exec_enter", in);
            if (auto fn = orig<QuickBattleExecFn>(Slot::QuickBattleRequestExec))
                fn(self, stack);
            ReplayTraceFields out;
            out.hex("game_instance", reinterpret_cast<uintptr_t>(self))
               .hex("stack", reinterpret_cast<uintptr_t>(stack));
            add_battle_request_queue_fields(out, self);
            emit("native_replay_quick_battle_request_exec_exit", out);
        }

        static void __fastcall detour_set_active_stage_map_path(
            void* gi, uint8_t left_or_stage, uint8_t right_or_chara,
            int32_t stage_or_left)
        {
            ReplayTraceFields f;
            f.hex("game_instance", reinterpret_cast<uintptr_t>(gi))
             .integer("arg1", left_or_stage)
             .integer("arg2", right_or_chara)
             .integer("arg3", stage_or_left);
            add_battle_request_queue_fields(f, gi);
            emit("native_replay_set_active_stage_map_path_enter", f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] set_active_stage_map_path gi=0x{:X} "
                "arg1={} arg2={} arg3={}\n"),
                reinterpret_cast<uintptr_t>(gi), left_or_stage,
                right_or_chara, stage_or_left);
            if (auto fn = orig<SetActiveStageMapPathFn>(Slot::SetActiveStageMapPath))
                fn(gi, left_or_stage, right_or_chara, stage_or_left);
            ReplayTraceFields out;
            out.hex("game_instance", reinterpret_cast<uintptr_t>(gi));
            add_battle_request_queue_fields(out, gi);
            emit("native_replay_set_active_stage_map_path_exit", out);
        }

        static uint64_t __fastcall detour_deferred_stage_map_path_callback(
            void* callback)
        {
            const std::string map_path = safe_read_fstring_utf8(
                static_cast<uint8_t*>(callback) + 8);
            ReplayTraceFields f;
            f.hex("callback", reinterpret_cast<uintptr_t>(callback))
             .hex("game_instance", safe_read_uintptr(callback))
             .string("map_path", map_path);
            emit("native_replay_deferred_stage_map_path_callback_enter", f);
            uint64_t result = 0;
            if (auto fn = orig<DeferredStageMapPathCallbackFn>(
                    Slot::DeferredStageMapPathCallback))
                result = fn(callback);
            ReplayTraceFields out;
            out.hex("callback", reinterpret_cast<uintptr_t>(callback))
               .string("map_path", map_path)
               .integer("result", static_cast<int64_t>(result));
            emit("native_replay_deferred_stage_map_path_callback_exit", out);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] deferred_stage_map_path_callback "
                "callback=0x{:X} result={} map='{}'\n"),
                reinterpret_cast<uintptr_t>(callback), result,
                RC::to_generic_string(map_path));
            return result;
        }

        static void __fastcall detour_process_replay_decoded_input_packets(
            void* chara)
        {
            if (!replay_input_stage_trace_active())
            {
                note_replay_input_stage_suppressed("stage2");
                if (auto fn = orig<VoidPtrFn>(
                        Slot::ProcessReplayDecodedInputPackets))
                    fn(chara);
                return;
            }

            if (chara)
            {
                instance().m_latest_replay_input_overlay.store(
                    reinterpret_cast<uintptr_t>(chara),
                    std::memory_order_release);
                instance().cache_replay_ring_entries(chara);
            }
            const uint32_t enter_index = s_stage2_enter_count.fetch_add(
                1, std::memory_order_acq_rel);
            const int32_t enter_master = safe_read_int32(
                static_cast<uint8_t*>(chara) + 0x3A4);
            if (enter_index < 256 || (enter_index % 120) == 0
                || (enter_index > 512 && enter_master >= 0
                    && enter_master < 512))
            {
                emit_replay_input_stage(
                    "native_replay_stage2_enter", chara, enter_index);
            }
            if (auto fn = orig<VoidPtrFn>(
                    Slot::ProcessReplayDecodedInputPackets))
                fn(chara);
            const uint32_t exit_index = s_stage2_exit_count.fetch_add(
                1, std::memory_order_acq_rel);
            const int32_t exit_master = safe_read_int32(
                static_cast<uint8_t*>(chara) + 0x3A4);
            if (exit_index < 256 || (exit_index % 120) == 0
                || (exit_index > 512 && exit_master >= 0
                    && exit_master < 512))
            {
                emit_replay_input_stage(
                    "native_replay_stage2_exit", chara, exit_index);
            }
        }

        static void __fastcall detour_replay_playback_push_inputs(
            void* chara)
        {
            if (!replay_input_stage_trace_active())
            {
                note_replay_input_stage_suppressed("stage3");
                if (auto fn = orig<VoidPtrFn>(Slot::ReplayPlaybackPushInputs))
                    fn(chara);
                return;
            }

            if (chara)
            {
                instance().m_latest_replay_input_overlay.store(
                    reinterpret_cast<uintptr_t>(chara),
                    std::memory_order_release);
                instance().cache_replay_ring_entries(chara);
            }
            const uint32_t enter_index = s_stage3_enter_count.fetch_add(
                1, std::memory_order_acq_rel);
            if (chara)
                instance().restore_cached_replay_ring_entries_for_stage3(
                    chara, enter_index);
            if (!g_replay_scrub_generation_diagnostics_suppressed.load(
                    std::memory_order_acquire))
            {
                const int32_t enter_master = safe_read_int32(
                    static_cast<uint8_t*>(chara) + 0x3A4);
                if (enter_index < 256 || (enter_index % 120) == 0
                    || (enter_index > 512 && enter_master >= 0
                        && enter_master < 512))
                {
                    emit_replay_input_stage(
                        "native_replay_stage3_enter", chara, enter_index);
                }
            }
            if (auto fn = orig<VoidPtrFn>(Slot::ReplayPlaybackPushInputs))
                fn(chara);
            const uint32_t exit_index = s_stage3_exit_count.fetch_add(
                1, std::memory_order_acq_rel);
            if (!g_replay_scrub_generation_diagnostics_suppressed.load(
                    std::memory_order_acquire))
            {
                const int32_t exit_master = safe_read_int32(
                    static_cast<uint8_t*>(chara) + 0x3A4);
                if (exit_index < 256 || (exit_index % 120) == 0
                    || (exit_index > 512 && exit_master >= 0
                        && exit_master < 512))
                {
                    emit_replay_input_stage(
                        "native_replay_stage3_exit", chara, exit_index);
                }
            }
        }

        static void __fastcall detour_move_system_pump_vm_slots()
        {
            emit_pre_main_motion_checkpoint(
                "MoveSystemPumpVMSlots", "enter");
            if (auto fn = orig<VoidNoArgFn>(Slot::MoveSystemPumpVMSlots))
                fn();
            emit_pre_main_motion_checkpoint(
                "MoveSystemPumpVMSlots", "exit");
        }

        static void __fastcall detour_tick_chara_input(
            void* chara,
            void* input_transform_list,
            void* encoded_input_transform_list)
        {
            emit_pre_main_motion_checkpoint(
                "TickCharaInput", "enter", chara, chara);
            const ReplayInputPairRepairResult input_repair =
                replay_scrub_repair_latest_engine_input_before_chara_input(
                    chara);
            if (input_repair == ReplayInputPairRepairResult::Failed)
            {
                emit_pre_main_motion_checkpoint(
                    "TickCharaInput", "repair-failed", chara, chara);
                return;
            }
            (void)replay_scrub_repair_secondary_action_stack_last_variant_before_chara_input(
                chara);
            if (auto fn = orig<TickCharaInputFn>(Slot::TickCharaInput))
                fn(chara, input_transform_list, encoded_input_transform_list);
            emit_pre_main_motion_checkpoint(
                "TickCharaInput", "exit", chara, chara);
        }

        static void __fastcall detour_advance_chara_anim_clip_player(
            void* clip_player)
        {
            void* chara = nullptr;
            for (size_t player = 0; player < 2; ++player)
            {
                void* candidate = chara_slot_from_global(player);
                if (candidate
                    && static_cast<uint8_t*>(candidate)
                            + kRollbackCharaAnimClipPlayerOffset
                        == clip_player)
                {
                    chara = candidate;
                    break;
                }
            }
            emit_pre_main_motion_checkpoint(
                "AdvanceCharaAnimClipPlayer", "enter",
                clip_player, chara);
            if (auto fn = orig<VoidPtrFn>(Slot::AdvanceCharaAnimClipPlayer))
                fn(clip_player);
            emit_pre_main_motion_checkpoint(
                "AdvanceCharaAnimClipPlayer", "exit",
                clip_player, chara);
        }

        static void __fastcall detour_tick_chara_event_cue_scheduler(
            void* scheduler)
        {
            void* chara = nullptr;
            for (size_t player = 0; player < 2; ++player)
            {
                void* candidate = chara_slot_from_global(player);
                if (!candidate) continue;
                const auto* owner = static_cast<const uint8_t*>(candidate)
                    + kRollbackPoseEventCueOwnerOffset;
                if (reinterpret_cast<void*>(safe_read_uintptr(
                        owner + kRollbackPoseEventCueOwnerSchedulerOffset))
                    == scheduler)
                {
                    chara = candidate;
                    break;
                }
            }
            emit_pre_main_motion_checkpoint(
                "TickCharaEventCueScheduler", "enter",
                scheduler, chara);
            if (auto fn = orig<VoidPtrFn>(Slot::TickCharaEventCueScheduler))
                fn(scheduler);
            emit_pre_main_motion_checkpoint(
                "TickCharaEventCueScheduler", "exit",
                scheduler, chara);
        }

        static void __fastcall
        detour_pre_tick_state_snapshot_and_round_decision()
        {
            emit_pre_main_motion_checkpoint(
                "PreTickStateSnapshotAndRoundDecision", "enter");
            if (auto fn = orig<VoidNoArgFn>(
                    Slot::PreTickStateSnapshotAndRoundDecision))
            {
                fn();
            }
            emit_pre_main_motion_checkpoint(
                "PreTickStateSnapshotAndRoundDecision", "exit");
        }

        static void __fastcall detour_battle_per_frame_tick(
            void* dispatcher, void* args)
        {
            const uint64_t entry_sequence =
                s_native_tick_root_entry_seq.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            uint32_t& depth = native_tick_root_depth();
            const uint32_t previous_depth = depth;
            depth = previous_depth + 1;
            const uintptr_t return_address =
                reinterpret_cast<uintptr_t>(_ReturnAddress());
            const bool detailed_trace =
                detailed_lifecycle_trace_enabled();
            const NativeTickRootTraceSample before = detailed_trace
                ? capture_native_tick_root_trace_sample()
                : NativeTickRootTraceSample {};
            bool original_called = false;
            if (auto fn = orig<Void2PtrFn>(Slot::BattlePerFrameTick))
            {
                original_called = true;
                fn(dispatcher, args);
            }
            if (detailed_trace)
            {
                const NativeTickRootTraceSample after =
                    capture_native_tick_root_trace_sample();
                emit_native_tick_root_transaction(
                    "LuxBattleChara_VTableThunk_PerFrameTick",
                    entry_sequence, depth,
                    return_address, original_called, before, after);
            }
            depth = previous_depth;
        }

        static void __fastcall detour_chara_per_tick_advance_all()
        {
            const uint64_t entry_sequence =
                s_native_tick_root_entry_seq.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            uint32_t& depth = native_tick_root_depth();
            const uint32_t previous_depth = depth;
            depth = previous_depth + 1;
            const uintptr_t return_address =
                reinterpret_cast<uintptr_t>(_ReturnAddress());
            const bool detailed_trace =
                detailed_lifecycle_trace_enabled();
            const NativeTickRootTraceSample before = detailed_trace
                ? capture_native_tick_root_trace_sample()
                : NativeTickRootTraceSample {};
            bool original_called = false;
            if (auto fn = orig<VoidNoArgFn>(Slot::CharaPerTickAdvanceAll))
            {
                original_called = true;
                fn();
            }
            if (detailed_trace)
            {
                const NativeTickRootTraceSample after =
                    capture_native_tick_root_trace_sample();
                emit_native_tick_root_transaction(
                    "HandleLuxBattleCharaPerTickAdvanceAllThunk",
                    entry_sequence, depth,
                    return_address, original_called, before, after);
            }
            depth = previous_depth;
        }

        static void __fastcall detour_frame_input_log_tick_control(
            void* input_log)
        {
            const uint64_t call_sequence =
                s_frame_input_log_tick_control_seq.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            const uintptr_t return_address =
                reinterpret_cast<uintptr_t>(_ReturnAddress());
            const FrameInputLogTickTraceSample before =
                capture_frame_input_log_tick_trace_sample(input_log);
            bool original_called = false;
            if (auto fn = orig<VoidPtrFn>(Slot::FrameInputLogTickControl))
            {
                original_called = true;
                fn(input_log);
            }
            const FrameInputLogTickTraceSample after =
                capture_frame_input_log_tick_trace_sample(input_log);
            emit_frame_input_log_tick_transaction(
                call_sequence, return_address, original_called,
                before, after);
        }

        static void __fastcall detour_tick_chara_main_simulation(
            void* slot)
        {
            void* chara = reinterpret_cast<void*>(safe_read_uintptr(slot));
            auto& transaction = pose_producer_trace_transaction();
            const PoseProducerTraceContext previous = transaction;
            transaction.transaction_id =
                s_pose_producer_transaction_seq.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            transaction.chara = chara;
            transaction.player_index = player_index_for_chara(chara);
            transaction.ledger = {};
            (void)transaction.ledger.admit(
                RollbackPoseProducerCheckpoint::TickMainEnter);
            emit_pose_producer_checkpoint(
                "TickCharaMainSimulation", "enter", chara);
            emit_chara_lifecycle(
                "TickCharaMainSimulation", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::TickCharaMainSimulation))
                fn(slot);
            (void)transaction.ledger.admit(
                RollbackPoseProducerCheckpoint::TickMainExit);
            emit_pose_producer_checkpoint(
                "TickCharaMainSimulation", "exit", chara);
            replay_scrub_note_tick_chara_main_simulation_exit(chara);
            emit_chara_lifecycle(
                "TickCharaMainSimulation", "exit", chara);
            transaction = previous;
        }

        static void __fastcall detour_update_opponent_relative_angles(
            void* chara)
        {
            emit_chara_lifecycle(
                "UpdateOpponentRelativeAngles", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::UpdateOpponentRelativeAngles))
                fn(chara);
            emit_chara_lifecycle(
                "UpdateOpponentRelativeAngles", "exit", chara);
        }

        static void __fastcall detour_finalize_tick_pose_and_state(
            void* chara)
        {
            (void)pose_producer_trace_transaction().ledger.admit(
                RollbackPoseProducerCheckpoint::FinalizeEnter);
            emit_pose_producer_checkpoint(
                "FinalizeTickPoseAndState", "enter", chara);
            emit_chara_lifecycle(
                "FinalizeTickPoseAndState", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::FinalizeTickPoseAndState))
                fn(chara);
            (void)pose_producer_trace_transaction().ledger.admit(
                RollbackPoseProducerCheckpoint::FinalizeExit);
            emit_pose_producer_checkpoint(
                "FinalizeTickPoseAndState", "exit", chara);
            emit_chara_lifecycle(
                "FinalizeTickPoseAndState", "exit", chara);
        }

        static void __fastcall detour_evaluate_bone_pose(
            void* anchor,
            void* in_out_bone_matrices)
        {
            void* chara = anchor
                ? static_cast<void*>(
                    static_cast<uint8_t*>(anchor)
                        - kCharaVfxEffectAnchorOffset)
                : nullptr;
            (void)pose_producer_trace_transaction().ledger.admit(
                RollbackPoseProducerCheckpoint::EvaluateEnter);
            emit_pose_producer_checkpoint(
                "EvaluateBonePose", "enter", chara);
            if (auto fn = orig<Void2PtrFn>(Slot::EvaluateBonePose))
                fn(anchor, in_out_bone_matrices);
            (void)pose_producer_trace_transaction().ledger.admit(
                RollbackPoseProducerCheckpoint::EvaluateExit);
            emit_pose_producer_checkpoint(
                "EvaluateBonePose", "exit", chara);
        }

        static int& active_motion_decode_player() noexcept
        {
            static thread_local int player = -1;
            return player;
        }

        static void*& active_motion_pose_buffer() noexcept
        {
            static thread_local void* pose = nullptr;
            return pose;
        }

        static int motion_decode_player_for_chara(void* chara) noexcept
        {
            if (!chara) return -1;
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return -1;
            const uintptr_t p1 = safe_read_uintptr(
                reinterpret_cast<const void*>(base + kRVA_CharaSlotP1));
            const uintptr_t p2 = safe_read_uintptr(
                reinterpret_cast<const void*>(base + kRVA_CharaSlotP2));
            const uintptr_t value = reinterpret_cast<uintptr_t>(chara);
            if (value == p1) return 0;
            if (value == p2) return 1;
            return -1;
        }

        static void __fastcall detour_solve_bone_pose(
            void* anchor,
            float* primary_provider_buffer,
            uint32_t flags)
        {
            void* chara = anchor
                ? static_cast<void*>(
                    static_cast<uint8_t*>(anchor)
                        - kCharaVfxEffectAnchorOffset)
                : nullptr;
            emit_chara_lifecycle("SolveBonePose", "enter", chara);
            int& active_player = active_motion_decode_player();
            void*& active_pose = active_motion_pose_buffer();
            const int previous_player = active_player;
            void* const previous_pose = active_pose;
            active_player = motion_decode_player_for_chara(chara);
            active_pose = nullptr;
            if (auto fn = orig<SolveBonePoseFn>(Slot::SolveBonePose))
                fn(anchor, primary_provider_buffer, flags);
            active_pose = previous_pose;
            active_player = previous_player;
            emit_chara_lifecycle("SolveBonePose", "exit", chara);
        }

        static void __fastcall detour_writeback_scaled_bone_transforms(
            void* anchor,
            void* in_out_bone_matrices,
            void* optional_blend_matrices,
            float* blend_weights)
        {
            if (auto fn = orig<WritebackScaledBoneTransformsFn>(
                    Slot::WritebackScaledBoneTransforms))
            {
                fn(anchor, in_out_bone_matrices,
                   optional_blend_matrices, blend_weights);
            }

            // SolveBonePose calls this helper once at 0x1402F0E14 after all
            // sampled-pose root, auxiliary, blend, FK and IK work. Capturing
            // here keeps sampledPoseScratch inside its owning stack lifetime;
            // capturing after SolveBonePose returned observed expired stack
            // storage and made rollback call depth part of gameplay state.
            void* const pose = active_motion_pose_buffer();
            const int player = active_motion_decode_player();
            if (pose && player >= 0 && player < 2)
            {
                (void)rollback_motion_pose_residue().capture_after_solve(
                    player, pose);
            }
        }

        static uint64_t __fastcall detour_sample_keyframe_transforms(
            void* playback_state,
            void* out_pose,
            void* base_pose,
            uint8_t* channel_type_stream,
            void* scratch_pair,
            uint64_t channel_mask,
            float yaw,
            float pitch)
        {
            const int player = active_motion_decode_player();
            void*& active_pose = active_motion_pose_buffer();
            const bool first_sample_in_solve = active_pose == nullptr;
            if (first_sample_in_solve && out_pose)
                active_motion_pose_buffer() = out_pose;
            const MotionDecodeTraceWindow trace_window =
                motion_decode_trace_window();
            MotionDecodeSampleEvidence before_restore {};
            if (trace_window.enabled)
            {
                before_restore = capture_motion_decode_sample_evidence(
                    playback_state,
                    out_pose,
                    channel_type_stream,
                    scratch_pair);
            }
            auto& scratch = rollback_motion_decode_scratch();
            const RollbackMotionDecodeScratchSeedReport seed =
                scratch.before_sample(player, scratch_pair);
            auto& pose_residue = rollback_motion_pose_residue();
            const RollbackMotionPoseResidueSeedReport pose_seed =
                first_sample_in_solve
                    ? pose_residue.before_sample(player, out_pose, base_pose)
                    : RollbackMotionPoseResidueSeedReport {};
            MotionDecodeSampleEvidence before_native {};
            if (trace_window.enabled)
            {
                before_native = capture_motion_decode_sample_evidence(
                    playback_state,
                    out_pose,
                    channel_type_stream,
                    scratch_pair);
            }
            if (seed.seeded
                && (seed.restored || seed.bootstrapped))
            {
                ReplayTraceFields f;
                f.integer("player", player + 1)
                 .hex("scratch_pair",
                      reinterpret_cast<uintptr_t>(scratch_pair))
                 .integer("bytes",
                          static_cast<int64_t>(
                              kRollbackMotionDecodeScratchPairBytes))
                 .boolean("restored", seed.restored)
                 .boolean("bootstrapped", seed.bootstrapped)
                 .boolean("carried", seed.carried)
                 .boolean("changed", seed.changed)
                 .boolean("verified", seed.verified)
                 .hex("expected_hash", seed.expected_hash)
                 .hex("before_hash", seed.before_hash)
                 .hex("after_hash", seed.after_hash)
                 .uinteger("seed_count", scratch.seed_count())
                 .string("reason",
                         "normalize-native-decoded-word-stack-scratch");
                ReplayDebugTrace::instance().event(
                    "motion_decode_scratch_seeded", f);
            }
            if (pose_seed.seeded
                && (pose_seed.restored || pose_seed.bootstrapped))
            {
                ReplayTraceFields f;
                f.integer("player", player + 1)
                 .hex("out_pose", reinterpret_cast<uintptr_t>(out_pose))
                 .integer("bytes",
                          static_cast<int64_t>(
                              kRollbackMotionPoseResidueBytes))
                 .boolean("restored", pose_seed.restored)
                 .boolean("bootstrapped", pose_seed.bootstrapped)
                 .boolean("carried", pose_seed.carried)
                 .boolean("changed", pose_seed.changed)
                 .boolean("verified", pose_seed.verified)
                 .hex("expected_hash", pose_seed.expected_hash)
                 .hex("before_hash", pose_seed.before_hash)
                 .hex("after_hash", pose_seed.after_hash)
                 .uinteger("seed_count", pose_residue.seed_count())
                 .string("reason",
                         "restore-native-sampled-pose-stack-residue");
                ReplayDebugTrace::instance().event(
                    "motion_pose_residue_seeded", f);
            }

            uint64_t result = 0;
            bool called = false;
            if (auto fn = orig<SampleKeyframeTransformsFn>(
                    Slot::SampleKeyframeTransforms))
            {
                called = true;
                result = fn(
                    playback_state,
                    out_pose,
                    base_pose,
                    channel_type_stream,
                    scratch_pair,
                    channel_mask,
                    yaw,
                    pitch);
            }
            MotionDecodeSampleEvidence after_native {};
            if (trace_window.enabled)
            {
                after_native = capture_motion_decode_sample_evidence(
                    playback_state,
                    out_pose,
                    channel_type_stream,
                    scratch_pair);
            }
            if (called)
            {
                (void)scratch.after_sample(player, scratch_pair);
            }

            if (!trace_window.enabled) return result;

            static std::atomic<uint64_t> s_sample_sequence {0};
            const uint64_t sequence = s_sample_sequence.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            uint32_t yaw_bits = 0;
            uint32_t pitch_bits = 0;
            std::memcpy(&yaw_bits, &yaw, sizeof(yaw_bits));
            std::memcpy(&pitch_bits, &pitch, sizeof(pitch_bits));
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            ReplayTraceFields fields;
            fields.uinteger("sample_sequence", sequence)
                .string(
                    "coordinate_source",
                    trace_window.coordinate_source)
                .integer("replay_sequence", trace_window.sequence)
                .integer("replay_round", trace_window.round)
                .integer("replay_master", trace_window.master)
                .integer(
                    "window_logical_frame",
                    trace_window.logical_frame)
                .integer("player", player + 1)
                .boolean("original_called", called)
                .boolean("rollback_session_active", native_scope.active)
                .boolean("native_scope_owned", native_scope.active)
                .boolean(
                    "rolling_back",
                    native_scope.active && native_scope.rolling_back)
                .integer(
                    "native_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_frame)
                        : int64_t{-1})
                .integer(
                    "native_applied_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_applied)
                        : int64_t{-1})
                .integer(
                    "logical_frame",
                    native_scope.active
                        ? static_cast<int64_t>(native_scope.logical_frame)
                        : int64_t{-1})
                .hex(
                    "playback_state",
                    reinterpret_cast<uintptr_t>(playback_state))
                .hex("out_pose", reinterpret_cast<uintptr_t>(out_pose))
                .hex(
                    "selector_stream",
                    reinterpret_cast<uintptr_t>(channel_type_stream))
                .hex(
                    "scratch_pair",
                    reinterpret_cast<uintptr_t>(scratch_pair))
                .hex("caller_channel_mask", channel_mask)
                .hex(
                    "sample_frame_bits",
                    playback_state
                        ? safe_read_uint32(
                            static_cast<uint8_t*>(playback_state) + 0x14)
                        : 0)
                .hex("yaw_bits", yaw_bits)
                .hex("pitch_bits", pitch_bits)
                .hex("dirty_channel_mask", result)
                .boolean("scratch_restore_seeded", seed.seeded)
                .boolean("scratch_restore_changed", seed.changed)
                .boolean("scratch_restore_verified", seed.verified)
                .hex("scratch_restore_expected_hash", seed.expected_hash)
                .hex("scratch_restore_before_hash", seed.before_hash)
                .hex("scratch_restore_after_hash", seed.after_hash);
            AddRollbackFloatingPointEnvironmentTraceFields(fields);
            add_motion_decode_sample_evidence_fields(
                fields, "before_restore", before_restore);
            add_motion_decode_sample_evidence_fields(
                fields, "before_native", before_native);
            add_motion_decode_sample_evidence_fields(
                fields, "after_native", after_native);
            ReplayDebugTrace::instance().event(
                "motion_decode_sample_checkpoint", fields);
            return result;
        }

        static void __fastcall detour_tick_hit_resolution_and_body_collision()
        {
            auto& transaction = root_motion_delta_trace_transaction();
            transaction.begin_outer_transaction(
                s_root_motion_transaction_seq.fetch_add(
                    1, std::memory_order_acq_rel) + 1);
            emit_collision_checkpoint(
                "TickHitResolutionAndBodyCollision", "enter");
            emit_lifecycle_slots(
                "TickHitResolutionAndBodyCollision", "enter");
            if (auto fn = orig<VoidNoArgFn>(
                    Slot::TickHitResolutionAndBodyCollision))
                fn();
            emit_collision_checkpoint(
                "TickHitResolutionAndBodyCollision", "exit");
            replay_scrub_note_hit_resolution_exit();
            emit_lifecycle_slots(
                "TickHitResolutionAndBodyCollision", "exit");
            transaction.finish_outer_transaction();
        }

        static uint8_t __fastcall detour_solve_phys_body_collision(
            void* p1_khit,
            void* p1_current_matrices,
            void* p2_khit,
            void* p2_current_matrices,
            float push_angle_turns)
        {
            emit_collision_checkpoint(
                "SolvePhysBodyCollision", "enter",
                -1, -1, -1,
                p1_khit, p1_current_matrices,
                p2_khit, p2_current_matrices,
                push_angle_turns, true);
            uint8_t result = 0;
            if (auto fn = orig<SolvePhysBodyCollisionFn>(
                    Slot::SolvePhysBodyCollision))
            {
                result = fn(
                    p1_khit,
                    p1_current_matrices,
                    p2_khit,
                    p2_current_matrices,
                    push_angle_turns);
            }
            emit_collision_checkpoint(
                "SolvePhysBodyCollision", "exit",
                static_cast<int>(result), -1, -1,
                p1_khit, p1_current_matrices,
                p2_khit, p2_current_matrices,
                push_angle_turns, true);
            return result;
        }

        static void __fastcall detour_tick_both_chara_collision_physics(
            void* p1,
            void* p2,
            uint8_t body_solve_result,
            uint32_t* p1_collision_result,
            uint32_t* p2_collision_result)
        {
            emit_collision_checkpoint(
                "TickBothCharaCollisionPhysics", "enter",
                static_cast<int>(body_solve_result),
                p1_collision_result
                    ? static_cast<int>(
                        safe_read_uint32(p1_collision_result)) : -1,
                p2_collision_result
                    ? static_cast<int>(
                        safe_read_uint32(p2_collision_result)) : -1);
            if (auto fn = orig<TickBothCharaCollisionPhysicsFn>(
                    Slot::TickBothCharaCollisionPhysics))
            {
                fn(
                    p1,
                    p2,
                    body_solve_result,
                    p1_collision_result,
                    p2_collision_result);
            }
            emit_collision_checkpoint(
                "TickBothCharaCollisionPhysics", "exit",
                static_cast<int>(body_solve_result),
                p1_collision_result
                    ? static_cast<int>(
                        safe_read_uint32(p1_collision_result)) : -1,
                p2_collision_result
                    ? static_cast<int>(
                        safe_read_uint32(p2_collision_result)) : -1);
        }

        static void __fastcall detour_update_root_motion_deltas_from_bone1(
            void* chara)
        {
            const bool trace_enabled =
                motion_decode_trace_window().enabled;
            const uintptr_t image_base = NativeBinding::imageBase();
            const uintptr_t return_address =
                reinterpret_cast<uintptr_t>(_ReturnAddress());
            const uintptr_t return_rva =
                image_base != 0 && return_address >= image_base
                ? return_address - image_base : 0;
            auto& transaction = root_motion_delta_trace_transaction();
            const RollbackRootMotionSamplePhase sample_phase =
                ClassifyRollbackRootMotionSampleReturnRva(return_rva);
            // The enclosing collision hook is optional. Reconstruct the
            // native transaction from the three verified callsites when it
            // is not armed: optional early sample, common P1, common P2.
            const int player_index = player_index_for_chara(chara);
            const bool begins_transaction =
                ShouldBeginRollbackRootMotionTraceTransaction(
                    transaction.active(), sample_phase);
            const uint64_t new_transaction_id = begins_transaction
                ? s_root_motion_transaction_seq.fetch_add(
                    1, std::memory_order_acq_rel) + 1
                : 0;
            const RollbackRootMotionTraceAdmission admission =
                transaction.admit(
                    sample_phase, player_index, new_transaction_id);
            RootMotionDeltaTraceSample before {};
            if (trace_enabled)
                before = capture_root_motion_delta_sample(chara);

            if (auto fn = orig<UpdateRootMotionDeltasFn>(
                    Slot::UpdateRootMotionDeltasFromBone1))
            {
                fn(chara);
            }

            RootMotionDeltaTraceSample after {};
            if (trace_enabled)
                after = capture_root_motion_delta_sample(chara);
            if (!trace_enabled) return;
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            ReplayTraceFields f;
            f.uinteger("transaction_id", admission.transaction_id)
             .boolean(
                 "transaction_active", admission.transaction_active)
             .boolean(
                 "transaction_began", admission.began_transaction)
             .boolean(
                 "transaction_ended", admission.ended_transaction)
             .integer("player",
                      player_index >= 0 ? player_index + 1 : 0)
             .uinteger("call_ordinal", admission.call_ordinal)
             .string(
                 "callsite_phase",
                 RollbackRootMotionSamplePhaseName(sample_phase))
             .hex("return_address", return_address)
             .hex("return_rva", return_rva)
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .integer(
                 "replay_frame",
                 chara ? safe_read_int32(
                     static_cast<const uint8_t*>(chara) + 0x3A0) : -1)
             .integer(
                 "replay_master",
                 chara ? safe_read_int32(
                     static_cast<const uint8_t*>(chara) + 0x3A4) : -1)
             .boolean("rollback_session_active", native_scope.active)
             .boolean("native_scope_owned", native_scope.active)
             .boolean(
                 "rolling_back",
                 native_scope.active && native_scope.rolling_back)
             .string(
                 "simulation_ownership",
                 native_scope.active ? "rollback-owned" : "stock")
             .integer(
                 "native_coordinate",
                 native_scope.active
                    ? static_cast<int64_t>(
                        native_scope.armed_clock.battle_last_frame)
                    : int64_t{-1})
             .integer(
                 "native_applied_coordinate",
                 native_scope.active
                    ? static_cast<int64_t>(
                        native_scope.armed_clock.battle_last_applied)
                    : int64_t{-1})
             .integer(
                 "logical_frame",
                 native_scope.active
                    ? static_cast<int64_t>(native_scope.logical_frame)
                    : int64_t{-1})
             .integer(
                 "input_log_frame",
                 native_scope.active
                    ? native_scope.armed_clock.input_log_last_frame : -1)
             .integer(
                 "input_log_master",
                 native_scope.active
                    ? native_scope.armed_clock.input_log_master_clock : -1)
             .real(
                 "vm_freeze_out_blend_w0",
                 image_base
                    ? safe_read_float(reinterpret_cast<const void*>(
                        image_base + kRVA_VMFreezeOutBlendW0))
                    : 0.0f)
             .integer(
                 "vm_freeze_out_mode",
                 image_base
                    ? safe_read_int32(reinterpret_cast<const void*>(
                        image_base + kRVA_VMFreezeOutModeTag))
                    : 0);
            add_root_motion_delta_sample_fields(f, "before", before);
            add_root_motion_delta_sample_fields(f, "after", after);
            emit("native_root_motion_delta_sample", f);
        }

        static void __fastcall detour_update_proximity_blend_weight(
            void* chara)
        {
            emit_chara_state_probe("UpdateProximityBlendWeight", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::UpdateProximityBlendWeight))
                fn(chara);
            emit_chara_state_probe("UpdateProximityBlendWeight", "exit", chara);
        }

        static void __fastcall detour_update_stance_category(void* chara)
        {
            emit_chara_state_probe("UpdateStanceCategory", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::UpdateStanceCategory))
                fn(chara);
            emit_chara_state_probe("UpdateStanceCategory", "exit", chara);
        }

        static void __fastcall detour_tick_hit_state_state_machine(
            void* chara)
        {
            emit_chara_state_probe("TickHitStateStateMachine", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::TickHitStateStateMachine))
                fn(chara);
            emit_chara_state_probe("TickHitStateStateMachine", "exit", chara);
        }

        static void __fastcall detour_integrate_physics_per_tick(void* chara)
        {
            emit_chara_state_probe("IntegratePhysicsPerTick", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::IntegratePhysicsPerTick))
                fn(chara);
            emit_chara_state_probe("IntegratePhysicsPerTick", "exit", chara);
        }

        static int __fastcall detour_evaluate_defense_mode(void* chara)
        {
            emit_chara_state_probe("EvaluateDefenseMode", "enter", chara);
            int result = 0;
            if (auto fn = orig<IntPtrFn>(Slot::EvaluateDefenseMode))
                result = fn(chara);
            emit_chara_state_probe("EvaluateDefenseMode", "exit", chara);
            return result;
        }

        static void __fastcall detour_update_block_state_stochastic(
            void* self_chara,
            void* chara,
            void* opp_chara)
        {
            void* traced = self_chara ? self_chara : chara;
            emit_chara_state_probe("UpdateBlockStateStochastic", "enter", traced);
            if (auto fn = orig<Void3PtrFn>(Slot::UpdateBlockStateStochastic))
                fn(self_chara, chara, opp_chara);
            emit_chara_state_probe("UpdateBlockStateStochastic", "exit", traced);
        }

        static void __fastcall detour_tick_damage_and_behavior_lock(
            uint64_t key_seed,
            void* chara)
        {
            emit_chara_state_probe("TickDamageAndBehaviorLock", "enter", chara);
            if (auto fn = orig<VoidU64PtrFn>(Slot::TickDamageAndBehaviorLock))
                fn(key_seed, chara);
            emit_chara_state_probe("TickDamageAndBehaviorLock", "exit", chara);
        }

        static void __fastcall detour_tick_chara_terrain_contact_blend(
            void* chara)
        {
            emit_chara_state_probe("TickCharaTerrainContactBlend", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::TickCharaTerrainContactBlend))
                fn(chara);
            emit_chara_state_probe("TickCharaTerrainContactBlend", "exit", chara);
        }

        static void __fastcall detour_movevm_execute_op_stream(
            void* chara,
            int lane,
            uint64_t param3,
            void* cmd_stream)
        {
            emit_movevm_lane_probe("MoveVMExecuteOpStream", "enter", chara, lane);
            if (auto fn = orig<MoveVMExecuteOpStreamFn>(
                    Slot::MoveVMExecuteOpStream))
                fn(chara, lane, param3, cmd_stream);
            emit_movevm_lane_probe("MoveVMExecuteOpStream", "exit", chara, lane);
        }

        static void __fastcall detour_movevm_check_transition_timing(
            void* chara,
            void* lane_ptr,
            uint64_t unused,
            uint32_t start_time)
        {
            const int lane = lane_ptr ? safe_read_int16(lane_ptr) : -1;
            const bool interesting = should_emit_movevm_inner_probe(
                chara, static_cast<uint8_t*>(lane_ptr));
            if (interesting)
                emit_movevm_lane_probe(
                    "MoveVMCheckTransitionTiming", "enter", chara, lane);
            if (auto fn = orig<MoveVMCheckTransitionTimingFn>(
                    Slot::MoveVMCheckTransitionTiming))
                fn(chara, lane_ptr, unused, start_time);
            if (interesting || should_emit_movevm_inner_probe(
                    chara, static_cast<uint8_t*>(lane_ptr)))
                emit_movevm_lane_probe(
                    "MoveVMCheckTransitionTiming", "exit", chara, lane);
        }

        static int16_t __fastcall detour_movevm_run_bytecode_script(
            void* chara,
            void* bytecode,
            uint16_t arg_count,
            uint16_t* args)
        {
            emit_movevm_bytecode_probe(
                "enter", chara, bytecode, arg_count, args, 0);
            int16_t result = 0;
            if (auto fn = orig<MoveVMRunBytecodeScriptFn>(
                    Slot::MoveVMRunBytecodeScript))
                result = fn(chara, bytecode, arg_count, args);
            emit_movevm_bytecode_probe(
                "exit", chara, bytecode, arg_count, args, result);
            return result;
        }

        static int __fastcall detour_movevm_transition_to_move(
            void* chara,
            int lane,
            uint32_t packed_move,
            uint32_t start_time,
            uint32_t param5,
            uint64_t motion_flags_spill,
            int param7,
            int param8,
            int motion_arg_count,
            void* motion_args)
        {
            emit_movevm_transition_call_probe(
                "enter", chara, lane, packed_move, start_time, 0);
            int result = 0;
            if (auto fn = orig<MoveVMTransitionToMoveFn>(
                    Slot::MoveVMTransitionToMove))
                result = fn(chara, lane, packed_move, start_time, param5,
                            motion_flags_spill, param7, param8,
                            motion_arg_count, motion_args);
            emit_movevm_transition_call_probe(
                "exit", chara, lane, packed_move, start_time, result);
            return result;
        }

        static uint64_t __fastcall detour_movevm_execute_bank_slot_script(
            void* chara,
            int arg_count,
            int16_t* args)
        {
            const uintptr_t ret_addr = reinterpret_cast<uintptr_t>(_ReturnAddress());
            emit_movevm_bank_slot_script_probe(
                "enter", chara, arg_count, args, 0, ret_addr);
            uint64_t result = 0;
            if (auto fn = orig<MoveVMExecuteBankSlotScriptFn>(
                    Slot::MoveVMExecuteBankSlotScript))
                result = fn(chara, arg_count, args);
            emit_movevm_bank_slot_script_probe(
                "exit", chara, arg_count, args, result, ret_addr);
            return result;
        }

        static uint64_t __fastcall detour_movevm_decode_variadic_stream_args(
            void* chara,
            int arg_count,
            uint16_t* args,
            int lane_idx)
        {
            const uintptr_t ret_addr = reinterpret_cast<uintptr_t>(_ReturnAddress());
            emit_movevm_transition_author_probe(
                "enter", chara, arg_count, args, lane_idx, 0, ret_addr);
            uint64_t result = 0;
            if (auto fn = orig<MoveVMDecodeVariadicStreamArgsFn>(
                    Slot::MoveVMDecodeVariadicStreamArgs))
                result = fn(chara, arg_count, args, lane_idx);
            emit_movevm_transition_author_probe(
                "exit", chara, arg_count, args, lane_idx, result, ret_addr);
            return result;
        }

        static uint32_t __fastcall detour_movevm_push_anim_notify_onto_secondary_stack(
            void* stack_base,
            void* chara_back_ptr,
            int event_id,
            uint32_t sub_id_mode,
            void* payload,
            uint32_t extra)
        {
            static constexpr uintptr_t kLastRandomVariantOff = 0x25C;
            const uintptr_t ret_addr = reinterpret_cast<uintptr_t>(_ReturnAddress());
            if (sub_id_mode == 0xfffffffeu)
                (void)replay_scrub_repair_secondary_action_stack_last_variant_before_random_push(
                    stack_base);
            const uint64_t rng_before = RngTraceHook::instance().total_calls();
            const uint32_t variant_before = stack_base
                ? safe_read_uint32(static_cast<const uint8_t*>(stack_base)
                    + kLastRandomVariantOff)
                : 0;

            uint32_t result = 0xffffffffu;
            if (auto fn = orig<MoveVMPushAnimNotifyOntoSecondaryStackFn>(
                    Slot::MoveVMPushAnimNotifyOntoSecondaryStack))
                result = fn(stack_base, chara_back_ptr, event_id, sub_id_mode,
                            payload, extra);

            const uint32_t variant_after = stack_base
                ? safe_read_uint32(static_cast<const uint8_t*>(stack_base)
                    + kLastRandomVariantOff)
                : 0;
            const uint64_t rng_after = RngTraceHook::instance().total_calls();
            emit_secondary_action_stack_push_probe(
                stack_base, chara_back_ptr, event_id, sub_id_mode, payload,
                extra, result, variant_before, variant_after, rng_before,
                rng_after, ret_addr);
            return result;
        }

        static void __fastcall detour_cpu_subvm_swap(
            void* pp_subvm_slot,
            void* replacement_subvm)
        {
            const uintptr_t image_base = NativeBinding::imageBase();
            const uintptr_t slot_address =
                reinterpret_cast<uintptr_t>(pp_subvm_slot);
            const uintptr_t replacement_address =
                reinterpret_cast<uintptr_t>(replacement_subvm);
            const uintptr_t old_subvm = safe_read_uintptr(pp_subvm_slot);

            int32_t player_index = -1;
            if (image_base != 0)
            {
                constexpr uintptr_t kCpuSchedArrayRva = 0x4715400;
                constexpr size_t kCpuSchedStride = 0x60;
                constexpr size_t kSubVMSlotOffset = 0x50;
                for (int32_t index = 0; index < 2; ++index)
                {
                    const uintptr_t expected_slot = image_base
                        + kCpuSchedArrayRva
                        + static_cast<uintptr_t>(index)
                            * kCpuSchedStride
                        + kSubVMSlotOffset;
                    if (slot_address == expected_slot)
                    {
                        player_index = index;
                        break;
                    }
                }
            }

            NativeTickRootTraceSample::CpuSubVM before {};
            if (player_index >= 0)
            {
                before = capture_cpu_subvm_trace_sample(
                    image_base, static_cast<size_t>(player_index));
            }

            const uintptr_t replacement_vtable = safe_read_uintptr(
                replacement_subvm);
            const uintptr_t replacement_vtable_rva =
                replacement_vtable >= image_base && image_base != 0
                ? replacement_vtable - image_base : 0;
            bool original_called = false;
            if (auto fn = orig<Void2PtrFn>(Slot::CpuSubVMSwap))
            {
                original_called = true;
                fn(pp_subvm_slot, replacement_subvm);
            }

            const uintptr_t published_subvm =
                safe_read_uintptr(pp_subvm_slot);
            NativeTickRootTraceSample::CpuSubVM after {};
            if (player_index >= 0)
            {
                after = capture_cpu_subvm_trace_sample(
                    image_base, static_cast<size_t>(player_index));
            }

            int32_t replay_sequence = -1;
            int32_t replay_round = -1;
            int32_t replay_master = -1;
            (void)replay_scrub_read_native_trace_position(
                replay_sequence, replay_round, replay_master);
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            ReplayTraceFields fields;
            fields.uinteger(
                    "swap_sequence",
                    s_cpu_subvm_swap_seq.fetch_add(
                        1, std::memory_order_acq_rel) + 1)
                .uinteger("thread_id", GetCurrentThreadId())
                .integer("player_index", player_index)
                .boolean("global_scheduler_slot", player_index >= 0)
                .boolean("original_called", original_called)
                .boolean("native_scope_owned", native_scope.active)
                .boolean(
                    "rolling_back",
                    native_scope.active && native_scope.rolling_back)
                .integer(
                    "native_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_frame)
                        : int64_t{-1})
                .integer(
                    "logical_frame",
                    native_scope.active
                        ? static_cast<int64_t>(native_scope.logical_frame)
                        : int64_t{-1})
                .integer("replay_sequence", replay_sequence)
                .integer("replay_round", replay_round)
                .integer("replay_master", replay_master)
                .hex("subvm_slot", slot_address)
                .hex("old_subvm", old_subvm)
                .hex("requested_replacement_subvm",
                    replacement_address)
                .hex("requested_replacement_vtable_rva",
                    replacement_vtable_rva)
                .hex("published_subvm", published_subvm)
                .boolean(
                    "replacement_published",
                    published_subvm == replacement_address)
                .boolean(
                    "address_reused",
                    old_subvm != 0
                        && old_subvm == replacement_address)
                .hex("before_subvm_vtable_rva",
                    before.subvm_vtable_rva)
                .hex("after_subvm_vtable_rva",
                    after.subvm_vtable_rva)
                .hex("before_subvm_semantic_hash",
                    before.subvm_common_semantic_hash)
                .hex("after_subvm_semantic_hash",
                    after.subvm_common_semantic_hash)
                .boolean("before_identity_links_valid",
                    before.identity_links_valid)
                .boolean("after_identity_links_valid",
                    after.identity_links_valid);
            emit("native_cpu_subvm_swap", fields);
        }

        static void __fastcall detour_cpu_subvm_factory(
            void* sched_state,
            int move_id)
        {
            const uintptr_t image_base = NativeBinding::imageBase();
            const uintptr_t sched_address =
                reinterpret_cast<uintptr_t>(sched_state);
            int32_t player_index = -1;
            if (image_base != 0)
            {
                constexpr uintptr_t kCpuSchedArrayRva = 0x4715400;
                constexpr size_t kCpuSchedStride = 0x60;
                for (int32_t index = 0; index < 2; ++index)
                {
                    if (sched_address == image_base + kCpuSchedArrayRva
                            + static_cast<uintptr_t>(index)
                                * kCpuSchedStride)
                    {
                        player_index = index;
                        break;
                    }
                }
            }

            NativeTickRootTraceSample::CpuSubVM before {};
            if (player_index >= 0)
            {
                before = capture_cpu_subvm_trace_sample(
                    image_base, static_cast<size_t>(player_index));
            }

            bool original_called = false;
            if (auto fn = orig<VoidPtrIntFn>(Slot::CpuSubVMFactory))
            {
                original_called = true;
                fn(sched_state, move_id);
            }

            NativeTickRootTraceSample::CpuSubVM after {};
            if (player_index >= 0)
            {
                after = capture_cpu_subvm_trace_sample(
                    image_base, static_cast<size_t>(player_index));
            }

            int32_t replay_sequence = -1;
            int32_t replay_round = -1;
            int32_t replay_master = -1;
            (void)replay_scrub_read_native_trace_position(
                replay_sequence, replay_round, replay_master);
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            ReplayTraceFields fields;
            fields.uinteger(
                    "factory_sequence",
                    s_cpu_subvm_factory_seq.fetch_add(
                        1, std::memory_order_acq_rel) + 1)
                .uinteger("thread_id", GetCurrentThreadId())
                .string("factory_kind", "ordinary-move")
                .integer("player_index", player_index)
                .integer("move_id", move_id)
                .integer("special_param0", -1)
                .integer("special_param1", -1)
                .boolean("global_scheduler_state", player_index >= 0)
                .boolean("original_called", original_called)
                .boolean("native_scope_owned", native_scope.active)
                .boolean(
                    "rolling_back",
                    native_scope.active && native_scope.rolling_back)
                .integer(
                    "native_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_frame)
                        : int64_t{-1})
                .integer(
                    "logical_frame",
                    native_scope.active
                        ? static_cast<int64_t>(native_scope.logical_frame)
                        : int64_t{-1})
                .integer("replay_sequence", replay_sequence)
                .integer("replay_round", replay_round)
                .integer("replay_master", replay_master)
                .hex("sched_state", sched_address)
                .hex("before_subvm", before.subvm)
                .hex("after_subvm", after.subvm)
                .boolean(
                    "replacement_published",
                    before.subvm != after.subvm)
                .boolean(
                    "subvm_address_unchanged",
                    before.subvm == after.subvm)
                .hex("before_subvm_vtable_rva",
                    before.subvm_vtable_rva)
                .hex("after_subvm_vtable_rva",
                    after.subvm_vtable_rva)
                .hex("before_sched_semantic_hash",
                    before.sched_semantic_hash)
                .hex("after_sched_semantic_hash",
                    after.sched_semantic_hash)
                .hex("before_subvm_semantic_hash",
                    before.subvm_common_semantic_hash)
                .hex("after_subvm_semantic_hash",
                    after.subvm_common_semantic_hash)
                .boolean("before_identity_links_valid",
                    before.identity_links_valid)
                .boolean("after_identity_links_valid",
                    after.identity_links_valid);
            emit("native_cpu_subvm_factory", fields);
        }

        static void __fastcall detour_cpu_subvm_special_factory(
            void* sched_state,
            uint16_t special_param0,
            uint16_t special_param1)
        {
            const uintptr_t image_base = NativeBinding::imageBase();
            const uintptr_t sched_address =
                reinterpret_cast<uintptr_t>(sched_state);
            int32_t player_index = -1;
            if (image_base != 0)
            {
                constexpr uintptr_t kCpuSchedArrayRva = 0x4715400;
                constexpr size_t kCpuSchedStride = 0x60;
                for (int32_t index = 0; index < 2; ++index)
                {
                    if (sched_address == image_base + kCpuSchedArrayRva
                            + static_cast<uintptr_t>(index)
                                * kCpuSchedStride)
                    {
                        player_index = index;
                        break;
                    }
                }
            }

            NativeTickRootTraceSample::CpuSubVM before {};
            if (player_index >= 0)
            {
                before = capture_cpu_subvm_trace_sample(
                    image_base, static_cast<size_t>(player_index));
            }

            bool original_called = false;
            if (auto fn = orig<VoidPtr2U16Fn>(
                    Slot::CpuSubVMSpecialFactory))
            {
                original_called = true;
                fn(sched_state, special_param0, special_param1);
            }

            NativeTickRootTraceSample::CpuSubVM after {};
            if (player_index >= 0)
            {
                after = capture_cpu_subvm_trace_sample(
                    image_base, static_cast<size_t>(player_index));
            }

            int32_t replay_sequence = -1;
            int32_t replay_round = -1;
            int32_t replay_master = -1;
            (void)replay_scrub_read_native_trace_position(
                replay_sequence, replay_round, replay_master);
            const auto& native_scope =
                g_rollback_native_simulation_scope;
            ReplayTraceFields fields;
            fields.uinteger(
                    "factory_sequence",
                    s_cpu_subvm_factory_seq.fetch_add(
                        1, std::memory_order_acq_rel) + 1)
                .uinteger("thread_id", GetCurrentThreadId())
                .string("factory_kind", "special-move")
                .integer("player_index", player_index)
                .integer("move_id", 0x69)
                .integer("special_param0", special_param0)
                .integer("special_param1", special_param1)
                .boolean("global_scheduler_state", player_index >= 0)
                .boolean("original_called", original_called)
                .boolean("native_scope_owned", native_scope.active)
                .boolean(
                    "rolling_back",
                    native_scope.active && native_scope.rolling_back)
                .integer(
                    "native_coordinate",
                    native_scope.active
                        ? static_cast<int64_t>(
                            native_scope.armed_clock.battle_last_frame)
                        : int64_t{-1})
                .integer(
                    "logical_frame",
                    native_scope.active
                        ? static_cast<int64_t>(native_scope.logical_frame)
                        : int64_t{-1})
                .integer("replay_sequence", replay_sequence)
                .integer("replay_round", replay_round)
                .integer("replay_master", replay_master)
                .hex("sched_state", sched_address)
                .hex("before_subvm", before.subvm)
                .hex("after_subvm", after.subvm)
                .boolean(
                    "replacement_published",
                    before.subvm != after.subvm)
                .boolean(
                    "subvm_address_unchanged",
                    before.subvm == after.subvm)
                .hex("before_subvm_vtable_rva",
                    before.subvm_vtable_rva)
                .hex("after_subvm_vtable_rva",
                    after.subvm_vtable_rva)
                .hex("before_sched_semantic_hash",
                    before.sched_semantic_hash)
                .hex("after_sched_semantic_hash",
                    after.sched_semantic_hash)
                .hex("before_subvm_semantic_hash",
                    before.subvm_common_semantic_hash)
                .hex("after_subvm_semantic_hash",
                    after.subvm_common_semantic_hash)
                .boolean("before_identity_links_valid",
                    before.identity_links_valid)
                .boolean("after_identity_links_valid",
                    after.identity_links_valid);
            emit("native_cpu_subvm_factory", fields);
        }

        static void __fastcall detour_replay_list_request_replay_file_exec(
            void* self, void* stack, void* out_bool)
        {
            log_ptrs("native_replay_list_request_replay_file_exec_enter",
                     reinterpret_cast<uintptr_t>(self),
                     reinterpret_cast<uintptr_t>(stack),
                     reinterpret_cast<uintptr_t>(out_bool));
            if (auto fn = orig<Exec3VoidFn>(Slot::ReplayListRequestReplayFileExec))
                fn(self, stack, out_bool);
            const bool result = safe_read_bool(out_bool);
            ReplayTraceFields f;
            f.hex("container", reinterpret_cast<uintptr_t>(self))
             .boolean("result", result)
             .integer("current_replay_version",
                      replay_container_current_version(self));
            emit("native_replay_list_request_replay_file_exec_exit", f);
        }

        static void __fastcall detour_replay_list_request_ready_replay_exec(
            void* self, void* stack)
        {
            log_ptrs("native_replay_list_request_ready_replay_exec_enter",
                     reinterpret_cast<uintptr_t>(self),
                     reinterpret_cast<uintptr_t>(stack));
            if (auto fn = orig<Void2PtrFn>(Slot::ReplayListRequestReadyReplayExec))
                fn(self, stack);
            log_ptrs("native_replay_list_request_ready_replay_exec_exit",
                     reinterpret_cast<uintptr_t>(self),
                     reinterpret_cast<uintptr_t>(stack));
        }

        static void __fastcall detour_replay_list_on_request_play_exec(
            void* self, void* stack)
        {
            log_ptrs("native_replay_list_on_request_play_exec_enter",
                     reinterpret_cast<uintptr_t>(self),
                     reinterpret_cast<uintptr_t>(stack));
            if (auto fn = orig<Void2PtrFn>(Slot::ReplayListOnRequestPlayExec))
                fn(self, stack);
            log_ptrs("native_replay_list_on_request_play_exec_exit",
                     reinterpret_cast<uintptr_t>(self),
                     reinterpret_cast<uintptr_t>(stack));
        }

        static void __fastcall detour_replay_list_apply_temporary_data_exec(
            void* self, void* stack)
        {
            ReplayTraceFields f;
            f.hex("container", reinterpret_cast<uintptr_t>(self))
             .hex("stack", reinterpret_cast<uintptr_t>(stack))
             .integer("current_replay_version_before",
                      replay_container_current_version(self));
            emit("native_replay_list_apply_temporary_data_exec_enter", f);
            if (auto fn = orig<Void2PtrFn>(Slot::ReplayListApplyTemporaryDataExec))
                fn(self, stack);
            ReplayTraceFields out;
            out.hex("container", reinterpret_cast<uintptr_t>(self))
               .hex("stack", reinterpret_cast<uintptr_t>(stack))
               .integer("current_replay_version_after",
                        replay_container_current_version(self));
            emit("native_replay_list_apply_temporary_data_exec_exit", out);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] apply_temporary_data container=0x{:X} "
                "version={}\n"), reinterpret_cast<uintptr_t>(self),
                replay_container_current_version(self));
        }

        static bool __fastcall detour_handle_replay_file_request(
            void* container, void* board_key, int32_t entry_index)
        {
            const std::string board = safe_read_fstring_utf8(board_key);
            ReplayTraceFields f;
            f.hex("container", reinterpret_cast<uintptr_t>(container))
             .hex("board_key_ptr", reinterpret_cast<uintptr_t>(board_key))
             .string("board_key", board)
             .integer("entry_index", entry_index)
             .integer("current_replay_version_before",
                      replay_container_current_version(container));
            emit("native_replay_handle_file_request_enter", f);
            bool result = false;
            if (auto fn = orig<ReplayFileRequestFn>(Slot::HandleReplayFileRequest))
                result = fn(container, board_key, entry_index);
            ReplayTraceFields out;
            out.hex("container", reinterpret_cast<uintptr_t>(container))
               .string("board_key", board)
               .integer("entry_index", entry_index)
               .boolean("result", result)
               .integer("current_replay_version_after",
                        replay_container_current_version(container));
            emit("native_replay_handle_file_request_exit", out);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] handle_file_request board='{}' index={} "
                "result={} version={}\n"), RC::to_generic_string(board),
                entry_index, result ? 1 : 0,
                replay_container_current_version(container));
            return result;
        }

        static void __fastcall detour_handle_replay_file_request_complete(
            void* container, uint64_t request_id, bool succeeded, void* payload)
        {
            const TArrayByteNative payload_header =
                safe_read_tarray_header(payload);
            bool captured = false;
            if (succeeded && payload_header.data && payload_header.num > 0 &&
                payload_header.max >= payload_header.num)
            {
                captured = replay_scrub_capture_native_replay_entry_payload(
                    container, request_id, payload_header.data,
                    static_cast<size_t>(payload_header.num));
            }

            ReplayTraceFields f;
            f.hex("container", reinterpret_cast<uintptr_t>(container))
             .uinteger("request_id", request_id)
             .boolean("succeeded", succeeded)
             .boolean("captured", captured)
             .integer("current_replay_version_before",
                      replay_container_current_version(container));
            add_tarray_fields(f, "payload", payload);
            emit("native_replay_handle_file_request_complete_enter", f);
            if (auto fn = orig<ReplayFileRequestCompleteFn>(
                    Slot::HandleReplayFileRequestComplete))
                fn(container, request_id, succeeded, payload);
            ReplayTraceFields out;
            out.hex("container", reinterpret_cast<uintptr_t>(container))
               .uinteger("request_id", request_id)
               .boolean("succeeded", succeeded)
               .boolean("captured", captured)
               .integer("current_replay_version_after",
                        replay_container_current_version(container));
            add_tarray_fields(out, "payload", payload);
            emit("native_replay_handle_file_request_complete_exit", out);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] handle_file_request_complete id={} "
                "succeeded={} captured={} payload_num={} version={}\n"),
                request_id, succeeded ? 1 : 0,
                captured ? 1 : 0, payload_header.num,
                replay_container_current_version(container));
        }

        static bool __fastcall detour_decompress_ulx1_entry_blob(
            void* out_payload, void* input_blob)
        {
            ReplayTraceFields f;
            add_tarray_fields(f, "input", input_blob);
            add_tarray_fields(f, "output_before", out_payload);
            emit("native_replay_decompress_ulx1_enter", f);
            bool result = false;
            if (auto fn = orig<Bool2PtrFn>(Slot::DecompressUlx1EntryBlob))
                result = fn(out_payload, input_blob);
            ReplayTraceFields out;
            out.boolean("result", result);
            add_tarray_fields(out, "input", input_blob);
            add_tarray_fields(out, "output_after", out_payload);
            emit("native_replay_decompress_ulx1_exit", out);
            return result;
        }

        static bool __fastcall detour_deserialize_entry_payload_to_list_item(
            void* payload, void* out_item)
        {
            ReplayTraceFields f;
            add_tarray_fields(f, "payload", payload);
            f.hex("out_item", reinterpret_cast<uintptr_t>(out_item))
             .integer("version_before", replay_list_item_version(out_item));
            emit("native_replay_deserialize_entry_payload_enter", f);
            bool result = false;
            if (auto fn = orig<Bool2PtrFn>(Slot::DeserializeEntryPayloadToListItem))
                result = fn(payload, out_item);
            ReplayTraceFields out;
            add_tarray_fields(out, "payload", payload);
            out.hex("out_item", reinterpret_cast<uintptr_t>(out_item))
               .boolean("result", result)
               .integer("version_after", replay_list_item_version(out_item));
            emit("native_replay_deserialize_entry_payload_exit", out);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] deserialize_entry_payload result={} "
                "payload_num={} version={}\n"), result ? 1 : 0,
                safe_read_tarray_header(payload).num,
                replay_list_item_version(out_item));
            return result;
        }

        std::array<HookSlot, kSlotCount> m_slots{};
        std::array<ReplayRingCacheEntry,
                   kCachedReplayRingRounds * kCachedReplayRingMasters
                       * kCachedReplayRingSlots> m_replay_ring_cache{};
        std::atomic<bool> m_installed {false};
        std::atomic<bool> m_lifecycle_installed {false};
        std::atomic<bool> m_lifecycle_frame_input_log_only {false};
        std::atomic<uint32_t> m_lifecycle_owner_mask {0};
        std::mutex m_lifecycle_owner_mutex;
        std::atomic<bool> m_resume_phase_signal_installed {false};
        std::atomic<uintptr_t> m_latest_replay_input_overlay {0};
        std::atomic<bool> m_stock_battle_asset_barrier_armed {false};
        std::atomic<bool> m_stock_battle_asset_barrier_ready {false};
        std::atomic<bool> m_stock_battle_asset_native_result {false};
        std::atomic<bool> m_stock_battle_asset_completion_withheld {false};
        std::atomic<bool> m_stock_battle_asset_release_emitted {false};
        std::atomic<bool> m_stock_battle_ready_emitted {false};
        std::atomic<bool> m_stock_battle_ready_callback_ok {false};
        RollbackScheduledReleaseClaim m_stock_battle_asset_release_claim {};
        std::atomic<StockBattleAssetReleaseCallback>
            m_stock_battle_asset_release_callback {nullptr};
        std::atomic<void*> m_stock_battle_asset_release_context {nullptr};
        static inline std::atomic<uint64_t> s_seq {0};
        static inline std::atomic<uint32_t> s_stage2_enter_count {0};
        static inline std::atomic<uint32_t> s_stage2_exit_count {0};
        static inline std::atomic<uint32_t> s_stage3_enter_count {0};
        static inline std::atomic<uint32_t> s_stage3_exit_count {0};
        static inline std::atomic<uint64_t>
            s_root_motion_transaction_seq {0};
        static inline std::atomic<uint64_t>
            s_pose_producer_transaction_seq {0};
        static inline std::atomic<uint64_t>
            s_pre_main_motion_checkpoint_seq {0};
        static inline std::atomic<uint64_t>
            s_native_tick_root_entry_seq {0};
        static inline std::atomic<uint64_t>
            s_frame_input_log_tick_control_seq {0};
        static inline std::atomic<uint64_t>
            s_cpu_subvm_swap_seq {0};
        static inline std::atomic<uint64_t>
            s_cpu_subvm_factory_seq {0};
        static inline std::atomic<uint32_t>
            s_input_stage_non_replay_suppress_count {0};
    };
}
