// ============================================================================
// Horse::ReplayScrubDiag - exhaustive diagnostic logging for the replay
// timeline mod.  Resolves UDemoNetDriver via UE4 reflection, dumps chara
// MoveVM state (CurrentMoveID, position, velocity), and reports per-tick
// deltas so we can confirm what's actually driving the simulation during
// match-replay viewing.
//
// Why this exists
// ---------------
// The simpler dump in ReplayScrub::dump_replay_state() only logs SC6
// input-pipeline state (BM/IL/RDB cursors, IL cache contents, chara
// replay-cursor fields at +0x39C..+0x3B4).  But the 2026-05-12 audit
// established those fields are dead during match-replay viewing -
// inputs reach the chara via UDemoNetDriver replication, not the
// custom SC6 pipeline.  This file captures the OTHER half of the
// picture: the UE4-side replication state and the chara MoveVM
// derived state, so we can correlate "did our restore stick?" /
// "is anything ticking forward?" against what the engine is actually
// doing.
//
// What's dumped
// -------------
//   * UDemoNetDriver presence + key fields:
//       - DemoCurrentTime (float seconds)
//       - DemoTotalTime
//       - DemoFrameNum
//       - bIsPlaying / bIsRecording
//   * Per-chara MoveVM state:
//       - nCurrentMoveId (int32) @ chara+0x324 (FLuxBattleChara.nCurrentMoveId)
//       - flSelfPos_X/Y/Z @ chara+0xA0/+0xA4/+0xA8 (world position)
//       - flMoveVelocity_X/Y/Z @ chara+0x140/+0x144/+0x148
//       - bVMPaused / bInputFreezeGate flags @ chara+0x16E6/+0x16E7
//       - pCurrentActiveAttackCell @ chara+0x44048 (non-null = mid-attack)
//   * Per-tick deltas: last-seen vs current MoveID/position so we can
//     tell if the chara is ticking forward (changing) or frozen.
//
// Lifecycle
// ---------
//   * No allocations or hooks - pure read-only via SafeReadXxx + UE4
//     reflection through Horse::Obj.
//   * Called from ReplayScrub::dump_engine_detailed_state(label) which
//     fires:
//       - On every PRE_SEEK / POST_SEEK dump (alongside existing dump)
//       - Every tick for `m_post_seek_dump_countdown` ticks after a seek
//       - On demand via the "Force diagnostic dump" UI button
//
// Threading
// ---------
// Game thread only (cockpit pre-tick).  UE4 reflection (Obj::getValueOr)
// is not thread-safe for cross-thread use.
// ============================================================================

#pragma once

#include "HorseLib.hpp"
#include "SafeMemoryRead.hpp"
#include "NativeBinding.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace Horse
{
    namespace ReplayScrubDiag
    {
        // FLuxBattleChara field offsets used for MoveVM state dump.
        // Verified 2026-05-13 against Ghidra FLuxBattleChara struct.
        //
        // Position: flSelfPos is the world position (what we want for
        // diagnostics).  Previous revision used flStepPos at 0xC0 which
        // is the 8WAYRUN step-target position, not the live world pos.
        //
        // Current move ID: nCurrentMoveId is a 4-byte int at 0x324
        // (NOT a u16 at 0x24E, which was incorrect in the prior rev).
        //
        // bVMPaused: at 0x16E6 per the struct (NOT 0x16E2, which is
        // actually bAirborneFlag).
        constexpr uintptr_t kChara_flSelfPos_X_Off        = 0xA0;
        constexpr uintptr_t kChara_flSelfPos_Y_Off        = 0xA4;
        constexpr uintptr_t kChara_flSelfPos_Z_Off        = 0xA8;
        constexpr uintptr_t kChara_flMoveVelocity_X_Off   = 0x140;
        constexpr uintptr_t kChara_flMoveVelocity_Y_Off   = 0x144;
        constexpr uintptr_t kChara_flMoveVelocity_Z_Off   = 0x148;
        constexpr uintptr_t kChara_nCurrentMoveId_Off     = 0x324;
        constexpr uintptr_t kChara_flCurrentClipFrame_Off  = 0x2B47C;
        // wVfxStateCheckA at +0x252 - 2-byte VFX-side state mirror.
        // Useful as a "is the engine ticking?" canary even though it's
        // not the move state per se.
        constexpr uintptr_t kChara_wVfxStateCheckA_Off    = 0x252;
        constexpr uintptr_t kChara_flBodyFacing_Off       = 0x94;
        constexpr uintptr_t kChara_bVMPaused_Off          = 0x16E6;
        constexpr uintptr_t kChara_bInputFreezeGate_Off   = 0x16E7;
        constexpr uintptr_t kChara_bInHitstunFlag_Off     = 0x16DB;
        constexpr uintptr_t kChara_bInBlockstunFlag_Off   = 0x16DC;
        constexpr uintptr_t kChara_pActiveAttackCell_Off  = 0x44048;
        // Not life/HP. Ghidra shows +0x1364 is a VM decay/counter field;
        // keep it only as diagnostic drift telemetry.
        constexpr uintptr_t kChara_flVmDecayCounter_Off   = 0x1364;
        // Native vital candidates from LuxBattleChara_AccumulateDamageTaken
        // and LuxBattleChara_UpdateLethalHitGauge. These are diagnostic until
        // live traces prove exact HUD/KO semantics.
        constexpr uintptr_t kChara_flVitalScale_Off       = 0x43E00;
        constexpr uintptr_t kChara_flVitalCandidate_Off   = 0x43E08;
        constexpr uintptr_t kChara_flVitalKoGate_Off      = 0x43E10;
        constexpr uintptr_t kChara_flVitalDisplayed_Off   = 0x43E14;
        constexpr uintptr_t kChara_dwVitalCategoryBits_Off = 0x43E18;
        constexpr uintptr_t kChara_wVitalState_Off        = 0x3D0;

        // RVAs for the chara slot pointers (mirrored from ReplayScrub).
        constexpr uintptr_t kRVA_CharaSlotP1 = 0x470DE90;
        constexpr uintptr_t kRVA_CharaSlotP2 = 0x470DE98;

        // RVA for g_LuxBattle_LatestEngineInput_PerPlayer @ 0x144855700.
        // Two adjacent qwords (one per player) - PerFrameTick mirrors
        // args[0]/args[1] into this global each frame.  The ONLY proven
        // per-frame input source during match-replay viewing - PRA bits
        // stay 0, ReplayPlayer.CurrentTime stays 0, but THIS global is
        // the live input each tick.  Reading it tells us whether the
        // upstream PerFrameTick dispatcher is feeding inputs at all.
        constexpr uintptr_t kRVA_LatestEngineInput = 0x4855700;

        // BM-side field offsets used for direct (non-reflection) diag reads.
        // These are verified against the Ghidra struct layouts.
        constexpr uintptr_t kBM_pBattleFrameInputLog_Off  = 0x478;
        constexpr uintptr_t kBM_bMoveStateByte_Off        = 0x1463;
        constexpr uintptr_t kBM_bStatusByte_Off           = 0x1480;
        constexpr uintptr_t kBM_nReplayLastFrameID_Off    = 0x1488;
        constexpr uintptr_t kBM_nReplayLastApplied_Off    = 0x148C;
        constexpr uintptr_t kBM_nFrameAdvanceCounter_Off  = 0x1490;

        // ALuxBattleFrameInputLog field offsets (verified against the
        // 0x44D0-byte struct in Ghidra).  These are the fields read or
        // written each frame by SimulationLoop @ 0x1403FE520 and its
        // helpers; we read them directly bypassing UE4 reflection
        // because most are NOT registered as UProperties.
        constexpr uintptr_t kIL_dwForwardReverseBitfield_Off = 0x394;
        constexpr uintptr_t kIL_bEnable_Off                  = 0x398;
        // +0x39C is an active-slot mask in the offline cache/current-input
        // paths used by replay seek diagnostics. Older notes called this a
        // playback cursor; keep that out of new authority decisions.
        constexpr uintptr_t kIL_dwPlaybackCursor_Off         = 0x39C;
        constexpr uintptr_t kIL_nLastFrameID_Off             = 0x3A0;
        constexpr uintptr_t kIL_nMasterClock_Off             = 0x3A4;
        constexpr uintptr_t kIL_pRecordedFrameBuffer_Off     = 0x3A8;
        constexpr uintptr_t kIL_nTotalRecordedFrames_Off     = 0x3B0;
        constexpr uintptr_t kIL_dwOnlineActive_Off           = 0x4400;
        constexpr uintptr_t kIL_bDoubleTickGuard_Off         = 0x4404;
        constexpr uintptr_t kIL_nDrainCursor_Off             = 0x4410;
        constexpr uintptr_t kIL_nMinStoreFrameIndex_Off      = 0x4414;

        // Chara-level replay state offsets (NOT captured by HgCpuDirect,
        // which only covers chara+0x90..+0x35A0).  These fields are at
        // +0x4400 and beyond - past the bone matrix region.
        constexpr uintptr_t kChara_dwReplayLookupKey_Off     = 0x43F4;
        constexpr uintptr_t kChara_dwReplayEnableFlag_Off    = 0x4400;
        constexpr uintptr_t kChara_dwReplayFrameOffset_Off   = 0x440C;
        constexpr uintptr_t kChara_dwReplayFrameTotal_Off    = 0x4410;
        constexpr uintptr_t kChara_dwReplayFrameTarget_Off   = 0x4414;
        constexpr uintptr_t kChara_dwReplayConsumerCursor_Off = 0x4420;
        constexpr uintptr_t kChara_bCharaMode_Off            = 0x4424;

        // Aggregate of chara MoveVM state we care about for diagnostics.
        // Used both for one-shot dumps and per-tick delta detection.
        struct CharaMoveVmSnap
        {
            uintptr_t chara_ptr   {0};
            uint32_t  current_move_id {0xFFFFFFFFu};
            uint32_t  current_move_frame {0};
            float     current_clip_frame {0.0f};
            uint16_t  vfx_state_check_a {0xFFFF};
            float     pos_x {0.0f}, pos_y {0.0f}, pos_z {0.0f};
            float     vel_x {0.0f}, vel_y {0.0f}, vel_z {0.0f};
            float     facing {0.0f};
            float     vm_decay_counter {0.0f};
            float     vital_scale {0.0f};
            float     vital_candidate {0.0f};
            float     vital_ko_gate {0.0f};
            float     vital_displayed {0.0f};
            uint32_t  vital_category_bits {0};
            int16_t   vital_state {0};
            uint8_t   vm_paused {0xFF};
            uint8_t   input_freeze_gate {0xFF};
            uint8_t   in_hitstun {0xFF};
            uint8_t   in_blockstun {0xFF};
            uintptr_t active_attack_cell {0};
            bool      readable {false};

            // True if any of the "interesting" fields changed.  Used by
            // per-tick logging to detect "engine is ticking" vs frozen.
            bool changed_from(const CharaMoveVmSnap& prev) const noexcept
            {
                return chara_ptr          != prev.chara_ptr
                    || current_move_id    != prev.current_move_id
                    || current_move_frame != prev.current_move_frame
                    || current_clip_frame != prev.current_clip_frame
                    || vfx_state_check_a  != prev.vfx_state_check_a
                    || pos_x              != prev.pos_x
                    || pos_y              != prev.pos_y
                    || pos_z              != prev.pos_z
                    || vel_x              != prev.vel_x
                    || vel_y              != prev.vel_y
                    || vel_z              != prev.vel_z
                    || vital_candidate    != prev.vital_candidate
                    || vital_displayed    != prev.vital_displayed
                    || vital_state        != prev.vital_state
                    || active_attack_cell != prev.active_attack_cell;
            }
        };

        // Read MoveVM state from the chara at one of g_LuxBattle_CharaSlotP*.
        // `player_idx` is 0 or 1.  Returns a zero-initialised snap with
        // readable=false if the slot is null or the chara was torn down.
        inline CharaMoveVmSnap read_chara_movevm(int player_idx) noexcept
        {
            CharaMoveVmSnap s{};
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return s;
            const uintptr_t slot_rva =
                (player_idx == 0) ? kRVA_CharaSlotP1 : kRVA_CharaSlotP2;
            void* chara_raw = nullptr;
            if (!SafeReadPtr(reinterpret_cast<const void*>(base + slot_rva),
                             &chara_raw) || !chara_raw)
                return s;
            s.chara_ptr = reinterpret_cast<uintptr_t>(chara_raw);
            uint8_t* c = reinterpret_cast<uint8_t*>(chara_raw);

            SafeReadUInt32(c + kChara_nCurrentMoveId_Off,    &s.current_move_id);
            float clip_frame = 0.0f;
            if (SafeReadFloat(c + kChara_flCurrentClipFrame_Off, &clip_frame)
                && clip_frame >= 0.0f)
            {
                s.current_clip_frame = clip_frame;
                s.current_move_frame = static_cast<uint32_t>(clip_frame);
            }
            SafeReadUInt16(c + kChara_wVfxStateCheckA_Off,   &s.vfx_state_check_a);
            SafeReadFloat (c + kChara_flSelfPos_X_Off,       &s.pos_x);
            SafeReadFloat (c + kChara_flSelfPos_Y_Off,       &s.pos_y);
            SafeReadFloat (c + kChara_flSelfPos_Z_Off,       &s.pos_z);
            SafeReadFloat (c + kChara_flMoveVelocity_X_Off,  &s.vel_x);
            SafeReadFloat (c + kChara_flMoveVelocity_Y_Off,  &s.vel_y);
            SafeReadFloat (c + kChara_flMoveVelocity_Z_Off,  &s.vel_z);
            SafeReadFloat (c + kChara_flBodyFacing_Off,      &s.facing);
            SafeReadFloat (c + kChara_flVmDecayCounter_Off,  &s.vm_decay_counter);
            SafeReadFloat (c + kChara_flVitalScale_Off,      &s.vital_scale);
            SafeReadFloat (c + kChara_flVitalCandidate_Off,  &s.vital_candidate);
            SafeReadFloat (c + kChara_flVitalKoGate_Off,     &s.vital_ko_gate);
            SafeReadFloat (c + kChara_flVitalDisplayed_Off,  &s.vital_displayed);
            SafeReadUInt32(c + kChara_dwVitalCategoryBits_Off,
                           &s.vital_category_bits);
            SafeReadInt16 (c + kChara_wVitalState_Off,       &s.vital_state);
            SafeReadUInt8 (c + kChara_bVMPaused_Off,         &s.vm_paused);
            SafeReadUInt8 (c + kChara_bInputFreezeGate_Off,  &s.input_freeze_gate);
            SafeReadUInt8 (c + kChara_bInHitstunFlag_Off,    &s.in_hitstun);
            SafeReadUInt8 (c + kChara_bInBlockstunFlag_Off,  &s.in_blockstun);

            void* cell = nullptr;
            if (SafeReadPtr(c + kChara_pActiveAttackCell_Off, &cell))
                s.active_attack_cell = reinterpret_cast<uintptr_t>(cell);

            s.readable = true;
            return s;
        }

        // Log a CharaMoveVmSnap to UE4SS.log.  `label` distinguishes
        // dump contexts (BASELINE/PRE_SEEK/POST_SEEK/POST_SEEK_T+N).
        inline void log_chara_movevm(const char* label, int player_idx,
                                     const CharaMoveVmSnap& s) noexcept
        {
            if (!s.readable)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.diag] {} P{} chara=null (slot torn down)\n"),
                    RC::to_generic_string(label), player_idx + 1);
                return;
            }
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] {} P{} chara=0x{:X} "
                    "moveID=0x{:X} vfxState=0x{:X} "
                    "moveFrame={} "
                    "pos=({:.2f},{:.2f},{:.2f}) vel=({:.2f},{:.2f},{:.2f}) "
                    "face={:.2f} vmDecay={:.1f} vital={:.1f}/{:.1f} "
                    "koGate={:.1f} vitalScale={:.3f} vitalBits=0x{:X} "
                    "vitalState={} "
                    "vmPaused={} inputFreeze={} hitstun={} blockstun={} "
                    "atkCell=0x{:X}\n"),
                RC::to_generic_string(label), player_idx + 1, s.chara_ptr,
                static_cast<unsigned>(s.current_move_id),
                static_cast<unsigned>(s.vfx_state_check_a),
                static_cast<unsigned>(s.current_move_frame),
                s.pos_x, s.pos_y, s.pos_z,
                s.vel_x, s.vel_y, s.vel_z,
                s.facing, s.vm_decay_counter,
                s.vital_candidate, s.vital_displayed,
                s.vital_ko_gate, s.vital_scale,
                s.vital_category_bits,
                static_cast<int>(s.vital_state),
                static_cast<int>(s.vm_paused),
                static_cast<int>(s.input_freeze_gate),
                static_cast<int>(s.in_hitstun),
                static_cast<int>(s.in_blockstun),
                s.active_attack_cell);
        }

        // Resolve UDemoNetDriver via UWorld->DemoNetDriver first, then
        // object-array fallbacks, and report its key state fields.
        // UE4.21 layout (canonical UDemoNetDriver):
        //   bIsPlaying:        BitField on UNetDriver - we read it as the
        //                      "NetDriverName == DemoNetDriver" identity test
        //   DemoCurrentTime:   float, often at +0x4F0..+0x510 region;
        //                      retrieved via property reflection to be
        //                      version-agnostic.
        //   DemoTotalTime:     float, sibling of DemoCurrentTime.
        //   DemoFrameNum:      int32, monotonic frame counter.
        //
        // All resolved via Horse::Obj reflection so we don't need
        // hard-coded offsets.  If reflection misses any field we log -1
        // / nan so the absence is visible.
        //
        // Returns true if UDemoNetDriver was found.
        struct DemoNetDriverSnap
        {
            uintptr_t driver_ptr     {0};
            float     demo_cur_time  {-1.0f};
            float     demo_total_time{-1.0f};
            int32_t   demo_frame_num {-1};
            bool      bIsPlaying     {false};
            bool      bIsRecording   {false};
            bool      bIsSavingCheckpoint {false};
            float     raw_demo_total_time {-1.0f}; // Ghidra: +0x414
            float     raw_demo_cur_time   {-1.0f}; // Ghidra: +0x418
            uint8_t   raw_busy_791        {0};
            uint8_t   raw_loading_794     {0};
            uintptr_t raw_task_data_7a8   {0};
            int32_t   raw_task_count_7b0  {0};
            int32_t   raw_task_max_7b4    {0};
            uintptr_t raw_current_task_7b8 {0};
            bool      readable       {false};
            bool      raw_fields_readable {false};
            bool      time_fields_readable {false};
            bool      task_fields_readable {false};
        };

        enum class DemoDriverResolveSource : int
        {
            None = 0,
            Cached,
            ReplayPlayerGetWorld,
            GWorld,
            GEngineGameViewportWorld,
            GEngineWorldContext,
            UWorldDemoNetDriver,
            UWorldNetDriver,
            UWorldLevelCollection,
            FLevelCollectionNetDriver,
            WorldContextActiveNetDrivers,
            ObjectArrayProbe,
            ReflectedProperty,
        };

        struct DemoTimeSourceSnap
        {
            uintptr_t source_ptr {0};
            DemoDriverResolveSource source {DemoDriverResolveSource::None};
            float raw_demo_total_time {-1.0f};
            float raw_demo_cur_time {-1.0f};
            bool readable {false};
            bool time_fields_readable {false};
            bool time_sane {false};
        };

        enum class DemoDriverResolveFailure : int
        {
            None = 0,
            NullWorld,
            WorldReadFailed,
            NullCandidate,
            CandidateRawReadFailed,
            CandidateTaskFieldsInvalid,
            CandidateClassInvalid,
            GWorldUnavailable,
            ReplayPlayerUnavailable,
            PropertyUnavailable,
            SlowProbeUnavailable,
        };

        struct DemoDriverResolveAttempt
        {
            DemoDriverResolveSource source {DemoDriverResolveSource::None};
            uintptr_t world {0};
            uintptr_t container {0};
            uintptr_t candidate_driver {0};
            float raw_demo_cur_time {-1.0f};
            float raw_demo_total_time {-1.0f};
            uintptr_t raw_task_data_7a8 {0};
            int32_t raw_task_count_7b0 {0};
            int32_t raw_task_max_7b4 {0};
            uintptr_t raw_current_task_7b8 {0};
            bool world_readable {false};
            bool candidate_readable {false};
            bool class_valid {false};
            bool task_fields_readable {false};
            bool time_fields_readable {false};
            int32_t failure_reason {
                static_cast<int32_t>(DemoDriverResolveFailure::None)};
        };

        struct DemoDriverResolveReport
        {
            static constexpr int32_t kMaxAttempts = 64;
            DemoNetDriverSnap snap {};
            DemoDriverResolveSource source {DemoDriverResolveSource::None};
            DemoDriverResolveAttempt attempts[kMaxAttempts] {};
            int32_t attempt_count {0};
        };

        struct DemoTimeSourceReport
        {
            static constexpr int32_t kMaxAttempts =
                DemoDriverResolveReport::kMaxAttempts;
            DemoTimeSourceSnap snap {};
            DemoDriverResolveSource source {DemoDriverResolveSource::None};
            DemoDriverResolveAttempt attempts[kMaxAttempts] {};
            int32_t attempt_count {0};
        };

        // Ghidra-verified world / engine traversal constants.
        static constexpr uintptr_t kRVA_GWorld = 0x43B4DB8;
        static constexpr uintptr_t kRVA_GEngine = 0x43B3068;
        static constexpr uintptr_t kUEngine_WorldContexts_Data_Off = 0xBE8;
        static constexpr uintptr_t kUEngine_WorldContexts_Count_Off = 0xBF0;
        static constexpr uintptr_t kUEngine_GameViewport_Off = 0x618;
        static constexpr uintptr_t kFWorldContext_CurrentWorld_Off = 0x298;
        static constexpr uintptr_t kUWorld_NetDriver_Off = 0x30;
        static constexpr uintptr_t kUWorld_DemoNetDriver_Off = 0xB8;
        static constexpr uintptr_t kUWorld_LevelCollections_Off = 0x120;
        static constexpr size_t    kFLevelCollection_Stride = 0x80;
        static constexpr uintptr_t kFLevelCollection_NetDriver_Off = 0x10;
        static constexpr uintptr_t kFLevelCollection_DemoNetDriver_Off = 0x18;
        static constexpr int32_t   kMaxWorldContexts = 16;

        inline GlobalPtr& replay_player_ptr() noexcept;
        inline GlobalPtr& demo_net_driver_ptr() noexcept;
        inline bool read_gworld_ptr(void** world_raw) noexcept;
        inline RC::Unreal::UObject* find_demo_net_driver_probed() noexcept;

        inline const char* demo_driver_source_name(
            DemoDriverResolveSource source) noexcept
        {
            switch (source)
            {
            case DemoDriverResolveSource::Cached: return "Cached";
            case DemoDriverResolveSource::ReplayPlayerGetWorld:
                return "ReplayPlayer->GetWorld";
            case DemoDriverResolveSource::GWorld: return "GWorld";
            case DemoDriverResolveSource::GEngineGameViewportWorld:
                return "GEngine.GameViewport->GetWorld";
            case DemoDriverResolveSource::GEngineWorldContext:
                return "GEngine.WorldContexts";
            case DemoDriverResolveSource::UWorldDemoNetDriver:
                return "UWorld.DemoNetDriver";
            case DemoDriverResolveSource::UWorldNetDriver:
                return "UWorld.NetDriver";
            case DemoDriverResolveSource::UWorldLevelCollection:
                return "FLevelCollection.DemoNetDriver";
            case DemoDriverResolveSource::FLevelCollectionNetDriver:
                return "FLevelCollection.NetDriver";
            case DemoDriverResolveSource::WorldContextActiveNetDrivers:
                return "WorldContext.ActiveNetDrivers";
            case DemoDriverResolveSource::ObjectArrayProbe:
                return "ObjectArrayProbe";
            case DemoDriverResolveSource::ReflectedProperty:
                return "ReflectedProperty";
            case DemoDriverResolveSource::None:
            default:
                return "None";
            }
        }

        inline void add_demo_driver_attempt(
            DemoDriverResolveReport* report,
            const DemoDriverResolveAttempt& attempt) noexcept
        {
            if (!report) return;
            if (report->attempt_count < DemoDriverResolveReport::kMaxAttempts)
            {
                report->attempts[report->attempt_count++] = attempt;
            }
        }

        inline void add_demo_time_source_attempt(
            DemoTimeSourceReport* report,
            const DemoDriverResolveAttempt& attempt) noexcept
        {
            if (!report) return;
            if (report->attempt_count < DemoTimeSourceReport::kMaxAttempts)
            {
                report->attempts[report->attempt_count++] = attempt;
            }
        }

        inline DemoDriverResolveSource last_success_source(
            const DemoDriverResolveReport& report,
            DemoDriverResolveSource fallback) noexcept
        {
            for (int32_t i = report.attempt_count - 1; i >= 0; --i)
            {
                if (report.attempts[i].candidate_readable)
                    return report.attempts[i].source;
            }
            return fallback;
        }

        inline DemoDriverResolveSource last_time_source_success_source(
            const DemoTimeSourceReport& report,
            DemoDriverResolveSource fallback) noexcept
        {
            for (int32_t i = report.attempt_count - 1; i >= 0; --i)
            {
                if (report.attempts[i].candidate_readable)
                    return report.attempts[i].source;
            }
            return fallback;
        }

        inline std::atomic<uintptr_t>& cached_demo_world_ptr() noexcept
        {
            static std::atomic<uintptr_t> s_cached{0};
            return s_cached;
        }

        inline std::atomic<uintptr_t>& cached_demo_time_source_ptr() noexcept
        {
            static std::atomic<uintptr_t> s_cached{0};
            return s_cached;
        }

        inline std::atomic<uintptr_t>& cached_demo_time_source_world_ptr()
            noexcept
        {
            static std::atomic<uintptr_t> s_cached{0};
            return s_cached;
        }

        inline std::atomic<int32_t>& cached_world_demo_driver_off() noexcept
        {
            static std::atomic<int32_t> s_off{-2};
            return s_off;
        }

        inline std::atomic<int32_t>& cached_world_level_collections_off() noexcept
        {
            static std::atomic<int32_t> s_off{-2};
            return s_off;
        }

        inline std::atomic<int32_t>& cached_demo_cur_time_off() noexcept
        {
            static std::atomic<int32_t> s_off{-2};
            return s_off;
        }

        inline std::atomic<int32_t>& cached_demo_total_time_off() noexcept
        {
            static std::atomic<int32_t> s_off{-2};
            return s_off;
        }

        template <class T>
        inline int32_t reflected_offset_of(RC::Unreal::UObject* obj,
                                           const wchar_t* property) noexcept
        {
            if (!obj) return -1;
            __try
            {
                T* ptr = obj->GetValuePtrByPropertyNameInChain<T>(property);
                if (!ptr) return -1;
                const uintptr_t base = reinterpret_cast<uintptr_t>(obj);
                const uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
                if (p < base || p - base > 0x100000) return -1;
                return static_cast<int32_t>(p - base);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
        }

        inline int32_t get_world_demo_driver_offset(void* world_raw,
                                                    bool allow_reflection)
            noexcept
        {
            int32_t off = cached_world_demo_driver_off().load(
                std::memory_order_acquire);
            if (off != -2) return off;
            if (allow_reflection && world_raw)
            {
                off = reflected_offset_of<RC::Unreal::UObject*>(
                    reinterpret_cast<RC::Unreal::UObject*>(world_raw),
                    L"DemoNetDriver");
                if (off >= 0)
                {
                    cached_world_demo_driver_off().store(
                        off, std::memory_order_release);
                    return off;
                }
            }
            return static_cast<int32_t>(kUWorld_DemoNetDriver_Off);
        }

        inline int32_t get_world_level_collections_offset(
            void* world_raw,
            bool allow_reflection) noexcept
        {
            int32_t off = cached_world_level_collections_off().load(
                std::memory_order_acquire);
            if (off != -2) return off;
            if (allow_reflection && world_raw)
            {
                off = reflected_offset_of<TArrHdr>(
                    reinterpret_cast<RC::Unreal::UObject*>(world_raw),
                    L"LevelCollections");
                if (off >= 0)
                {
                    cached_world_level_collections_off().store(
                        off, std::memory_order_release);
                    return off;
                }
            }
            return static_cast<int32_t>(kUWorld_LevelCollections_Off);
        }

        inline bool demo_snap_task_state_is_sane(
            const DemoNetDriverSnap& s) noexcept
        {
            return s.raw_task_count_7b0 >= 0
                && s.raw_task_count_7b0 < 128
                && s.raw_task_max_7b4 >= 0
                && (s.raw_task_max_7b4 >= s.raw_task_count_7b0
                    || (s.raw_task_count_7b0 == 0
                        && s.raw_task_data_7a8 == 0))
                && s.raw_task_max_7b4 < 1024;
        }

        inline bool demo_snap_time_is_sane(
            const DemoNetDriverSnap& s) noexcept
        {
            return s.raw_demo_total_time == s.raw_demo_total_time
                && s.raw_demo_cur_time == s.raw_demo_cur_time
                && s.raw_demo_total_time > 0.001f
                && s.raw_demo_total_time < 86400.0f
                && s.raw_demo_cur_time >= 0.0f
                && s.raw_demo_cur_time <= s.raw_demo_total_time + 5.0f;
        }

        inline void copy_demo_snap_raw_to_attempt(
            DemoDriverResolveAttempt& attempt,
            const DemoNetDriverSnap& snap) noexcept
        {
            attempt.raw_demo_cur_time = snap.raw_demo_cur_time;
            attempt.raw_demo_total_time = snap.raw_demo_total_time;
            attempt.raw_task_data_7a8 = snap.raw_task_data_7a8;
            attempt.raw_task_count_7b0 = snap.raw_task_count_7b0;
            attempt.raw_task_max_7b4 = snap.raw_task_max_7b4;
            attempt.raw_current_task_7b8 = snap.raw_current_task_7b8;
        }

        inline bool read_demo_time_source_raw(
            void* candidate_raw,
            DemoTimeSourceSnap& s) noexcept
        {
            if (!candidate_raw) return false;
            const uintptr_t a = reinterpret_cast<uintptr_t>(candidate_raw);
            DemoTimeSourceSnap t{};
            t.source_ptr = a;

            int32_t total_off = cached_demo_total_time_off().load(
                std::memory_order_acquire);
            int32_t cur_off = cached_demo_cur_time_off().load(
                std::memory_order_acquire);
            if (total_off < 0) total_off = 0x414;
            if (cur_off < 0) cur_off = 0x418;

            t.time_fields_readable =
                SafeReadFloat(reinterpret_cast<const void*>(a + total_off),
                              &t.raw_demo_total_time)
                && SafeReadFloat(reinterpret_cast<const void*>(a + cur_off),
                                 &t.raw_demo_cur_time);
            t.time_sane = t.time_fields_readable
                && t.raw_demo_total_time == t.raw_demo_total_time
                && t.raw_demo_cur_time == t.raw_demo_cur_time
                && t.raw_demo_total_time > 0.001f
                && t.raw_demo_total_time < 86400.0f
                && t.raw_demo_cur_time >= 0.0f
                && t.raw_demo_cur_time <= t.raw_demo_total_time + 5.0f;
            t.readable = t.time_sane;
            s = t;
            return t.readable;
        }

        inline bool read_demo_driver_raw(
            void* driver_raw,
            DemoNetDriverSnap& s) noexcept
        {
            if (!driver_raw) return false;
            const uintptr_t a = reinterpret_cast<uintptr_t>(driver_raw);
            DemoNetDriverSnap t{};
            t.driver_ptr = a;
            bool task_ok = true;

            int32_t total_off = cached_demo_total_time_off().load(
                std::memory_order_acquire);
            int32_t cur_off = cached_demo_cur_time_off().load(
                std::memory_order_acquire);
            if (total_off < 0) total_off = 0x414;
            if (cur_off < 0) cur_off = 0x418;
            const bool time_ok =
                SafeReadFloat(reinterpret_cast<const void*>(a + total_off),
                              &t.raw_demo_total_time)
                && SafeReadFloat(reinterpret_cast<const void*>(a + cur_off),
                                 &t.raw_demo_cur_time);

            task_ok = task_ok
                && SafeReadUInt8(reinterpret_cast<const void*>(a + 0x791),
                                 &t.raw_busy_791);
            task_ok = task_ok
                && SafeReadUInt8(reinterpret_cast<const void*>(a + 0x794),
                                 &t.raw_loading_794);
            void* task_data = nullptr;
            if (SafeReadPtr(reinterpret_cast<const void*>(a + 0x7A8),
                            &task_data))
            {
                t.raw_task_data_7a8 =
                    reinterpret_cast<uintptr_t>(task_data);
            }
            else
            {
                task_ok = false;
            }
            task_ok = task_ok
                && SafeReadInt32(reinterpret_cast<const void*>(a + 0x7B0),
                                 &t.raw_task_count_7b0);
            task_ok = task_ok
                && SafeReadInt32(reinterpret_cast<const void*>(a + 0x7B4),
                                 &t.raw_task_max_7b4);
            void* current_task = nullptr;
            if (SafeReadPtr(reinterpret_cast<const void*>(a + 0x7B8),
                            &current_task))
            {
                t.raw_current_task_7b8 =
                    reinterpret_cast<uintptr_t>(current_task);
            }
            else
            {
                task_ok = false;
            }
            // Do not reject the UWorld-owned driver just because its
            // timing fields are temporarily odd.  Ghidra shows
            // UDemoNetDriver::GotoTimeInSeconds only needs the driver
            // pointer and the task queue/busy fields; the time fields are
            // diagnostic/capture data.  Treat them as optional so a stale
            // or uninitialised DemoTotalTime cannot turn every seek into a
            // visual-only fallback.
            t.time_fields_readable = time_ok;
            t.task_fields_readable = task_ok && demo_snap_task_state_is_sane(t);
            t.raw_fields_readable = time_ok || task_ok;
            if (!t.task_fields_readable)
            {
                s = t;
                return false;
            }
            t.readable = true;
            s = t;
            return true;
        }

        inline bool probe_demo_driver_candidate(
            DemoDriverResolveReport* report,
            DemoDriverResolveSource source,
            uintptr_t world,
            uintptr_t container,
            void* driver_raw,
            DemoNetDriverSnap& s) noexcept
        {
            DemoDriverResolveAttempt attempt{};
            attempt.source = source;
            attempt.world = world;
            attempt.container = container;
            attempt.world_readable = world != 0;
            attempt.candidate_driver =
                reinterpret_cast<uintptr_t>(driver_raw);

            if (!driver_raw)
            {
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::NullCandidate);
                add_demo_driver_attempt(report, attempt);
                return false;
            }

            DemoNetDriverSnap probe{};
            if (read_demo_driver_raw(driver_raw, probe))
            {
                attempt.candidate_readable = true;
                attempt.task_fields_readable = probe.task_fields_readable;
                attempt.time_fields_readable = probe.time_fields_readable;
                copy_demo_snap_raw_to_attempt(attempt, probe);
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::None);
                add_demo_driver_attempt(report, attempt);
                s = probe;
                return true;
            }

            attempt.task_fields_readable = probe.task_fields_readable;
            attempt.time_fields_readable = probe.time_fields_readable;
            copy_demo_snap_raw_to_attempt(attempt, probe);
            attempt.failure_reason = static_cast<int32_t>(
                probe.raw_fields_readable
                    ? DemoDriverResolveFailure::CandidateTaskFieldsInvalid
                    : DemoDriverResolveFailure::CandidateRawReadFailed);
            add_demo_driver_attempt(report, attempt);
            return false;
        }

        inline void add_pointer_read_failure_attempt(
            DemoDriverResolveReport* report,
            DemoDriverResolveSource source,
            uintptr_t world,
            uintptr_t container) noexcept
        {
            DemoDriverResolveAttempt attempt{};
            attempt.source = source;
            attempt.world = world;
            attempt.container = container;
            attempt.world_readable = world != 0;
            attempt.failure_reason = static_cast<int32_t>(
                DemoDriverResolveFailure::WorldReadFailed);
            add_demo_driver_attempt(report, attempt);
        }

        inline bool read_level_collection_demo_driver(
            void* world_raw,
            DemoNetDriverSnap& s,
            DemoDriverResolveReport* report = nullptr,
            bool allow_reflection = false) noexcept
        {
            if (!world_raw) return false;
            uint8_t* world = static_cast<uint8_t*>(world_raw);
            void* collections = nullptr;
            int32_t num = 0;
            const int32_t collections_off =
                get_world_level_collections_offset(world_raw,
                                                   allow_reflection);
            DemoDriverResolveAttempt header_attempt{};
            header_attempt.source =
                DemoDriverResolveSource::UWorldLevelCollection;
            header_attempt.world = reinterpret_cast<uintptr_t>(world_raw);
            header_attempt.world_readable = true;
            if (!SafeReadPtr(world + collections_off, &collections)
                || !collections)
            {
                header_attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_driver_attempt(report, header_attempt);
                return false;
            }
            header_attempt.container =
                reinterpret_cast<uintptr_t>(collections);
            if (!SafeReadInt32(world + collections_off + 0x8, &num))
            {
                header_attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_driver_attempt(report, header_attempt);
                return false;
            }
            add_demo_driver_attempt(report, header_attempt);
            if (num <= 0 || num > 16)
                return false;

            auto* base = static_cast<uint8_t*>(collections);
            for (int32_t i = 0; i < num; ++i)
            {
                const size_t collection_off = static_cast<size_t>(i)
                    * kFLevelCollection_Stride;
                const uintptr_t collection =
                    reinterpret_cast<uintptr_t>(collections)
                    + static_cast<uintptr_t>(collection_off);
                void* driver_raw = nullptr;

                const size_t demo_off = collection_off
                    + kFLevelCollection_DemoNetDriver_Off;
                if (SafeReadPtr(base + demo_off, &driver_raw))
                {
                    if (probe_demo_driver_candidate(
                            report,
                            DemoDriverResolveSource::UWorldLevelCollection,
                            reinterpret_cast<uintptr_t>(world_raw),
                            collection, driver_raw, s))
                        return true;
                }
                else
                {
                    add_pointer_read_failure_attempt(
                        report,
                        DemoDriverResolveSource::UWorldLevelCollection,
                        reinterpret_cast<uintptr_t>(world_raw),
                        collection);
                }

                driver_raw = nullptr;
                const size_t net_off = collection_off
                    + kFLevelCollection_NetDriver_Off;
                if (SafeReadPtr(base + net_off, &driver_raw))
                {
                    if (probe_demo_driver_candidate(
                            report,
                            DemoDriverResolveSource::FLevelCollectionNetDriver,
                            reinterpret_cast<uintptr_t>(world_raw),
                            collection, driver_raw, s))
                        return true;
                }
                else
                {
                    add_pointer_read_failure_attempt(
                        report,
                        DemoDriverResolveSource::FLevelCollectionNetDriver,
                        reinterpret_cast<uintptr_t>(world_raw),
                        collection);
                }
            }
            return false;
        }

        inline bool read_world_demo_driver(
            void* world_raw,
            DemoNetDriverSnap& s,
            DemoDriverResolveReport* report = nullptr,
            bool allow_reflection = false) noexcept
        {
            if (!world_raw) return false;
            void* driver_raw = nullptr;
            const uintptr_t world = reinterpret_cast<uintptr_t>(world_raw);
            const int32_t demo_off =
                get_world_demo_driver_offset(world_raw, allow_reflection);
            if (SafeReadPtr(static_cast<uint8_t*>(world_raw) + demo_off,
                            &driver_raw))
            {
                if (probe_demo_driver_candidate(
                        report, DemoDriverResolveSource::UWorldDemoNetDriver,
                        world, 0, driver_raw, s))
                    return true;
            }
            else
            {
                add_pointer_read_failure_attempt(
                    report, DemoDriverResolveSource::UWorldDemoNetDriver,
                    world, 0);
            }

            driver_raw = nullptr;
            if (SafeReadPtr(static_cast<uint8_t*>(world_raw)
                                + kUWorld_NetDriver_Off,
                            &driver_raw))
            {
                if (probe_demo_driver_candidate(
                        report, DemoDriverResolveSource::UWorldNetDriver,
                        world, 0, driver_raw, s))
                    return true;
            }
            else
            {
                add_pointer_read_failure_attempt(
                    report, DemoDriverResolveSource::UWorldNetDriver,
                    world, 0);
            }

            return read_level_collection_demo_driver(
                world_raw, s, report, allow_reflection);
        }

        inline bool probe_demo_time_source_candidate(
            DemoTimeSourceReport* report,
            DemoDriverResolveSource source,
            uintptr_t world,
            uintptr_t container,
            void* candidate_raw,
            DemoTimeSourceSnap& snap) noexcept
        {
            DemoDriverResolveAttempt attempt{};
            attempt.source = source;
            attempt.world = world;
            attempt.container = container;
            attempt.world_readable = world != 0;
            attempt.candidate_driver =
                reinterpret_cast<uintptr_t>(candidate_raw);

            if (!candidate_raw)
            {
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::NullCandidate);
                add_demo_time_source_attempt(report, attempt);
                return false;
            }

            DemoNetDriverSnap raw_probe{};
            if (report)
            {
                (void)read_demo_driver_raw(candidate_raw, raw_probe);
                attempt.task_fields_readable =
                    raw_probe.task_fields_readable;
                attempt.time_fields_readable =
                    raw_probe.time_fields_readable;
                copy_demo_snap_raw_to_attempt(attempt, raw_probe);
            }

            DemoTimeSourceSnap time_probe{};
            if (read_demo_time_source_raw(candidate_raw, time_probe))
            {
                time_probe.source = source;
                attempt.candidate_readable = true;
                attempt.time_fields_readable = true;
                attempt.raw_demo_cur_time = time_probe.raw_demo_cur_time;
                attempt.raw_demo_total_time = time_probe.raw_demo_total_time;
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::None);
                add_demo_time_source_attempt(report, attempt);
                snap = time_probe;
                return true;
            }

            attempt.time_fields_readable =
                time_probe.time_fields_readable;
            attempt.raw_demo_cur_time = time_probe.raw_demo_cur_time;
            attempt.raw_demo_total_time = time_probe.raw_demo_total_time;
            attempt.failure_reason = static_cast<int32_t>(
                time_probe.time_fields_readable
                    ? DemoDriverResolveFailure::CandidateTaskFieldsInvalid
                    : DemoDriverResolveFailure::CandidateRawReadFailed);
            add_demo_time_source_attempt(report, attempt);
            return false;
        }

        inline void add_pointer_read_failure_time_source_attempt(
            DemoTimeSourceReport* report,
            DemoDriverResolveSource source,
            uintptr_t world,
            uintptr_t container) noexcept
        {
            DemoDriverResolveAttempt attempt{};
            attempt.source = source;
            attempt.world = world;
            attempt.container = container;
            attempt.world_readable = world != 0;
            attempt.failure_reason = static_cast<int32_t>(
                DemoDriverResolveFailure::WorldReadFailed);
            add_demo_time_source_attempt(report, attempt);
        }

        inline bool read_level_collection_demo_time_source(
            void* world_raw,
            DemoTimeSourceSnap& snap,
            DemoTimeSourceReport* report = nullptr,
            bool allow_reflection = false) noexcept
        {
            if (!world_raw) return false;
            uint8_t* world = static_cast<uint8_t*>(world_raw);
            void* collections = nullptr;
            int32_t num = 0;
            const int32_t collections_off =
                get_world_level_collections_offset(world_raw,
                                                   allow_reflection);
            DemoDriverResolveAttempt header_attempt{};
            header_attempt.source =
                DemoDriverResolveSource::UWorldLevelCollection;
            header_attempt.world = reinterpret_cast<uintptr_t>(world_raw);
            header_attempt.world_readable = true;
            if (!SafeReadPtr(world + collections_off, &collections)
                || !collections)
            {
                header_attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_time_source_attempt(report, header_attempt);
                return false;
            }
            header_attempt.container =
                reinterpret_cast<uintptr_t>(collections);
            if (!SafeReadInt32(world + collections_off + 0x8, &num))
            {
                header_attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_time_source_attempt(report, header_attempt);
                return false;
            }
            add_demo_time_source_attempt(report, header_attempt);
            if (num <= 0 || num > 16)
                return false;

            auto* base = static_cast<uint8_t*>(collections);
            for (int32_t i = 0; i < num; ++i)
            {
                const size_t collection_off = static_cast<size_t>(i)
                    * kFLevelCollection_Stride;
                const uintptr_t collection =
                    reinterpret_cast<uintptr_t>(collections)
                    + static_cast<uintptr_t>(collection_off);
                void* candidate_raw = nullptr;

                const size_t demo_off = collection_off
                    + kFLevelCollection_DemoNetDriver_Off;
                if (SafeReadPtr(base + demo_off, &candidate_raw))
                {
                    if (probe_demo_time_source_candidate(
                            report,
                            DemoDriverResolveSource::UWorldLevelCollection,
                            reinterpret_cast<uintptr_t>(world_raw),
                            collection, candidate_raw, snap))
                        return true;
                }
                else
                {
                    add_pointer_read_failure_time_source_attempt(
                        report,
                        DemoDriverResolveSource::UWorldLevelCollection,
                        reinterpret_cast<uintptr_t>(world_raw),
                        collection);
                }

                candidate_raw = nullptr;
                const size_t net_off = collection_off
                    + kFLevelCollection_NetDriver_Off;
                if (SafeReadPtr(base + net_off, &candidate_raw))
                {
                    if (probe_demo_time_source_candidate(
                            report,
                            DemoDriverResolveSource::FLevelCollectionNetDriver,
                            reinterpret_cast<uintptr_t>(world_raw),
                            collection, candidate_raw, snap))
                        return true;
                }
                else
                {
                    add_pointer_read_failure_time_source_attempt(
                        report,
                        DemoDriverResolveSource::FLevelCollectionNetDriver,
                        reinterpret_cast<uintptr_t>(world_raw),
                        collection);
                }
            }
            return false;
        }

        inline bool read_world_demo_time_source(
            void* world_raw,
            DemoTimeSourceSnap& snap,
            DemoTimeSourceReport* report = nullptr,
            bool allow_reflection = false) noexcept
        {
            if (!world_raw) return false;
            void* candidate_raw = nullptr;
            const uintptr_t world = reinterpret_cast<uintptr_t>(world_raw);
            const int32_t demo_off =
                get_world_demo_driver_offset(world_raw, allow_reflection);
            if (SafeReadPtr(static_cast<uint8_t*>(world_raw) + demo_off,
                            &candidate_raw))
            {
                if (probe_demo_time_source_candidate(
                        report, DemoDriverResolveSource::UWorldDemoNetDriver,
                        world, 0, candidate_raw, snap))
                    return true;
            }
            else
            {
                add_pointer_read_failure_time_source_attempt(
                    report, DemoDriverResolveSource::UWorldDemoNetDriver,
                    world, 0);
            }

            candidate_raw = nullptr;
            if (SafeReadPtr(static_cast<uint8_t*>(world_raw)
                                + kUWorld_NetDriver_Off,
                            &candidate_raw))
            {
                if (probe_demo_time_source_candidate(
                        report, DemoDriverResolveSource::UWorldNetDriver,
                        world, 0, candidate_raw, snap))
                    return true;
            }
            else
            {
                add_pointer_read_failure_time_source_attempt(
                    report, DemoDriverResolveSource::UWorldNetDriver,
                    world, 0);
            }

            return read_level_collection_demo_time_source(
                world_raw, snap, report, allow_reflection);
        }

        inline std::atomic<uintptr_t>& cached_demo_driver_ptr() noexcept
        {
            static std::atomic<uintptr_t> s_cached{0};
            return s_cached;
        }

        inline void clear_cached_demo_driver() noexcept
        {
            cached_demo_driver_ptr().store(0, std::memory_order_release);
            cached_demo_world_ptr().store(0, std::memory_order_release);
            demo_net_driver_ptr().invalidate();
        }

        inline void clear_cached_demo_time_source() noexcept
        {
            cached_demo_time_source_ptr().store(
                0, std::memory_order_release);
            cached_demo_time_source_world_ptr().store(
                0, std::memory_order_release);
        }

        inline void cache_demo_time_source(
            const DemoTimeSourceSnap& snap,
            void* world_raw) noexcept
        {
            if (!snap.readable || !snap.source_ptr) return;
            cached_demo_time_source_ptr().store(
                snap.source_ptr, std::memory_order_release);
            cached_demo_time_source_world_ptr().store(
                reinterpret_cast<uintptr_t>(world_raw),
                std::memory_order_release);
        }

        inline bool safe_get_world(RC::Unreal::UObject* obj,
                                   void** world_raw) noexcept
        {
            if (!obj || !world_raw) return false;
            __try
            {
                *world_raw = obj->GetWorld();
                return *world_raw != nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                *world_raw = nullptr;
                return false;
            }
        }

        inline bool safe_get_world_vfunc_138(void* obj,
                                             void** world_raw) noexcept
        {
            if (!obj || !world_raw) return false;
            __try
            {
                void** vtbl = *reinterpret_cast<void***>(obj);
                if (!vtbl || !vtbl[0x138 / sizeof(void*)])
                {
                    *world_raw = nullptr;
                    return false;
                }
                using GetWorldFn = void* (__fastcall*)(void*);
                auto fn = reinterpret_cast<GetWorldFn>(
                    vtbl[0x138 / sizeof(void*)]);
                *world_raw = fn(obj);
                return *world_raw != nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                *world_raw = nullptr;
                return false;
            }
        }

        inline bool safe_get_world_context_object(RC::Unreal::UObject* obj,
                                                  void** world_raw) noexcept
        {
            if (!obj || !world_raw) return false;

            // UE4's GetWorldFromContextObject dispatches the UObject virtual
            // GetWorld slot at vtable+0x138.  UE4SS UObject::GetWorld() is a
            // useful fallback, but is not equivalent on every SC6 context
            // object we use for replay authority discovery.
            if (safe_get_world_vfunc_138(reinterpret_cast<void*>(obj),
                                         world_raw))
                return true;
            return safe_get_world(obj, world_raw);
        }

        inline bool read_gengine_ptr(void** engine_raw) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base || !engine_raw) return false;
            return SafeReadPtr(reinterpret_cast<const void*>(
                                   base + kRVA_GEngine),
                               engine_raw)
                && *engine_raw;
        }

        inline void cache_demo_driver(const DemoNetDriverSnap& snap,
                                      void* world_raw) noexcept
        {
            if (!snap.readable || !snap.driver_ptr) return;
            cached_demo_driver_ptr().store(snap.driver_ptr,
                                           std::memory_order_release);
            cached_demo_world_ptr().store(
                reinterpret_cast<uintptr_t>(world_raw),
                std::memory_order_release);
        }

        inline void populate_driver_reflected_offsets(
            DemoNetDriverSnap& snap,
            bool allow_reflection) noexcept
        {
            if (!allow_reflection || !snap.driver_ptr) return;
            auto* driver =
                reinterpret_cast<RC::Unreal::UObject*>(snap.driver_ptr);
            const int32_t cur = reflected_offset_of<float>(
                driver, L"DemoCurrentTime");
            const int32_t total = reflected_offset_of<float>(
                driver, L"DemoTotalTime");
            if (cur >= 0)
                cached_demo_cur_time_off().store(
                    cur, std::memory_order_release);
            if (total >= 0)
                cached_demo_total_time_off().store(
                    total, std::memory_order_release);
            if (cur >= 0 || total >= 0)
            {
                DemoNetDriverSnap refreshed{};
                if (read_demo_driver_raw(
                        reinterpret_cast<void*>(snap.driver_ptr),
                        refreshed))
                {
                    snap = refreshed;
                }
            }
        }

        inline bool read_gengine_game_viewport_demo_driver(
            DemoNetDriverSnap& snap,
            DemoDriverResolveReport* report,
            bool allow_reflection) noexcept
        {
            DemoDriverResolveAttempt attempt{};
            attempt.source =
                DemoDriverResolveSource::GEngineGameViewportWorld;

            void* engine_raw = nullptr;
            if (!read_gengine_ptr(&engine_raw))
            {
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::GWorldUnavailable);
                add_demo_driver_attempt(report, attempt);
                return false;
            }

            void* viewport_raw = nullptr;
            if (!SafeReadPtr(static_cast<uint8_t*>(engine_raw)
                                + kUEngine_GameViewport_Off,
                             &viewport_raw) || !viewport_raw)
            {
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::NullCandidate);
                add_demo_driver_attempt(report, attempt);
                return false;
            }

            attempt.container = reinterpret_cast<uintptr_t>(viewport_raw);
            void* world_raw = nullptr;
            if (!safe_get_world_vfunc_138(viewport_raw, &world_raw)
                && !safe_get_world(
                    reinterpret_cast<RC::Unreal::UObject*>(viewport_raw),
                    &world_raw))
            {
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_driver_attempt(report, attempt);
                return false;
            }

            attempt.world = reinterpret_cast<uintptr_t>(world_raw);
            attempt.world_readable = true;
            add_demo_driver_attempt(report, attempt);
            return read_world_demo_driver(world_raw, snap, report,
                                          allow_reflection);
        }

        inline bool read_gengine_game_viewport_demo_time_source(
            DemoTimeSourceSnap& snap,
            DemoTimeSourceReport* report,
            bool allow_reflection) noexcept
        {
            DemoDriverResolveAttempt attempt{};
            attempt.source =
                DemoDriverResolveSource::GEngineGameViewportWorld;

            void* engine_raw = nullptr;
            if (!read_gengine_ptr(&engine_raw))
            {
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::GWorldUnavailable);
                add_demo_time_source_attempt(report, attempt);
                return false;
            }

            void* viewport_raw = nullptr;
            if (!SafeReadPtr(static_cast<uint8_t*>(engine_raw)
                                + kUEngine_GameViewport_Off,
                             &viewport_raw) || !viewport_raw)
            {
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::NullCandidate);
                add_demo_time_source_attempt(report, attempt);
                return false;
            }

            attempt.container = reinterpret_cast<uintptr_t>(viewport_raw);
            void* world_raw = nullptr;
            if (!safe_get_world_vfunc_138(viewport_raw, &world_raw)
                && !safe_get_world(
                    reinterpret_cast<RC::Unreal::UObject*>(viewport_raw),
                    &world_raw))
            {
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_time_source_attempt(report, attempt);
                return false;
            }

            attempt.world = reinterpret_cast<uintptr_t>(world_raw);
            attempt.world_readable = true;
            add_demo_time_source_attempt(report, attempt);
            return read_world_demo_time_source(world_raw, snap, report,
                                               allow_reflection);
        }

        inline bool read_gengine_world_context_demo_driver(
            DemoNetDriverSnap& snap,
            DemoDriverResolveReport* report,
            bool allow_reflection) noexcept
        {
            void* engine_raw = nullptr;
            if (!read_gengine_ptr(&engine_raw))
            {
                DemoDriverResolveAttempt attempt{};
                attempt.source = DemoDriverResolveSource::GEngineWorldContext;
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::GWorldUnavailable);
                add_demo_driver_attempt(report, attempt);
                return false;
            }

            auto* engine = static_cast<uint8_t*>(engine_raw);
            void* contexts_raw = nullptr;
            int32_t count = 0;
            if (!SafeReadPtr(engine + kUEngine_WorldContexts_Data_Off,
                             &contexts_raw)
                || !contexts_raw
                || !SafeReadInt32(engine + kUEngine_WorldContexts_Count_Off,
                                  &count))
            {
                DemoDriverResolveAttempt attempt{};
                attempt.source = DemoDriverResolveSource::GEngineWorldContext;
                attempt.container = reinterpret_cast<uintptr_t>(engine_raw);
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_driver_attempt(report, attempt);
                return false;
            }

            if (count <= 0 || count > kMaxWorldContexts)
            {
                DemoDriverResolveAttempt attempt{};
                attempt.source = DemoDriverResolveSource::GEngineWorldContext;
                attempt.container = reinterpret_cast<uintptr_t>(contexts_raw);
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_driver_attempt(report, attempt);
                return false;
            }

            auto** contexts = reinterpret_cast<void**>(contexts_raw);
            for (int32_t i = 0; i < count; ++i)
            {
                void* context_raw = nullptr;
                if (!SafeReadPtr(contexts + i, &context_raw) || !context_raw)
                {
                    DemoDriverResolveAttempt attempt{};
                    attempt.source =
                        DemoDriverResolveSource::GEngineWorldContext;
                    attempt.container =
                        reinterpret_cast<uintptr_t>(contexts_raw)
                        + static_cast<uintptr_t>(i * sizeof(void*));
                    attempt.failure_reason = static_cast<int32_t>(
                        DemoDriverResolveFailure::WorldReadFailed);
                    add_demo_driver_attempt(report, attempt);
                    continue;
                }

                void* world_raw = nullptr;
                DemoDriverResolveAttempt attempt{};
                attempt.source = DemoDriverResolveSource::GEngineWorldContext;
                attempt.container = reinterpret_cast<uintptr_t>(context_raw);
                if (!SafeReadPtr(
                        static_cast<uint8_t*>(context_raw)
                            + kFWorldContext_CurrentWorld_Off,
                        &world_raw) || !world_raw)
                {
                    attempt.failure_reason = static_cast<int32_t>(
                        DemoDriverResolveFailure::NullWorld);
                    add_demo_driver_attempt(report, attempt);
                    continue;
                }

                attempt.world = reinterpret_cast<uintptr_t>(world_raw);
                attempt.world_readable = true;
                add_demo_driver_attempt(report, attempt);
                if (read_world_demo_driver(world_raw, snap, report,
                                           allow_reflection))
                    return true;
            }

            return false;
        }

        inline bool read_gengine_world_context_demo_time_source(
            DemoTimeSourceSnap& snap,
            DemoTimeSourceReport* report,
            bool allow_reflection) noexcept
        {
            void* engine_raw = nullptr;
            if (!read_gengine_ptr(&engine_raw))
            {
                DemoDriverResolveAttempt attempt{};
                attempt.source = DemoDriverResolveSource::GEngineWorldContext;
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::GWorldUnavailable);
                add_demo_time_source_attempt(report, attempt);
                return false;
            }

            auto* engine = static_cast<uint8_t*>(engine_raw);
            void* contexts_raw = nullptr;
            int32_t count = 0;
            if (!SafeReadPtr(engine + kUEngine_WorldContexts_Data_Off,
                             &contexts_raw)
                || !contexts_raw
                || !SafeReadInt32(engine + kUEngine_WorldContexts_Count_Off,
                                  &count))
            {
                DemoDriverResolveAttempt attempt{};
                attempt.source = DemoDriverResolveSource::GEngineWorldContext;
                attempt.container = reinterpret_cast<uintptr_t>(engine_raw);
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_time_source_attempt(report, attempt);
                return false;
            }

            if (count <= 0 || count > kMaxWorldContexts)
            {
                DemoDriverResolveAttempt attempt{};
                attempt.source = DemoDriverResolveSource::GEngineWorldContext;
                attempt.container = reinterpret_cast<uintptr_t>(contexts_raw);
                attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::WorldReadFailed);
                add_demo_time_source_attempt(report, attempt);
                return false;
            }

            auto** contexts = reinterpret_cast<void**>(contexts_raw);
            for (int32_t i = 0; i < count; ++i)
            {
                void* context_raw = nullptr;
                if (!SafeReadPtr(contexts + i, &context_raw) || !context_raw)
                {
                    DemoDriverResolveAttempt attempt{};
                    attempt.source =
                        DemoDriverResolveSource::GEngineWorldContext;
                    attempt.container =
                        reinterpret_cast<uintptr_t>(contexts_raw)
                        + static_cast<uintptr_t>(i * sizeof(void*));
                    attempt.failure_reason = static_cast<int32_t>(
                        DemoDriverResolveFailure::WorldReadFailed);
                    add_demo_time_source_attempt(report, attempt);
                    continue;
                }

                void* world_raw = nullptr;
                DemoDriverResolveAttempt attempt{};
                attempt.source = DemoDriverResolveSource::GEngineWorldContext;
                attempt.container = reinterpret_cast<uintptr_t>(context_raw);
                if (!SafeReadPtr(
                        static_cast<uint8_t*>(context_raw)
                            + kFWorldContext_CurrentWorld_Off,
                        &world_raw) || !world_raw)
                {
                    attempt.failure_reason = static_cast<int32_t>(
                        DemoDriverResolveFailure::NullWorld);
                    add_demo_time_source_attempt(report, attempt);
                    continue;
                }

                attempt.world = reinterpret_cast<uintptr_t>(world_raw);
                attempt.world_readable = true;
                add_demo_time_source_attempt(report, attempt);
                if (read_world_demo_time_source(world_raw, snap, report,
                                                allow_reflection))
                    return true;
            }

            return false;
        }

        inline DemoTimeSourceReport resolve_demo_time_source_report(
            bool allow_slow_probe) noexcept
        {
            DemoTimeSourceReport report{};
            DemoTimeSourceSnap snap{};

            const uintptr_t cached =
                cached_demo_time_source_ptr().load(
                    std::memory_order_acquire);
            if (cached)
            {
                if (probe_demo_time_source_candidate(
                        &report, DemoDriverResolveSource::Cached, 0, 0,
                        reinterpret_cast<void*>(cached), snap))
                {
                    report.snap = snap;
                    report.source = DemoDriverResolveSource::Cached;
                    return report;
                }
                clear_cached_demo_time_source();
            }

            void* world_raw = nullptr;
            if (allow_slow_probe)
            {
                RC::Unreal::UObject* rp = replay_player_ptr().get(
                    L"LuxBattleReplayPlayer");
                DemoDriverResolveAttempt rp_attempt{};
                rp_attempt.source =
                    DemoDriverResolveSource::ReplayPlayerGetWorld;
                rp_attempt.candidate_driver =
                    reinterpret_cast<uintptr_t>(rp);
                if (rp && safe_get_world_context_object(rp, &world_raw))
                {
                    rp_attempt.world =
                        reinterpret_cast<uintptr_t>(world_raw);
                    rp_attempt.world_readable = true;
                    add_demo_time_source_attempt(&report, rp_attempt);
                    if (read_world_demo_time_source(world_raw, snap, &report,
                                                    true))
                    {
                        cache_demo_time_source(snap, world_raw);
                        report.snap = snap;
                        report.source = last_time_source_success_source(
                            report,
                            DemoDriverResolveSource::ReplayPlayerGetWorld);
                        report.snap.source = report.source;
                        return report;
                    }
                }
                else
                {
                    rp_attempt.failure_reason = static_cast<int32_t>(
                        rp ? DemoDriverResolveFailure::WorldReadFailed
                           : DemoDriverResolveFailure::ReplayPlayerUnavailable);
                    add_demo_time_source_attempt(&report, rp_attempt);
                }
            }

            const uintptr_t cached_world =
                cached_demo_time_source_world_ptr().load(
                    std::memory_order_acquire);
            if (cached_world)
            {
                world_raw = reinterpret_cast<void*>(cached_world);
                DemoDriverResolveAttempt world_attempt{};
                world_attempt.source = DemoDriverResolveSource::GWorld;
                world_attempt.world = cached_world;
                world_attempt.world_readable = true;
                add_demo_time_source_attempt(&report, world_attempt);
                if (read_world_demo_time_source(world_raw, snap, &report,
                                                allow_slow_probe))
                {
                    cache_demo_time_source(snap, world_raw);
                    report.snap = snap;
                    report.source = last_time_source_success_source(
                        report, DemoDriverResolveSource::GWorld);
                    report.snap.source = report.source;
                    return report;
                }
            }

            world_raw = nullptr;
            DemoDriverResolveAttempt gworld_attempt{};
            gworld_attempt.source = DemoDriverResolveSource::GWorld;
            if (read_gworld_ptr(&world_raw))
            {
                gworld_attempt.world = reinterpret_cast<uintptr_t>(world_raw);
                gworld_attempt.world_readable = true;
                add_demo_time_source_attempt(&report, gworld_attempt);
                if (read_world_demo_time_source(world_raw, snap, &report,
                                                allow_slow_probe))
                {
                    cache_demo_time_source(snap, world_raw);
                    report.snap = snap;
                    report.source = last_time_source_success_source(
                        report, DemoDriverResolveSource::GWorld);
                    report.snap.source = report.source;
                    return report;
                }
            }
            else
            {
                gworld_attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::GWorldUnavailable);
                add_demo_time_source_attempt(&report, gworld_attempt);
            }

            if (allow_slow_probe)
            {
                if (read_gengine_game_viewport_demo_time_source(
                        snap, &report, allow_slow_probe))
                {
                    cache_demo_time_source(
                        snap, reinterpret_cast<void*>(
                            report.attempt_count > 0
                                ? report.attempts[
                                    report.attempt_count - 1].world
                                : 0));
                    report.snap = snap;
                    report.source = last_time_source_success_source(
                        report,
                        DemoDriverResolveSource::GEngineGameViewportWorld);
                    report.snap.source = report.source;
                    return report;
                }

                if (read_gengine_world_context_demo_time_source(
                        snap, &report, allow_slow_probe))
                {
                    cache_demo_time_source(
                        snap, reinterpret_cast<void*>(
                            report.attempt_count > 0
                                ? report.attempts[
                                    report.attempt_count - 1].world
                                : 0));
                    report.snap = snap;
                    report.source = last_time_source_success_source(
                        report,
                        DemoDriverResolveSource::GEngineWorldContext);
                    report.snap.source = report.source;
                    return report;
                }

                RC::Unreal::UObject* d = demo_net_driver_ptr().get(
                    L"DemoNetDriver");
                if (!d) d = find_demo_net_driver_probed();
                if (d && probe_demo_time_source_candidate(
                            &report,
                            DemoDriverResolveSource::ObjectArrayProbe,
                            0, 0, d, snap))
                {
                    cache_demo_time_source(snap, nullptr);
                    report.snap = snap;
                    report.source =
                        DemoDriverResolveSource::ObjectArrayProbe;
                    report.snap.source = report.source;
                    return report;
                }
                if (!d)
                {
                    DemoDriverResolveAttempt probe_attempt{};
                    probe_attempt.source =
                        DemoDriverResolveSource::ObjectArrayProbe;
                    probe_attempt.failure_reason = static_cast<int32_t>(
                        DemoDriverResolveFailure::SlowProbeUnavailable);
                    add_demo_time_source_attempt(&report, probe_attempt);
                }
            }

            return report;
        }

        inline DemoDriverResolveReport resolve_demo_net_driver_report(
            bool allow_slow_probe) noexcept
        {
            DemoDriverResolveReport report{};
            DemoNetDriverSnap snap{};

            const uintptr_t cached =
                cached_demo_driver_ptr().load(std::memory_order_acquire);
            if (cached)
            {
                if (probe_demo_driver_candidate(
                        &report, DemoDriverResolveSource::Cached, 0, 0,
                        reinterpret_cast<void*>(cached), snap))
                {
                    report.snap = snap;
                    report.source = DemoDriverResolveSource::Cached;
                    return report;
                }
                clear_cached_demo_driver();
            }

            void* world_raw = nullptr;
            if (allow_slow_probe)
            {
                RC::Unreal::UObject* rp = replay_player_ptr().get(
                    L"LuxBattleReplayPlayer");
                DemoDriverResolveAttempt rp_attempt{};
                rp_attempt.source =
                    DemoDriverResolveSource::ReplayPlayerGetWorld;
                rp_attempt.candidate_driver =
                    reinterpret_cast<uintptr_t>(rp);
                if (rp && safe_get_world_context_object(rp, &world_raw))
                {
                    rp_attempt.world =
                        reinterpret_cast<uintptr_t>(world_raw);
                    rp_attempt.world_readable = true;
                    add_demo_driver_attempt(&report, rp_attempt);
                    if (read_world_demo_driver(world_raw, snap, &report,
                                               true))
                    {
                        populate_driver_reflected_offsets(
                            snap, allow_slow_probe);
                        cache_demo_driver(snap, world_raw);
                        report.snap = snap;
                        report.source =
                            last_success_source(
                                report,
                                DemoDriverResolveSource::ReplayPlayerGetWorld);
                        return report;
                    }
                }
                else
                {
                    rp_attempt.failure_reason = static_cast<int32_t>(
                        rp ? DemoDriverResolveFailure::WorldReadFailed
                           : DemoDriverResolveFailure::ReplayPlayerUnavailable);
                    add_demo_driver_attempt(&report, rp_attempt);
                }
            }

            const uintptr_t cached_world =
                cached_demo_world_ptr().load(std::memory_order_acquire);
            if (cached_world)
            {
                world_raw = reinterpret_cast<void*>(cached_world);
                DemoDriverResolveAttempt world_attempt{};
                world_attempt.source = DemoDriverResolveSource::GWorld;
                world_attempt.world = cached_world;
                world_attempt.world_readable = true;
                add_demo_driver_attempt(&report, world_attempt);
                if (read_world_demo_driver(world_raw, snap, &report,
                                           allow_slow_probe))
                {
                    populate_driver_reflected_offsets(
                        snap, allow_slow_probe);
                    cache_demo_driver(snap, world_raw);
                    report.snap = snap;
                    report.source = last_success_source(
                        report, DemoDriverResolveSource::GWorld);
                    return report;
                }
            }

            world_raw = nullptr;
            DemoDriverResolveAttempt gworld_attempt{};
            gworld_attempt.source = DemoDriverResolveSource::GWorld;
            if (read_gworld_ptr(&world_raw))
            {
                gworld_attempt.world = reinterpret_cast<uintptr_t>(world_raw);
                gworld_attempt.world_readable = true;
                add_demo_driver_attempt(&report, gworld_attempt);
                if (read_world_demo_driver(world_raw, snap, &report,
                                           allow_slow_probe))
                {
                    populate_driver_reflected_offsets(
                        snap, allow_slow_probe);
                    cache_demo_driver(snap, world_raw);
                    report.snap = snap;
                    report.source = last_success_source(
                        report, DemoDriverResolveSource::GWorld);
                    return report;
                }
            }
            else
            {
                gworld_attempt.failure_reason = static_cast<int32_t>(
                    DemoDriverResolveFailure::GWorldUnavailable);
                add_demo_driver_attempt(&report, gworld_attempt);
            }

            if (allow_slow_probe)
            {
                if (read_gengine_game_viewport_demo_driver(
                        snap, &report, allow_slow_probe))
                {
                    populate_driver_reflected_offsets(
                        snap, allow_slow_probe);
                    cache_demo_driver(
                        snap, reinterpret_cast<void*>(
                            report.attempt_count > 0
                                ? report.attempts[
                                    report.attempt_count - 1].world
                                : 0));
                    report.snap = snap;
                    report.source =
                        last_success_source(
                            report,
                            DemoDriverResolveSource::GEngineGameViewportWorld);
                    return report;
                }

                if (read_gengine_world_context_demo_driver(
                        snap, &report, allow_slow_probe))
                {
                    populate_driver_reflected_offsets(
                        snap, allow_slow_probe);
                    cache_demo_driver(
                        snap, reinterpret_cast<void*>(
                            report.attempt_count > 0
                                ? report.attempts[report.attempt_count - 1].world
                                : 0));
                    report.snap = snap;
                    report.source =
                        last_success_source(
                            report,
                            DemoDriverResolveSource::GEngineWorldContext);
                    return report;
                }
            }

            if (allow_slow_probe)
            {
                RC::Unreal::UObject* d = demo_net_driver_ptr().get(
                    L"DemoNetDriver");
                if (!d) d = find_demo_net_driver_probed();
                if (d && probe_demo_driver_candidate(
                            &report,
                            DemoDriverResolveSource::ObjectArrayProbe,
                            0, 0, d, snap))
                {
                    populate_driver_reflected_offsets(
                        snap, allow_slow_probe);
                    cache_demo_driver(snap, nullptr);
                    report.snap = snap;
                    report.source = DemoDriverResolveSource::ObjectArrayProbe;
                    return report;
                }
                if (!d)
                {
                    DemoDriverResolveAttempt probe_attempt{};
                    probe_attempt.source =
                        DemoDriverResolveSource::ObjectArrayProbe;
                    probe_attempt.failure_reason = static_cast<int32_t>(
                        DemoDriverResolveFailure::SlowProbeUnavailable);
                    add_demo_driver_attempt(&report, probe_attempt);
                }
            }

            return report;
        }

        inline RC::Unreal::UObject*
        find_demo_net_driver_from_world() noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return nullptr;

            void* world_raw = nullptr;
            if (RC::Unreal::UObject* rp = replay_player_ptr().get(
                    L"LuxBattleReplayPlayer"))
            {
                (void)safe_get_world_context_object(rp, &world_raw);
            }
            if (!world_raw)
            {
                if (!SafeReadPtr(
                        reinterpret_cast<const void*>(base + kRVA_GWorld),
                        &world_raw) || !world_raw)
                    return nullptr;
            }

            DemoNetDriverSnap snap{};
            if (!read_world_demo_driver(world_raw, snap))
                return nullptr;

            auto* driver =
                reinterpret_cast<RC::Unreal::UObject*>(snap.driver_ptr);
            if (!RC::Unreal::UObject::IsReal(driver)) return nullptr;
            cached_demo_driver_ptr().store(
                reinterpret_cast<uintptr_t>(driver),
                std::memory_order_release);

            static std::atomic<bool> s_logged{false};
            if (!s_logged.exchange(true, std::memory_order_relaxed))
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.diag] resolved demo driver via "
                        "UWorld demo-driver fields -> 0x{:X} "
                        "(cur={:.3f}s total={:.3f}s time_sane={})\n"),
                    snap.driver_ptr, snap.raw_demo_cur_time,
                    snap.raw_demo_total_time,
                    demo_snap_time_is_sane(snap) ? 1 : 0);
            }
            return driver;
        }

        inline bool read_gworld_ptr(void** world_raw) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;
            return SafeReadPtr(reinterpret_cast<const void*>(
                                   base + kRVA_GWorld),
                               world_raw)
                && *world_raw;
        }

        inline void cache_and_log_fast_demo_driver(
            const DemoNetDriverSnap& s,
            const char* source) noexcept
        {
            cached_demo_driver_ptr().store(s.driver_ptr,
                                           std::memory_order_release);
            static std::atomic<bool> s_logged{false};
            if (!s_logged.exchange(true, std::memory_order_relaxed))
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.diag] fast demo driver resolved via "
                        "{} -> 0x{:X} (cur={:.3f}s total={:.3f}s "
                        "time_sane={})\n"),
                    RC::to_generic_string(source),
                    s.driver_ptr, s.raw_demo_cur_time,
                    s.raw_demo_total_time,
                    demo_snap_time_is_sane(s) ? 1 : 0);
            }
        }

        inline void log_demo_driver_resolve_report_once(
            const char* label,
            const DemoDriverResolveReport& report) noexcept
        {
            static std::atomic<bool> s_logged_generation{false};
            static std::atomic<bool> s_logged_seek{false};
            static std::atomic<bool> s_logged_other{false};
            std::atomic<bool>* gate = &s_logged_other;
            if (label && std::strstr(label, "GEN"))
                gate = &s_logged_generation;
            else if (label && std::strstr(label, "SEEK"))
                gate = &s_logged_seek;
            if (gate->exchange(true, std::memory_order_relaxed))
                return;

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.diag] demo driver resolve report [{}]: "
                "source={} readable={} driver=0x{:X} cur={:.3f}s "
                "total={:.3f}s time_ok={} task_ok={} "
                "raw[+791=0x{:02X} +794=0x{:02X} +7A8=0x{:X} "
                "+7B0={} +7B4={} +7B8=0x{:X}] attempts={}\n"),
                RC::to_generic_string(label ? label : "?"),
                RC::to_generic_string(
                    demo_driver_source_name(report.source)),
                report.snap.readable ? 1 : 0,
                report.snap.driver_ptr,
                report.snap.raw_demo_cur_time,
                report.snap.raw_demo_total_time,
                report.snap.time_fields_readable ? 1 : 0,
                report.snap.task_fields_readable ? 1 : 0,
                static_cast<unsigned>(report.snap.raw_busy_791),
                static_cast<unsigned>(report.snap.raw_loading_794),
                report.snap.raw_task_data_7a8,
                report.snap.raw_task_count_7b0,
                report.snap.raw_task_max_7b4,
                report.snap.raw_current_task_7b8,
                report.attempt_count);

            for (int32_t i = 0; i < report.attempt_count; ++i)
            {
                const DemoDriverResolveAttempt& a = report.attempts[i];
                const bool attempt_time_sane =
                    a.raw_demo_total_time == a.raw_demo_total_time
                    && a.raw_demo_cur_time == a.raw_demo_cur_time
                    && a.raw_demo_total_time >= 0.0f
                    && a.raw_demo_total_time < 86400.0f
                    && a.raw_demo_cur_time >= 0.0f
                    && a.raw_demo_cur_time <= a.raw_demo_total_time + 5.0f;
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.diag]   attempt {} source={} world=0x{:X} "
                    "container=0x{:X} candidate=0x{:X} world_ok={} "
                    "candidate_ok={} class_ok={} task_ok={} time_ok={} "
                    "time_sane={} cur={:.3f}s total={:.3f}s "
                    "raw[+7A8=0x{:X} +7B0={} +7B4={} +7B8=0x{:X}] "
                    "failure={}\n"),
                    i,
                    RC::to_generic_string(
                        demo_driver_source_name(a.source)),
                    a.world, a.container, a.candidate_driver,
                    a.world_readable ? 1 : 0,
                    a.candidate_readable ? 1 : 0,
                    a.class_valid ? 1 : 0,
                    a.task_fields_readable ? 1 : 0,
                    a.time_fields_readable ? 1 : 0,
                    attempt_time_sane ? 1 : 0,
                    a.raw_demo_cur_time,
                    a.raw_demo_total_time,
                    a.raw_task_data_7a8,
                    a.raw_task_count_7b0,
                    a.raw_task_max_7b4,
                    a.raw_current_task_7b8,
                    a.failure_reason);
            }
        }

        inline void log_demo_time_source_report_once(
            const char* label,
            const DemoTimeSourceReport& report) noexcept
        {
            static std::atomic<bool> s_logged_generation{false};
            static std::atomic<bool> s_logged_seek{false};
            static std::atomic<bool> s_logged_other{false};
            std::atomic<bool>* gate = &s_logged_other;
            if (label && std::strstr(label, "GEN"))
                gate = &s_logged_generation;
            else if (label && std::strstr(label, "SEEK"))
                gate = &s_logged_seek;
            if (gate->exchange(true, std::memory_order_relaxed))
                return;

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.diag] demo time source report [{}]: "
                "source={} readable={} ptr=0x{:X} cur={:.3f}s "
                "total={:.3f}s time_ok={} time_sane={} attempts={}\n"),
                RC::to_generic_string(label ? label : "?"),
                RC::to_generic_string(
                    demo_driver_source_name(report.source)),
                report.snap.readable ? 1 : 0,
                report.snap.source_ptr,
                report.snap.raw_demo_cur_time,
                report.snap.raw_demo_total_time,
                report.snap.time_fields_readable ? 1 : 0,
                report.snap.time_sane ? 1 : 0,
                report.attempt_count);

            for (int32_t i = 0; i < report.attempt_count; ++i)
            {
                const DemoDriverResolveAttempt& a = report.attempts[i];
                const bool attempt_time_sane =
                    a.raw_demo_total_time == a.raw_demo_total_time
                    && a.raw_demo_cur_time == a.raw_demo_cur_time
                    && a.raw_demo_total_time >= 0.0f
                    && a.raw_demo_total_time < 86400.0f
                    && a.raw_demo_cur_time >= 0.0f
                    && a.raw_demo_cur_time <= a.raw_demo_total_time + 5.0f;
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.diag]   time attempt {} source={} "
                    "world=0x{:X} container=0x{:X} candidate=0x{:X} "
                    "world_ok={} candidate_ok={} task_ok={} time_ok={} "
                    "time_sane={} cur={:.3f}s total={:.3f}s "
                    "raw[+7A8=0x{:X} +7B0={} +7B4={} +7B8=0x{:X}] "
                    "failure={}\n"),
                    i,
                    RC::to_generic_string(
                        demo_driver_source_name(a.source)),
                    a.world, a.container, a.candidate_driver,
                    a.world_readable ? 1 : 0,
                    a.candidate_readable ? 1 : 0,
                    a.task_fields_readable ? 1 : 0,
                    a.time_fields_readable ? 1 : 0,
                    attempt_time_sane ? 1 : 0,
                    a.raw_demo_cur_time,
                    a.raw_demo_total_time,
                    a.raw_task_data_7a8,
                    a.raw_task_count_7b0,
                    a.raw_task_max_7b4,
                    a.raw_current_task_7b8,
                    a.failure_reason);
            }
        }

        // Hot capture path helper.  This intentionally avoids UE4SS object
        // reflection, UObject::IsReal(), and FindFirstOf(): those walk large
        // engine object tables and are far too expensive to call once per
        // replay capture tick.  The pointer and field reads are SEH-guarded;
        // an absent/torn-down demo driver simply returns readable=false.
        inline DemoNetDriverSnap read_demo_net_driver_fast() noexcept
        {
            if (!NativeBinding::imageBase()) return {};
            return resolve_demo_net_driver_report(false).snap;
        }

        inline DemoTimeSourceSnap read_demo_time_source_fast() noexcept
        {
            if (!NativeBinding::imageBase()) return {};
            return resolve_demo_time_source_report(false).snap;
        }

        // GlobalPtr cache for the demo net driver.  get() throttles its
        // O(N) UObject::IsReal/FindFirstOf revalidation scan (see
        // HorseLib.hpp), so a torn-down driver is re-resolved at the next
        // throttle tick (≤ a fraction of a second) rather than each call.
        inline GlobalPtr& demo_net_driver_ptr() noexcept
        {
            static GlobalPtr s_ptr;
            return s_ptr;
        }

        // Once-per-session probe: try several plausible UClass names
        // for UDemoNetDriver and log which one resolves.  Helps when
        // the canonical "DemoNetDriver" name returns null - SC6 might
        // ship the class under a renamed Blueprint subclass or with a
        // /Script/Engine. prefix variant.  Result is cached.
        inline RC::Unreal::UObject* find_demo_net_driver_probed() noexcept
        {
            struct Candidate {
                const wchar_t* name;
                const char*    debug_name;  // narrow for logging
            };
            static const Candidate candidates[] = {
                { L"DemoNetDriver",             "DemoNetDriver" },
                { L"LuxDemoNetDriver",          "LuxDemoNetDriver" },
                { L"SoulcaliburDemoNetDriver",  "SoulcaliburDemoNetDriver" },
                { L"LuxReplayNetDriver",        "LuxReplayNetDriver" },
            };
            static std::atomic<bool> s_probed{false};
            static RC::Unreal::UObject* s_resolved{nullptr};
            static std::chrono::steady_clock::time_point s_last_probe{};
            if (s_resolved)
            {
                if (RC::Unreal::UObject::IsReal(s_resolved))
                    return s_resolved;
                s_resolved = nullptr;
            }

            const auto now = std::chrono::steady_clock::now();
            if (s_probed.load(std::memory_order_relaxed) && !s_resolved
                && (now - s_last_probe) < std::chrono::seconds(1))
                return nullptr;
            s_last_probe = now;

            // Re-probe at most once per second after a miss.  Each
            // FindFirstOf walks the full UObject array, so retrying all
            // candidates every capture tick is a frame-time killer.
            for (const auto& c : candidates)
            {
                RC::Unreal::UObject* obj =
                    RC::Unreal::UObjectGlobals::FindFirstOf(c.name);
                if (obj)
                {
                    if (!s_probed.exchange(true, std::memory_order_relaxed))
                    {
                        RC::Output::send<RC::LogLevel::Default>(
                            STR("[ReplayScrub.diag] resolved demo driver "
                                "via UClass=\"{}\" -> 0x{:X}\n"),
                            RC::to_generic_string(c.debug_name),
                            reinterpret_cast<uintptr_t>(obj));
                    }
                    s_resolved = obj;
                    return obj;
                }
            }
            if (!s_probed.exchange(true, std::memory_order_relaxed))
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.diag] no demo driver class found "
                        "(tried DemoNetDriver, LuxDemoNetDriver, "
                        "SoulcaliburDemoNetDriver, LuxReplayNetDriver)\n"));
            }
            return nullptr;
        }

        inline DemoNetDriverSnap read_demo_net_driver() noexcept
        {
            DemoDriverResolveReport report =
                resolve_demo_net_driver_report(true);
            DemoNetDriverSnap s = report.snap;
            if (!s.readable) return s;
            auto* d = reinterpret_cast<RC::Unreal::UObject*>(s.driver_ptr);
            if (!RC::Unreal::UObject::IsReal(d)) return s;
            Obj o{d};
            s.demo_cur_time   = o.getValueOr<float>(L"DemoCurrentTime", -1.0f);
            s.demo_total_time = o.getValueOr<float>(L"DemoTotalTime",   -1.0f);
            s.demo_frame_num  = o.getValueOr<int32_t>(L"DemoFrameNum",  -1);
            // UE4.21 UNetDriver/UDemoNetDriver bool members are
            // single-byte fields exposed as UPROPERTY(BoolProperty).
            // getValueOr<bool> reads the single byte directly.
            s.bIsPlaying      = o.getValueOr<bool>(L"bIsPlaying", false);
            s.bIsRecording    = o.getValueOr<bool>(L"bIsRecording", false);
            s.bIsSavingCheckpoint =
                o.getValueOr<bool>(L"bSavingCheckpoint", false);
            s.readable = true;
            return s;
        }

        inline DemoTimeSourceSnap read_demo_time_source() noexcept
        {
            return resolve_demo_time_source_report(true).snap;
        }

        inline void log_demo_net_driver(const char* label,
                                        const DemoNetDriverSnap& s) noexcept
        {
            if (!s.readable)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.diag] {} UDemoNetDriver=null "
                        "(UWorld->DemoNetDriver and object-array fallback "
                        "both failed; replay viewing may not be active)\n"),
                    RC::to_generic_string(label));
                return;
            }
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] {} UDemoNetDriver=0x{:X} "
                    "DemoCurrentTime={:.3f}s DemoTotalTime={:.3f}s "
                    "DemoFrameNum={} bIsPlaying={} bIsRecording={} "
                    "bSavingCheckpoint={} raw[+414 total={:.3f}s "
                    "+418 cur={:.3f}s +791=0x{:02X} +794=0x{:02X} "
                    "+7A8 taskData=0x{:X} +7B0 taskCount={} "
                    "+7B4 taskMax={} +7B8 curTask=0x{:X}]\n"),
                RC::to_generic_string(label), s.driver_ptr,
                s.demo_cur_time, s.demo_total_time, s.demo_frame_num,
                s.bIsPlaying ? 1 : 0, s.bIsRecording ? 1 : 0,
                s.bIsSavingCheckpoint ? 1 : 0,
                s.raw_demo_total_time, s.raw_demo_cur_time,
                static_cast<unsigned>(s.raw_busy_791),
                static_cast<unsigned>(s.raw_loading_794),
                s.raw_task_data_7a8, s.raw_task_count_7b0,
                s.raw_task_max_7b4, s.raw_current_task_7b8);
        }

        // -------------------------------------------------------------
        // ALuxBattleReplayPlayer — the SC6 actor that holds the .replay
        // recording, the per-round reset data, and the UE4-replicated
        // playback cursor (CurrentTime / CurrentRound / bIsPlayingBack).
        // This is THE authoritative playback head for match-replay
        // viewing.  The 2026-05-12 audit established that:
        //   - The custom SC6 input pipeline (LuxReplay_Decode -> Stage 2
        //     -> Stage 3) is the per-frame mover of inputs.
        //   - But the source-of-truth for "what frame are we on" is
        //     ALuxBattleReplayPlayer.CurrentTime + .CurrentRound.
        //   - UE4's UDemoNetDriver replicates these per tick.
        //   - HgCpuDirect snapshots capture chara state but NOT these
        //     replicated fields.  So after a backward seek, our chara
        //     restore lasts exactly until the next replication tick
        //     overwrites it (or, if at EOF, until the active MoveVM
        //     animation completes - the user's "stand still" symptom).
        //
        // Field offsets (verified via ALuxBattleReplayPlayer_RegisterProperties
        // @ 0x14097beb0):
        //   +0x398  byte   bEnable (gating byte)
        //   +0x39C  int32  CurrentRound       (replicated)
        //   +0x3A0  float  CurrentTime        (replicated)
        //   +0x3A8  ptr    StateResetData     (ULuxBattleStateResetData*)
        //   +0x3B0  int32  TotalRounds
        //   +0x3B8  ptr    RecordingData      (ULuxBattleRecordingData*)
        //   +0x3D0  bool   bIsPlayingBack     (replicated)
        // -------------------------------------------------------------
        struct ReplayPlayerSnap
        {
            uintptr_t actor_ptr     {0};
            int32_t   current_round {-1};
            float     current_time  {-1.0f};
            int32_t   total_rounds  {-1};
            bool      is_playing_back {false};
            bool      bEnable        {false};
            uintptr_t recording_data {0};
            uintptr_t state_reset_data {0};
            bool      readable       {false};
        };

        // GlobalPtr cache for ALuxBattleReplayPlayer.  get() throttles
        // its O(N) IsReal/FindFirstOf revalidation scan (see HorseLib.hpp);
        // ReplayScrub::reset_for_new_replay() invalidate()s this on a
        // replay swap / presence change so a torn-down replay-viewer
        // instance is re-resolved promptly rather than at the next tick.
        inline GlobalPtr& replay_player_ptr() noexcept
        {
            static GlobalPtr s_ptr;
            return s_ptr;
        }

        inline ReplayPlayerSnap read_replay_player() noexcept
        {
            ReplayPlayerSnap s{};
            RC::Unreal::UObject* obj =
                replay_player_ptr().get(L"LuxBattleReplayPlayer");
            if (!obj) return s;
            s.actor_ptr = reinterpret_cast<uintptr_t>(obj);
            Obj o{obj};
            s.current_round =
                o.getValueOr<int32_t>(L"CurrentRound", -1);
            s.current_time =
                o.getValueOr<float>(L"CurrentTime", -1.0f);
            s.total_rounds =
                o.getValueOr<int32_t>(L"TotalRounds", -1);
            s.is_playing_back =
                o.getValueOr<bool>(L"bIsPlayingBack", false);
            s.bEnable =
                o.getValueOr<bool>(L"bEnable", false);

            // Pointer fields read via raw byte access since
            // GetValuePtrByPropertyNameInChain<UObject*> requires
            // exact type match.  Fall back to raw byte deref.
            uint8_t* a = reinterpret_cast<uint8_t*>(obj);
            void* rec = nullptr;
            void* srd = nullptr;
            SafeReadPtr(a + 0x3B8, &rec);
            SafeReadPtr(a + 0x3A8, &srd);
            s.recording_data    = reinterpret_cast<uintptr_t>(rec);
            s.state_reset_data  = reinterpret_cast<uintptr_t>(srd);
            s.readable = true;
            return s;
        }

        inline void log_replay_player(const char* label,
                                      const ReplayPlayerSnap& s) noexcept
        {
            if (!s.readable)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.diag] {} ALuxBattleReplayPlayer=null "
                        "(FindFirstOf(L\"LuxBattleReplayPlayer\") returned "
                        "nothing - replay actor not in world)\n"),
                    RC::to_generic_string(label));
                return;
            }
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] {} ALuxBattleReplayPlayer=0x{:X} "
                    "CurrentRound={} CurrentTime={:.4f}s TotalRounds={} "
                    "bIsPlayingBack={} bEnable={} "
                    "RecordingData=0x{:X} StateResetData=0x{:X}\n"),
                RC::to_generic_string(label),
                s.actor_ptr, s.current_round, s.current_time,
                s.total_rounds, s.is_playing_back ? 1 : 0,
                s.bEnable ? 1 : 0,
                s.recording_data, s.state_reset_data);
        }

        // Seek primitive: write ALuxBattleReplayPlayer.CurrentTime +
        // CurrentRound directly via VERIFIED BYTE OFFSETS.  This is the
        // engine's UE4-level replay playback head; SC6's per-frame
        // ALuxBattleReplayPlayer Actor::Tick reads it to decide which
        // recorded frame to push into BM->ReplayCharaSnapshot.  Without
        // rewinding this, the playback head stays at the live edge and
        // post-seek the engine pushes inputs from frame F (the live
        // edge) instead of frame T (the seek target).
        //
        // 2026-05-14 finding: UE4SS property-reflection getPtr<>("CurrentTime")
        // is UNRELIABLE in this build (some sessions return null pointers
        // even though getValueOr<>("CurrentTime") reads fine on the same
        // actor).  Switched to direct byte writes at the offsets
        // VERIFIED against the ALuxBattleReplayPlayer Ghidra struct:
        //
        //   +0x398  byte   bEnable
        //   +0x39C  int32  nCurrentRound       (replicated)
        //   +0x3A0  float  flCurrentTime       (replicated)
        //   +0x3D0  bool   fIsPlayingBack      (replicated)
        //
        // target_master_frame: the master_clock frame to seek to.
        // target_round: 0-based round index (the caller currently
        //   hardcodes 0; multi-round support TODO).
        //
        // Returns true if the actor was resolvable and the writes hit.
        // Reads the prev values via SafeRead before writing for the
        // sticky-write diagnostic log.
        inline bool write_replay_player_cursor(int32_t target_master_frame,
                                               int32_t target_round) noexcept
        {
            RC::Unreal::UObject* obj =
                replay_player_ptr().get(L"LuxBattleReplayPlayer");
            if (!obj) return false;
            uint8_t* a = reinterpret_cast<uint8_t*>(obj);

            constexpr uintptr_t kRP_CurrentRound_Off    = 0x39C;
            constexpr uintptr_t kRP_CurrentTime_Off     = 0x3A0;
            constexpr uintptr_t kRP_IsPlayingBack_Off   = 0x3D0;

            float   prev_time  = -1.0f;
            int32_t prev_round = -1;
            uint8_t prev_play  = 0;
            SafeReadFloat(a + kRP_CurrentTime_Off,   &prev_time);
            SafeReadInt32(a + kRP_CurrentRound_Off,  &prev_round);
            SafeReadUInt8(a + kRP_IsPlayingBack_Off, &prev_play);

            const float new_time =
                static_cast<float>(target_master_frame) / 60.0f;

            // Direct writes - actor is heap memory, just stores.
            // We don't wrap in SEH because the actor was just successfully
            // read above; if it's torn down between read and write we
            // crash, but that's a much smaller window than reflection.
            *reinterpret_cast<float*>  (a + kRP_CurrentTime_Off)  = new_time;
            *reinterpret_cast<int32_t*>(a + kRP_CurrentRound_Off) = target_round;

            // Force bIsPlayingBack = 1 so the actor does not advertise
            // end-of-replay.  This is cursor/UI repair only; movement
            // resume comes from UDemoNetDriver::GotoTimeInSeconds.
            *reinterpret_cast<uint8_t*>(a + kRP_IsPlayingBack_Off) = 1;

            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] seek-via-direct wrote "
                    "CurrentTime {:.4f}->{:.4f}s "
                    "CurrentRound {}->{} "
                    "bIsPlayingBack {}->1\n"),
                prev_time, new_time, prev_round, target_round,
                static_cast<int>(prev_play));
            return true;
        }

        // Snapshot of g_LuxBattle_LatestEngineInput_PerPlayer (2 qwords).
        // Compares pre/post-seek input flow.  This is the live per-frame
        // input that the simulation reads each tick.  If post-seek shows
        // 0/0 while baseline shows non-zero, the upstream dispatcher
        // stopped feeding inputs after our state restore.
        struct LatestEngineInputSnap
        {
            uint64_t p1_input {0};
            uint64_t p2_input {0};
            bool     readable {false};
        };

        inline LatestEngineInputSnap read_latest_engine_input() noexcept
        {
            LatestEngineInputSnap s{};
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return s;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(
                base + kRVA_LatestEngineInput);
            SafeReadUInt64(p + 0, &s.p1_input);
            SafeReadUInt64(p + 8, &s.p2_input);
            s.readable = true;
            return s;
        }

        inline void log_latest_engine_input(const char* label,
                                            const LatestEngineInputSnap& s) noexcept
        {
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] {} LatestEngineInput "
                    "P1=0x{:X} P2=0x{:X}\n"),
                RC::to_generic_string(label),
                s.p1_input, s.p2_input);
        }

        // 2026-05-15 (ultrathink): Direct read of ALuxBattleFrameInputLog
        // state via BM->pBattleFrameInputLog (BM+0x478).  Bypasses UE4
        // reflection - reads raw bytes at known offsets verified against
        // the Ghidra struct.  This gives empirical visibility into the
        // SimulationLoop catch-up driver state:
        //   - bEnable: input-log playback enable
        //   - dwPlaybackCursor: input-log playback cursor
        //   - nLastFrameID: round-id; mismatch with BM cache triggers reset
        //   - nMasterClock: catch-up loop "advance to" counter
        //   - nTotalRecordedFrames: bound for the cursor
        //   - bDoubleTickGuard: gates the second tick in a frame
        //   - nDrainCursor: online drain cursor
        // Plus BM-side cache fields and chara-level replay state at
        // chara+0x4400 (NOT captured by HgCpuDirect).
        struct ReplayDriverSnap
        {
            uintptr_t bm_ptr           {0};
            uintptr_t il_ptr           {0};
            uint8_t   bm_move_state    {0xFF};
            uint8_t   bm_status        {0xFF};
            int32_t   bm_last_frame_id {-1};
            int32_t   bm_last_applied  {-1};
            int32_t   bm_frame_advance {-1};
            uint32_t  il_fwdrev_bits   {0xFFFFFFFFu};
            uint8_t   il_b_enable      {0xFF};
            uint32_t  il_playback_cursor {0xFFFFFFFFu};
            int32_t   il_last_frame_id {-1};
            int32_t   il_master_clock  {-1};
            uintptr_t il_recorded_buf  {0};
            int32_t   il_total_frames  {-1};
            uint32_t  il_online_active {0xFFFFFFFFu};
            uint8_t   il_double_tick_guard {0xFF};
            int32_t   il_drain_cursor  {-1};
            int32_t   il_min_store_idx {-1};
            // Per-chara replay state (chara+0x4400 region, NOT in HgCpuDirect)
            uint32_t  p1_replay_lookup_key   {0xFFFFFFFFu};
            uint32_t  p1_replay_enable_flag  {0xFFFFFFFFu};
            uint32_t  p1_replay_frame_offset {0xFFFFFFFFu};
            uint32_t  p1_replay_frame_total  {0xFFFFFFFFu};
            uint32_t  p1_replay_frame_target {0xFFFFFFFFu};
            uint32_t  p1_replay_consumer_cursor {0xFFFFFFFFu};
            uint8_t   p1_chara_mode    {0xFF};
            uint32_t  p2_replay_lookup_key   {0xFFFFFFFFu};
            uint32_t  p2_replay_enable_flag  {0xFFFFFFFFu};
            uint32_t  p2_replay_frame_offset {0xFFFFFFFFu};
            uint32_t  p2_replay_frame_total  {0xFFFFFFFFu};
            uint32_t  p2_replay_frame_target {0xFFFFFFFFu};
            uint32_t  p2_replay_consumer_cursor {0xFFFFFFFFu};
            uint8_t   p2_chara_mode    {0xFF};
            bool      readable         {false};
        };

        // Cached BM pointer for direct reads.  Mirrors what ReplayScrub uses.
        inline GlobalPtr& bm_diag_ptr() noexcept
        {
            static GlobalPtr s_ptr;
            return s_ptr;
        }

        inline ReplayDriverSnap read_replay_driver() noexcept
        {
            ReplayDriverSnap s{};
            RC::Unreal::UObject* bm_obj =
                bm_diag_ptr().get(L"LuxBattleManager");
            if (!bm_obj) return s;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
            s.bm_ptr = reinterpret_cast<uintptr_t>(bm_obj);

            SafeReadUInt8 (bm + kBM_bMoveStateByte_Off,         &s.bm_move_state);
            SafeReadUInt8 (bm + kBM_bStatusByte_Off,            &s.bm_status);
            SafeReadInt32 (bm + kBM_nReplayLastFrameID_Off,     &s.bm_last_frame_id);
            SafeReadInt32 (bm + kBM_nReplayLastApplied_Off,     &s.bm_last_applied);
            SafeReadInt32 (bm + kBM_nFrameAdvanceCounter_Off,   &s.bm_frame_advance);

            void* il_raw = nullptr;
            if (SafeReadPtr(bm + kBM_pBattleFrameInputLog_Off, &il_raw) && il_raw)
            {
                uint8_t* il = reinterpret_cast<uint8_t*>(il_raw);
                s.il_ptr = reinterpret_cast<uintptr_t>(il_raw);

                SafeReadUInt32(il + kIL_dwForwardReverseBitfield_Off, &s.il_fwdrev_bits);
                SafeReadUInt8 (il + kIL_bEnable_Off,                  &s.il_b_enable);
                SafeReadUInt32(il + kIL_dwPlaybackCursor_Off,         &s.il_playback_cursor);
                SafeReadInt32 (il + kIL_nLastFrameID_Off,             &s.il_last_frame_id);
                SafeReadInt32 (il + kIL_nMasterClock_Off,             &s.il_master_clock);

                void* rec_buf = nullptr;
                if (SafeReadPtr(il + kIL_pRecordedFrameBuffer_Off, &rec_buf))
                    s.il_recorded_buf = reinterpret_cast<uintptr_t>(rec_buf);

                SafeReadInt32 (il + kIL_nTotalRecordedFrames_Off,     &s.il_total_frames);
                SafeReadUInt32(il + kIL_dwOnlineActive_Off,           &s.il_online_active);
                SafeReadUInt8 (il + kIL_bDoubleTickGuard_Off,         &s.il_double_tick_guard);
                SafeReadInt32 (il + kIL_nDrainCursor_Off,             &s.il_drain_cursor);
                SafeReadInt32 (il + kIL_nMinStoreFrameIndex_Off,      &s.il_min_store_idx);
            }

            // Chara-level replay fields (NOT in HgCpuDirect range).
            const uintptr_t base = NativeBinding::imageBase();
            if (base)
            {
                for (int pi = 0; pi < 2; ++pi)
                {
                    const uintptr_t slot_rva =
                        (pi == 0) ? kRVA_CharaSlotP1 : kRVA_CharaSlotP2;
                    void* chara_raw = nullptr;
                    if (!SafeReadPtr(reinterpret_cast<const void*>(base + slot_rva),
                                     &chara_raw) || !chara_raw)
                        continue;
                    uint8_t* c = reinterpret_cast<uint8_t*>(chara_raw);
                    uint32_t lookup = 0, enable_flag = 0, frame_offset = 0;
                    uint32_t frame_total = 0, frame_target = 0, cons_cursor = 0;
                    uint8_t  chara_mode = 0;
                    SafeReadUInt32(c + kChara_dwReplayLookupKey_Off,     &lookup);
                    SafeReadUInt32(c + kChara_dwReplayEnableFlag_Off,    &enable_flag);
                    SafeReadUInt32(c + kChara_dwReplayFrameOffset_Off,   &frame_offset);
                    SafeReadUInt32(c + kChara_dwReplayFrameTotal_Off,    &frame_total);
                    SafeReadUInt32(c + kChara_dwReplayFrameTarget_Off,   &frame_target);
                    SafeReadUInt32(c + kChara_dwReplayConsumerCursor_Off, &cons_cursor);
                    SafeReadUInt8 (c + kChara_bCharaMode_Off,             &chara_mode);

                    if (pi == 0) {
                        s.p1_replay_lookup_key   = lookup;
                        s.p1_replay_enable_flag  = enable_flag;
                        s.p1_replay_frame_offset = frame_offset;
                        s.p1_replay_frame_total  = frame_total;
                        s.p1_replay_frame_target = frame_target;
                        s.p1_replay_consumer_cursor = cons_cursor;
                        s.p1_chara_mode = chara_mode;
                    } else {
                        s.p2_replay_lookup_key   = lookup;
                        s.p2_replay_enable_flag  = enable_flag;
                        s.p2_replay_frame_offset = frame_offset;
                        s.p2_replay_frame_total  = frame_total;
                        s.p2_replay_frame_target = frame_target;
                        s.p2_replay_consumer_cursor = cons_cursor;
                        s.p2_chara_mode = chara_mode;
                    }
                }
            }

            s.readable = true;
            return s;
        }

        inline void log_replay_driver(const char* label,
                                      const ReplayDriverSnap& s) noexcept
        {
            if (!s.readable)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.diag] {} ReplayDriver=null (BM not resolvable)\n"),
                    RC::to_generic_string(label));
                return;
            }
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] {} BM[movestate=0x{:X} status=0x{:X} "
                    "lastFID={} lastApplied={} frameAdv={}] "
                    "IL[ptr=0x{:X} fwdrev=0x{:X} bEnable={} cursor={} "
                    "lastFID={} master={} recBuf=0x{:X} total={} "
                    "online={} doubleTick={} drain={} minStore={}]\n"),
                RC::to_generic_string(label),
                static_cast<unsigned>(s.bm_move_state),
                static_cast<unsigned>(s.bm_status),
                s.bm_last_frame_id, s.bm_last_applied, s.bm_frame_advance,
                s.il_ptr, s.il_fwdrev_bits,
                static_cast<unsigned>(s.il_b_enable),
                s.il_playback_cursor, s.il_last_frame_id, s.il_master_clock,
                s.il_recorded_buf, s.il_total_frames,
                s.il_online_active,
                static_cast<unsigned>(s.il_double_tick_guard),
                s.il_drain_cursor, s.il_min_store_idx);

            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] {} CharaReplay "
                    "P1[lookup=0x{:X} enable={} fOff={} fTot={} fTgt={} "
                    "consCur={} mode=0x{:X}] "
                    "P2[lookup=0x{:X} enable={} fOff={} fTot={} fTgt={} "
                    "consCur={} mode=0x{:X}]\n"),
                RC::to_generic_string(label),
                s.p1_replay_lookup_key,  s.p1_replay_enable_flag,
                s.p1_replay_frame_offset, s.p1_replay_frame_total,
                s.p1_replay_frame_target, s.p1_replay_consumer_cursor,
                static_cast<unsigned>(s.p1_chara_mode),
                s.p2_replay_lookup_key,  s.p2_replay_enable_flag,
                s.p2_replay_frame_offset, s.p2_replay_frame_total,
                s.p2_replay_frame_target, s.p2_replay_consumer_cursor,
                static_cast<unsigned>(s.p2_chara_mode));
        }

        // Full one-shot dump combining UDemoNetDriver + ALuxBattleReplayPlayer
        // + both chara MoveVM snapshots + latest engine input.  Called from
        // ReplayScrub at:
        //   * BASELINE periodic dumps (every 60 wall frames)
        //   * PRE_SEEK / POST_SEEK pair
        //   * post-seek countdown ticks (every frame for N frames after
        //     a seek so we can see the engine ticking forward)
        //   * on-demand via the "Force diagnostic dump" UI button
        inline void dump_full(const char* label) noexcept
        {
            DemoNetDriverSnap d = read_demo_net_driver();
            log_demo_net_driver(label, d);
            ReplayPlayerSnap r = read_replay_player();
            log_replay_player(label, r);
            LatestEngineInputSnap ei = read_latest_engine_input();
            log_latest_engine_input(label, ei);
            ReplayDriverSnap rd = read_replay_driver();
            log_replay_driver(label, rd);
            for (int pi = 0; pi < 2; ++pi)
            {
                CharaMoveVmSnap c = read_chara_movevm(pi);
                log_chara_movevm(label, pi, c);
            }
        }
    } // namespace ReplayScrubDiag
} // namespace Horse
