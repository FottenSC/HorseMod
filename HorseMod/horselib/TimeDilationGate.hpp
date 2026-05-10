// ============================================================================
// Horse::TimeDilationGate — force LuxMoveVM_GetTimeDilationScalar to return
// 0 when WorldTickGate's policy slot is 0 (frozen).  Sibling to WorldTickGate
// / ReplayClockGate / ActorTickGate.
//
// Why this exists
// ---------------
// The user reported that holding HorseMod freeze in match-replay watching
// for longer than a round duration still let the chara animation play out,
// the BM round timer count down, and the replay auto-advance to the next
// round.  Investigation traced the leak to the time-dilation scalar
// function:
//
//   float LuxMoveVM_GetTimeDilationScalar(chara) {                  // 0x14030A8C0
//     // ... outer guards ...
//     if (DAT_144846458 < 0) {
//       if (chara->state_at0x19EC != 2 ||  ← chara in NORMAL play (state==2)
//           (charaKindByte != 0 && opp.state == 2)) {
//         // path A or B — CONSULTS VMFreezeByte
//         return (VMFreezeByte == 0 ? speedval : 0) * scale;
//       }
//       // chara state==2: skip path A/B, fall through ↓
//     }
//     else if (DAT_144846458 != charaKindByte) {
//       return 0.0;
//     }
//     return chara->baseTimeScale_at0x3500;  ← FALL-THROUGH (bypasses VMFreezeByte!)
//   }
//
// In match-replay watching, the chara is in normal play state (+0x19EC ==
// 2) — even though the inputs come from the replay file, the state byte
// is still 2.  For P1 (charaKindByte==0), the state==2 branch falls
// through to `return chara+0x3500` (the per-chara base time-scale, normally
// 1.0) — completely BYPASSING VMFreezeByte.
//
// So setting VMFreezeByte=1 has no effect on P1 in normal play.  P1's
// time-dilation = 1.0 → UE4 anim advances normally → BM Update_Impl
// ticks BattleTime FName at native dt → round timer counts down → on
// round-end the simulation transitions and the replay auto-advances.
//
// Hit-stop "works" in stock SC6 because the engine writes chara+0x3510 to
// a negative value (Path A) or transitions chara state out of 2 — making
// the VMFreezeByte path actually execute.  HorseMod doesn't touch those
// fields, so VMFreezeByte alone never engages the freeze path for normal-
// play charas.
//
// The fix
// -------
// Patch the function entry to JMP into a trampoline that, when the
// WorldTickGate policy slot is 0, ZEROES XMM0 and bare RETs — making the
// function return 0.0 unconditionally.  When policy is non-zero (running
// or step credits armed), the trampoline replicates the displaced 7-byte
// MOVZX EDX prologue and JMPs back to function+7 to execute normally.
//
// Bare RET at entry is safe:
//   * The function returns float in XMM0 (Windows x64 ABI).  Zeroing XMM0
//     via `xorps xmm0, xmm0` produces a clean +0.0 return.
//   * No prologue bytes have executed at entry — RSP unchanged, all
//     callee-saved registers (XMM6-15, RBX, RSI, RDI, R12-15) untouched.
//   * The function in stock SC6 doesn't save callee-saved regs in its
//     prologue (verified by disassembly), so we don't need to either.
//
// Effect at every consumer of LuxMoveVM_GetTimeDilationScalar's return:
// position integration in LuxBattleChara_IntegratePhysics_PerTick,
// MoveVM lane advance, hit detection, animation montage advance, and
// every other dt-multiply integrator throughout the engine all see 0.0
// and produce 0 deltas.  Combined with WorldTickGate's RET on Site 9,
// this halts the simulation completely — including UE4 anim instances
// that scale by the engine's tick dilation.
//
// Sibling gates kept active in lockstep:
//   * WorldTickGate          — RETs PerFrameTick body
//   * ReplayClockGate        — pins replay master clock
//   * ActorTickGate          — RETs surrounding Actor::Tick prologues
//   * THIS gate              — forces time-dilation to 0
//   * VMFreezeByte (legacy)  — kept enabled for the path-A/B branch
//                              (still affects hit-stop / cinematic flows)
//
// Trampoline reads the policy slot, never decrements — Site 9 stays the
// sole step-credit consumer.  Same rationale as ReplayClockGate.
// ============================================================================

#pragma once

#include "BytePatch.hpp"
#include "CodeCave.hpp"
#include "SigScan.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace Horse
{
    class TimeDilationGate
    {
    public:
        // Sig-scan LuxMoveVM_GetTimeDilationScalar's entry, build the
        // freeze-trampoline reading from the shared `policy_slot`,
        // prepare the BytePatch.  Idempotent.
        bool resolve(int32_t* policy_slot)
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved    = true;
            m_resolved_ok = false;

            if (!policy_slot)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.TimeDilationGate] resolve() got null policy "
                        "slot — call WorldTickGate.resolve() first and pass "
                        "its policy_slot_address()\n"));
                return false;
            }

            // AOB locks the function prologue through the XOR RAX,1:
            //   0F B6 91 3C 02 00 00            movzx edx, [rcx+0x23C]
            //   4C 8D 0D ?? ?? ?? ??            lea   r9, [VMFreezeRecord]
            //   8B C2                           mov   eax, edx
            //   48 83 F0 01                     xor   rax, 1
            //
            // Wildcard the LEA's RIP-relative disp32 (4 bytes) so the
            // pattern survives a binary rebase.
            void* site = sig_scan_sc6(
                "0F B6 91 3C 02 00 00 4C 8D 0D ?? ?? ?? ?? 8B C2 48 83 F0 01",
                "TimeDilationGate (LuxMoveVM_GetTimeDilationScalar entry)");
            if (!site) return false;

            // We displace the first 7 bytes (the MOVZX EDX, [RCX+0x23C]).
            // The next instruction at site+7 is the LEA R9, [...] which
            // is what the trampoline jumps back to in the run-normal path.
            constexpr size_t kOrigLen = 7;

            // Trampoline layout (26 bytes):
            //   [0x00] 8B 05 <disp32_policy>     mov  eax, [rip+policy]
            //   [0x06] 85 C0                     test eax, eax
            //   [0x08] 75 04                     jne  +4 (skip xorps+ret to run-normal)
            //   [0x0A] 0F 57 C0                  xorps xmm0, xmm0  (return 0.0)
            //   [0x0D] C3                        ret
            //   [0x0E] <7 displaced prologue bytes>  movzx edx, [rcx+0x23C]
            //   [0x15] E9 <rel32>                jmp <site+7> (LEA)
            //   [0x1A] (end)
            constexpr size_t kTrampSize = 26;
            void* tramp = CodeCave::allocate(kTrampSize);
            if (!tramp)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.TimeDilationGate] cave alloc failed\n"));
                return false;
            }

            uint8_t buf[kTrampSize] = {0};
            size_t off = 0;

            // [0x00] mov eax, [rip+disp32_policy]
            buf[off++] = 0x8B;
            buf[off++] = 0x05;
            {
                const int64_t disp =
                      reinterpret_cast<int64_t>(policy_slot)
                    - (reinterpret_cast<int64_t>(tramp) + off + 4);
                if (disp < INT32_MIN || disp > INT32_MAX) return false;
                const int32_t d32 = static_cast<int32_t>(disp);
                std::memcpy(&buf[off], &d32, sizeof(d32));
                off += 4;
            }

            // [0x06] test eax, eax
            buf[off++] = 0x85;
            buf[off++] = 0xC0;

            // [0x08] jne +4 (jumps over the xorps + ret that follow)
            buf[off++] = 0x75;
            buf[off++] = 0x04;

            // [0x0A] xorps xmm0, xmm0 — zero the float return register
            buf[off++] = 0x0F;
            buf[off++] = 0x57;
            buf[off++] = 0xC0;

            // [0x0D] ret
            buf[off++] = 0xC3;

            // [0x0E] replicated 7-byte MOVZX EDX, [RCX+0x23C]
            std::memcpy(&buf[off], site, kOrigLen);
            off += kOrigLen;

            // [0x15] jmp rel32 -> site + 7  (back to LEA R9, [VMFreezeRecord])
            {
                uint8_t jmp_back[5];
                void* jmp_at      = static_cast<uint8_t*>(tramp) + off;
                void* back_target = static_cast<uint8_t*>(site) + kOrigLen;
                if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                    return false;
                std::memcpy(&buf[off], jmp_back, sizeof(jmp_back));
                off += 5;
            }

            std::memcpy(tramp, buf, off);
            ::FlushInstructionCache(::GetCurrentProcess(), tramp, off);

            // Site patch: 5-byte JMP rel32 to trampoline + 2 NOPs over
            // the displaced 7-byte MOVZX.
            uint8_t patch_buf[7];
            uint8_t jmp[5];
            if (!encode_jmp_rel32(site, tramp, jmp)) return false;
            std::memcpy(patch_buf, jmp, 5);
            patch_buf[5] = 0x90; // NOP
            patch_buf[6] = 0x90; // NOP

            if (!m_patch.prepare(site, patch_buf, kOrigLen))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.TimeDilationGate] patch.prepare() failed\n"));
                return false;
            }

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.TimeDilationGate] resolved (entry @ 0x{:x}, "
                    "tramp @ 0x{:x})\n"),
                reinterpret_cast<uintptr_t>(site),
                reinterpret_cast<uintptr_t>(tramp));
            return true;
        }

        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.TimeDilationGate] enable() before resolve() "
                        "— ignoring\n"));
                return false;
            }
            if (m_enabled.load(std::memory_order_acquire)) return true;
            if (!m_patch.enable())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.TimeDilationGate] BytePatch.enable() failed\n"));
                return false;
            }
            m_enabled.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.TimeDilationGate] enabled\n"));
            return true;
        }

        void disable()
        {
            if (!m_enabled.load(std::memory_order_acquire)) return;
            m_patch.disable();
            m_enabled.store(false, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.TimeDilationGate] disabled\n"));
        }

        bool is_enabled()  const { return m_enabled.load(std::memory_order_acquire); }
        bool is_resolved() const { return m_resolved_ok; }

    private:
        BytePatch m_patch{};
        bool      m_resolved    = false;
        bool      m_resolved_ok = false;
        std::atomic<bool> m_enabled{false};
    };

} // namespace Horse
