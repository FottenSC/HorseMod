// ============================================================================
// Horse::NativeReplayTraceHook
//
// Lightweight PolyHook probes for SC6's native replay launch path.  This is
// the in-process equivalent of the x64dbg breakpoint plan: it records the
// order and key arguments/return values while the user drives the stock replay
// UI, without changing game behavior.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "ReplayDebugTrace.hpp"
#include "RngTraceHook.hpp"

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
#include <string>

#ifndef HORSEMOD_ENABLE_EXTENDED_NATIVE_REPLAY_TRACE
#define HORSEMOD_ENABLE_EXTENDED_NATIVE_REPLAY_TRACE 0
#endif

#ifndef HORSEMOD_ENABLE_REPLAY_INPUT_STAGE_TRACE
#define HORSEMOD_ENABLE_REPLAY_INPUT_STAGE_TRACE 1
#endif

#ifndef HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
#define HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE 0
#endif

namespace Horse
{
    bool replay_scrub_repair_latest_engine_input_before_chara_input(
        void* chara) noexcept;
    void replay_scrub_append_secondary_action_stack_push_trace_context(
        ReplayTraceFields& f) noexcept;
    bool replay_scrub_repair_secondary_action_stack_last_variant_before_random_push(
        void* stack_base) noexcept;
    bool replay_scrub_repair_secondary_action_stack_last_variant_before_chara_input(
        void* chara) noexcept;
    void replay_scrub_note_tick_chara_main_simulation_exit(
        void* chara) noexcept;

    class NativeReplayTraceHook
    {
    public:
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
            ok &= hook(Slot::HandleReplayFileRequestComplete,
                       base + kHandleReplayFileRequestCompleteRVA,
                       &NativeReplayTraceHook::detour_handle_replay_file_request_complete);
            ok &= hook(Slot::DecompressUlx1EntryBlob,
                       base + kDecompressUlx1EntryBlobRVA,
                       &NativeReplayTraceHook::detour_decompress_ulx1_entry_blob);
            ok &= hook(Slot::DeserializeEntryPayloadToListItem,
                       base + kDeserializeEntryPayloadToListItemRVA,
                       &NativeReplayTraceHook::detour_deserialize_entry_payload_to_list_item);
#endif

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

            bool lifecycle_trace_ok = true;
            lifecycle_trace_ok &= hook(
                Slot::TickCharaInput,
                base + kTickCharaInputRVA,
                &NativeReplayTraceHook::detour_tick_chara_input);
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
                Slot::SolveBonePose,
                base + kSolveBonePoseRVA,
                &NativeReplayTraceHook::detour_solve_bone_pose);
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
            lifecycle_trace_ok &= hook(
                Slot::MoveVMTransitionToMove,
                base + kMoveVMTransitionToMoveRVA,
                &NativeReplayTraceHook::detour_movevm_transition_to_move);
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
            if (!lifecycle_trace_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[NativeReplayTraceHook] lifecycle trace hooks "
                    "partially failed; continuing with replay trace hooks\n"));
            }

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
             .boolean("lifecycle_probe_trace",
#if HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
                      true
#else
                      false
#endif
             );
            emit("native_replay_trace_hooks_installed", f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTraceHook] installed replay native trace hooks "
                "(image_base=0x{:X})\n"), base);
            return true;
        }

        void uninstall()
        {
            if (!m_installed.exchange(false)) return;
            for (auto& s : m_slots)
            {
                if (s.detour)
                {
                    s.detour->unHook();
                    s.detour.reset();
                }
                s.trampoline = 0;
            }
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
            TickCharaInput,
            TickCharaMainSimulation,
            UpdateOpponentRelativeAngles,
            FinalizeTickPoseAndState,
            SolveBonePose,
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
        static constexpr uintptr_t kTickCharaInputRVA = 0x312510;
        static constexpr uintptr_t kTickCharaMainSimulationRVA = 0x34DA70;
        static constexpr uintptr_t kUpdateOpponentRelativeAnglesRVA = 0x305E50;
        static constexpr uintptr_t kFinalizeTickPoseAndStateRVA = 0x305B50;
        static constexpr uintptr_t kSolveBonePoseRVA = 0x2EDB90;
        static constexpr uintptr_t kTickHitResolutionAndBodyCollisionRVA = 0x33CCA0;
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
        static constexpr uintptr_t kMoveVMGlobalVarBankBaseRVA = 0x470D200;
        static constexpr uintptr_t kMoveVMTransitionThresholdNowFlagRVA = 0x470DE64;
        static constexpr uintptr_t kMoveVMDeferredTransitionScheduleFrameRVA = 0x470DE68;
        static constexpr uintptr_t kMoveVMDeferredTransitionScheduleFlagRVA = 0x470DE6C;
        static constexpr uintptr_t kMoveVMDeferredTransitionCommitFlagRVA = 0x470DEC0;
        static constexpr uintptr_t kRVA_LatestEngineInput = 0x4855700;
        static constexpr uintptr_t kRVA_InputRingBaseOffset = 0x470DED0;
        static constexpr uintptr_t kRVA_PerPlayerInputRing = 0x485E750;
        static constexpr uintptr_t kRVA_PerPlayerInputCursor = 0x485EB20;
        static constexpr uintptr_t kRVA_CharaSlotP1 = 0x470DE90;
        static constexpr uintptr_t kRVA_CharaSlotP2 = 0x470DE98;
        static constexpr uintptr_t kCharaVfxEffectAnchorOffset = 0x95FA0;

        static constexpr size_t kCachedReplayRingRounds = 16;
        static constexpr size_t kCachedReplayRingMasters = 8192;
        static constexpr size_t kCachedReplayRingSlots = 2;
        static constexpr size_t kCachedReplayRingEntryBytes = 16;

        static constexpr size_t slot_index(Slot s) noexcept
        {
            return static_cast<size_t>(s);
        }
        static constexpr size_t kSlotCount = static_cast<size_t>(Slot::Count);

        static constexpr size_t installed_hook_count() noexcept
        {
#if HORSEMOD_ENABLE_EXTENDED_NATIVE_REPLAY_TRACE
            return kSlotCount;
#else
            size_t count = slot_index(Slot::DeferredStageMapPathCallback) + 1;
#if HORSEMOD_ENABLE_REPLAY_INPUT_STAGE_TRACE
            count += 2;
#endif
            count += 21;
            return count;
#endif
        }

        bool hook(Slot slot, uintptr_t target, void* detour_fn)
        {
            HookSlot& s = m_slots[slot_index(slot)];
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
        using SolveBonePoseFn = void(__fastcall*)(void*, float*, uint32_t);

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
             .real("clip_frame", safe_read_float(c + 0x2B47C))
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

        static bool should_emit_movevm_inner_probe(void* chara,
                                                   uint8_t* lane) noexcept
        {
#if !HORSEMOD_ENABLE_REPLAY_LIFECYCLE_TRACE
            (void)chara;
            (void)lane;
            return false;
#else
            if (!chara || !lane) return false;
            if (player_index_for_chara(chara) != 0) return false;
            if (safe_read_int16(lane + 0x00) != 1) return false;
            const int move = safe_read_int16(lane + 0x02);
            const int target = safe_read_int16(lane + 0x5A);
            const float frame = safe_read_float(lane + 0x08);
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
             .integer("player", 1)
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
             .integer("player", 1)
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
            if (sub_id_mode != 0xfffffffeu) return;

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
            emit("secondary_action_stack_push_random_variant", f);
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
            const size_t ring_index = static_cast<size_t>(cursor) & 0x1FF;
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
                std::memcpy(entry.bytes, bytes, sizeof(bytes));
                entry.frame_id = frame_id;
                entry.cursor = cursor;
                entry.slot = slot;
                entry.valid = true;
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

        static void add_replay_input_overlay_fields(
            ReplayTraceFields& f,
            void* chara) noexcept
        {
            const auto* c = static_cast<const uint8_t*>(chara);
            const int32_t input_cursor = safe_read_int32(c + 0x3B0);
            const size_t ring_index = static_cast<size_t>(
                static_cast<uint32_t>(input_cursor)) & 0x1FF;
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
            bool result = false;
            if (auto fn = orig<BoolPtrFn>(Slot::HasAnyBattleRequest))
                result = fn(gi);
            ReplayTraceFields f;
            f.hex("game_instance", reinterpret_cast<uintptr_t>(gi))
             .boolean("result", result);
            add_battle_request_queue_fields(f, gi);
            emit("native_replay_has_any_battle_request", f);
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
            if (chara)
            {
                instance().m_latest_replay_input_overlay.store(
                    reinterpret_cast<uintptr_t>(chara),
                    std::memory_order_release);
                instance().cache_replay_ring_entries(chara);
            }
            const uint32_t enter_index = s_stage3_enter_count.fetch_add(
                1, std::memory_order_acq_rel);
            const int32_t enter_master = safe_read_int32(
                static_cast<uint8_t*>(chara) + 0x3A4);
            if (enter_index < 256 || (enter_index % 120) == 0
                || (enter_index > 512 && enter_master >= 0
                    && enter_master < 512))
            {
                emit_replay_input_stage(
                    "native_replay_stage3_enter", chara, enter_index);
            }
            if (auto fn = orig<VoidPtrFn>(Slot::ReplayPlaybackPushInputs))
                fn(chara);
            const uint32_t exit_index = s_stage3_exit_count.fetch_add(
                1, std::memory_order_acq_rel);
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

        static void __fastcall detour_tick_chara_input(
            void* chara,
            void* input_transform_list,
            void* encoded_input_transform_list)
        {
            (void)replay_scrub_repair_latest_engine_input_before_chara_input(
                chara);
            (void)replay_scrub_repair_secondary_action_stack_last_variant_before_chara_input(
                chara);
            if (auto fn = orig<TickCharaInputFn>(Slot::TickCharaInput))
                fn(chara, input_transform_list, encoded_input_transform_list);
        }

        static void __fastcall detour_tick_chara_main_simulation(
            void* slot)
        {
            void* chara = reinterpret_cast<void*>(safe_read_uintptr(slot));
            emit_chara_lifecycle(
                "TickCharaMainSimulation", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::TickCharaMainSimulation))
                fn(slot);
            replay_scrub_note_tick_chara_main_simulation_exit(chara);
            emit_chara_lifecycle(
                "TickCharaMainSimulation", "exit", chara);
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
            emit_chara_lifecycle(
                "FinalizeTickPoseAndState", "enter", chara);
            if (auto fn = orig<VoidPtrFn>(Slot::FinalizeTickPoseAndState))
                fn(chara);
            emit_chara_lifecycle(
                "FinalizeTickPoseAndState", "exit", chara);
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
            if (auto fn = orig<SolveBonePoseFn>(Slot::SolveBonePose))
                fn(anchor, primary_provider_buffer, flags);
            emit_chara_lifecycle("SolveBonePose", "exit", chara);
        }

        static void __fastcall detour_tick_hit_resolution_and_body_collision()
        {
            emit_lifecycle_slots(
                "TickHitResolutionAndBodyCollision", "enter");
            if (auto fn = orig<VoidNoArgFn>(
                    Slot::TickHitResolutionAndBodyCollision))
                fn();
            emit_lifecycle_slots(
                "TickHitResolutionAndBodyCollision", "exit");
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
            ReplayTraceFields f;
            f.hex("container", reinterpret_cast<uintptr_t>(container))
             .uinteger("request_id", request_id)
             .boolean("succeeded", succeeded)
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
               .integer("current_replay_version_after",
                        replay_container_current_version(container));
            add_tarray_fields(out, "payload", payload);
            emit("native_replay_handle_file_request_complete_exit", out);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[NativeReplayTrace] handle_file_request_complete id={} "
                "succeeded={} payload_num={} version={}\n"),
                request_id, succeeded ? 1 : 0,
                safe_read_tarray_header(payload).num,
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
        std::atomic<uintptr_t> m_latest_replay_input_overlay {0};
        static inline std::atomic<uint64_t> s_seq {0};
        static inline std::atomic<uint32_t> s_stage2_enter_count {0};
        static inline std::atomic<uint32_t> s_stage2_exit_count {0};
        static inline std::atomic<uint32_t> s_stage3_enter_count {0};
        static inline std::atomic<uint32_t> s_stage3_exit_count {0};
    };
}
