// ============================================================================
// Horse::RollbackInputLogProbe
//
// Read-only cache ownership diagnostics for the rollback lab. This does not
// drive the stock InputLog/BM cache path; it proves whether the current direct
// PerFrameTick rollback probe leaves ALuxBattleFrameInputLog untouched.
// ============================================================================

#pragma once

#include "HorseLib.hpp"
#include "RollbackStateHash.hpp"
#include "RollbackStepHarness.hpp"
#include "SafeMemoryRead.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Horse
{
    static constexpr uintptr_t kRollbackBM_BattleFrameInputLog_Off = 0x478;
    static constexpr uintptr_t kRollbackIL_CaptureStart_Off = 0x394;
    static constexpr uintptr_t kRollbackIL_CaptureEnd_Off = 0x4418;
    static constexpr size_t kRollbackIL_CaptureBytes =
        kRollbackIL_CaptureEnd_Off - kRollbackIL_CaptureStart_Off;
    static constexpr uintptr_t kRollbackIL_nLastFrameID_Off = 0x3A0;
    static constexpr uintptr_t kRollbackIL_nMasterClock_Off = 0x3A4;
    static constexpr uintptr_t kRollbackIL_CurrentInputMirror_Off = 0x3B8;
    static constexpr uintptr_t kRollbackIL_InputCacheStart_Off = 0x3C0;
    static constexpr uintptr_t kRollbackIL_nDrainCursor_Off = 0x4410;

    struct RollbackInputLogWindowSnapshot
    {
        bool ok {false};
        uintptr_t battle_manager {0};
        uintptr_t input_log {0};
        uint32_t last_frame_id {0};
        uint32_t master_clock {0};
        uint32_t drain_cursor {0};
        uint32_t current_input[2] {};
        uint64_t full_hash {0};
        uint64_t cache_hash {0};
        std::vector<uint8_t> bytes;
        const char* failure {"not-run"};

        void clear()
        {
            ok = false;
            battle_manager = 0;
            input_log = 0;
            last_frame_id = 0;
            master_clock = 0;
            drain_cursor = 0;
            current_input[0] = 0;
            current_input[1] = 0;
            full_hash = 0;
            cache_hash = 0;
            bytes.clear();
            failure = "not-run";
        }
    };

    struct RollbackInputLogOwnershipReport
    {
        bool ok {false};
        bool context_ready {false};
        bool before_ok {false};
        bool after_ok {false};
        bool same_battle_manager {false};
        bool same_input_log {false};
        bool full_hash_match {false};
        bool cache_hash_match {false};
        bool current_input_match {false};
        bool master_clock_match {false};
        bool drain_cursor_match {false};
        bool warmup_ready {false};
        uint32_t min_master_clock {0};
        uint32_t warmup_master_clock {0};
        bool rollback_resim_ok {false};
        RollbackInputLogWindowSnapshot before {};
        RollbackInputLogWindowSnapshot after {};
        RollbackResimWindowReport resim {};
        const char* failure {"not-run"};
    };

    static inline bool CaptureRollbackInputLogWindow(
        RollbackInputLogWindowSnapshot& out) noexcept
    {
        out.clear();
        out.failure = "ok";

        RC::Unreal::UObject* bm_obj =
            RC::Unreal::UObjectGlobals::FindFirstOf(L"LuxBattleManager");
        if (!bm_obj)
        {
            out.failure = "battle-manager-not-found";
            return false;
        }

        auto* bm = reinterpret_cast<uint8_t*>(bm_obj);
        void* input_log_raw = nullptr;
        if (!SafeReadPtr(
                bm + kRollbackBM_BattleFrameInputLog_Off,
                &input_log_raw)
            || !input_log_raw)
        {
            out.failure = "input-log-not-found";
            return false;
        }

        auto* il = static_cast<uint8_t*>(input_log_raw);
        out.bytes.resize(kRollbackIL_CaptureBytes);
        if (!SafeReadBytes(
                il + kRollbackIL_CaptureStart_Off,
                out.bytes.data(),
                out.bytes.size()))
        {
            out.failure = "input-log-read-failed";
            out.bytes.clear();
            return false;
        }

        out.battle_manager = reinterpret_cast<uintptr_t>(bm_obj);
        out.input_log = reinterpret_cast<uintptr_t>(input_log_raw);
        (void)SafeReadUInt32(
            il + kRollbackIL_nLastFrameID_Off, &out.last_frame_id);
        (void)SafeReadUInt32(
            il + kRollbackIL_nMasterClock_Off, &out.master_clock);
        (void)SafeReadUInt32(
            il + kRollbackIL_nDrainCursor_Off, &out.drain_cursor);
        (void)SafeReadBytes(
            il + kRollbackIL_CurrentInputMirror_Off,
            out.current_input,
            sizeof(out.current_input));

        out.full_hash = RollbackHashBytes(
            out.bytes.data(), out.bytes.size());
        const size_t cache_rel =
            kRollbackIL_InputCacheStart_Off - kRollbackIL_CaptureStart_Off;
        constexpr size_t kInputCacheBytes = 0x4000;
        if (cache_rel + kInputCacheBytes <= out.bytes.size())
        {
            out.cache_hash = RollbackHashBytes(
                out.bytes.data() + cache_rel, kInputCacheBytes);
        }
        out.ok = out.full_hash != 0 && out.cache_hash != 0;
        return out.ok;
    }

    static inline RollbackInputLogOwnershipReport
    RunRollbackInputLogOwnershipProbe(
        const RollbackSnapshotManifest& manifest,
        uint32_t rollback_window,
        uint32_t seed,
        uint32_t min_master_clock = 0) noexcept
    {
        RollbackInputLogOwnershipReport report {};
        report.failure = "ok";
        report.min_master_clock = min_master_clock;

        report.before_ok = CaptureRollbackInputLogWindow(report.before);
        report.context_ready = report.before_ok;
        if (!report.before_ok)
        {
            report.failure = report.before.failure;
            return report;
        }
        report.warmup_master_clock = report.before.master_clock;
        report.warmup_ready =
            min_master_clock == 0
            || report.before.master_clock >= min_master_clock;
        if (!report.warmup_ready)
        {
            report.context_ready = false;
            report.failure = "input-log-warmup-pending";
            return report;
        }

        report.resim = RunRollbackResimWindowProbe(
            manifest, rollback_window, seed, true);
        report.context_ready = report.before_ok && report.resim.context_ready;
        report.rollback_resim_ok = report.resim.ok;
        if (!report.rollback_resim_ok)
        {
            report.failure = report.resim.failure;
            return report;
        }

        report.after_ok = CaptureRollbackInputLogWindow(report.after);
        if (!report.after_ok)
        {
            report.failure = report.after.failure;
            return report;
        }

        report.same_battle_manager =
            report.before.battle_manager != 0
            && report.before.battle_manager == report.after.battle_manager;
        report.same_input_log =
            report.before.input_log != 0
            && report.before.input_log == report.after.input_log;
        report.full_hash_match =
            report.before.full_hash != 0
            && report.before.full_hash == report.after.full_hash;
        report.cache_hash_match =
            report.before.cache_hash != 0
            && report.before.cache_hash == report.after.cache_hash;
        report.current_input_match =
            report.before.current_input[0] == report.after.current_input[0]
            && report.before.current_input[1] == report.after.current_input[1];
        report.master_clock_match =
            report.before.master_clock == report.after.master_clock;
        report.drain_cursor_match =
            report.before.drain_cursor == report.after.drain_cursor;

        report.ok =
            report.rollback_resim_ok
            && report.after_ok
            && report.same_battle_manager
            && report.same_input_log
            && report.full_hash_match
            && report.cache_hash_match
            && report.current_input_match
            && report.master_clock_match
            && report.drain_cursor_match;

        if (!report.ok)
        {
            if (!report.same_battle_manager)
                report.failure = "battle-manager-changed";
            else if (!report.same_input_log)
                report.failure = "input-log-changed";
            else if (!report.full_hash_match)
                report.failure = "input-log-window-mutated";
            else if (!report.cache_hash_match)
                report.failure = "input-cache-mutated";
            else if (!report.current_input_match)
                report.failure = "current-input-mirror-mutated";
            else if (!report.master_clock_match)
                report.failure = "master-clock-mutated";
            else if (!report.drain_cursor_match)
                report.failure = "drain-cursor-mutated";
            else
                report.failure = "cache-ownership-failed";
        }
        return report;
    }
}
