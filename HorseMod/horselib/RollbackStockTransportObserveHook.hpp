// ============================================================================
// Horse::RollbackStockTransportObserveHook
//
// Observe-only in normal operation. The explicitly enabled stock diagnostic
// script may write native send-cache cells, but never routes packets or claims
// those write/readbacks were consumed by gameplay.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "RollbackFrameStamp.hpp"
#include "RollbackInputCacheAdapter.hpp"
#include "RollbackInputLogProbe.hpp"
#include "RollbackReplayInputScript.hpp"
#include "RollbackStockTransportObserveModel.hpp"
#include "SafeMemoryRead.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Horse
{
    static constexpr uintptr_t kRollbackRVA_AcquireOnlineTransportSession =
        0x3F0CC0;
    static constexpr uintptr_t kRollbackRVA_SendInputOpcode0 = 0x3F84E0;
    static constexpr uintptr_t kRollbackRVA_SendInputOpcode1 = 0x3F8710;
    static constexpr uintptr_t kRollbackRVA_BattleSyncRequestStage = 0x51DBC0;
    static constexpr uintptr_t kRollbackRVA_ReceiveInputEnqueue = 0x3F4BE0;

    class RollbackStockTransportObserveHook
    {
    public:
        static RollbackStockTransportObserveHook& instance() noexcept
        {
            static RollbackStockTransportObserveHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire))
                return true;

            uintptr_t image_base = NativeBinding::imageBase();
            if (!image_base)
            {
                HMODULE module = ::GetModuleHandleW(L"SoulcaliburVI.exe");
                image_base = reinterpret_cast<uintptr_t>(module);
            }
            if (!image_base)
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[RollbackStockTransportObserveHook] NativeBinding "
                    "image base not resolved - cannot install\n"));
                return false;
            }

            m_acquire_trampoline = 0;
            m_opcode0_trampoline = 0;
            m_opcode1_trampoline = 0;
            m_battle_sync_trampoline = 0;
            m_receive_enqueue_trampoline = 0;

            if (!install_one(
                    m_acquire_detour,
                    m_acquire_trampoline,
                    image_base + kRollbackRVA_AcquireOnlineTransportSession,
                    reinterpret_cast<uint64_t>(&detour_acquire),
                    "AcquireOnlineTransportSession"))
            {
                return false;
            }
            if (!install_one(
                    m_opcode0_detour,
                    m_opcode0_trampoline,
                    image_base + kRollbackRVA_SendInputOpcode0,
                    reinterpret_cast<uint64_t>(&detour_opcode0),
                    "SendInputOpcode0"))
            {
                uninstall();
                return false;
            }
            if (!install_one(
                    m_opcode1_detour,
                    m_opcode1_trampoline,
                    image_base + kRollbackRVA_SendInputOpcode1,
                    reinterpret_cast<uint64_t>(&detour_opcode1),
                    "SendInputOpcode1"))
            {
                uninstall();
                return false;
            }
            if (!install_one(
                    m_battle_sync_detour,
                    m_battle_sync_trampoline,
                    image_base + kRollbackRVA_BattleSyncRequestStage,
                    reinterpret_cast<uint64_t>(&detour_battle_sync_request_stage),
                    "BattleSyncRequestStage"))
            {
                uninstall();
                return false;
            }
            if (!install_one(
                    m_receive_enqueue_detour,
                    m_receive_enqueue_trampoline,
                    image_base + kRollbackRVA_ReceiveInputEnqueue,
                    reinterpret_cast<uint64_t>(&detour_receive_enqueue),
                    "ReceiveInputEnqueue"))
            {
                uninstall();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[RollbackStockTransportObserveHook] installed acquire=0x{:X} "
                "opcode0=0x{:X} opcode1=0x{:X} battle_sync=0x{:X} "
                "recv_enqueue=0x{:X}\n"),
                image_base + kRollbackRVA_AcquireOnlineTransportSession,
                image_base + kRollbackRVA_SendInputOpcode0,
                image_base + kRollbackRVA_SendInputOpcode1,
                image_base + kRollbackRVA_BattleSyncRequestStage,
                image_base + kRollbackRVA_ReceiveInputEnqueue);
            return true;
        }

        void uninstall()
        {
            m_trace_enabled.store(false, std::memory_order_release);
            if (m_receive_enqueue_detour)
            {
                m_receive_enqueue_detour->unHook();
                m_receive_enqueue_detour.reset();
            }
            if (m_battle_sync_detour)
            {
                m_battle_sync_detour->unHook();
                m_battle_sync_detour.reset();
            }
            if (m_opcode1_detour)
            {
                m_opcode1_detour->unHook();
                m_opcode1_detour.reset();
            }
            if (m_opcode0_detour)
            {
                m_opcode0_detour->unHook();
                m_opcode0_detour.reset();
            }
            if (m_acquire_detour)
            {
                m_acquire_detour->unHook();
                m_acquire_detour.reset();
            }
            m_acquire_trampoline = 0;
            m_opcode0_trampoline = 0;
            m_opcode1_trampoline = 0;
            m_battle_sync_trampoline = 0;
            m_receive_enqueue_trampoline = 0;
            m_installed.store(false, std::memory_order_release);
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
                mark_tracker_hooks_locked();
                m_tracker.mark_trace_active(true);
            }
            m_trace_enabled.store(true, std::memory_order_release);
        }

        void end_trace() noexcept
        {
            m_trace_enabled.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> lock(m_mutex);
            mark_tracker_hooks_locked();
            m_tracker.mark_trace_active(false);
        }

        RollbackStockTransportObserveReport report() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            mark_tracker_hooks_locked();
            return m_tracker.report();
        }

        void begin_replay_input_script(
            uint32_t local_player_slot,
            const std::vector<uint32_t>& inputs,
            uint64_t input_hash) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_script_inputs = inputs;
            m_script_report = {};
            m_script_report.enabled = true;
            m_script_report.hooks_installed = installed();
            m_script_report.script_available = !m_script_inputs.empty();
            m_script_report.local_player_slot = local_player_slot;
            m_script_report.script_frames = m_script_inputs.size();
            m_script_report.input_hash = input_hash;
            m_script_report.failure =
                m_script_inputs.empty() ? "script-empty" : "waiting";
            m_script_base_frame = 0;
            m_script_base_set = false;
            ResetRollbackReplayInputScriptEpoch();
            m_cache_write_readback_hash = 1469598103934665603ull;
            m_cache_write_readback_count = 0;
            m_script_enabled.store(
                local_player_slot < 2 && !m_script_inputs.empty(),
                std::memory_order_release);
        }

        void end_replay_input_script() noexcept
        {
            m_script_enabled.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_script_report.enabled = false;
            m_script_inputs.clear();
            m_script_base_set = false;
        }

        RollbackReplayInputInjectionReport
        replay_input_script_report() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_script_report.hooks_installed = installed();
            m_script_report.enabled =
                m_script_enabled.load(std::memory_order_acquire);
            m_script_report.script_available = !m_script_inputs.empty();
            m_script_report.injected =
                m_script_report.send_cache_written;
            if (m_script_report.injected)
                m_script_report.failure = "ok";
            return m_script_report;
        }

    private:
        RollbackStockTransportObserveHook() = default;
        ~RollbackStockTransportObserveHook() { uninstall(); }
        RollbackStockTransportObserveHook(
            const RollbackStockTransportObserveHook&) = delete;
        RollbackStockTransportObserveHook& operator=(
            const RollbackStockTransportObserveHook&) = delete;

        static void* __fastcall detour_acquire(void* pOutSession)
        {
            auto& self = instance();
            using Fn = void*(__fastcall*)(void*);
            Fn orig = reinterpret_cast<Fn>(self.m_acquire_trampoline);
            void* ret = orig ? orig(pOutSession) : pOutSession;
            if (self.m_trace_enabled.load(std::memory_order_acquire))
            {
                void* pSession = nullptr;
                void* pRefController = nullptr;
                void* pVtable = nullptr;
                if (pOutSession)
                {
                    auto* raw = static_cast<uint8_t*>(pOutSession);
                    (void)SafeReadPtr(raw, &pSession);
                    (void)SafeReadPtr(raw + 8, &pRefController);
                    if (pSession)
                        (void)SafeReadPtr(pSession, &pVtable);
                }
                std::lock_guard<std::mutex> lock(self.m_mutex);
                self.m_tracker.record_acquire(
                    ::GetCurrentThreadId(),
                    reinterpret_cast<uintptr_t>(pOutSession),
                    reinterpret_cast<uintptr_t>(pSession),
                    reinterpret_cast<uintptr_t>(pRefController),
                    reinterpret_cast<uintptr_t>(pVtable));
            }
            return ret;
        }

        static void __fastcall detour_opcode0(
            void* pInputLog,
            uint8_t bInputByte,
            int32_t nFrameID,
            uint64_t qwUnused2)
        {
            auto& self = instance();
            uint32_t scripted_input = bInputByte;
            uint32_t absolute_frame = 0;
            const bool absolute_frame_valid =
                resolve_opcode0_absolute_frame(
                    pInputLog, nFrameID, absolute_frame);
            if (self.m_script_enabled.load(std::memory_order_acquire))
            {
                std::lock_guard<std::mutex> lock(self.m_mutex);
                if (absolute_frame_valid
                    && self.script_input_for_frame_locked(
                        absolute_frame, scripted_input))
                {
                    bInputByte = static_cast<uint8_t>(scripted_input);
                }
            }
            if (self.m_trace_enabled.load(std::memory_order_acquire))
            {
                std::lock_guard<std::mutex> lock(self.m_mutex);
                self.m_tracker.record_opcode0(
                    ::GetCurrentThreadId(),
                    reinterpret_cast<uintptr_t>(pInputLog),
                    bInputByte,
                    nFrameID);
            }
            using Fn = void(__fastcall*)(void*, uint8_t, int32_t, uint64_t);
            Fn orig = reinterpret_cast<Fn>(self.m_opcode0_trampoline);
            if (orig)
                orig(pInputLog, bInputByte, nFrameID, qwUnused2);
            self.apply_replay_script_opcode0(
                pInputLog, absolute_frame, absolute_frame_valid,
                scripted_input);
        }

        static void __fastcall detour_opcode1(
            void* pInputLog,
            uint32_t dwSlotBitmask,
            int32_t nFrameID,
            int32_t nCurrentFrame,
            int32_t nWindowFrames,
            uint32_t dwResendCounter)
        {
            auto& self = instance();
            self.apply_replay_script_opcode1(
                pInputLog, dwSlotBitmask, nFrameID, nCurrentFrame,
                nWindowFrames);
            if (self.m_trace_enabled.load(std::memory_order_acquire))
            {
                std::lock_guard<std::mutex> lock(self.m_mutex);
                self.m_tracker.record_opcode1(
                    ::GetCurrentThreadId(),
                    reinterpret_cast<uintptr_t>(pInputLog),
                    dwSlotBitmask,
                    nFrameID,
                    nCurrentFrame,
                    nWindowFrames,
                    dwResendCounter);
            }
            using Fn = void(__fastcall*)(
                void*, uint32_t, int32_t, int32_t, int32_t, uint32_t);
            Fn orig = reinterpret_cast<Fn>(self.m_opcode1_trampoline);
            if (orig)
            {
                orig(pInputLog, dwSlotBitmask, nFrameID, nCurrentFrame,
                     nWindowFrames, dwResendCounter);
            }
        }

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

        static bool write_cache_entry(
            uintptr_t pInputLog,
            uint32_t dwPlayerIdx,
            int32_t nFrameID,
            uint32_t dwFrameIndex,
            uint32_t dwInputValue) noexcept
        {
            if (!pInputLog || dwPlayerIdx >= 2)
                return false;
            FLuxReplayInputCacheEntry_Model entry {};
            entry.nFrameID = nFrameID;
            entry.dwFrameIndex = dwFrameIndex;
            entry.dwInputValue = dwInputValue;
            entry.bFilled = 1;
            const uintptr_t entry_addr =
                cache_entry_address(pInputLog, dwPlayerIdx, dwFrameIndex);
            return SafeWriteBytes(
                reinterpret_cast<void*>(entry_addr),
                &entry,
                sizeof(entry));
        }

        static bool read_cache_entry(
            uintptr_t pInputLog,
            uint32_t dwPlayerIdx,
            uint32_t dwFrameIndex,
            FLuxReplayInputCacheEntry_Model& out) noexcept
        {
            out = {};
            if (!pInputLog || dwPlayerIdx >= 2) return false;
            return SafeReadBytes(
                reinterpret_cast<const void*>(cache_entry_address(
                    pInputLog, dwPlayerIdx, dwFrameIndex)),
                &out,
                sizeof(out));
        }

        static bool read_input_log_last_frame_id(
            void* pInputLog,
            int32_t& nLastFrameId) noexcept
        {
            nLastFrameId = 0;
            if (!pInputLog) return false;
            return SafeReadBytes(
                static_cast<const uint8_t*>(pInputLog)
                    + kRollbackIL_nLastFrameID_Off,
                &nLastFrameId,
                sizeof(nLastFrameId));
        }

        static bool resolve_opcode0_absolute_frame(
            void* pInputLog,
            int32_t nFrameID,
            uint32_t& absoluteFrame) noexcept
        {
            absoluteFrame = 0;
            if (nFrameID >= 0)
            {
                absoluteFrame = static_cast<uint32_t>(nFrameID);
                return true;
            }
            int32_t nLastFrameId = 0;
            if (!read_input_log_last_frame_id(pInputLog, nLastFrameId)
                || nLastFrameId < 0)
                return false;
            absoluteFrame = static_cast<uint32_t>(nLastFrameId);
            return true;
        }

        bool script_input_for_frame_locked(
            uint32_t frame,
            uint32_t& input) noexcept
        {
            // Opcode 1 owns the shared absolute epoch because only its native
            // window supplies currentFrame-window. Opcode 0 performs exact
            // frame lookup only after that epoch exists.
            if (!m_script_base_set) return false;
            if (RollbackFrameIsBefore(frame, m_script_base_frame))
                return false;
            const uint32_t index = RollbackFrameDistance(
                frame, m_script_base_frame);
            if (index >= m_script_inputs.size()) return false;
            input = m_script_inputs[index];
            return true;
        }

        void record_cache_write_readback_locked(uint32_t input) noexcept
        {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&input);
            for (size_t i = 0; i < sizeof(input); ++i)
            {
                m_cache_write_readback_hash ^= bytes[i];
                m_cache_write_readback_hash *= 1099511628211ull;
            }
            ++m_cache_write_readback_count;
            m_script_report.cache_write_readback_hash =
                m_cache_write_readback_hash;
            m_script_report.cache_write_readbacks =
                m_cache_write_readback_count;
            m_script_report.cache_write_readback_ok = true;
        }

        void apply_replay_script_opcode0(
            void* pInputLog,
            uint32_t absoluteFrame,
            bool absoluteFrameValid,
            uint32_t scripted_input) noexcept
        {
            if (!m_script_enabled.load(std::memory_order_acquire)
                || !pInputLog || !absoluteFrameValid)
                return;
            std::lock_guard<std::mutex> lock(m_mutex);
            uint32_t exact = 0;
            if (!script_input_for_frame_locked(absoluteFrame, exact)) return;
            int32_t nLastFrameId = 0;
            if (!read_input_log_last_frame_id(pInputLog, nLastFrameId))
                return;
            const uint32_t slot = m_script_report.local_player_slot;
            if (!write_cache_entry(
                    reinterpret_cast<uintptr_t>(pInputLog),
                    slot, nLastFrameId, absoluteFrame, exact))
                return;
            FLuxReplayInputCacheEntry_Model applied {};
            if (read_cache_entry(
                    reinterpret_cast<uintptr_t>(pInputLog),
                    slot, absoluteFrame, applied)
                && applied.bFilled
                && applied.nFrameID == nLastFrameId
                && applied.dwFrameIndex == absoluteFrame)
            {
                record_cache_write_readback_locked(applied.dwInputValue);
                if (m_cache_write_readback_count == 1)
                    m_script_report.first_injected_frame = absoluteFrame;
                m_script_report.last_injected_frame = absoluteFrame;
            }
            (void)scripted_input;
        }

        void apply_replay_script_opcode1(
            void* pInputLog,
            uint32_t dwSlotBitmask,
            int32_t nFrameID,
            int32_t nCurrentFrame,
            int32_t nWindowFrames) noexcept
        {
            if (!m_script_enabled.load(std::memory_order_acquire))
                return;
            if (!pInputLog || nCurrentFrame < 0 || nWindowFrames <= 0)
                return;

            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_script_inputs.empty() ||
                m_script_report.local_player_slot >= 2)
            {
                m_script_report.failure = "script-empty";
                return;
            }
            int32_t nLastFrameId = 0;
            if (!read_input_log_last_frame_id(pInputLog, nLastFrameId))
            {
                m_script_report.failure = "input-log-last-frame-unreadable";
                return;
            }
            const uint32_t slot = m_script_report.local_player_slot;
            if ((dwSlotBitmask & (1u << slot)) == 0)
            {
                m_script_report.failure = "waiting-for-local-slot-window";
                return;
            }

            const int64_t start64 =
                static_cast<int64_t>(nCurrentFrame)
                - static_cast<int64_t>(nWindowFrames);
            const uint32_t start_frame =
                static_cast<uint32_t>(start64 < 0 ? 0 : start64);
            if (!m_script_base_set)
            {
                if (!EstablishRollbackReplayInputScriptEpoch(start_frame)
                    || !GetRollbackReplayInputScriptEpoch(
                        m_script_base_frame))
                {
                    m_script_report.failure =
                        "script-epoch-establish-failed";
                    return;
                }
                m_script_base_set = true;
                m_script_report.script_epoch = m_script_base_frame;
                m_script_report.script_epoch_set = true;
            }

            uint64_t writes = 0;
            const uint32_t count =
                static_cast<uint32_t>(
                    std::min<int32_t>(nWindowFrames, 120));
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t frame = start_frame + i;
                uint32_t input = 0;
                if (!script_input_for_frame_locked(frame, input))
                    break;
                if (write_cache_entry(
                        reinterpret_cast<uintptr_t>(pInputLog),
                        slot,
                        nLastFrameId,
                        frame,
                        input))
                {
                    ++writes;
                    if (m_script_report.first_injected_frame == 0)
                        m_script_report.first_injected_frame = frame;
                    m_script_report.last_injected_frame = frame;
                    FLuxReplayInputCacheEntry_Model applied {};
                    if (read_cache_entry(
                            reinterpret_cast<uintptr_t>(pInputLog),
                            slot, frame, applied)
                        && applied.bFilled
                        && applied.nFrameID == nLastFrameId
                        && applied.dwFrameIndex == frame)
                    {
                        record_cache_write_readback_locked(
                            applied.dwInputValue);
                    }
                }
            }

            ++m_script_report.send_windows;
            m_script_report.send_cache_writes += writes;
            m_script_report.send_cache_written =
                m_script_report.send_cache_writes > 0;
            m_script_report.injected =
                m_script_report.send_cache_written;
            m_script_report.failure =
                writes > 0 ? "ok" : "script-window-out-of-range";
            (void)nFrameID;
        }

        static void __fastcall detour_battle_sync_request_stage()
        {
            auto& self = instance();
            if (self.m_trace_enabled.load(std::memory_order_acquire))
            {
                std::lock_guard<std::mutex> lock(self.m_mutex);
                self.m_tracker.record_battle_sync_request_stage(
                    ::GetCurrentThreadId());
            }
            using Fn = void(__fastcall*)();
            Fn orig = reinterpret_cast<Fn>(self.m_battle_sync_trampoline);
            if (orig) orig();
        }

        static void __fastcall detour_receive_enqueue(
            void* pInputLog,
            uint8_t bUnusedFlag,
            void* pPacketWrapper)
        {
            auto& self = instance();
            if (self.m_trace_enabled.load(std::memory_order_acquire))
            {
                std::lock_guard<std::mutex> lock(self.m_mutex);
                self.m_tracker.record_receive_enqueue(
                    ::GetCurrentThreadId(),
                    reinterpret_cast<uintptr_t>(pInputLog),
                    bUnusedFlag,
                    reinterpret_cast<uintptr_t>(pPacketWrapper));
            }
            using Fn = void(__fastcall*)(void*, uint8_t, void*);
            Fn orig = reinterpret_cast<Fn>(self.m_receive_enqueue_trampoline);
            if (orig)
                orig(pInputLog, bUnusedFlag, pPacketWrapper);
        }

        bool install_one(
            std::unique_ptr<PLH::x64Detour>& detour,
            uint64_t& trampoline,
            uintptr_t target,
            uint64_t replacement,
            const char* label)
        {
            detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target), replacement, &trampoline);
            if (detour->hook())
                return true;
            RC::Output::send<RC::LogLevel::Error>(STR(
                "[RollbackStockTransportObserveHook] x64Detour::hook() "
                "failed on {} target=0x{:X}\n"),
                RC::to_generic_string(std::string(label ? label : "?")),
                target);
            detour.reset();
            trampoline = 0;
            return false;
        }

        void mark_tracker_hooks_locked() noexcept
        {
            m_tracker.mark_hooks(
                m_acquire_detour != nullptr,
                m_opcode0_detour != nullptr,
                m_opcode1_detour != nullptr,
                m_battle_sync_detour != nullptr,
                m_receive_enqueue_detour != nullptr);
        }

        std::unique_ptr<PLH::x64Detour> m_acquire_detour;
        std::unique_ptr<PLH::x64Detour> m_opcode0_detour;
        std::unique_ptr<PLH::x64Detour> m_opcode1_detour;
        std::unique_ptr<PLH::x64Detour> m_battle_sync_detour;
        std::unique_ptr<PLH::x64Detour> m_receive_enqueue_detour;
        uint64_t m_acquire_trampoline {0};
        uint64_t m_opcode0_trampoline {0};
        uint64_t m_opcode1_trampoline {0};
        uint64_t m_battle_sync_trampoline {0};
        uint64_t m_receive_enqueue_trampoline {0};
        std::atomic<bool> m_installed {false};
        std::atomic<bool> m_trace_enabled {false};
        std::atomic<bool> m_script_enabled {false};
        std::mutex m_mutex;
        RollbackStockTransportObserveTracker m_tracker {};
        std::vector<uint32_t> m_script_inputs {};
        uint32_t m_script_base_frame {0};
        bool m_script_base_set {false};
        uint64_t m_cache_write_readback_hash {1469598103934665603ull};
        uint64_t m_cache_write_readback_count {0};
        RollbackReplayInputInjectionReport m_script_report {};
    };
}
