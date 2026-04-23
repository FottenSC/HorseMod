// ============================================================================
// Horse::VFXOff — disable SC6 hit-effect VFX by trampoline-injecting a
// sentinel-write into a per-frame VFX-slot writer.
//
// Origin
// ------
// Ported from somberness's CE table ("SC6nepafu.CT", the "VFX off"
// cheat).  The CE script targets the tail of a function at
// SoulcaliburVI.exe+5FE2EF9 that ZEROS a slot in some VFX state array:
//
//   ; rax = [rbx+0x400], rdi = slot index
//   F3 0F 11 34 B8        movss [rax+rdi*4], xmm6     ; <- INJECT HERE
//   48 8B 7C 24 40        mov   rdi, [rsp+0x40]
//   ...
//   5B                    pop   rbx
//   C3                    ret
//
// xmm6 is zero at this point (loaded earlier in the function via
// xorps xmm6,xmm6 or similar).  So the writer is "clear this slot to
// 0".  Empirically, the engine's downstream renderer treats slot==0 as
// "active hit effect, render this".  The CE patch overrides the write
// to a sentinel constant 0x468FAF9C (~73567.22f) which the renderer
// interprets as "out of range / culled" and silently skips.
//
// Why a trampoline (not just a NOP)
// ---------------------------------
// We can't NOP the original write — that would FREEZE whatever was
// last in the slot, which is "render this VFX" if anything was active
// when we toggled.  We can't replace the 5-byte movss with a 7-byte
// `mov [rax+rdi*4], imm32` (the immediate-write encoding is too long
// to fit in 5 bytes).  We need to keep the original write AND add a
// second write of the sentinel.  That's a midfunction trampoline:
//
//   1. JMP at injection site -> our cave page.
//   2. Cave: `movss [rax+rdi*4], xmm6` (orig), then
//            `mov   [rax+rdi*4], 0x468FAF9C` (sentinel),
//            then `jmp` back to (injection_site + 5).
//
// The CE script does exactly this, just with CE's auto-managed
// allocator instead of our own near-module CodeCave.
//
// vs. the existing DestroyAllVFx polling
// --------------------------------------
// HorseMod already had a "Suppress VFX" toggle that polled
// ALuxVFxInstanceManager::DestroyAllVFx every frame via UFunction
// reflection.  This bytepatch is strictly cheaper (zero per-frame work)
// and prevents effects from spawning at all rather than nuking them
// after they've spawned.  Both are now exposed; the bytepatch one is
// the recommended default.
//
// Limits
// ------
//   * If the AOB stops matching after an SC6 patch, the toggle becomes
//     a no-op and resolve() logs a warning.
//   * The sentinel value (0x468FAF9C) is what the CE author empirically
//     determined kills rendering — we don't independently understand
//     why.  If a future SC6 patch changes that semantic the toggle
//     might silently stop working visibly even though the patch is
//     applied; user should file a bug.
//   * We don't expose a "freeze active VFX" mode (NOP without
//     sentinel-write); easy to add later as a sibling class if useful.
//
// Threading: same as CamLock — toggle from the ImGui callback only.
// ============================================================================

#pragma once

#include "BytePatch.hpp"
#include "CodeCave.hpp"
#include "SigScan.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <cstdint>
#include <cstring>

namespace Horse
{
    class VFXOff
    {
    public:
        // Sig-scan, allocate trampoline in CodeCave, prepare the
        // 5-byte JMP patch.  Idempotent.  Returns true iff every step
        // succeeded.  Safe before any battle is loaded.
        bool resolve()
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved = true;
            m_resolved_ok = false;

            // 7-byte AOB; the trailing "48 8B" of the next instruction
            // disambiguates from any other movss [rax+rdi*4],xmm6 in
            // the binary (they exist — VFX bookkeeping is repetitive).
            void* site = sig_scan_sc6(
                "F3 0F 11 34 B8 48 8B", "VFXOff site");
            if (!site) return false;

            // Cave allocation for the trampoline.  17 bytes:
            //   5 (orig movss) + 7 (sentinel mov-imm) + 5 (jmp back).
            constexpr size_t kTrampSize = 5 + 7 + 5;
            void* tramp = CodeCave::allocate(kTrampSize);
            if (!tramp)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.VFXOff] cave allocation failed\n"));
                return false;
            }

            // Build the trampoline content into a local buffer first,
            // then memcpy into the cave page in one shot.  The cave
            // page is PAGE_EXECUTE_READWRITE so direct write is fine,
            // but staging makes the code easier to read.
            //
            // Layout:
            //   +0x00  F3 0F 11 34 B8                        ; movss [rax+rdi*4], xmm6 (orig)
            //   +0x05  C7 04 B8 9C AF 8F 46                  ; mov dword ptr [rax+rdi*4], 0x468FAF9C
            //   +0x0C  E9 xx xx xx xx                        ; jmp back to (site + 5)
            uint8_t buf[kTrampSize];
            const uint8_t orig[5]   = { 0xF3, 0x0F, 0x11, 0x34, 0xB8 };
            const uint8_t patch[7]  = { 0xC7, 0x04, 0xB8,
                                        0x9C, 0xAF, 0x8F, 0x46 };
            std::memcpy(buf + 0,     orig,  sizeof(orig));
            std::memcpy(buf + 5,     patch, sizeof(patch));

            uint8_t jmp_back[5];
            void* back_target = static_cast<uint8_t*>(site) + 5;
            void* jmp_back_at = static_cast<uint8_t*>(tramp) + 12;
            if (!encode_jmp_rel32(jmp_back_at, back_target, jmp_back))
                return false;
            std::memcpy(buf + 12, jmp_back, sizeof(jmp_back));

            std::memcpy(tramp, buf, kTrampSize);
            ::FlushInstructionCache(::GetCurrentProcess(), tramp, kTrampSize);

            // Build the 5-byte JMP that goes at the injection site,
            // pointing at our trampoline.
            uint8_t jmp_to_tramp[5];
            if (!encode_jmp_rel32(site, tramp, jmp_to_tramp))
                return false;

            if (!m_jmp_patch.prepare(site, jmp_to_tramp, sizeof(jmp_to_tramp)))
                return false;

            m_trampoline = tramp;
            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.VFXOff] resolved: site=0x{:x} tramp=0x{:x}\n"),
                reinterpret_cast<uintptr_t>(site),
                reinterpret_cast<uintptr_t>(tramp));
            return true;
        }

        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.VFXOff] enable() before successful "
                        "resolve() — ignoring\n"));
                return false;
            }
            if (m_jmp_patch.is_enabled()) return true;
            const bool ok = m_jmp_patch.enable();
            if (ok)
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[Horse.VFXOff] VFX writes redirected to sentinel\n"));
            return ok;
        }

        void disable()
        {
            if (!m_jmp_patch.is_enabled()) return;
            m_jmp_patch.disable();
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.VFXOff] VFX writes restored\n"));
        }

        void set(bool on)
        {
            if (on)
            {
                if (!m_resolved) resolve();
                enable();
            }
            else
            {
                disable();
            }
        }

        bool is_enabled()  const { return m_jmp_patch.is_enabled(); }
        bool is_resolved() const { return m_resolved_ok; }

    private:
        BytePatch m_jmp_patch{};
        void*     m_trampoline = nullptr;
        bool      m_resolved    = false;
        bool      m_resolved_ok = false;
    };

} // namespace Horse
