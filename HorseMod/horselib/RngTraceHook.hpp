// ============================================================================
// Horse::RngTraceHook - diagnostic caller attribution for LuxMoveVM_GetRandU32.
//
// This is observability only. It records a fixed-size histogram of return
// addresses that called the shared battle LFSR RNG while ReplayScrub explicitly
// opens a diagnostic window. The hot path uses atomics and does not allocate.
// ============================================================================

#pragma once

#include "ReplayDebugTrace.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Horse
{
    class RngTraceHook
    {
    public:
        static RngTraceHook& instance() noexcept
        {
            static RngTraceHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            HMODULE mod = ::GetModuleHandleW(L"SoulcaliburVI.exe");
            if (!mod)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] SoulcaliburVI.exe unavailable\n"));
                return false;
            }

            const uintptr_t target = reinterpret_cast<uintptr_t>(mod)
                + kLuxMoveVM_GetRandU32_RVA;
            m_trampoline = 0;
            m_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(&RngTraceHook::detour),
                &m_trampoline);
            if (!m_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] x64Detour::hook failed target=0x{:X}\n"),
                    target);
                m_detour.reset();
                m_trampoline = 0;
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[Horse.RngTraceHook] installed target=0x{:X} detour=0x{:X} trampoline=0x{:X}\n"),
                target,
                reinterpret_cast<uintptr_t>(&RngTraceHook::detour),
                static_cast<uintptr_t>(m_trampoline));
            return true;
        }

        void uninstall()
        {
            set_active(false);
            if (!m_installed.exchange(false, std::memory_order_acq_rel))
                return;
            if (m_detour)
            {
                m_detour->unHook();
                m_detour.reset();
            }
            m_trampoline = 0;
        }

        void reset() noexcept
        {
            m_total.store(0, std::memory_order_release);
            m_overflow.store(0, std::memory_order_release);
            for (auto& s : m_slots)
            {
                s.addr.store(0, std::memory_order_release);
                s.count.store(0, std::memory_order_release);
            }
        }

        void begin_window() noexcept
        {
            if (!m_installed.load(std::memory_order_acquire)) return;
            reset();
            set_active(true);
        }

        void discard_window() noexcept
        {
            set_active(false);
            reset();
        }

        void end_window_and_emit(const char* phase,
                                 const char* label,
                                 int32_t requested_seq,
                                 int32_t seq,
                                 int32_t master,
                                 int32_t compare_seq = -1,
                                 int32_t compare_master = -1) noexcept
        {
            set_active(false);
            emit_and_reset(phase, label, requested_seq, seq, master,
                           compare_seq, compare_master);
        }

        void emit_and_reset(const char* phase,
                            const char* label,
                            int32_t requested_seq,
                            int32_t seq,
                            int32_t master,
                            int32_t compare_seq = -1,
                            int32_t compare_master = -1) noexcept
        {
            if (!ReplayDebugTrace::instance().enabled())
            {
                reset();
                return;
            }

            struct Entry
            {
                uintptr_t addr;
                uint32_t count;
            };
            std::vector<Entry> entries;
            entries.reserve(kMaxCallers);
            for (const auto& s : m_slots)
            {
                const uintptr_t addr = s.addr.load(std::memory_order_acquire);
                const uint32_t count = s.count.load(std::memory_order_acquire);
                if (addr && count) entries.push_back({addr, count});
            }
            std::sort(entries.begin(), entries.end(),
                      [](const Entry& a, const Entry& b)
                      {
                          if (a.count != b.count) return a.count > b.count;
                          return a.addr < b.addr;
                      });

            ReplayTraceFields f;
            f.string("phase", phase ? phase : "?")
             .string("label", label ? label : "?")
             .integer("requested_seq", requested_seq)
             .integer("seq", seq)
             .integer("master", master)
             .integer("compare_seq", compare_seq)
             .integer("compare_master", compare_master)
             .boolean("installed", m_installed.load(std::memory_order_acquire))
             .uinteger("total_calls", m_total.load(std::memory_order_acquire))
             .uinteger("overflow_calls", m_overflow.load(std::memory_order_acquire))
             .integer("caller_count", static_cast<int64_t>(entries.size()));

            const uintptr_t base = NativeBinding::imageBase();
            const size_t n = (std::min)(entries.size(), kEmitTopCallers);
            for (size_t i = 0; i < n; ++i)
            {
                char key[48]{};
                std::snprintf(key, sizeof(key), "caller%zu_rip", i);
                f.hex(key, entries[i].addr);
                std::snprintf(key, sizeof(key), "caller%zu_rva", i);
                f.hex(key, (base && entries[i].addr >= base)
                           ? entries[i].addr - base : 0);
                std::snprintf(key, sizeof(key), "caller%zu_count", i);
                f.uinteger(key, entries[i].count);
                const std::string fn =
                    ReplayDebugTrace::instance().format_absolute_rip(
                        entries[i].addr);
                if (!fn.empty())
                {
                    std::snprintf(key, sizeof(key), "caller%zu_fn", i);
                    f.string(key, fn);
                }
            }
            ReplayDebugTrace::instance().event("rng_u32_callers", f);
            reset();
        }

        bool installed() const noexcept
        {
            return m_installed.load(std::memory_order_acquire);
        }

        bool active() const noexcept
        {
            return m_active.load(std::memory_order_acquire);
        }

        uint64_t total_calls() const noexcept
        {
            return m_total.load(std::memory_order_acquire);
        }

    private:
        RngTraceHook() = default;
        ~RngTraceHook() { uninstall(); }
        RngTraceHook(const RngTraceHook&) = delete;
        RngTraceHook& operator=(const RngTraceHook&) = delete;

        using GetRandU32Fn = uint32_t(__fastcall*)();
        static uint32_t __fastcall detour()
        {
            RngTraceHook& self = instance();
            const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
            if (self.m_active.load(std::memory_order_acquire))
                self.record(caller);
            auto* original = reinterpret_cast<GetRandU32Fn>(self.m_trampoline);
            return original ? original() : 0;
        }

        void set_active(bool active) noexcept
        {
            m_active.store(active, std::memory_order_release);
        }

        void record(uintptr_t caller) noexcept
        {
            if (!caller) return;
            m_total.fetch_add(1, std::memory_order_relaxed);

            for (auto& s : m_slots)
            {
                const uintptr_t addr = s.addr.load(std::memory_order_acquire);
                if (addr == caller)
                {
                    s.count.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }

            for (auto& s : m_slots)
            {
                uintptr_t expected = 0;
                if (s.addr.compare_exchange_strong(
                        expected, caller,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    s.count.store(1, std::memory_order_release);
                    return;
                }
                if (expected == caller)
                {
                    s.count.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }

            m_overflow.fetch_add(1, std::memory_order_relaxed);
        }

        struct Slot
        {
            std::atomic<uintptr_t> addr{0};
            std::atomic<uint32_t> count{0};
        };

        static constexpr uintptr_t kLuxMoveVM_GetRandU32_RVA = 0x34F130;
        static constexpr size_t kMaxCallers = 64;
        static constexpr size_t kEmitTopCallers = 12;

        std::array<Slot, kMaxCallers> m_slots{};
        std::atomic<uint64_t> m_total{0};
        std::atomic<uint64_t> m_overflow{0};
        std::atomic<bool> m_active{false};
        std::atomic<bool> m_installed{false};
        uint64_t m_trampoline{0};
        std::unique_ptr<PLH::x64Detour> m_detour{};
    };
}
