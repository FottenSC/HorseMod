// ============================================================================
// Horse::RollbackStockTransportObserveHook
//
// Observe-only detours for the native SC6 online transport acquisition, stock
// send entry points, and receive enqueue boundary. These hooks never modify
// payloads or route packets; they only record counters/pointers for rollback
// live-peer integration work.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
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
        std::mutex m_mutex;
        RollbackStockTransportObserveTracker m_tracker {};
    };
}
