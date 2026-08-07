// ============================================================================
// Horse::RngTraceHook - caller attribution and rollback-owned battle CRT RNG.
//
// It records separate fixed-size histograms for
// the shared battle LFSR and gameplay xorshift96 generators, plus an ordered
// trace of the process UCRT rand/srand entry points, while a diagnostic
// window is open. Hooking the resolved UCRT targets, rather than only
// SoulcaliburVI.exe's IAT slots, is required to observe same-thread calls
// made through other modules/import paths. The hot paths use atomics and do
// not allocate. When explicitly configured by the production rollback
// runtime, it also virtualizes gameplay UCRT callers behind a snapshottable
// stream while leaving presentation callers on native thread-local UCRT.
// ============================================================================

#pragma once

#include "ReplayDebugTrace.hpp"
#include "PeImportSlotResolver.hpp"
#include "RollbackGameplayCrt.hpp"
#include "RollbackNativeSimulationIteration.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Horse
{
    class RngTraceHook
    {
    private:
        struct Slot
        {
            std::atomic<uintptr_t> addr{0};
            std::atomic<uint32_t> count{0};
        };

        struct CrtEventSlot
        {
            // Published last. UINT64_MAX means the remaining fields are not
            // a complete event for the current window.
            std::atomic<uint64_t> sequence{UINT64_MAX};
            std::atomic<uintptr_t> caller{0};
            std::atomic<uint32_t> thread_id{0};
            std::atomic<int32_t> value{0};
            std::atomic<uint8_t> kind{0};
        };

        struct CrtPersistentThreadSlot
        {
            std::atomic<uint32_t> thread_id{0};
            std::atomic<uint32_t> last_seed{0};
            std::atomic<uint32_t> predicted_state{0};
            std::atomic<uint64_t> draws_since_seed{0};
            std::atomic<uint64_t> seed_generation{0};
            std::atomic<uint64_t> prediction_mismatches{0};
            std::atomic<bool> seed_known{false};
        };

        class GameplayCrtLock
        {
        public:
            explicit GameplayCrtLock(std::atomic_flag& lock) noexcept
                : m_lock(lock)
            {
                while (m_lock.test_and_set(std::memory_order_acquire))
                    _mm_pause();
            }
            ~GameplayCrtLock() noexcept
            {
                m_lock.clear(std::memory_order_release);
            }
            GameplayCrtLock(const GameplayCrtLock&) = delete;
            GameplayCrtLock& operator=(const GameplayCrtLock&) = delete;

        private:
            std::atomic_flag& m_lock;
        };

    public:
        static constexpr size_t kMaxCallers = 64;
        static constexpr size_t kMaxCrtEvents = 256;
        static constexpr size_t kMaxCrtThreads = 16;

        struct CallerCount
        {
            uintptr_t address {0};
            uint32_t rva {0};
            uint32_t count {0};
        };

        struct HistogramSnapshot
        {
            uint64_t total_calls {0};
            uint64_t overflow_calls {0};
            uint32_t caller_count {0};
            std::array<CallerCount, kMaxCallers> callers {};
        };

        struct CrtSnapshot
        {
            HistogramSnapshot callers {};
            uint64_t seed_calls {0};
            uint64_t event_count {0};
            uint64_t overflow_events {0};
            uint64_t sequence_hash {1469598103934665603ull};
            uint64_t execution_hash {1469598103934665603ull};
            uint32_t last_seed {0};
            uint32_t thread_count {0};
            std::array<CallerCount, kMaxCrtThreads> threads {};
            bool persistent_thread_observed {false};
            bool persistent_seed_known {false};
            uint32_t persistent_last_seed {0};
            uint32_t persistent_predicted_state {0};
            uint32_t persistent_expected_next {0};
            uint64_t persistent_draws_since_seed {0};
            uint64_t persistent_seed_generation {0};
            uint64_t persistent_prediction_mismatches {0};
        };

        using GameplayCrtOwnsSimulationFn = bool(*)(void*) noexcept;
        using GameplayCrtPendingDebtFn = bool(*)(void*) noexcept;
        using GameplayCrtFatalFn = void(*)(
            void*, const char*, uint32_t) noexcept;

        enum class GameplayCrtRoutingMode : uint8_t
        {
            RollbackOwned,
            SplitControl,
        };

        bool configure_gameplay_crt(
            void* owner,
            GameplayCrtOwnsSimulationFn owns_simulation,
            GameplayCrtPendingDebtFn pending_debt,
            GameplayCrtFatalFn fatal,
            GameplayCrtRoutingMode mode =
                GameplayCrtRoutingMode::RollbackOwned) noexcept
        {
            if (!owner || !owns_simulation || !pending_debt || !fatal
                || !m_installed.load(std::memory_order_acquire))
                return false;
            GameplayCrtLock lock(m_gameplay_crt_lock);
            if (m_gameplay_crt_enabled.load(std::memory_order_acquire))
            {
                // Configuration is process-global because the detour is
                // process-global. Treat an identical request as idempotent,
                // but never let one lane reset another lane's live stream.
                return m_gameplay_crt_owner.load(std::memory_order_acquire)
                        == owner
                    && m_gameplay_crt_mode.load(std::memory_order_acquire)
                        == mode
                    && m_gameplay_crt_owns_simulation.load(
                        std::memory_order_acquire) == owns_simulation
                    && m_gameplay_crt_pending_debt.load(
                        std::memory_order_acquire) == pending_debt
                    && m_gameplay_crt_fatal.load(
                        std::memory_order_acquire) == fatal;
            }
            m_gameplay_crt.reset();
            m_gameplay_crt_owner.store(owner, std::memory_order_release);
            m_gameplay_crt_owns_simulation.store(
                owns_simulation, std::memory_order_release);
            m_gameplay_crt_pending_debt.store(
                pending_debt, std::memory_order_release);
            m_gameplay_crt_fatal.store(fatal, std::memory_order_release);
            m_gameplay_crt_mode.store(mode, std::memory_order_release);
            m_gameplay_crt_enabled.store(true, std::memory_order_release);
            return true;
        }

        bool disable_gameplay_crt(void* expected_owner) noexcept
        {
            if (!expected_owner) return false;
            GameplayCrtLock lock(m_gameplay_crt_lock);
            if (m_gameplay_crt_owner.load(std::memory_order_acquire)
                != expected_owner)
                return false;
            disable_gameplay_crt_locked();
            return true;
        }

        void disable_gameplay_crt() noexcept
        {
            GameplayCrtLock lock(m_gameplay_crt_lock);
            disable_gameplay_crt_locked();
        }

    private:
        void disable_gameplay_crt_locked() noexcept
        {
            m_gameplay_crt_enabled.store(false, std::memory_order_release);
            m_gameplay_crt_owner.store(nullptr, std::memory_order_release);
            m_gameplay_crt_owns_simulation.store(
                nullptr, std::memory_order_release);
            m_gameplay_crt_pending_debt.store(
                nullptr, std::memory_order_release);
            m_gameplay_crt_fatal.store(nullptr, std::memory_order_release);
            m_gameplay_crt_mode.store(
                GameplayCrtRoutingMode::RollbackOwned,
                std::memory_order_release);
            m_gameplay_crt.reset();
        }

    public:

        bool capture_gameplay_crt_state(
            RollbackGameplayCrtState& out) noexcept
        {
            if (!m_gameplay_crt_enabled.load(std::memory_order_acquire))
                return false;
            GameplayCrtLock lock(m_gameplay_crt_lock);
            out = m_gameplay_crt.state();
            return RollbackGameplayCrtStateIsCanonical(out);
        }

        bool restore_gameplay_crt_state(
            const RollbackGameplayCrtState& state) noexcept
        {
            if (!m_gameplay_crt_enabled.load(std::memory_order_acquire))
                return false;
            GameplayCrtLock lock(m_gameplay_crt_lock);
            return m_gameplay_crt.restore(state);
        }

        bool gameplay_crt_enabled() const noexcept
        {
            return m_gameplay_crt_enabled.load(std::memory_order_acquire);
        }

        bool gameplay_crt_configured_for(
            const void* owner,
            GameplayCrtRoutingMode mode) const noexcept
        {
            return owner
                && m_gameplay_crt_enabled.load(std::memory_order_acquire)
                && m_gameplay_crt_owner.load(std::memory_order_acquire)
                    == owner
                && m_gameplay_crt_mode.load(std::memory_order_acquire)
                    == mode;
        }

        static RngTraceHook& instance() noexcept
        {
            static RngTraceHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            reset_persistent_crt_threads();

            HMODULE mod = ::GetModuleHandleW(L"SoulcaliburVI.exe");
            if (!mod)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] SoulcaliburVI.exe unavailable\n"));
                return false;
            }
            if (!validate_rollback_crt_sites(
                    reinterpret_cast<uintptr_t>(mod)))
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] rollback CRT native-site signatures do not match this executable\n"));
                return false;
            }

            const uintptr_t target = reinterpret_cast<uintptr_t>(mod)
                + kLuxMoveVM_GetRandU32_RVA;
            m_trampoline = 0;
            m_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(&RngTraceHook::lfsr_detour),
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

            const uintptr_t xorshift_target =
                reinterpret_cast<uintptr_t>(mod)
                + kLuxMoveVM_GetRandXorshift96Gameplay_RVA;
            m_xorshift_trampoline = 0;
            m_xorshift_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(xorshift_target),
                reinterpret_cast<uint64_t>(
                    &RngTraceHook::xorshift96_detour),
                &m_xorshift_trampoline);
            if (!m_xorshift_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] xorshift96 detour failed target=0x{:X}\n"),
                    xorshift_target);
                m_xorshift_detour.reset();
                m_xorshift_trampoline = 0;
                m_detour->unHook();
                m_detour.reset();
                m_trampoline = 0;
                return false;
            }

            const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(
                mod);
            const auto* const nt = dos && dos->e_magic == IMAGE_DOS_SIGNATURE
                ? reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                    reinterpret_cast<const uint8_t*>(mod) + dos->e_lfanew)
                : nullptr;
            const size_t mapped_size = nt
                && nt->Signature == IMAGE_NT_SIGNATURE
                && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
                ? static_cast<size_t>(nt->OptionalHeader.SizeOfImage) : 0;
            const PeImportSlot rand_import = ResolvePeImportSlot(
                mod, mapped_size, kCrtUtilityImportDll, "rand");
            const PeImportSlot srand_import = ResolvePeImportSlot(
                mod, mapped_size, kCrtUtilityImportDll, "srand");
            if (!rand_import || !srand_import)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] CRT import lookup failed image_size={} rand_slot=0x{:X} srand_slot=0x{:X}\n"),
                    mapped_size,
                    reinterpret_cast<uintptr_t>(rand_import.slot),
                    reinterpret_cast<uintptr_t>(srand_import.slot));
                rollback_native_detours();
                clear_crt_detour_state();
                return false;
            }
            m_crt_rand_trampoline = 0;
            m_crt_rand_detour = std::make_unique<PLH::x64Detour>(
                reinterpret_cast<uint64_t>(rand_import.target),
                reinterpret_cast<uint64_t>(
                    &RngTraceHook::crt_rand_detour),
                &m_crt_rand_trampoline);
            if (!m_crt_rand_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] CRT rand target detour failed target=0x{:X}\n"),
                    reinterpret_cast<uintptr_t>(rand_import.target));
                m_crt_rand_detour.reset();
                m_crt_rand_trampoline = 0;
                rollback_native_detours();
                clear_crt_detour_state();
                return false;
            }
            m_crt_srand_trampoline = 0;
            m_crt_srand_detour = std::make_unique<PLH::x64Detour>(
                reinterpret_cast<uint64_t>(srand_import.target),
                reinterpret_cast<uint64_t>(
                    &RngTraceHook::crt_srand_detour),
                &m_crt_srand_trampoline);
            if (!m_crt_srand_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] CRT srand target detour failed target=0x{:X}\n"),
                    reinterpret_cast<uintptr_t>(srand_import.target));
                m_crt_srand_detour.reset();
                m_crt_srand_trampoline = 0;
                m_crt_rand_detour->unHook();
                m_crt_rand_detour.reset();
                m_crt_rand_trampoline = 0;
                rollback_native_detours();
                clear_crt_detour_state();
                return false;
            }

            const uintptr_t seed_transaction_target =
                reinterpret_cast<uintptr_t>(mod)
                + kLuxBattleInitRngAndHashPrimesRva;
            m_seed_transaction_trampoline = 0;
            m_seed_transaction_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(seed_transaction_target),
                reinterpret_cast<uint64_t>(
                    &RngTraceHook::seed_transaction_detour),
                &m_seed_transaction_trampoline);
            if (!m_seed_transaction_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] battle CRT seed transaction detour failed target=0x{:X}\n"),
                    seed_transaction_target);
                m_seed_transaction_detour.reset();
                m_seed_transaction_trampoline = 0;
                m_crt_srand_detour->unHook();
                m_crt_srand_detour.reset();
                m_crt_srand_trampoline = 0;
                m_crt_rand_detour->unHook();
                m_crt_rand_detour.reset();
                m_crt_rand_trampoline = 0;
                rollback_native_detours();
                clear_crt_detour_state();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[Horse.RngTraceHook] installed lfsr=0x{:X} xorshift96=0x{:X} rand_target=0x{:X} srand_target=0x{:X} seed_transaction=0x{:X} trampoline=0x{:X}\n"),
                target,
                xorshift_target,
                reinterpret_cast<uintptr_t>(rand_import.target),
                reinterpret_cast<uintptr_t>(srand_import.target),
                seed_transaction_target,
                static_cast<uintptr_t>(m_trampoline));
            return true;
        }

        void uninstall()
        {
            disable_gameplay_crt();
            close_crt_window(true);
            set_active(false);
            if (!m_installed.exchange(false, std::memory_order_acq_rel))
                return;
            if (m_seed_transaction_detour)
            {
                m_seed_transaction_detour->unHook();
                m_seed_transaction_detour.reset();
            }
            m_seed_transaction_trampoline = 0;
            if (m_crt_srand_detour)
            {
                m_crt_srand_detour->unHook();
                m_crt_srand_detour.reset();
            }
            m_crt_srand_trampoline = 0;
            if (m_crt_rand_detour)
            {
                m_crt_rand_detour->unHook();
                m_crt_rand_detour.reset();
            }
            m_crt_rand_trampoline = 0;
            clear_crt_detour_state();
            if (m_xorshift_detour)
            {
                m_xorshift_detour->unHook();
                m_xorshift_detour.reset();
            }
            m_xorshift_trampoline = 0;
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
            m_xorshift_total.store(0, std::memory_order_release);
            m_xorshift_overflow.store(0, std::memory_order_release);
            for (auto& s : m_xorshift_slots)
            {
                s.addr.store(0, std::memory_order_release);
                s.count.store(0, std::memory_order_release);
            }
            m_crt_total.store(0, std::memory_order_release);
            m_crt_caller_overflow.store(0, std::memory_order_release);
            m_crt_overflow.store(0, std::memory_order_release);
            m_crt_seed_calls.store(0, std::memory_order_release);
            m_crt_last_seed.store(0, std::memory_order_release);
            m_crt_event_write.store(0, std::memory_order_release);
            for (auto& s : m_crt_slots)
            {
                s.addr.store(0, std::memory_order_release);
                s.count.store(0, std::memory_order_release);
            }
            for (auto& s : m_crt_thread_slots)
            {
                s.addr.store(0, std::memory_order_release);
                s.count.store(0, std::memory_order_release);
            }
            for (auto& event : m_crt_events)
            {
                event.sequence.store(UINT64_MAX, std::memory_order_release);
                event.caller.store(0, std::memory_order_release);
                event.thread_id.store(0, std::memory_order_release);
                event.value.store(0, std::memory_order_release);
                event.kind.store(0, std::memory_order_release);
            }
        }

        void begin_window() noexcept
        {
            if (!m_installed.load(std::memory_order_acquire)) return;
            if ((m_crt_window_state.load(std::memory_order_acquire)
                    & kCrtWindowCountMask) != 0)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.RngTraceHook] CRT trace window still busy; refusing overlapping window\n"));
                return;
            }
            reset();
            m_crt_window_state.store(
                kCrtWindowActiveBit, std::memory_order_release);
            set_active(true);
        }

        void discard_window() noexcept
        {
            set_active(false);
            close_crt_window(true);
            reset();
        }

        void end_window_and_emit(const char* phase,
                                 const char* label,
                                 int32_t requested_seq,
                                 int32_t seq,
                                 int32_t master,
                                 int32_t compare_seq = -1,
                                 int32_t compare_master = -1,
                                 int32_t replay_round = -1,
                                 int32_t logical_frame = -1,
                                 int32_t source_index = -1,
                                 uint64_t round_epoch = 0,
                                 uint64_t round_generation = 0) noexcept
        {
            set_active(false);
            close_crt_window(true);
            emit_and_reset(phase, label, requested_seq, seq, master,
                           compare_seq, compare_master, replay_round,
                           logical_frame, source_index, round_epoch,
                           round_generation);
        }

        void end_window(bool emit_events,
                        const char* phase,
                        const char* label,
                        int32_t requested_seq,
                        int32_t seq,
                        int32_t master,
                        int32_t compare_seq = -1,
                        int32_t compare_master = -1,
                        int32_t replay_round = -1,
                        int32_t logical_frame = -1,
                        int32_t source_index = -1,
                        uint64_t round_epoch = 0,
                        uint64_t round_generation = 0) noexcept
        {
            if (!emit_events)
            {
                discard_window();
                return;
            }
            end_window_and_emit(
                phase, label, requested_seq, seq, master,
                compare_seq, compare_master, replay_round,
                logical_frame, source_index, round_epoch,
                round_generation);
        }

        void emit_and_reset(const char* phase,
                            const char* label,
                            int32_t requested_seq,
                            int32_t seq,
                            int32_t master,
                            int32_t compare_seq = -1,
                            int32_t compare_master = -1,
                            int32_t replay_round = -1,
                            int32_t logical_frame = -1,
                            int32_t source_index = -1,
                            uint64_t round_epoch = 0,
                            uint64_t round_generation = 0) noexcept
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
             .string("rng_family", "lfsr-25-word")
             .integer("requested_seq", requested_seq)
             .integer("seq", seq)
             .integer("master", master)
             .integer("compare_seq", compare_seq)
             .integer("compare_master", compare_master)
             .integer("replay_round", replay_round)
             .integer("logical_frame", logical_frame)
             .integer("source_index", source_index)
             .hex("round_epoch", round_epoch)
             .uinteger("round_generation", round_generation)
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

            entries.clear();
            for (const auto& s : m_xorshift_slots)
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
            ReplayTraceFields xorshift;
            xorshift.string("phase", phase ? phase : "?")
                .string("label", label ? label : "?")
                .string("rng_family", "xorshift96-gameplay")
                .integer("requested_seq", requested_seq)
                .integer("seq", seq)
                .integer("master", master)
                .integer("compare_seq", compare_seq)
                .integer("compare_master", compare_master)
                .integer("replay_round", replay_round)
                .integer("logical_frame", logical_frame)
                .integer("source_index", source_index)
                .hex("round_epoch", round_epoch)
                .uinteger("round_generation", round_generation)
                .boolean("installed",
                    m_installed.load(std::memory_order_acquire))
                .uinteger("total_calls",
                    m_xorshift_total.load(std::memory_order_acquire))
                .uinteger("overflow_calls",
                    m_xorshift_overflow.load(std::memory_order_acquire))
                .integer("caller_count",
                    static_cast<int64_t>(entries.size()));
            const size_t xn =
                (std::min)(entries.size(), kEmitTopCallers);
            for (size_t i = 0; i < xn; ++i)
            {
                char key[48]{};
                std::snprintf(key, sizeof(key), "caller%zu_rip", i);
                xorshift.hex(key, entries[i].addr);
                std::snprintf(key, sizeof(key), "caller%zu_rva", i);
                xorshift.hex(key, (base && entries[i].addr >= base)
                    ? entries[i].addr - base : 0);
                std::snprintf(key, sizeof(key), "caller%zu_count", i);
                xorshift.uinteger(key, entries[i].count);
            }
            ReplayDebugTrace::instance().event(
                "rng_xorshift96_callers", xorshift);

            const CrtSnapshot crt = snapshot_crt();
            const uint32_t emitting_thread_id = ::GetCurrentThreadId();
            const CrtSnapshot current_thread_crt =
                snapshot_crt_for_thread(emitting_thread_id);
            ReplayTraceFields crt_fields;
            crt_fields.string("phase", phase ? phase : "?")
                .string("label", label ? label : "?")
                .string("rng_family", "crt-rand-import")
                .integer("requested_seq", requested_seq)
                .integer("seq", seq)
                .integer("master", master)
                .integer("compare_seq", compare_seq)
                .integer("compare_master", compare_master)
                .integer("replay_round", replay_round)
                .integer("logical_frame", logical_frame)
                .integer("source_index", source_index)
                .hex("round_epoch", round_epoch)
                .uinteger("round_generation", round_generation)
                .uinteger("total_calls", crt.callers.total_calls)
                .uinteger("overflow_calls",
                    crt.callers.overflow_calls)
                .uinteger("seed_calls", crt.seed_calls)
                .uinteger("last_seed", crt.last_seed)
                .uinteger("event_count", crt.event_count)
                .uinteger("overflow_events", crt.overflow_events)
                .hex("sequence_hash", crt.sequence_hash)
                .hex("execution_hash", crt.execution_hash)
                .uinteger("caller_count", crt.callers.caller_count)
                .uinteger("thread_count", crt.thread_count)
                .uinteger("current_thread_id", emitting_thread_id)
                .uinteger("current_thread_total_calls",
                    current_thread_crt.callers.total_calls)
                .uinteger("current_thread_seed_calls",
                    current_thread_crt.seed_calls)
                .uinteger("current_thread_event_count",
                    current_thread_crt.event_count)
                .uinteger("current_thread_overflow_events",
                    current_thread_crt.overflow_events)
                .hex("current_thread_sequence_hash",
                    current_thread_crt.sequence_hash)
                .hex("current_thread_execution_hash",
                    current_thread_crt.execution_hash)
                .uinteger("current_thread_caller_count",
                    current_thread_crt.callers.caller_count)
                .boolean("window_quiescent",
                    (m_crt_window_state.load(std::memory_order_acquire)
                        & kCrtWindowCountMask) == 0);
            for (uint32_t i = 0;
                 i < crt.callers.caller_count && i < kEmitTopCallers; ++i)
            {
                char key[48]{};
                std::snprintf(key, sizeof(key), "caller%u_rip", i);
                crt_fields.hex(key, crt.callers.callers[i].address);
                std::snprintf(key, sizeof(key), "caller%u_rva", i);
                crt_fields.hex(key, crt.callers.callers[i].rva);
                std::snprintf(key, sizeof(key), "caller%u_count", i);
                crt_fields.uinteger(key, crt.callers.callers[i].count);
            }
            for (uint32_t i = 0;
                 i < crt.thread_count && i < kEmitTopCrtThreads; ++i)
            {
                char key[48]{};
                std::snprintf(key, sizeof(key), "thread%u_id", i);
                crt_fields.uinteger(key,
                    static_cast<uint32_t>(crt.threads[i].address));
                std::snprintf(key, sizeof(key), "thread%u_count", i);
                crt_fields.uinteger(key, crt.threads[i].count);
            }
            for (uint32_t i = 0;
                 i < current_thread_crt.callers.caller_count
                    && i < kEmitTopCallers;
                 ++i)
            {
                char key[64]{};
                std::snprintf(
                    key, sizeof(key), "current_thread_caller%u_rip", i);
                crt_fields.hex(
                    key, current_thread_crt.callers.callers[i].address);
                std::snprintf(
                    key, sizeof(key), "current_thread_caller%u_rva", i);
                crt_fields.hex(
                    key, current_thread_crt.callers.callers[i].rva);
                std::snprintf(
                    key, sizeof(key), "current_thread_caller%u_count", i);
                crt_fields.uinteger(
                    key, current_thread_crt.callers.callers[i].count);
            }
            const uint64_t emitted_events = (std::min)(
                crt.event_count,
                static_cast<uint64_t>(kEmitTopCrtEvents));
            for (uint64_t i = 0; i < emitted_events; ++i)
            {
                const CrtEventSlot& event = m_crt_events[
                    static_cast<size_t>(i)];
                if (event.sequence.load(std::memory_order_acquire) != i)
                    continue;
                const uintptr_t caller =
                    event.caller.load(std::memory_order_relaxed);
                char key[48]{};
                std::snprintf(key, sizeof(key), "event%llu_kind",
                    static_cast<unsigned long long>(i));
                crt_fields.uinteger(key,
                    event.kind.load(std::memory_order_relaxed));
                std::snprintf(key, sizeof(key), "event%llu_caller_rva",
                    static_cast<unsigned long long>(i));
                crt_fields.hex(key,
                    (base && caller >= base && caller - base <= UINT32_MAX)
                        ? caller - base : 0);
                std::snprintf(key, sizeof(key), "event%llu_caller_rip",
                    static_cast<unsigned long long>(i));
                crt_fields.hex(key, caller);
                std::snprintf(key, sizeof(key), "event%llu_thread",
                    static_cast<unsigned long long>(i));
                crt_fields.uinteger(key,
                    event.thread_id.load(std::memory_order_relaxed));
                std::snprintf(key, sizeof(key), "event%llu_value",
                    static_cast<unsigned long long>(i));
                crt_fields.integer(key,
                    event.value.load(std::memory_order_relaxed));
            }
            ReplayDebugTrace::instance().event(
                "rng_crt_import_callers", crt_fields);
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

        HistogramSnapshot snapshot_histogram() const noexcept
        {
            return snapshot_histogram(
                m_slots, m_total, m_overflow);
        }

        HistogramSnapshot snapshot_xorshift_histogram() const noexcept
        {
            return snapshot_histogram(
                m_xorshift_slots, m_xorshift_total,
                m_xorshift_overflow);
        }

        CrtSnapshot snapshot_crt() const noexcept
        {
            CrtSnapshot snapshot {};
            snapshot.callers = snapshot_histogram(
                m_crt_slots, m_crt_total, m_crt_caller_overflow);
            snapshot.seed_calls =
                m_crt_seed_calls.load(std::memory_order_acquire);
            snapshot.last_seed =
                m_crt_last_seed.load(std::memory_order_acquire);
            snapshot.overflow_events =
                m_crt_overflow.load(std::memory_order_acquire);
            snapshot.event_count = (std::min)(
                m_crt_event_write.load(std::memory_order_acquire),
                static_cast<uint64_t>(m_crt_events.size()));
            const uintptr_t base = NativeBinding::imageBase();
            for (uint64_t i = 0; i < snapshot.event_count; ++i)
            {
                const CrtEventSlot& event =
                    m_crt_events[static_cast<size_t>(i)];
                if (event.sequence.load(std::memory_order_acquire) != i)
                {
                    ++snapshot.overflow_events;
                    continue;
                }
                const uintptr_t caller =
                    event.caller.load(std::memory_order_relaxed);
                const uint32_t rva =
                    (base && caller >= base && caller - base <= UINT32_MAX)
                    ? static_cast<uint32_t>(caller - base) : 0;
                hash_crt_value(snapshot.sequence_hash,
                    event.kind.load(std::memory_order_relaxed));
                hash_crt_value(snapshot.sequence_hash, rva);
                hash_crt_value(snapshot.sequence_hash,
                    event.value.load(std::memory_order_relaxed));
                hash_crt_value(snapshot.execution_hash,
                    event.kind.load(std::memory_order_relaxed));
                hash_crt_value(snapshot.execution_hash, rva);
                hash_crt_value(snapshot.execution_hash,
                    event.thread_id.load(std::memory_order_relaxed));
                hash_crt_value(snapshot.execution_hash,
                    event.value.load(std::memory_order_relaxed));
            }
            for (const auto& thread : m_crt_thread_slots)
            {
                const uintptr_t id =
                    thread.addr.load(std::memory_order_acquire);
                const uint32_t count =
                    thread.count.load(std::memory_order_acquire);
                if (!id || !count
                    || snapshot.thread_count >= snapshot.threads.size())
                    continue;
                CallerCount& out = snapshot.threads[
                    snapshot.thread_count++];
                out.address = id;
                out.rva = static_cast<uint32_t>(id);
                out.count = count;
            }
            std::sort(snapshot.threads.begin(),
                snapshot.threads.begin() + snapshot.thread_count,
                [](const CallerCount& lhs, const CallerCount& rhs)
                {
                    return lhs.address < rhs.address;
                });
            return snapshot;
        }

        // MSVC's UCRT stores rand/srand state in __acrt_ptd. Ownership
        // evidence for a native battle transaction must consequently compare
        // only calls made by that transaction's thread; process-wide events
        // remain useful for the scheduling trace but are not state mutation
        // by the current native tick.
        CrtSnapshot snapshot_crt_for_thread(uint32_t thread_id) const noexcept
        {
            CrtSnapshot snapshot {};
            if (!thread_id) return snapshot;

            const uint64_t published =
                m_crt_event_write.load(std::memory_order_acquire);
            const uint64_t available = (std::min)(
                published, static_cast<uint64_t>(m_crt_events.size()));
            if (published > available)
                snapshot.overflow_events = published - available;

            const uintptr_t base = NativeBinding::imageBase();
            for (uint64_t i = 0; i < available; ++i)
            {
                const CrtEventSlot& event =
                    m_crt_events[static_cast<size_t>(i)];
                if (event.sequence.load(std::memory_order_acquire) != i)
                {
                    ++snapshot.overflow_events;
                    continue;
                }
                if (event.thread_id.load(std::memory_order_relaxed)
                    != thread_id)
                {
                    continue;
                }

                const uintptr_t caller =
                    event.caller.load(std::memory_order_relaxed);
                const uint32_t rva =
                    (base && caller >= base && caller - base <= UINT32_MAX)
                    ? static_cast<uint32_t>(caller - base) : 0;
                const uint8_t kind =
                    event.kind.load(std::memory_order_relaxed);
                const int32_t value =
                    event.value.load(std::memory_order_relaxed);

                ++snapshot.event_count;
                ++snapshot.callers.total_calls;
                if (kind == 1)
                {
                    ++snapshot.seed_calls;
                    snapshot.last_seed = static_cast<uint32_t>(value);
                }
                hash_crt_value(snapshot.sequence_hash, kind);
                hash_crt_value(snapshot.sequence_hash, rva);
                hash_crt_value(snapshot.sequence_hash, value);
                hash_crt_value(snapshot.execution_hash, kind);
                hash_crt_value(snapshot.execution_hash, rva);
                hash_crt_value(snapshot.execution_hash, thread_id);
                hash_crt_value(snapshot.execution_hash, value);

                uint32_t caller_index = snapshot.callers.caller_count;
                for (uint32_t j = 0;
                     j < snapshot.callers.caller_count; ++j)
                {
                    if (snapshot.callers.callers[j].address == caller)
                    {
                        caller_index = j;
                        break;
                    }
                }
                if (caller_index < snapshot.callers.caller_count)
                {
                    ++snapshot.callers.callers[caller_index].count;
                }
                else if (snapshot.callers.caller_count
                         < snapshot.callers.callers.size())
                {
                    CallerCount& out = snapshot.callers.callers[
                        snapshot.callers.caller_count++];
                    out.address = caller;
                    out.rva = rva;
                    out.count = 1;
                }
                else
                {
                    ++snapshot.callers.overflow_calls;
                }
            }

            if (snapshot.event_count)
            {
                snapshot.thread_count = 1;
                snapshot.threads[0].address = thread_id;
                snapshot.threads[0].rva = thread_id;
                snapshot.threads[0].count =
                    static_cast<uint32_t>((std::min)(
                        snapshot.event_count,
                        static_cast<uint64_t>(UINT32_MAX)));
            }
            std::sort(
                snapshot.callers.callers.begin(),
                snapshot.callers.callers.begin()
                    + snapshot.callers.caller_count,
                [](const CallerCount& lhs, const CallerCount& rhs)
                {
                    if (lhs.rva != rhs.rva) return lhs.rva < rhs.rva;
                    return lhs.address < rhs.address;
                });
            add_persistent_crt_thread_snapshot(thread_id, snapshot);
            return snapshot;
        }

    private:
        template<size_t N>
        static HistogramSnapshot snapshot_histogram(
            const std::array<Slot, N>& slots,
            const std::atomic<uint64_t>& total,
            const std::atomic<uint64_t>& overflow) noexcept
        {
            HistogramSnapshot snapshot {};
            snapshot.total_calls =
                total.load(std::memory_order_acquire);
            snapshot.overflow_calls =
                overflow.load(std::memory_order_acquire);
            const uintptr_t base = NativeBinding::imageBase();
            for (const auto& slot : slots)
            {
                const uintptr_t address =
                    slot.addr.load(std::memory_order_acquire);
                const uint32_t count =
                    slot.count.load(std::memory_order_acquire);
                if (!address || !count
                    || snapshot.caller_count >= snapshot.callers.size())
                {
                    continue;
                }
                CallerCount& caller =
                    snapshot.callers[snapshot.caller_count++];
                caller.address = address;
                caller.rva = (base && address >= base
                    && address - base <= UINT32_MAX)
                    ? static_cast<uint32_t>(address - base)
                    : 0;
                caller.count = count;
            }
            std::sort(
                snapshot.callers.begin(),
                snapshot.callers.begin() + snapshot.caller_count,
                [](const CallerCount& lhs, const CallerCount& rhs)
                {
                    if (lhs.rva != rhs.rva) return lhs.rva < rhs.rva;
                    if (lhs.address != rhs.address)
                        return lhs.address < rhs.address;
                    return lhs.count < rhs.count;
                });
            return snapshot;
        }

        RngTraceHook() = default;
        ~RngTraceHook() { uninstall(); }
        RngTraceHook(const RngTraceHook&) = delete;
        RngTraceHook& operator=(const RngTraceHook&) = delete;

        using GetRandU32Fn = uint32_t(__fastcall*)();
        static uint32_t __fastcall lfsr_detour()
        {
            RngTraceHook& self = instance();
            const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
            if (self.m_active.load(std::memory_order_acquire))
                self.record(caller);
            auto* original = reinterpret_cast<GetRandU32Fn>(self.m_trampoline);
            return original ? original() : 0;
        }

        static uint32_t __fastcall xorshift96_detour()
        {
            RngTraceHook& self = instance();
            const uintptr_t caller =
                reinterpret_cast<uintptr_t>(_ReturnAddress());
            if (self.m_active.load(std::memory_order_acquire))
            {
                self.record(
                    caller, self.m_xorshift_slots,
                    self.m_xorshift_total, self.m_xorshift_overflow);
            }
            auto* original = reinterpret_cast<GetRandU32Fn>(
                self.m_xorshift_trampoline);
            return original ? original() : 0;
        }

        using CrtRandFn = int(__cdecl*)();
        using CrtSrandFn = void(__cdecl*)(unsigned int);
        using SeedTransactionFn = void(__fastcall*)(uint32_t);

        static void __fastcall seed_transaction_detour(uint32_t full_seed)
        {
            RngTraceHook& self = instance();
            const uint32_t thread_id = ::GetCurrentThreadId();
            const bool enabled = self.m_gameplay_crt_enabled.load(
                std::memory_order_acquire);
            bool began = false;
            if (enabled)
            {
                void* owner = self.m_gameplay_crt_owner.load(
                    std::memory_order_acquire);
                const auto pending = self.m_gameplay_crt_pending_debt.load(
                    std::memory_order_acquire);
                const bool debt = !owner || !pending || pending(owner);
                {
                    GameplayCrtLock lock(self.m_gameplay_crt_lock);
                    began = self.m_gameplay_crt.begin_seed_transaction(
                        full_seed, thread_id, debt);
                }
                if (!began)
                    self.notify_gameplay_crt_fatal(
                        self.gameplay_crt_failure(),
                        kLuxBattleInitRngAndHashPrimesRva);
            }

            auto* original = reinterpret_cast<SeedTransactionFn>(
                self.m_seed_transaction_trampoline);
            if (original) original(full_seed);

            if (enabled && began)
            {
                bool finished = false;
                const char* failure = "ok";
                {
                    GameplayCrtLock lock(self.m_gameplay_crt_lock);
                    finished = self.m_gameplay_crt.finish_seed_transaction(
                        thread_id);
                    failure = self.m_gameplay_crt.failure();
                }
                if (!finished)
                    self.notify_gameplay_crt_fatal(
                        failure, kLuxBattleInitRngAndHashPrimesRva);
            }
        }

        static int __cdecl crt_rand_detour()
        {
            RngTraceHook& self = instance();
            const bool participant = self.enter_crt_window();
            const uintptr_t caller =
                reinterpret_cast<uintptr_t>(_ReturnAddress());
            const uint32_t return_rva = self.gameplay_crt_rva(caller);
            const uint32_t thread_id = ::GetCurrentThreadId();
            auto* original = reinterpret_cast<CrtRandFn>(
                self.m_crt_rand_trampoline);

            const auto record_and_leave = [&](int value, uint8_t kind)
                noexcept
            {
                if (participant)
                {
                    self.record_crt_event(
                        kind, caller, static_cast<int32_t>(value));
                    self.leave_crt_window();
                }
            };

            if (self.m_gameplay_crt_enabled.load(
                    std::memory_order_acquire))
            {
                RollbackGameplayCrtState state {};
                {
                    GameplayCrtLock lock(self.m_gameplay_crt_lock);
                    state = self.m_gameplay_crt.state();
                }

                if (state.phase == RollbackGameplayCrtPhase::Seeding)
                {
                    const int value = original ? original() : 0;
                    self.observe_crt_rand(thread_id, value);
                    RollbackGameplayCrtDrawResult routed {};
                    {
                        GameplayCrtLock lock(self.m_gameplay_crt_lock);
                        routed = self.m_gameplay_crt
                            .observe_native_warmup_draw(
                                return_rva, thread_id, value);
                    }
                    if (routed.fatal)
                        self.notify_gameplay_crt_fatal(
                            routed.failure, return_rva);
                    record_and_leave(value, 0);
                    return value;
                }

                void* owner = self.m_gameplay_crt_owner.load(
                    std::memory_order_acquire);
                const auto* scope =
                    CurrentRollbackNativeSimulationScope();
                const bool inside_owned_scope = owner && scope
                    && scope->owner == owner;
                const bool known_gameplay =
                    RollbackCrtCallerIsGameplay(return_rva);
                const bool known_presentation =
                    RollbackCrtCallerIsPresentation(return_rva);
                const auto owns_callback =
                    self.m_gameplay_crt_owns_simulation.load(
                        std::memory_order_acquire);
                const bool owns_simulation = owner && owns_callback
                    && owns_callback(owner);
                const bool split_control =
                    self.m_gameplay_crt_mode.load(
                        std::memory_order_acquire)
                    == GameplayCrtRoutingMode::SplitControl;

                if (split_control && known_gameplay
                    && state.phase == RollbackGameplayCrtPhase::Ready)
                {
                    RollbackGameplayCrtDrawResult routed {};
                    {
                        GameplayCrtLock lock(self.m_gameplay_crt_lock);
                        routed = self.m_gameplay_crt.draw_owned(
                            return_rva, thread_id);
                    }
                    if (routed.fatal)
                        self.notify_gameplay_crt_fatal(
                            routed.failure, return_rva);
                    record_and_leave(routed.value, 2);
                    return routed.value;
                }

                if (inside_owned_scope && known_presentation)
                {
                    // Keep known particle/audio/ground-debris draws on the native
                    // presentation stream. Feeding them through the gameplay
                    // broker would make later MoveVM RAND results depend on
                    // nondeterministic presentation order across peers.
                    const int value = original ? original() : 0;
                    self.observe_crt_rand(thread_id, value);
                    record_and_leave(value, 0);
                    return value;
                }

                if (inside_owned_scope
                    || (owns_simulation && known_gameplay))
                {
                    RollbackGameplayCrtDrawResult routed {};
                    {
                        GameplayCrtLock lock(self.m_gameplay_crt_lock);
                        routed = self.m_gameplay_crt.draw_owned(
                            return_rva, thread_id);
                    }
                    if (!inside_owned_scope && !routed.fatal)
                    {
                        routed.fatal = true;
                        routed.failure = "unowned-gameplay-crt-caller";
                    }
                    if (routed.fatal)
                        self.notify_gameplay_crt_fatal(
                            routed.failure, return_rva);
                    record_and_leave(routed.value, 2);
                    return routed.value;
                }

                if (!owns_simulation && known_gameplay
                    && state.phase == RollbackGameplayCrtPhase::Ready)
                {
                    const int value = original ? original() : 0;
                    self.observe_crt_rand(thread_id, value);
                    RollbackGameplayCrtDrawResult routed {};
                    {
                        GameplayCrtLock lock(self.m_gameplay_crt_lock);
                        routed = self.m_gameplay_crt
                            .observe_native_gameplay_draw(
                                return_rva, thread_id, value);
                    }
                    if (routed.fatal)
                        self.notify_gameplay_crt_fatal(
                            routed.failure, return_rva);
                    record_and_leave(value, 0);
                    return value;
                }
            }

            const int value = original ? original() : 0;
            self.observe_crt_rand(thread_id, value);
            record_and_leave(value, 0);
            return value;
        }

        static void __cdecl crt_srand_detour(unsigned int seed)
        {
            RngTraceHook& self = instance();
            const bool participant = self.enter_crt_window();
            auto* original = reinterpret_cast<CrtSrandFn>(
                self.m_crt_srand_trampoline);
            if (original) original(seed);
            const uint32_t thread_id = ::GetCurrentThreadId();
            self.observe_crt_seed(thread_id, seed);
            if (self.m_gameplay_crt_enabled.load(
                    std::memory_order_acquire))
            {
                bool accepted = true;
                bool requires_broker = false;
                const char* failure = "ok";
                {
                    GameplayCrtLock lock(self.m_gameplay_crt_lock);
                    if (self.m_gameplay_crt.state().phase
                        == RollbackGameplayCrtPhase::Seeding)
                    {
                        requires_broker = true;
                        accepted = self.m_gameplay_crt.observe_native_seed(
                            seed, thread_id);
                        failure = self.m_gameplay_crt.failure();
                    }
                    else if (const auto* scope =
                        CurrentRollbackNativeSimulationScope())
                    {
                        void* owner = self.m_gameplay_crt_owner.load(
                            std::memory_order_acquire);
                        if (owner && scope->owner == owner)
                        {
                            requires_broker = true;
                            accepted = false;
                            failure =
                                "owned-crt-srand-outside-seed-transaction";
                        }
                    }
                }
                if (requires_broker && !accepted)
                    self.notify_gameplay_crt_fatal(
                        failure, self.gameplay_crt_rva(
                            reinterpret_cast<uintptr_t>(_ReturnAddress())));
            }
            if (participant)
            {
                self.m_crt_seed_calls.fetch_add(
                    1, std::memory_order_relaxed);
                self.m_crt_last_seed.store(seed, std::memory_order_release);
                self.record_crt_event(
                    1, reinterpret_cast<uintptr_t>(_ReturnAddress()),
                    static_cast<int32_t>(seed));
                self.leave_crt_window();
            }
        }

        void set_active(bool active) noexcept
        {
            m_active.store(active, std::memory_order_release);
        }

        void record(uintptr_t caller) noexcept
        {
            record(caller, m_slots, m_total, m_overflow);
        }

        uint32_t gameplay_crt_rva(uintptr_t address) const noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            return base && address >= base && address - base <= UINT32_MAX
                ? static_cast<uint32_t>(address - base) : 0;
        }

        const char* gameplay_crt_failure() noexcept
        {
            GameplayCrtLock lock(m_gameplay_crt_lock);
            return m_gameplay_crt.failure();
        }

        void notify_gameplay_crt_fatal(
            const char* failure, uint32_t return_rva) noexcept
        {
            void* owner = m_gameplay_crt_owner.load(
                std::memory_order_acquire);
            const auto callback = m_gameplay_crt_fatal.load(
                std::memory_order_acquire);
            if (owner && callback)
                callback(owner, failure ? failure : "gameplay-crt-fatal",
                    return_rva);
        }

        template<size_t N>
        static void record(
            uintptr_t caller,
            std::array<Slot, N>& slots,
            std::atomic<uint64_t>& total,
            std::atomic<uint64_t>& overflow) noexcept
        {
            if (!caller) return;
            total.fetch_add(1, std::memory_order_relaxed);

            for (auto& s : slots)
            {
                const uintptr_t addr = s.addr.load(std::memory_order_acquire);
                if (addr == caller)
                {
                    s.count.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }

            for (auto& s : slots)
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

            overflow.fetch_add(1, std::memory_order_relaxed);
        }

        bool enter_crt_window() noexcept
        {
            uint64_t state =
                m_crt_window_state.load(std::memory_order_acquire);
            while ((state & kCrtWindowActiveBit) != 0)
            {
                if ((state & kCrtWindowCountMask) == kCrtWindowCountMask)
                    return false;
                if (m_crt_window_state.compare_exchange_weak(
                        state, state + 1,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                    return true;
            }
            return false;
        }

        void leave_crt_window() noexcept
        {
            m_crt_window_state.fetch_sub(1, std::memory_order_release);
        }

        void close_crt_window(bool wait_until_quiescent) noexcept
        {
            m_crt_window_state.fetch_and(
                kCrtWindowCountMask, std::memory_order_acq_rel);
            // No new participant can enter after the active bit is cleared.
            // Existing calls only execute one CRT operation and fixed atomic
            // bookkeeping, so a bounded yield is sufficient and avoids
            // resetting a buffer while a publisher still owns a slot.
            const ULONGLONG deadline = ::GetTickCount64() + 10;
            while ((m_crt_window_state.load(std::memory_order_acquire)
                    & kCrtWindowCountMask) != 0
                && (wait_until_quiescent
                    || ::GetTickCount64() < deadline))
            {
                ::SwitchToThread();
            }
        }

        void record_crt_event(
            uint8_t kind, uintptr_t caller, int32_t value) noexcept
        {
            record(caller, m_crt_slots, m_crt_total,
                m_crt_caller_overflow);
            const uint32_t thread_id = ::GetCurrentThreadId();
            record(static_cast<uintptr_t>(thread_id),
                m_crt_thread_slots, m_crt_thread_total,
                m_crt_thread_overflow);
            const uint64_t sequence = m_crt_event_write.fetch_add(
                1, std::memory_order_relaxed);
            if (sequence >= m_crt_events.size())
            {
                m_crt_overflow.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            CrtEventSlot& event =
                m_crt_events[static_cast<size_t>(sequence)];
            event.caller.store(caller, std::memory_order_relaxed);
            event.thread_id.store(thread_id, std::memory_order_relaxed);
            event.value.store(value, std::memory_order_relaxed);
            event.kind.store(kind, std::memory_order_relaxed);
            event.sequence.store(sequence, std::memory_order_release);
        }

        static constexpr uint32_t advance_msvc_crt_rand_state(
            uint32_t state) noexcept
        {
            return state * 214013u + 2531011u;
        }

        static constexpr uint32_t msvc_crt_rand_output(
            uint32_t state) noexcept
        {
            return (state >> 16) & 0x7fffu;
        }

        CrtPersistentThreadSlot* find_or_claim_persistent_crt_thread(
            uint32_t thread_id) noexcept
        {
            if (!thread_id) return nullptr;
            for (auto& slot : m_crt_persistent_thread_slots)
            {
                if (slot.thread_id.load(std::memory_order_acquire)
                    == thread_id)
                {
                    return &slot;
                }
            }
            for (auto& slot : m_crt_persistent_thread_slots)
            {
                uint32_t expected = 0;
                if (slot.thread_id.compare_exchange_strong(
                        expected, thread_id,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)
                    || expected == thread_id)
                {
                    return &slot;
                }
            }
            return nullptr;
        }

        void observe_crt_seed(uint32_t thread_id, uint32_t seed) noexcept
        {
            auto* slot = find_or_claim_persistent_crt_thread(thread_id);
            if (!slot) return;
            slot->seed_known.store(false, std::memory_order_release);
            slot->last_seed.store(seed, std::memory_order_relaxed);
            slot->predicted_state.store(seed, std::memory_order_relaxed);
            slot->draws_since_seed.store(0, std::memory_order_relaxed);
            slot->prediction_mismatches.store(0,
                std::memory_order_relaxed);
            slot->seed_generation.fetch_add(1,
                std::memory_order_relaxed);
            slot->seed_known.store(true, std::memory_order_release);
        }

        void observe_crt_rand(uint32_t thread_id, int value) noexcept
        {
            auto* slot = find_or_claim_persistent_crt_thread(thread_id);
            if (!slot
                || !slot->seed_known.load(std::memory_order_acquire))
            {
                return;
            }
            const uint32_t next = advance_msvc_crt_rand_state(
                slot->predicted_state.load(std::memory_order_relaxed));
            if (msvc_crt_rand_output(next)
                != (static_cast<uint32_t>(value) & 0x7fffu))
            {
                slot->prediction_mismatches.fetch_add(
                    1, std::memory_order_relaxed);
            }
            slot->predicted_state.store(next, std::memory_order_release);
            slot->draws_since_seed.fetch_add(1,
                std::memory_order_relaxed);
        }

        void add_persistent_crt_thread_snapshot(
            uint32_t thread_id, CrtSnapshot& snapshot) const noexcept
        {
            for (const auto& slot : m_crt_persistent_thread_slots)
            {
                if (slot.thread_id.load(std::memory_order_acquire)
                    != thread_id)
                {
                    continue;
                }
                snapshot.persistent_thread_observed = true;
                snapshot.persistent_seed_known =
                    slot.seed_known.load(std::memory_order_acquire);
                snapshot.persistent_last_seed =
                    slot.last_seed.load(std::memory_order_relaxed);
                snapshot.persistent_predicted_state =
                    slot.predicted_state.load(std::memory_order_acquire);
                snapshot.persistent_expected_next = msvc_crt_rand_output(
                    advance_msvc_crt_rand_state(
                        snapshot.persistent_predicted_state));
                snapshot.persistent_draws_since_seed =
                    slot.draws_since_seed.load(std::memory_order_acquire);
                snapshot.persistent_seed_generation =
                    slot.seed_generation.load(std::memory_order_relaxed);
                snapshot.persistent_prediction_mismatches =
                    slot.prediction_mismatches.load(
                        std::memory_order_relaxed);
                return;
            }
        }

        void reset_persistent_crt_threads() noexcept
        {
            for (auto& slot : m_crt_persistent_thread_slots)
            {
                slot.seed_known.store(false, std::memory_order_release);
                slot.last_seed.store(0, std::memory_order_relaxed);
                slot.predicted_state.store(0,
                    std::memory_order_relaxed);
                slot.draws_since_seed.store(0,
                    std::memory_order_relaxed);
                slot.seed_generation.store(0,
                    std::memory_order_relaxed);
                slot.prediction_mismatches.store(0,
                    std::memory_order_relaxed);
                slot.thread_id.store(0, std::memory_order_release);
            }
        }

        template<typename T>
        static void hash_crt_value(uint64_t& hash, const T& value) noexcept
        {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            for (size_t i = 0; i < sizeof(T); ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        }

        void rollback_native_detours() noexcept
        {
            if (m_xorshift_detour)
            {
                m_xorshift_detour->unHook();
                m_xorshift_detour.reset();
            }
            m_xorshift_trampoline = 0;
            if (m_detour)
            {
                m_detour->unHook();
                m_detour.reset();
            }
            m_trampoline = 0;
        }

        static bool validate_rollback_crt_sites(
            uintptr_t image_base) noexcept
        {
            if (!image_base) return false;
            static constexpr uint8_t seed_prefix[] = {
                0x48, 0x89, 0x5C, 0x24, 0x08,
                0x48, 0x89, 0x6C, 0x24, 0x10,
                0x48, 0x89, 0x74, 0x24, 0x18,
                0x57, 0x41, 0x56, 0x41, 0x57,
                0x48, 0x83, 0xEC, 0x40,
            };
            static constexpr uint8_t warmup_call[] = {
                0xFF, 0x15, 0xA8, 0xE1, 0xED, 0x02,
            };
            static constexpr uint8_t movevm_call_and_mask[] = {
                0xFF, 0x15, 0x0C, 0x68, 0xEC, 0x02,
                0x25, 0xFF, 0x7F, 0x00, 0x00,
            };
            static constexpr uint8_t rannyu_call_and_use[] = {
                0xFF, 0x15, 0x98, 0x74, 0xF0, 0x02,
                0xF3, 0x44, 0x0F, 0x10,
            };
            static constexpr uint8_t particle_emitter_call[] = {
                0xFF, 0x15, 0xA4, 0x1A, 0x29, 0x01,
            };
            static constexpr uint8_t audio_voice_call[] = {
                0xFF, 0x15, 0xE2, 0xDE, 0xCD, 0x02,
            };
            static constexpr uint8_t ground_debris_base_yaw_call[] = {
                0xFF, 0x15, 0x92, 0x7A, 0x99, 0x02,
            };
            static constexpr uint8_t ground_debris_yaw_jitter_call[] = {
                0xFF, 0x15, 0xFB, 0x76, 0x99, 0x02,
            };
            static constexpr uint8_t particle_module_seed_call[] = {
                0xFF, 0x15, 0xCE, 0xCE, 0x28, 0x01,
            };
            static constexpr uint8_t emitter_delay_range_call[] = {
                0xFF, 0x15, 0xF5, 0x7C, 0x28, 0x01,
            };
            static constexpr uint8_t emitter_duration_range_call[] = {
                0xFF, 0x15, 0xAE, 0x7C, 0x28, 0x01,
            };
            const auto matches = [image_base](
                    uintptr_t rva, const uint8_t* expected,
                    size_t bytes) noexcept {
                MEMORY_BASIC_INFORMATION memory {};
                const uintptr_t address = image_base + rva;
                return ::VirtualQuery(
                        reinterpret_cast<const void*>(address),
                        &memory, sizeof(memory)) == sizeof(memory)
                    && memory.State == MEM_COMMIT
                    && (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0
                    && std::memcmp(
                        reinterpret_cast<const void*>(address),
                        expected, bytes) == 0;
            };
            return matches(kLuxBattleInitRngAndHashPrimesRva,
                    seed_prefix, sizeof(seed_prefix))
                && matches(kRollbackCrtWarmupReturnRva - 6u,
                    warmup_call, sizeof(warmup_call))
                && matches(kRollbackCrtMoveVmReturnRva - 6u,
                    movevm_call_and_mask, sizeof(movevm_call_and_mask))
                && matches(kRollbackCrtRannyuReturnRva - 6u,
                    rannyu_call_and_use, sizeof(rannyu_call_and_use))
                && matches(kRollbackCrtParticleEmitterReturnRva - 6u,
                    particle_emitter_call,
                    sizeof(particle_emitter_call))
                && matches(kRollbackCrtAudioVoiceReturnRva - 6u,
                    audio_voice_call, sizeof(audio_voice_call))
                && matches(kRollbackCrtGroundDebrisBaseYawReturnRva - 6u,
                    ground_debris_base_yaw_call,
                    sizeof(ground_debris_base_yaw_call))
                && matches(kRollbackCrtGroundDebrisYawJitterReturnRva - 6u,
                    ground_debris_yaw_jitter_call,
                    sizeof(ground_debris_yaw_jitter_call))
                && matches(kRollbackCrtParticleModuleSeedReturnRva - 6u,
                    particle_module_seed_call,
                    sizeof(particle_module_seed_call))
                && matches(kRollbackCrtEmitterDelayRangeReturnRva - 6u,
                    emitter_delay_range_call,
                    sizeof(emitter_delay_range_call))
                && matches(kRollbackCrtEmitterDurationRangeReturnRva - 6u,
                    emitter_duration_range_call,
                    sizeof(emitter_duration_range_call));
        }

        void clear_crt_detour_state() noexcept
        {
            m_crt_rand_trampoline = 0;
            m_crt_srand_trampoline = 0;
        }

        static constexpr uintptr_t kLuxMoveVM_GetRandU32_RVA = 0x34F130;
        static constexpr uintptr_t
            kLuxMoveVM_GetRandXorshift96Gameplay_RVA = 0x34F1F0;
        static constexpr uintptr_t
            kLuxBattleInitRngAndHashPrimesRva = 0x34F610;
        static constexpr const char* kCrtUtilityImportDll =
            "api-ms-win-crt-utility-l1-1-0.dll";
        static constexpr size_t kEmitTopCallers = 12;
        static constexpr size_t kEmitTopCrtEvents = 32;
        static constexpr size_t kEmitTopCrtThreads = 8;
        static constexpr uint64_t kCrtWindowActiveBit = 1ull << 63;
        static constexpr uint64_t kCrtWindowCountMask =
            ~kCrtWindowActiveBit;

        std::array<Slot, kMaxCallers> m_slots{};
        std::array<Slot, kMaxCallers> m_xorshift_slots{};
        std::array<Slot, kMaxCallers> m_crt_slots{};
        std::array<Slot, kMaxCrtThreads> m_crt_thread_slots{};
        std::array<CrtPersistentThreadSlot, kMaxCrtThreads>
            m_crt_persistent_thread_slots{};
        std::array<CrtEventSlot, kMaxCrtEvents> m_crt_events{};
        std::atomic<uint64_t> m_total{0};
        std::atomic<uint64_t> m_overflow{0};
        std::atomic<uint64_t> m_xorshift_total{0};
        std::atomic<uint64_t> m_xorshift_overflow{0};
        std::atomic<uint64_t> m_crt_total{0};
        std::atomic<uint64_t> m_crt_caller_overflow{0};
        std::atomic<uint64_t> m_crt_overflow{0};
        std::atomic<uint64_t> m_crt_seed_calls{0};
        std::atomic<uint64_t> m_crt_event_write{0};
        std::atomic<uint64_t> m_crt_thread_total{0};
        std::atomic<uint64_t> m_crt_thread_overflow{0};
        std::atomic<uint64_t> m_crt_window_state{0};
        std::atomic<uint32_t> m_crt_last_seed{0};
        std::atomic<bool> m_active{false};
        std::atomic<bool> m_installed{false};
        std::atomic<bool> m_gameplay_crt_enabled{false};
        std::atomic<void*> m_gameplay_crt_owner{nullptr};
        std::atomic<GameplayCrtOwnsSimulationFn>
            m_gameplay_crt_owns_simulation{nullptr};
        std::atomic<GameplayCrtPendingDebtFn>
            m_gameplay_crt_pending_debt{nullptr};
        std::atomic<GameplayCrtFatalFn> m_gameplay_crt_fatal{nullptr};
        std::atomic<GameplayCrtRoutingMode> m_gameplay_crt_mode{
            GameplayCrtRoutingMode::RollbackOwned};
        std::atomic_flag m_gameplay_crt_lock = ATOMIC_FLAG_INIT;
        RollbackGameplayCrtBroker m_gameplay_crt{};
        uint64_t m_trampoline{0};
        uint64_t m_xorshift_trampoline{0};
        uint64_t m_crt_rand_trampoline{0};
        uint64_t m_crt_srand_trampoline{0};
        uint64_t m_seed_transaction_trampoline{0};
        std::unique_ptr<PLH::x64Detour> m_detour{};
        std::unique_ptr<PLH::x64Detour> m_xorshift_detour{};
        std::unique_ptr<PLH::x64Detour> m_crt_rand_detour{};
        std::unique_ptr<PLH::x64Detour> m_crt_srand_detour{};
        std::unique_ptr<PLH::x64Detour> m_seed_transaction_detour{};
    };
}
