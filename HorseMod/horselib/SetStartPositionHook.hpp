// ============================================================================
// Horse::SetStartPositionHook — direct C++ hook on
// LuxBattleChara_SetStartPosition @ image+0x301E60.
//
// Why this exists
// ---------------
// The first attempt at the reset-override feature hooked four candidate
// UFunctions on LuxBattleManager / LuxBattleGameMode (TrainingModePosition-
// Reset, RestartBattle, RestartBattleImmediately, RequestTrainingModeBattle-
// Reset).  All four registered cleanly on game start, the user pressed
// their reset bind in training mode, and ZERO of the four post-hooks
// fired.  Conclusion: the user's reset bind is dispatched via a path that
// never goes through the BlueprintCallable layer — most likely a direct
// C++ call from the input handler into the engine's reset chain
// (RawInput -> training-mode controller -> reset-both-charas -> ...).
//
// What does universally fire on every reset path is
// LuxBattleChara_SetStartPosition.  Per Ghidra xrefs (the plate comment
// on 0x140301e60 enumerates them):
//
//   LuxBattle_InitCharaRoundPositionAndFacing
//   LuxBattle_InitializeMatchRoundState           (x2)
//   LuxBattle_PositionCharasSymmetrically         (round intro / training reset)
//   LuxBattle_AllocAndInitCharaSlot               (chara construction)
//   LuxBattle_ResetBothCharaPositionsAndFacing    (reset bind chain)
//   LuxBattle_SetCharaSlotStartPosition
//
// All paths funnel through SetStartPosition.  Hooking it directly means
// we catch every reset, regardless of whether the trigger is a UFunction,
// a state-machine transition, or a raw input event.
//
// ----------------------------------------------------------------------------
// Why we need an assembly stub instead of just a C++ detour function
// ----------------------------------------------------------------------------
// LuxBattle_PositionCharasSymmetrically (disasm @ 140302670) calls
// SetStartPosition like this (instructions excerpted around the CALL):
//
//   140302737: MOV  R10, qword ptr [RDI]      ; R10 = chara ptr (set BEFORE the call)
//   ...
//   1403027a4: ADDSS XMM1, XMM13               ; XMM1 = arg X
//   1403027a9: CVTDQ2PS XMM0, XMM0             ; XMM0 = some compiler-threaded value
//   1403027ac: ADDSS XMM4, XMM12
//   1403027b1: MULSS XMM0, XMM6
//   1403027b5: ADDSS XMM0, XMM5
//   1403027b9: TEST R14, R14
//   1403027bc: JZ   1403027c2
//   1403027be: ADDSS XMM0, dword ptr [RBX]    ; XMM0 finalised pre-call
//   1403027c2: MOVAPS XMM3, XMM4               ; XMM3 = arg Z
//   1403027c5: XORPS  XMM2, XMM2               ; XMM2 = arg Y (=0.0)
//   1403027c8: MOV    RCX, R10                 ; RCX = chara
//   1403027cb: CALL   0x140301e60              ; SetStartPosition
//   1403027d0: DIVSS  XMM0, XMM9               ; <<< READS XMM0 POST-CALL
//   1403027d5: MOV    [R10 + 0x98], R15D       ; <<< READS R10 POST-CALL
//   1403027dc: MOV    [R10 + 0x90], R15D
//   1403027e3: MOVSS  [R10 + 0x94], XMM0
//   ...
//   140302815: INC    R11D                     ; <<< READS R11 POST-CALL (loop counter)
//   1403028e1: CMP    R11D, 0x2
//   1403028e5: JL     140302737                ; ...and JLs based on the loop counter
//
// In MS x64 ABI XMM0, R10, and R11 are VOLATILE — a callee is permitted
// to clobber them.  The original caller is technically wrong to rely on
// them surviving the call.  But vanilla SetStartPosition's body
// (140301e60..140301f8b) only uses RAX, RCX, RDX, R8, R9, XMM1, XMM2,
// XMM3 — it never touches R10, R11, XMM0, XMM4, XMM5.  PositionChara-
// sSymmetrically's compiler exploited that to save stack spills:
//   - XMM0 carries an intermediate (the loop_counter*const+const yaw
//     contribution) that's divided by XMM9 after the call to produce
//     the chara's facing-velocity X.
//   - R10 carries the chara ptr (avoid re-loading it from memory).
//   - R11 carries the loop counter (R11D is read with INC then CMP).
//
// The MOMENT we install a C++ detour at SetStartPosition's address, our
// detour's prologue / body / epilogue WILL clobber R10, R11, XMM0 — the
// MSVC code generator uses them freely for any temporary.  When P1's
// helper returns and PositionCharasSymmetrically resumes:
//
//   * R10 holds garbage  ->  MOV [R10 + 0x98], R15D writes to a random
//                            address  ->  immediate AV crash, or silent
//                            memory corruption that surfaces later.
//   * R11D holds garbage ->  INC R11D + CMP/JL exits the loop early or
//                            never (uninitialised loop variable).  The
//                            "no P2 helper fired" symptom in our log
//                            matches early exit on wraparound.
//   * XMM0 holds garbage ->  DIVSS XMM0, XMM9 produces a NaN/Inf that
//                            is then written to chara+0x94 (velocity X)
//                            and chara+0x22c (camera-anchor X).  Camera
//                            angle goes infinite -> render goes black
//                            for one frame -> physics re-derive a sane
//                            value next tick.  This is exactly the
//                            "weird teleport + black screen" the user
//                            saw with the override DISABLED before we
//                            preserved XMM0.
//
// Fix: wrap our C++ helper in an assembly stub that preserves every
// volatile register vanilla SetStartPosition de facto preserves:
//
//   General-purpose: R10, R11
//                    (RAX, RDX, R8, R9 are touched by vanilla too,
//                    so callers can't rely on those.  RCX is the
//                    chara ptr; PositionCharasSymmetrically saves
//                    its own copy in R10.)
//   Vector:          XMM0, XMM4, XMM5
//                    (XMM1, XMM2, XMM3 are arg slots and vanilla
//                    modifies XMM2 itself, so callers don't rely
//                    on those.  XMM6-XMM15 are non-volatile so the
//                    helper compiler saves them itself.)
//
// The stub is allocated at install() time via VirtualAlloc(MEM_COMMIT,
// PAGE_EXECUTE_READWRITE), written byte-by-byte, then frozen to
// PAGE_EXECUTE_READ.  PolyHook x64Detour points at the stub instead of
// our C++ function — the compiler never gets a chance to corrupt these
// registers in the post-helper return path because the asm stub puts
// the saved values back before RET.
//
// Stack layout (offsets from RSP after `sub rsp, 0x68`):
//
//   [0x00..0x1F]  32B home space for the helper call (MS x64 ABI)
//   [0x20..0x2F]  16B saved XMM0
//   [0x30..0x3F]  16B saved XMM4
//   [0x40..0x4F]  16B saved XMM5
//   [0x50..0x57]   8B saved R10
//   [0x58..0x5F]   8B saved R11
//   [0x60..0x67]   8B align pad
//
// Initial RSP at stub entry is 8-mod-16 (after the caller's CALL push).
// After `sub rsp, 0x68` (=104), RSP = (8-104) mod 16 = 0 mod 16, so
// the helper sees a 16-aligned stack on its CALL push (16-aligned
// becomes 8-mod-16 inside the helper, which is what the ABI expects).
//
// Stub disassembly (x86-64, 77 bytes):
//
//   00  48 83 EC 68               sub    rsp, 0x68
//   04  F3 0F 7F 44 24 20         movdqu [rsp+0x20], xmm0
//   0A  F3 0F 7F 64 24 30         movdqu [rsp+0x30], xmm4
//   10  F3 0F 7F 6C 24 40         movdqu [rsp+0x40], xmm5
//   16  4C 89 54 24 50            mov    [rsp+0x50], r10
//   1B  4C 89 5C 24 58            mov    [rsp+0x58], r11
//   20  48 B8 .. .. .. .. .. .. .. ..   mov rax, <helper addr>
//   2A  FF D0                     call   rax
//   2C  4C 8B 5C 24 58            mov    r11, [rsp+0x58]
//   31  4C 8B 54 24 50            mov    r10, [rsp+0x50]
//   36  F3 0F 6F 6C 24 40         movdqu xmm5, [rsp+0x40]
//   3C  F3 0F 6F 64 24 30         movdqu xmm4, [rsp+0x30]
//   42  F3 0F 6F 44 24 20         movdqu xmm0, [rsp+0x20]
//   48  48 83 C4 68               add    rsp, 0x68
//   4C  C3                        ret
//
// Threading
// ---------
// SetStartPosition is called from the game thread.  Our stub + helper run
// on the same thread.  The helper reads atomic state (ResetOverride::
// enabled, captured-pose snapshot under its mutex).  No additional locks
// in the stub itself (it's pure register / stack manipulation).
//
// Install / uninstall
// -------------------
// install() is called once from on_unreal_init AFTER NativeBinding::
// resolve() has succeeded.  Idempotent.  uninstall() is called from
// the dtor.  If install() fails (PolyHook returns false, VirtualAlloc
// fails, etc.) we log and fall back to no-op mode — the override toggle
// becomes inert but the rest of the mod is unaffected.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "ResetOverride.hpp"
#include "KHitWalker.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace Horse
{
    class SetStartPositionHook
    {
    public:
        static SetStartPositionHook& instance()
        {
            static SetStartPositionHook s;
            return s;
        }

        // Install the detour.  Returns true if the hook is active after
        // this call (already-installed counts as success).  Safe to call
        // before NativeBinding::resolve() — returns false in that case.
        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            if (!NativeBinding::hasSetStartPosition())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[SetStartPositionHook] NativeBinding not resolved — "
                        "cannot install (call NativeBinding::resolve() first)\n"));
                return false;
            }

            // 1. Allocate a small executable page and write the
            //    XMM0-save asm stub into it.  This is what PolyHook
            //    will redirect SetStartPosition's call site to.
            if (!build_xmm0_save_stub())
            {
                return false;
            }

            // 2. Install the PolyHook x64Detour from SetStartPosition
            //    onto our stub (NOT directly onto the C++ helper).
            const uintptr_t target = NativeBinding::imageBase()
                + NativeBinding::kLuxBattleCharaSetStartPositionRVA;

            m_trampoline = 0;
            m_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(m_stub_page),
                &m_trampoline);

            if (!m_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[SetStartPositionHook] x64Detour::hook() failed "
                        "on LuxBattleChara_SetStartPosition (target=0x{:X}). "
                        "Reset-override will be inert.\n"),
                    target);
                m_detour.reset();
                free_stub_page();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[SetStartPositionHook] installed (target=0x{:X}, "
                    "stub=0x{:X}, helper=0x{:X}, trampoline=0x{:X})\n"),
                target,
                reinterpret_cast<uintptr_t>(m_stub_page),
                reinterpret_cast<uintptr_t>(&SetStartPositionHook::helper),
                static_cast<uintptr_t>(m_trampoline));
            return true;
        }

        void uninstall()
        {
            if (!m_installed.exchange(false)) return;
            if (m_detour)
            {
                m_detour->unHook();
                m_detour.reset();
            }
            m_trampoline = 0;
            free_stub_page();
        }

        bool installed() const noexcept
        {
            return m_installed.load(std::memory_order_acquire);
        }

    private:
        SetStartPositionHook() = default;
        ~SetStartPositionHook() { uninstall(); }
        SetStartPositionHook(const SetStartPositionHook&)            = delete;
        SetStartPositionHook& operator=(const SetStartPositionHook&) = delete;

        // ------------------------------------------------------------------
        // Build the XMM0-save stub.  Sets m_stub_page on success.
        // Returns false on VirtualAlloc / VirtualProtect failure.
        // ------------------------------------------------------------------
        bool build_xmm0_save_stub()
        {
            static constexpr SIZE_T kStubSize = 0x80;   // 128 bytes — well over what we need

            void* page = ::VirtualAlloc(nullptr, kStubSize,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_READWRITE);
            if (!page)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[SetStartPositionHook] VirtualAlloc failed err={}\n"),
                    ::GetLastError());
                return false;
            }

            // ---- Write the stub bytes ------------------------------------
            // See the file-header doc-comment for the design / layout
            // rationale.  Saves R10, R11, XMM0, XMM4, XMM5 — every
            // volatile reg PositionCharasSymmetrically + other vanilla
            // callers rely on surviving the SetStartPosition call.
            uint8_t*       p           = static_cast<uint8_t*>(page);
            const uint64_t helper_addr =
                reinterpret_cast<uint64_t>(&SetStartPositionHook::helper);

            // sub rsp, 0x68     ; 48 83 EC 68
            *p++ = 0x48; *p++ = 0x83; *p++ = 0xEC; *p++ = 0x68;
            // movdqu [rsp+0x20], xmm0   ; F3 0F 7F 44 24 20
            *p++ = 0xF3; *p++ = 0x0F; *p++ = 0x7F; *p++ = 0x44; *p++ = 0x24; *p++ = 0x20;
            // movdqu [rsp+0x30], xmm4   ; F3 0F 7F 64 24 30
            *p++ = 0xF3; *p++ = 0x0F; *p++ = 0x7F; *p++ = 0x64; *p++ = 0x24; *p++ = 0x30;
            // movdqu [rsp+0x40], xmm5   ; F3 0F 7F 6C 24 40
            *p++ = 0xF3; *p++ = 0x0F; *p++ = 0x7F; *p++ = 0x6C; *p++ = 0x24; *p++ = 0x40;
            // mov [rsp+0x50], r10       ; 4C 89 54 24 50
            *p++ = 0x4C; *p++ = 0x89; *p++ = 0x54; *p++ = 0x24; *p++ = 0x50;
            // mov [rsp+0x58], r11       ; 4C 89 5C 24 58
            *p++ = 0x4C; *p++ = 0x89; *p++ = 0x5C; *p++ = 0x24; *p++ = 0x58;
            // mov rax, <helper addr>    ; 48 B8 <8 bytes>
            *p++ = 0x48; *p++ = 0xB8;
            std::memcpy(p, &helper_addr, sizeof(helper_addr));
            p += sizeof(helper_addr);
            // call rax                  ; FF D0
            *p++ = 0xFF; *p++ = 0xD0;
            // mov r11, [rsp+0x58]       ; 4C 8B 5C 24 58
            *p++ = 0x4C; *p++ = 0x8B; *p++ = 0x5C; *p++ = 0x24; *p++ = 0x58;
            // mov r10, [rsp+0x50]       ; 4C 8B 54 24 50
            *p++ = 0x4C; *p++ = 0x8B; *p++ = 0x54; *p++ = 0x24; *p++ = 0x50;
            // movdqu xmm5, [rsp+0x40]   ; F3 0F 6F 6C 24 40
            *p++ = 0xF3; *p++ = 0x0F; *p++ = 0x6F; *p++ = 0x6C; *p++ = 0x24; *p++ = 0x40;
            // movdqu xmm4, [rsp+0x30]   ; F3 0F 6F 64 24 30
            *p++ = 0xF3; *p++ = 0x0F; *p++ = 0x6F; *p++ = 0x64; *p++ = 0x24; *p++ = 0x30;
            // movdqu xmm0, [rsp+0x20]   ; F3 0F 6F 44 24 20
            *p++ = 0xF3; *p++ = 0x0F; *p++ = 0x6F; *p++ = 0x44; *p++ = 0x24; *p++ = 0x20;
            // add rsp, 0x68             ; 48 83 C4 68
            *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x68;
            // ret                       ; C3
            *p++ = 0xC3;

            // Pad the rest with INT3 in case execution ever runs past the
            // RET (it shouldn't, but cheap insurance).
            const size_t used = static_cast<size_t>(p - static_cast<uint8_t*>(page));
            std::memset(p, 0xCC, kStubSize - used);

            // Flip page protection to executable + read-only and flush
            // the i-cache so the CPU doesn't execute stale d-cache copy.
            DWORD old_protect = 0;
            if (!::VirtualProtect(page, kStubSize, PAGE_EXECUTE_READ, &old_protect))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[SetStartPositionHook] VirtualProtect failed err={}\n"),
                    ::GetLastError());
                ::VirtualFree(page, 0, MEM_RELEASE);
                return false;
            }
            ::FlushInstructionCache(::GetCurrentProcess(), page, kStubSize);

            m_stub_page      = page;
            m_stub_page_size = kStubSize;
            return true;
        }

        void free_stub_page()
        {
            if (m_stub_page)
            {
                ::VirtualFree(m_stub_page, 0, MEM_RELEASE);
                m_stub_page      = nullptr;
                m_stub_page_size = 0;
            }
        }

        // ------------------------------------------------------------------
        // The C++ helper.  Called BY the asm stub, which has already saved
        // XMM0 to its own stack frame.  We can clobber XMM0 freely here —
        // the stub will restore it before returning to SetStartPosition's
        // original caller.
        //
        // Same signature as LuxBattleChara_SetStartPosition.  The asm stub
        // passes through RCX, XMM1-3 unchanged, so this is just a normal
        // __fastcall function from C++'s perspective.
        // ------------------------------------------------------------------
        static void __fastcall helper(void* chara, float x, float y, float z)
        {
            auto& self = instance();

            using Fn = void(__fastcall*)(void*, float, float, float);
            Fn orig = reinterpret_cast<Fn>(self.m_trampoline);

            auto& ro = ResetOverride::instance();

            // Fast path: override disabled.  Forward to trampoline
            // unchanged.  Side-effect-free w.r.t. XMM0 — the trampoline
            // itself doesn't touch XMM0, and the stub restores it after
            // we return.
            if (!ro.enabled() || !chara || !orig)
            {
                if (orig) orig(chara, x, y, z);
                return;
            }

            // Identify which player slot (if any) this chara belongs to.
            // SetStartPosition is also called for non-player contexts
            // (chara construction off the menu) — those don't match
            // either slot, and we pass through unchanged.
            int matched_player = -1;
            for (uint32_t pi = 0; pi < 2; ++pi)
            {
                void* player_chara = KHitWalker::charaSlotFromGlobal(pi);
                if (player_chara && player_chara == chara)
                {
                    matched_player = static_cast<int>(pi);
                    break;
                }
            }

            if (matched_player < 0)
            {
                if (orig) orig(chara, x, y, z);
                return;
            }

            const auto pose = ro.get_pose(matched_player);
            if (!pose.has)
            {
                // Toggle on but no captured pose for this player — leave
                // the engine's spawn pose alone for them.
                if (orig) orig(chara, x, y, z);
                return;
            }

            // Y-coordinate semantics:
            //
            // SetStartPosition's Y arg is the chara's "spawn Y" — the
            // engine always passes 0.0 here (see e.g. PositionChara-
            // sSymmetrically @ 140302670 and InitCharaRoundPosition-
            // AndFacing @ 140302280 — both call SetStartPosition with
            // y=0.0 unconditionally).  The function itself adds
            // DAT_143e8a33c (the per-stage ground-plane offset) inside
            // and writes y+offset to the render-Y at +0x2094.
            //
            // The Y we capture from chara+0xC4 during gameplay is the
            // chara's CURRENT logical Y, which drifts ~1.0 above 0.0
            // even when the chara is just standing on the ground —
            // because the gameplay-Y is measured at the chara's pelvis
            // (or some other reference) while the spawn-Y references
            // the feet.  Passing the captured Y straight into
            // SetStartPosition spawns the chara ~1 unit above the
            // ground; gravity then yanks them down to the ground over
            // ~5 frames — the user sees a brief "teleport into the
            // air, then fall" artifact.
            //
            // Fix: send 0.0 as Y (matching the engine's own callers),
            // and let DAT_143e8a33c + the chara's standard physics
            // settle the chara onto the stage's ground plane.  The
            // captured pose.pos_y is still preserved in our struct so
            // the UI readout shows what we sampled — only the value
            // we *send to SetStartPosition* gets clamped to 0.0.
            const float y_for_engine = 0.0f;

            RC::Output::send<RC::LogLevel::Default>(
                STR("[SetStartPositionHook] override P{} ({:.2f}, {:.2f}, "
                    "{:.2f}) -> ({:.2f}, {:.2f}, {:.2f}) [Y forced to 0.0] "
                    "side={} chara=0x{:X}\n"),
                matched_player + 1,
                x, y, z,
                pose.pos_x, y_for_engine, pose.pos_z,
                static_cast<uint32_t>(pose.side_flag),
                reinterpret_cast<uintptr_t>(chara));

            // Forward with the substituted (X, 0.0, Z).  The engine's
            // helper will write all three position triples, zero
            // velocity, walk the +0x29130 sub-component list, and
            // apply the render-Y stage offset for us.
            orig(chara, pose.pos_x, y_for_engine, pose.pos_z);

            RC::Output::send<RC::LogLevel::Default>(
                STR("[SetStartPositionHook] P{} post-orig OK\n"),
                matched_player + 1);

            // Write side-flag separately (engine helper does NOT touch
            // +0x23C — that field is normally maintained by
            // PositionCharasSymmetrically, which runs OUTSIDE this
            // function).  Doing it here means a free-yaw user pose still
            // forces the chara to face the captured side after reset.
            auto* base = reinterpret_cast<uint8_t*>(chara);
            *(base + 0x23C) = pose.side_flag;

            RC::Output::send<RC::LogLevel::Default>(
                STR("[SetStartPositionHook] P{} post-flag OK (returning to caller)\n"),
                matched_player + 1);
        }

        std::unique_ptr<PLH::x64Detour> m_detour;
        uint64_t                        m_trampoline    {0};
        void*                           m_stub_page     {nullptr};
        size_t                          m_stub_page_size{0};
        std::atomic<bool>               m_installed     {false};
    };
}
