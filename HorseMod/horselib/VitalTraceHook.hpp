// ============================================================================
// Horse::VitalTraceHook - native vital/damage diagnostics for replay oracle work.
//
// This is observability only. It records before/after vital candidate fields
// around the native damage accumulator and lethal-gauge updater so replay seek
// code can validate KO/fatal boundaries before any field becomes authoritative.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "ReplayDebugTrace.hpp"
#include "ReplayScrubDiag.hpp"
#include "SafeMemoryRead.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace Horse
{
    extern std::atomic<bool>
        g_replay_scrub_generation_diagnostics_suppressed;

    class VitalTraceHook
    {
    public:
        static VitalTraceHook& instance() noexcept
        {
            static VitalTraceHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.VitalTraceHook] image base unresolved\n"));
                return false;
            }

            bool ok = true;
            ok &= install_one(m_accumulate_detour, m_accumulate_trampoline,
                              base + kRVA_AccumulateDamageTaken,
                              reinterpret_cast<uint64_t>(&detour_accumulate),
                              STR("AccumulateDamageTaken"));
            ok &= install_one(m_lethal_detour, m_lethal_trampoline,
                              base + kRVA_UpdateLethalHitGauge,
                              reinterpret_cast<uint64_t>(&detour_lethal),
                              STR("UpdateLethalHitGauge"));
            if (!ok)
            {
                uninstall();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[Horse.VitalTraceHook] installed accumulate=0x{:X} lethal=0x{:X}\n"),
                base + kRVA_AccumulateDamageTaken,
                base + kRVA_UpdateLethalHitGauge);
            return true;
        }

        void uninstall()
        {
            if (!m_installed.exchange(false, std::memory_order_acq_rel))
            {
                unhook_one(m_accumulate_detour, m_accumulate_trampoline);
                unhook_one(m_lethal_detour, m_lethal_trampoline);
                return;
            }
            unhook_one(m_accumulate_detour, m_accumulate_trampoline);
            unhook_one(m_lethal_detour, m_lethal_trampoline);
        }

    private:
        VitalTraceHook() = default;
        ~VitalTraceHook() { uninstall(); }
        VitalTraceHook(const VitalTraceHook&) = delete;
        VitalTraceHook& operator=(const VitalTraceHook&) = delete;

        static constexpr uintptr_t kRVA_AccumulateDamageTaken = 0x308890;
        static constexpr uintptr_t kRVA_UpdateLethalHitGauge  = 0x308990;
        static constexpr uintptr_t kRVA_LastRoundResultType   = 0x4846408;

        struct VitalSnap
        {
            float scale {0.0f};
            float candidate {0.0f};
            float ko_gate {0.0f};
            float displayed {0.0f};
            uint32_t category_bits {0};
            int16_t state {0};
            int16_t last_round_result {0};
            bool readable {false};
        };

        std::atomic<bool> m_installed{false};
        std::unique_ptr<PLH::x64Detour> m_accumulate_detour{};
        std::unique_ptr<PLH::x64Detour> m_lethal_detour{};
        uint64_t m_accumulate_trampoline{0};
        uint64_t m_lethal_trampoline{0};

        static VitalSnap read_vital(void* chara) noexcept
        {
            VitalSnap s{};
            const uintptr_t base = NativeBinding::imageBase();
            if (!base || !chara) return s;

            const uint8_t* c = reinterpret_cast<const uint8_t*>(chara);
            bool ok = true;
            ok &= SafeReadFloat(c + ReplayScrubDiag::kChara_flVitalScale_Off,
                                &s.scale);
            ok &= SafeReadFloat(c + ReplayScrubDiag::kChara_flVitalCandidate_Off,
                                &s.candidate);
            ok &= SafeReadFloat(c + ReplayScrubDiag::kChara_flVitalKoGate_Off,
                                &s.ko_gate);
            ok &= SafeReadFloat(c + ReplayScrubDiag::kChara_flVitalDisplayed_Off,
                                &s.displayed);
            ok &= SafeReadUInt32(c + ReplayScrubDiag::kChara_dwVitalCategoryBits_Off,
                                 &s.category_bits);
            ok &= SafeReadInt16(c + ReplayScrubDiag::kChara_wVitalState_Off,
                                &s.state);
            (void)SafeReadInt16(reinterpret_cast<const void*>(
                                    base + kRVA_LastRoundResultType),
                                &s.last_round_result);
            s.readable = ok;
            return s;
        }

        static bool changed(const VitalSnap& a, const VitalSnap& b) noexcept
        {
            return a.readable != b.readable
                || a.scale != b.scale
                || a.candidate != b.candidate
                || a.ko_gate != b.ko_gate
                || a.displayed != b.displayed
                || a.category_bits != b.category_bits
                || a.state != b.state
                || a.last_round_result != b.last_round_result;
        }

        static void emit(const char* event_name,
                         void* chara,
                         int32_t damage_delta,
                         const VitalSnap& before,
                         const VitalSnap& after) noexcept
        {
            ReplayTraceFields f;
            f.hex("chara", reinterpret_cast<uintptr_t>(chara))
             .integer("damage_delta", damage_delta)
             .boolean("changed", changed(before, after))
             .boolean("before_readable", before.readable)
             .boolean("after_readable", after.readable)
             .real("before_scale", before.scale)
             .real("after_scale", after.scale)
             .real("before_candidate", before.candidate)
             .real("after_candidate", after.candidate)
             .real("candidate_delta", after.candidate - before.candidate)
             .real("before_ko_gate", before.ko_gate)
             .real("after_ko_gate", after.ko_gate)
             .real("before_displayed", before.displayed)
             .real("after_displayed", after.displayed)
             .real("displayed_delta", after.displayed - before.displayed)
             .hex("before_category_bits", before.category_bits)
             .hex("after_category_bits", after.category_bits)
             .integer("before_vital_state", before.state)
             .integer("after_vital_state", after.state)
             .integer("before_last_round_result", before.last_round_result)
             .integer("after_last_round_result", after.last_round_result);
            ReplayDebugTrace::instance().event(event_name, f);
        }

        static void __fastcall detour_accumulate(void* chara,
                                                 int32_t damage_delta)
        {
            const bool trace = ReplayDebugTrace::instance().enabled()
                && !g_replay_scrub_generation_diagnostics_suppressed.load(
                    std::memory_order_acquire);
            const VitalSnap before = trace ? read_vital(chara) : VitalSnap{};
            using Fn = void(__fastcall*)(void*, int32_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_accumulate_trampoline);
            orig(chara, damage_delta);
            if (!trace) return;
            const VitalSnap after = read_vital(chara);
            if (damage_delta != 0 || changed(before, after))
                emit("vital_damage_accumulate", chara, damage_delta,
                     before, after);
        }

        static void __fastcall detour_lethal(void* chara)
        {
            const bool trace = ReplayDebugTrace::instance().enabled()
                && !g_replay_scrub_generation_diagnostics_suppressed.load(
                    std::memory_order_acquire);
            const VitalSnap before = trace ? read_vital(chara) : VitalSnap{};
            using Fn = void(__fastcall*)(void*);
            Fn orig = reinterpret_cast<Fn>(instance().m_lethal_trampoline);
            orig(chara);
            if (!trace) return;
            const VitalSnap after = read_vital(chara);
            if (changed(before, after))
                emit("vital_lethal_update", chara, 0, before, after);
        }

        bool install_one(std::unique_ptr<PLH::x64Detour>& detour,
                         uint64_t& trampoline,
                         uintptr_t target,
                         uint64_t hook_fn,
                         const wchar_t* name)
        {
            trampoline = 0;
            detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target), hook_fn, &trampoline);
            if (!detour->hook())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[Horse.VitalTraceHook] hook failed for {} target=0x{:X}\n"),
                    name, target);
                detour.reset();
                trampoline = 0;
                return false;
            }
            return true;
        }

        static void unhook_one(std::unique_ptr<PLH::x64Detour>& detour,
                               uint64_t& trampoline) noexcept
        {
            if (detour)
            {
                detour->unHook();
                detour.reset();
            }
            trampoline = 0;
        }
    };
}
