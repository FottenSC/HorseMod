// ============================================================================
// Horse::WindRngGate - validation-only gate for visual wind RNG.
//
// Stage-wind spawn and oscillation update paths consume LuxMoveVM_GetRandU32
// draws for visual motion. During captured seek validation those visual-only
// draws can advance the shared battle LFSR or create an extra node after the
// restored gameplay step. Gate only these visual paths during validation
// instead of relaxing RNG comparison or restoring RNG after compare.
// ============================================================================

#pragma once

#include "BytePatch.hpp"
#include "SigScan.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>

namespace Horse
{
    class WindRngGate
    {
    public:
        bool resolve()
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved = true;
            m_resolved_ok = false;

            // Function entries. They are void functions and no prologue bytes
            // have executed yet, so a bare RET is stack-clean.
            void* parallel_site = sig_scan_sc6(
                "48 8B C4 53 48 81 EC D0 00 00 00 80 3D ?? ?? ?? ?? 00 48 8B D9 0F 85",
                "WindRngGate (IwWind_UpdateParallelOscillation entry)");
            if (!parallel_site) return false;

            void* ring_out_site = sig_scan_sc6(
                "48 8B C4 57 48 81 EC D0 00 00 00 80 3D ?? ?? ?? ?? 00 48 8B F9 0F 85",
                "WindRngGate (IwWind_UpdateRingOutOscillation entry)");
            if (!ring_out_site) return false;

            void* ring_in_site = sig_scan_sc6(
                "48 8B C4 53 48 81 EC D0 00 00 00 80 3D ?? ?? ?? ?? 00 48 8B D9 0F 85 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? 48 89 68 08",
                "WindRngGate (IwWind_UpdateRingInOscillation entry)");
            if (!ring_in_site) return false;

            void* spawn_site = sig_scan_sc6(
                "48 8B C4 57 48 81 EC B0 00 00 00 80 3D ?? ?? ?? ?? 00 48 8B F9 0F 85",
                "WindRngGate (LuxBattle_SpawnStageWindParticles entry)");
            if (!spawn_site) return false;

            const uint8_t ret = 0xC3;
            if (!m_parallel_patch.prepare(parallel_site, &ret, 1)) return false;
            if (!m_ring_out_patch.prepare(ring_out_site, &ret, 1)) return false;
            if (!m_ring_in_patch.prepare(ring_in_site, &ret, 1)) return false;
            if (!m_spawn_patch.prepare(spawn_site, &ret, 1)) return false;

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.WindRngGate] resolved Parallel @ 0x{:x} RingOut @ 0x{:x} RingIn @ 0x{:x} Spawn @ 0x{:x}\n"),
                reinterpret_cast<uintptr_t>(parallel_site),
                reinterpret_cast<uintptr_t>(ring_out_site),
                reinterpret_cast<uintptr_t>(ring_in_site),
                reinterpret_cast<uintptr_t>(spawn_site));
            return true;
        }

        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.WindRngGate] enable() before resolve()\n"));
                return false;
            }
            if (m_enabled.load(std::memory_order_acquire)) return true;
            if (!m_parallel_patch.enable()) return false;
            if (!m_ring_out_patch.enable())
            {
                m_parallel_patch.disable();
                return false;
            }
            if (!m_ring_in_patch.enable())
            {
                m_ring_out_patch.disable();
                m_parallel_patch.disable();
                return false;
            }
            if (!m_spawn_patch.enable())
            {
                m_ring_in_patch.disable();
                m_ring_out_patch.disable();
                m_parallel_patch.disable();
                return false;
            }
            m_enabled.store(true, std::memory_order_release);
            return true;
        }

        void disable()
        {
            if (!m_enabled.load(std::memory_order_acquire)) return;
            m_spawn_patch.disable();
            m_ring_in_patch.disable();
            m_ring_out_patch.disable();
            m_parallel_patch.disable();
            m_enabled.store(false, std::memory_order_release);
        }

        bool is_enabled() const
        {
            return m_enabled.load(std::memory_order_acquire);
        }

        bool is_resolved() const { return m_resolved_ok; }

    private:
        BytePatch m_parallel_patch{};
        BytePatch m_ring_out_patch{};
        BytePatch m_ring_in_patch{};
        BytePatch m_spawn_patch{};
        std::atomic<bool> m_enabled{false};
        bool m_resolved{false};
        bool m_resolved_ok{false};
    };
}
