// ============================================================================
// Horse::RollbackLiveBoundaryHook
//
// Developer-only ordering proof for the live online input boundary. It observes
// the stock game-thread online drain and the round-cache consumer. Its cache
// injection/prediction probes are lab-only and restore touched cache/output
// state before the game observes the detour return.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "RollbackCacheInjection.hpp"
#include "RollbackInputCacheAdapter.hpp"
#include "RollbackInputLogProbe.hpp"
#include "RollbackLiveBoundary.hpp"
#include "SafeMemoryRead.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Horse
{
    static constexpr uintptr_t kRollbackRVA_LuxOnlineDrainInputPackets =
        0x3F6770;
    static constexpr uintptr_t kRollbackRVA_LuxBattleCharaConsumeInputCache =
        0x3FCD10;

    class RollbackLiveBoundaryHook
    {
    public:
        static RollbackLiveBoundaryHook& instance() noexcept
        {
            static RollbackLiveBoundaryHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire))
                return true;

            const uintptr_t image_base = NativeBinding::imageBase();
            if (!image_base)
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[RollbackLiveBoundaryHook] NativeBinding image base "
                    "not resolved - cannot install\n"));
                return false;
            }

            const uintptr_t drain_target =
                image_base + kRollbackRVA_LuxOnlineDrainInputPackets;
            const uintptr_t consumer_target =
                image_base + kRollbackRVA_LuxBattleCharaConsumeInputCache;

            m_drain_trampoline = 0;
            m_consumer_trampoline = 0;
            m_drain_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(drain_target),
                reinterpret_cast<uint64_t>(&RollbackLiveBoundaryHook::detour_drain),
                &m_drain_trampoline);
            if (!m_drain_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[RollbackLiveBoundaryHook] x64Detour::hook() failed "
                    "on stock drain target=0x{:X}\n"),
                    drain_target);
                m_drain_detour.reset();
                return false;
            }

            m_consumer_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(consumer_target),
                reinterpret_cast<uint64_t>(
                    &RollbackLiveBoundaryHook::detour_consumer),
                &m_consumer_trampoline);
            if (!m_consumer_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[RollbackLiveBoundaryHook] x64Detour::hook() failed "
                    "on cache consumer target=0x{:X}\n"),
                    consumer_target);
                m_drain_detour->unHook();
                m_drain_detour.reset();
                m_consumer_detour.reset();
                m_drain_trampoline = 0;
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[RollbackLiveBoundaryHook] installed drain=0x{:X} "
                "consumer=0x{:X}\n"),
                drain_target,
                consumer_target);
            return true;
        }

        void uninstall()
        {
            m_trace_enabled.store(false, std::memory_order_release);
            m_cache_probe_enabled.store(false, std::memory_order_release);
            if (!m_installed.exchange(false)) return;
            if (m_consumer_detour)
            {
                m_consumer_detour->unHook();
                m_consumer_detour.reset();
            }
            if (m_drain_detour)
            {
                m_drain_detour->unHook();
                m_drain_detour.reset();
            }
            m_consumer_trampoline = 0;
            m_drain_trampoline = 0;
        }

        bool installed() const noexcept
        {
            return m_installed.load(std::memory_order_acquire);
        }

        void begin_trace() noexcept
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_tracker.reset();
                m_tracker.mark_hooks_installed(installed());
                m_tracker.mark_trace_active(true);
            }
            m_trace_enabled.store(true, std::memory_order_release);
        }

        void end_trace() noexcept
        {
            m_trace_enabled.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tracker.mark_trace_active(false);
            m_tracker.mark_hooks_installed(installed());
        }

        RollbackLiveBoundaryReport report() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tracker.mark_hooks_installed(installed());
            m_tracker.mark_trace_active(
                m_trace_enabled.load(std::memory_order_acquire));
            return m_tracker.report();
        }

        void begin_cache_injection_probe() noexcept
        {
            begin_cache_probe(false);
        }

        void begin_cache_prediction_probe() noexcept
        {
            begin_cache_probe(true);
        }

        void begin_cache_probe(bool non_idempotent) noexcept
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_cache_report = {};
                m_cache_report.failure = "waiting";
                m_cache_report.hooks_installed = installed();
                m_cache_report.probe_active = true;
            }
            m_cache_probe_non_idempotent.store(
                non_idempotent, std::memory_order_release);
            m_cache_probe_claimed.store(false, std::memory_order_release);
            m_cache_probe_enabled.store(true, std::memory_order_release);
        }

        void end_cache_injection_probe() noexcept
        {
            m_cache_probe_enabled.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cache_report.probe_active = false;
            m_cache_report.hooks_installed = installed();
        }

        RollbackCacheInjectionReport cache_injection_report() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cache_report.hooks_installed = installed();
            m_cache_report.probe_active =
                m_cache_probe_enabled.load(std::memory_order_acquire);
            return m_cache_report;
        }

    private:
        RollbackLiveBoundaryHook() = default;
        ~RollbackLiveBoundaryHook() { uninstall(); }
        RollbackLiveBoundaryHook(const RollbackLiveBoundaryHook&) = delete;
        RollbackLiveBoundaryHook& operator=(
            const RollbackLiveBoundaryHook&) = delete;

        static void __fastcall detour_drain(void* pInputLog)
        {
            auto& self = instance();
            const bool active =
                self.m_trace_enabled.load(std::memory_order_acquire);
            if (active)
            {
                std::lock_guard<std::mutex> lock(self.m_mutex);
                self.m_tracker.on_drain_enter(
                    ::GetCurrentThreadId(),
                    reinterpret_cast<uintptr_t>(pInputLog));
            }

            using Fn = void(__fastcall*)(void*);
            Fn orig = reinterpret_cast<Fn>(self.m_drain_trampoline);
            if (orig) orig(pInputLog);

            if (active)
            {
                std::lock_guard<std::mutex> lock(self.m_mutex);
                self.m_tracker.on_drain_exit(
                    ::GetCurrentThreadId(),
                    reinterpret_cast<uintptr_t>(pInputLog));
            }
        }

        static void __fastcall detour_consumer(
            void* pBattleManager,
            uint32_t dwPlayerIdx,
            int nFramesBack,
            uint8_t bSuppressKeyDown,
            uint8_t bSuppressKeyPress)
        {
            auto& self = instance();
            CacheProbeFrame cache_probe =
                self.try_begin_cache_probe(
                    pBattleManager,
                    dwPlayerIdx,
                    nFramesBack,
                    bSuppressKeyDown,
                    bSuppressKeyPress);
            if (self.m_trace_enabled.load(std::memory_order_acquire))
            {
                uintptr_t input_log = 0;
                uint32_t master_clock = 0;
                int32_t cache_frame = -1;
                if (pBattleManager)
                {
                    void* pInputLogRaw = nullptr;
                    auto* bm = static_cast<uint8_t*>(pBattleManager);
                    if (SafeReadPtr(
                            bm + kRollbackBM_BattleFrameInputLog_Off,
                            &pInputLogRaw)
                        && pInputLogRaw)
                    {
                        input_log = reinterpret_cast<uintptr_t>(pInputLogRaw);
                        auto* il = static_cast<uint8_t*>(pInputLogRaw);
                        (void)SafeReadUInt32(
                            il + kRollbackIL_nMasterClock_Off,
                            &master_clock);
                        const int64_t frame =
                            static_cast<int64_t>(master_clock)
                            - static_cast<int64_t>(nFramesBack) - 1;
                        if (frame >= INT32_MIN && frame <= INT32_MAX)
                            cache_frame = static_cast<int32_t>(frame);
                    }
                }

                std::lock_guard<std::mutex> lock(self.m_mutex);
                self.m_tracker.on_cache_consumer(
                    ::GetCurrentThreadId(),
                    reinterpret_cast<uintptr_t>(pBattleManager),
                    input_log,
                    dwPlayerIdx,
                    nFramesBack,
                    master_clock,
                    cache_frame);
            }

            using Fn = void(__fastcall*)(void*, uint32_t, int, uint8_t, uint8_t);
            Fn orig = reinterpret_cast<Fn>(self.m_consumer_trampoline);
            if (orig)
            {
                orig(pBattleManager, dwPlayerIdx, nFramesBack,
                     bSuppressKeyDown, bSuppressKeyPress);
            }
            if (cache_probe.active)
                self.finish_cache_probe(cache_probe);
        }

        struct CacheProbeFrame
        {
            bool active {false};
            uint32_t dwPlayerIndex {0};
            uint32_t dwMasterClock {0};
            int32_t nFramesBack {0};
            int32_t nFrameIndex {0};
            int32_t nFrameID {0};
            uint32_t dwInputValue {0};
            uint32_t dwOriginalInputValue {0};
            uint32_t dwPrevInputValue {0};
            uint32_t dwForbiddenInputMask {0};
            bool non_idempotent {false};
            uintptr_t pBattleManager {0};
            uintptr_t pInputLog {0};
            uintptr_t pCacheEntry {0};
            uintptr_t pCurrentInputSlot {0};
            uintptr_t pInputPairSlot {0};
            FLuxReplayInputCacheEntry_Model original_entry {};
        };

        static uintptr_t cache_entry_address(
            uintptr_t pInputLog,
            uint32_t dwPlayerIdx,
            uint32_t dwFrameIndex) noexcept
        {
            const uintptr_t entry_index =
                static_cast<uintptr_t>(dwPlayerIdx) * 0x200u
                + static_cast<uintptr_t>(dwFrameIndex & 0x1FFu);
            return pInputLog + kRollbackIL_InputCacheStart_Off
                + entry_index * sizeof(FLuxReplayInputCacheEntry_Model);
        }

        CacheProbeFrame try_begin_cache_probe(
            void* pBattleManager,
            uint32_t dwPlayerIdx,
            int nFramesBack,
            uint8_t bSuppressKeyDown,
            uint8_t bSuppressKeyPress) noexcept
        {
            CacheProbeFrame ctx {};
            if (!m_cache_probe_enabled.load(std::memory_order_acquire))
                return ctx;
            const bool non_idempotent =
                m_cache_probe_non_idempotent.load(std::memory_order_acquire);
            if (non_idempotent
                && (bSuppressKeyDown != 0 || bSuppressKeyPress != 0))
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_cache_report.hooks_installed = installed();
                m_cache_report.probe_active = true;
                m_cache_report.failure = "waiting-unsuppressed-consumer";
                return ctx;
            }
            if (m_cache_probe_claimed.exchange(
                    true, std::memory_order_acq_rel))
                return ctx;

            RollbackCacheInjectionReport report {};
            report.hooks_installed = installed();
            report.probe_active = true;
            report.attempted = true;
            report.dwPlayerIndex = dwPlayerIdx;
            report.nFramesBack = nFramesBack;
            report.pBattleManager = reinterpret_cast<uintptr_t>(pBattleManager);
            report.failure = "ok";

            auto fail = [&](const char* reason) noexcept {
                report.failure = reason;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    report.invalid_context_count =
                        m_cache_report.invalid_context_count + 1;
                    m_cache_report = report;
                }
                m_cache_probe_enabled.store(false, std::memory_order_release);
                return CacheProbeFrame {};
            };

            if (!pBattleManager || dwPlayerIdx >= 2)
                return fail("invalid-consumer-context");

            auto* bm = static_cast<uint8_t*>(pBattleManager);
            void* pInputLogRaw = nullptr;
            if (!SafeReadPtr(
                    bm + kRollbackBM_BattleFrameInputLog_Off,
                    &pInputLogRaw)
                || !pInputLogRaw)
                return fail("input-log-not-found");

            auto* il = static_cast<uint8_t*>(pInputLogRaw);
            uint32_t dwMasterClock = 0;
            uint32_t dwFrameID = 0;
            if (!SafeReadUInt32(
                    il + kRollbackIL_nMasterClock_Off, &dwMasterClock)
                || !SafeReadUInt32(
                    il + kRollbackIL_nLastFrameID_Off, &dwFrameID))
                return fail("input-log-clock-read-failed");

            const int64_t llFrameIndex =
                static_cast<int64_t>(dwMasterClock)
                - static_cast<int64_t>(nFramesBack) - 1;
            if (llFrameIndex < 0 || llFrameIndex > INT32_MAX)
                return fail("invalid-cache-frame");
            const uint32_t dwFrameIndex =
                static_cast<uint32_t>(llFrameIndex);

            const uintptr_t entry_addr = cache_entry_address(
                reinterpret_cast<uintptr_t>(pInputLogRaw),
                dwPlayerIdx,
                dwFrameIndex);
            FLuxReplayInputCacheEntry_Model original {};
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(entry_addr),
                    &original,
                    sizeof(original)))
                return fail("cache-entry-read-failed");
            if (original.bFilled == 0
                || original.nFrameID != static_cast<int32_t>(dwFrameID)
                || original.dwFrameIndex != dwFrameIndex)
                return fail("source-cache-cell-not-current-frame");

            void* pCurrentInputArray = nullptr;
            if (!SafeReadPtr(bm + 0x1498, &pCurrentInputArray)
                || !pCurrentInputArray)
                return fail("current-input-array-not-found");
            const uintptr_t current_slot =
                reinterpret_cast<uintptr_t>(pCurrentInputArray)
                + static_cast<uintptr_t>(dwPlayerIdx) * sizeof(uint32_t);
            uint32_t dwPrevInput = 0;
            if (!SafeReadUInt32(
                    reinterpret_cast<const void*>(current_slot),
                    &dwPrevInput))
                return fail("current-input-read-failed");
            void* pInputPairArray = nullptr;
            if (!SafeReadPtr(bm + 0x14A8, &pInputPairArray)
                || !pInputPairArray)
                return fail("input-pair-array-not-found");
            const uintptr_t pair_slot =
                reinterpret_cast<uintptr_t>(pInputPairArray)
                + static_cast<uintptr_t>(dwPlayerIdx)
                    * sizeof(FLuxBattleInputPair_Model);
            uint32_t dwForbiddenInputMask = 0;
            if (!SafeReadUInt32(bm + 0x12F8, &dwForbiddenInputMask))
                return fail("forbidden-mask-read-failed");

            FLuxReplayInputCacheEntry_Model injected = original;
            if (non_idempotent)
                injected.dwInputValue ^= 0x1u;
            if (!SafeWriteBytes(
                    reinterpret_cast<void*>(entry_addr),
                    &injected,
                    sizeof(injected)))
                return fail("cache-entry-write-failed");

            report.context_ready = true;
            report.source_cell_valid = true;
            report.wrote_cache = true;
            report.idempotent_write = !non_idempotent;
            report.non_idempotent_write = non_idempotent;
            report.injected_differs_from_original =
                injected.dwInputValue != original.dwInputValue;
            report.dwMasterClock = dwMasterClock;
            report.dwForbiddenInputMask = dwForbiddenInputMask;
            report.nFrameIndex = static_cast<int32_t>(dwFrameIndex);
            report.nFrameID = static_cast<int32_t>(dwFrameID);
            report.dwOriginalInput = original.dwInputValue;
            report.dwInjectedInput = injected.dwInputValue;
            report.pInputLog = reinterpret_cast<uintptr_t>(pInputLogRaw);
            report.pCacheEntry = entry_addr;
            report.pCurrentInputSlot = current_slot;
            report.pInputPairSlot = pair_slot;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                report.invalid_context_count =
                    m_cache_report.invalid_context_count;
                m_cache_report = report;
            }

            ctx.active = true;
            ctx.dwPlayerIndex = dwPlayerIdx;
            ctx.dwMasterClock = dwMasterClock;
            ctx.nFramesBack = nFramesBack;
            ctx.nFrameIndex = static_cast<int32_t>(dwFrameIndex);
            ctx.nFrameID = static_cast<int32_t>(dwFrameID);
            ctx.dwInputValue = injected.dwInputValue;
            ctx.dwOriginalInputValue = original.dwInputValue;
            ctx.dwPrevInputValue = dwPrevInput;
            ctx.dwForbiddenInputMask = dwForbiddenInputMask;
            ctx.non_idempotent = non_idempotent;
            ctx.pBattleManager = reinterpret_cast<uintptr_t>(pBattleManager);
            ctx.pInputLog = reinterpret_cast<uintptr_t>(pInputLogRaw);
            ctx.pCacheEntry = entry_addr;
            ctx.pCurrentInputSlot = current_slot;
            ctx.pInputPairSlot = pair_slot;
            ctx.original_entry = original;
            return ctx;
        }

        static FLuxBattleInputPair_Model compute_unsuppressed_output_pair(
            uint32_t dwPrevInput,
            uint32_t dwCachedInput,
            uint32_t dwForbiddenInputMask,
            bool bNetworkEventMask) noexcept
        {
            FLuxBattleInputPair_Model out {};
            out.dwInputWord = dwCachedInput & ~dwForbiddenInputMask;
            out.dwFlags =
                ((dwPrevInput ^ dwCachedInput) & dwCachedInput)
                & ~dwForbiddenInputMask;
            if (bNetworkEventMask)
            {
                out.dwInputWord &= 0xFFFFC3F0u;
                out.dwFlags &= 0xFFFFC3F0u;
            }
            return out;
        }

        static bool input_pair_equal(
            const FLuxBattleInputPair_Model& a,
            const FLuxBattleInputPair_Model& b) noexcept
        {
            return a.dwInputWord == b.dwInputWord && a.dwFlags == b.dwFlags;
        }

        void finish_cache_probe(const CacheProbeFrame& ctx) noexcept
        {
            RollbackCacheInjectionReport report {};
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                report = m_cache_report;
            }

            uint32_t observed = 0;
            const bool observed_ok = SafeReadUInt32(
                reinterpret_cast<const void*>(ctx.pCurrentInputSlot),
                &observed);
            FLuxBattleInputPair_Model observed_pair {};
            bool observed_pair_ok = false;
            FLuxBattleInputPair_Model expected_injected_pair {};
            FLuxBattleInputPair_Model expected_restored_pair {};
            bool network_mask = false;
            if (ctx.non_idempotent)
            {
                observed_pair_ok = SafeReadBytes(
                    reinterpret_cast<const void*>(ctx.pInputPairSlot),
                    &observed_pair,
                    sizeof(observed_pair));
                const FLuxBattleInputPair_Model expected_no_mask =
                    compute_unsuppressed_output_pair(
                        ctx.dwPrevInputValue,
                        ctx.dwInputValue,
                        ctx.dwForbiddenInputMask,
                        false);
                const FLuxBattleInputPair_Model expected_with_mask =
                    compute_unsuppressed_output_pair(
                        ctx.dwPrevInputValue,
                        ctx.dwInputValue,
                        ctx.dwForbiddenInputMask,
                        true);
                const bool pair_matches_no_mask =
                    observed_pair_ok
                    && input_pair_equal(observed_pair, expected_no_mask);
                const bool pair_matches_network_mask =
                    observed_pair_ok
                    && input_pair_equal(observed_pair, expected_with_mask);
                network_mask =
                    !pair_matches_no_mask && pair_matches_network_mask;
                expected_injected_pair =
                    network_mask ? expected_with_mask : expected_no_mask;
                report.output_pair_observed_prediction =
                    pair_matches_no_mask || pair_matches_network_mask;
                report.network_event_mask_inferred = network_mask;
                report.observed_output_pair = observed_pair;
                report.expected_injected_output_pair = expected_injected_pair;
                expected_restored_pair =
                    compute_unsuppressed_output_pair(
                        ctx.dwPrevInputValue,
                        ctx.dwOriginalInputValue,
                        ctx.dwForbiddenInputMask,
                        network_mask);
                report.expected_restored_output_pair = expected_restored_pair;
            }
            const bool restored_cache = SafeWriteBytes(
                reinterpret_cast<void*>(ctx.pCacheEntry),
                &ctx.original_entry,
                sizeof(ctx.original_entry));
            bool restored_current = false;
            bool restored_pair = false;
            if (ctx.non_idempotent)
            {
                restored_current = SafeWriteBytes(
                    reinterpret_cast<void*>(ctx.pCurrentInputSlot),
                    &ctx.dwOriginalInputValue,
                    sizeof(ctx.dwOriginalInputValue));
                restored_pair = SafeWriteBytes(
                    reinterpret_cast<void*>(ctx.pInputPairSlot),
                    &expected_restored_pair,
                    sizeof(expected_restored_pair));
                uint32_t restored_input = 0;
                const bool restored_input_read = SafeReadUInt32(
                    reinterpret_cast<const void*>(ctx.pCurrentInputSlot),
                    &restored_input);
                FLuxBattleInputPair_Model restored_output {};
                const bool restored_pair_read = SafeReadBytes(
                    reinterpret_cast<const void*>(ctx.pInputPairSlot),
                    &restored_output,
                    sizeof(restored_output));
                report.dwRestoredCurrentInput = restored_input;
                report.restored_output_pair_value = restored_output;
                report.restored_current_input =
                    restored_current
                    && restored_input_read
                    && restored_input == ctx.dwOriginalInputValue;
                report.restored_output_pair =
                    restored_pair
                    && restored_pair_read
                    && input_pair_equal(
                        restored_output, expected_restored_pair);
            }

            report.dwObservedCurrentInput = observed;
            report.consumer_observed_cache =
                observed_ok && observed == ctx.dwInputValue;
            report.restored_cache = restored_cache;
            const bool common_ok =
                report.hooks_installed
                && report.context_ready
                && report.source_cell_valid
                && report.wrote_cache
                && report.consumer_observed_cache
                && report.restored_cache;
            report.ok =
                ctx.non_idempotent
                ? (common_ok
                   && report.non_idempotent_write
                   && report.injected_differs_from_original
                   && report.output_pair_observed_prediction
                   && report.restored_current_input
                   && report.restored_output_pair)
                : (common_ok && report.idempotent_write);
            if (!observed_ok)
                report.failure = "current-input-read-failed";
            else if (!report.consumer_observed_cache)
                report.failure = "consumer-did-not-observe-cache-write";
            else if (ctx.non_idempotent
                && !report.output_pair_observed_prediction)
                report.failure = "output-pair-did-not-observe-prediction";
            else if (!restored_cache)
                report.failure = "cache-entry-restore-failed";
            else if (ctx.non_idempotent
                && !report.restored_current_input)
                report.failure = "current-input-restore-failed";
            else if (ctx.non_idempotent
                && !report.restored_output_pair)
                report.failure = "output-pair-restore-failed";
            else
                report.failure = "ok";
            report.probe_active = false;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_cache_report = report;
            }
            m_cache_probe_enabled.store(false, std::memory_order_release);
        }

        std::unique_ptr<PLH::x64Detour> m_drain_detour;
        std::unique_ptr<PLH::x64Detour> m_consumer_detour;
        uint64_t m_drain_trampoline {0};
        uint64_t m_consumer_trampoline {0};
        std::atomic<bool> m_installed {false};
        std::atomic<bool> m_trace_enabled {false};
        std::atomic<bool> m_cache_probe_enabled {false};
        std::atomic<bool> m_cache_probe_claimed {false};
        std::atomic<bool> m_cache_probe_non_idempotent {false};
        std::mutex m_mutex;
        RollbackLiveBoundaryTracker m_tracker {};
        RollbackCacheInjectionReport m_cache_report {};
    };
}
