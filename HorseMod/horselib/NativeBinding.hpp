// ============================================================================
// Horse::NativeBinding — typed access to SoulCalibur VI native functions.
//
// Why this exists
// ---------------
// A UE4SS mod normally calls game behaviour via UFunction reflection
// (ProcessEvent).  That only works when the UFunction is registered on
// the class we're calling it on.  The bindings below cover SC6 native
// functions we still call or hook directly.
//
// KHit presentation reads the native world buffers the collision tests consume
// and converts those points to UE render space.  It deliberately avoids the
// ALuxBattleChara pose-bank helper because that helper samples a different
// path from the KHit update matrix array.
//
// Verification
// ------------
// resolve() is called once from on_unreal_init (game thread).  It reads
// the live image base of SoulcaliburVI.exe and computes raw function
// pointers from it.  No signature check — if SC6 ever ships a new build
// with different RVAs, we'll find out via a crash, not a silent miss.
// Rebuilding against a new RVA is a one-line edit in this file.
//
// Thread-safety
// -------------
// Reading the function pointers after resolve() is lock-free.  The
// underlying functions are themselves NOT thread-safe — call them only
// from the game thread (the cockpit update pre-hook is where we do this).
// ============================================================================

#pragma once

#include "HorseLib.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace Horse
{
    struct KHitScratchVec4
    {
        float X, Y, Z, W;
    };
    static_assert(sizeof(KHitScratchVec4) == 0x10,
                  "KHit scratch vec4 must match native layout");

    struct KHitObbScratchBlock
    {
        // Native world-to-local matrix used by LuxBattle_Test*HitboxShape.
        // Row-major FMatrix layout; columns 0..2 are the local basis axes.
        float Matrix[4][4];
        KHitScratchVec4 LocalP2;
        KHitScratchVec4 LocalP3;
        KHitScratchVec4 EdgeP3MinusP2;
        float LocalP2LenSq;
        float LocalP3LenSq;
        float EdgeLenSq;
        uint8_t Active;
        uint8_t WindingSign;
        uint8_t Pad7E[2];
    };
    static_assert(sizeof(KHitObbScratchBlock) == 0x80,
                  "KHit OBB scratch block must stay 0x80 bytes");
    static_assert(offsetof(KHitObbScratchBlock, LocalP2) == 0x40,
                  "KHit scratch LocalP2 offset drifted");
    static_assert(offsetof(KHitObbScratchBlock, Active) == 0x7C,
                  "KHit scratch Active offset drifted");

    struct KHitOverlapScratch
    {
        KHitObbScratchBlock CurObb;
        KHitObbScratchBlock PrevObb;
    };
    static_assert(sizeof(KHitOverlapScratch) == 0x100,
                  "KHit overlap scratch must stay 0x100 bytes");

    // ------------------------------------------------------------------
    // Native function signatures.  Both take __fastcall by default on
    // MSVC x64 (no shadow-space hijinks).
    // ------------------------------------------------------------------

    // void LuxBattle_BuildHitboxLocalMatrix(
    //     KHitObbScratchBlock* out,
    //     FVector* pWorldP1,
    //     FVector* pWorldP2,
    //     FVector* pWorldP3);
    //
    // This is the collision-shape builder used by KHitArea and KHitFixArea
    // before their overlap tests.  It builds a world-to-local scratch frame
    // from three live battle-world points and writes the LocalP2/LocalP3
    // fields consumed by LuxBattle_TestPointInHitboxShape and
    // LuxBattle_TestSegmentHitsHitboxShape.
    using LuxBattle_BuildHitboxLocalMatrixFn =
        void(__fastcall*)(KHitObbScratchBlock* out,
                          const FVec3* pWorldP1,
                          const FVec3* pWorldP2,
                          const FVec3* pWorldP3);

    // void LuxBattleChara_SetStartPosition(void* chara, float x, float y,
    //                                      float z);
    //
    // Verified calling convention (Ghidra disasm @ 140301e60):
    //   RCX  = chara*       (1st arg slot, integer/pointer)
    //   XMM1 = x            (2nd arg slot, float)
    //   XMM2 = y            (3rd arg slot, float)
    //   XMM3 = z            (4th arg slot, float)
    //
    // Writes the supplied (x, y, z) into all THREE redundant copies of
    // the chara's position in the chara struct:
    //   +0xa0 / +0xc0 / +0x2090   X
    //   +0xa4 / +0xc4 / +0x2094   Y  (render-Y additionally adds the
    //                                  per-stage offset DAT_143e8a33c)
    //   +0xa8 / +0xc8 / +0x2098   Z
    // Also clears post-impulse bookkeeping fields and walks the chara's
    // sub-component linked list at +0x29130, zeroing per-node state.
    //
    // This is the canonical "teleport this chara" call and the same one
    // every engine-internal reset path uses (RoundIntroSetup,
    // PositionCharasSymmetrically, ResetBothCharaPositionsAndFacing,
    // AllocAndInitCharaSlot, ...).  By calling it ourselves we get the
    // EXACT side-effects the engine expects — no missing fields, no
    // half-updated state machines.
    using LuxBattleChara_SetStartPositionFn =
        void(__fastcall*)(void* chara, float x, float y, float z);

    // Generic "void(launcher, bool)" signature shared by all 5 BattleRule
    // setters on ULuxUIBattleLauncher.  Each writes the bool to
    // BattleRule.<RuleName> in the launcher's data-table cache.
    using LuxUIBattleLauncher_SetBoolModeFn =
        void(__fastcall*)(void* launcher, bool bEnable);

    // ULuxUIBattleLauncher::Start signature.  param2 is a struct holding
    // the start parameters (FUIBattleLauncherStartParam — opaque to us;
    // we just pass it through unchanged when forwarding the call).
    using LuxUIBattleLauncher_StartFn =
        void(__fastcall*)(void* launcher, void* InStartParam);

    // void UActorComponent::MarkRenderStateDirty(this)
    //
    // Sets bRenderStateDirty on the component (ComponentFlags @ +0x188,
    // bit 0x20) and queues an end-of-frame render-state recreate.  The
    // scene proxy is destroyed and rebuilt from the component's current
    // state before the next frame draws.
    //
    // Why HorseMod calls this directly: SC6's Shipping build stripped
    // ULineBatchComponent's TickComponent override, so the stock-UE4
    // lifetime sweep that normally fires this on entry expiry doesn't
    // run.  PersistentLineBatcher (UWorld+0x48) is also never auto-
    // Flushed, so without an explicit MarkRenderStateDirty after we
    // append, the proxy stays frozen at whatever snapshot was last
    // built — visible as a delay between when a hit lands and when its
    // wireframe first shows.  Calling this once per frame after our
    // appends forces the rebuild.
    using UActorComponent_MarkRenderStateDirtyFn =
        void(__fastcall*)(void* component);

    // int LuxMoveVM_GetCharaEffectiveHeight(longlong pChara)
    //
    // Computes the chara's effective-height bucket used by the throw-height
    // gate in LuxMoveVM_TickPickAndDispatchReaction @ 0x1402DEF50.  Reads
    // chara+0x44968 (head bone Y), chara+0x44960 (foot bone Y), the per-
    // charakind stature offset at g_LuxBattle_CharaKindStatureTable[bKind*0xF0],
    // and several state bytes (+0x16DC, +0x198C, +0x16EB/EC/FE, +0x19A4,
    // +0x1722, +0x19DC, +0x197E, +0x19DE).  Returns a small integer height
    // bucket; the dispatch gate fires only when this is < 5 for the
    // defender unless yarareId is in an unconditional-allow set.
    //
    // HorseMod uses this for the "would this throw land?" visualisation:
    // when `defender_height >= 5` AND the throw's yarareId isn't in the
    // allow-set, ResolveAttackVsHurtboxMask22 will register the geometric
    // hit + yarareId stamp, but TickPickAndDispatchReaction will silently
    // drop the dispatch — boxes appear to overlap but the throw whiffs.
    using LuxMoveVM_GetCharaEffectiveHeightFn =
        int(__fastcall*)(void* pChara);

    // void ULuxUIGamePresenceUtil::SetPresence(ELuxGamePresence InPresence) [static]
    //
    // Single-byte parameter — the ELuxGamePresence enum value for the
    // current scene.  Microsoft x64 ABI: the byte is passed in CL (low
    // byte of RCX), zero-extended.  Used by Horse::GameMode for the
    // "Auto disable online" gate to track the user's current scene.
    //
    // We hook this with a PolyHook x64Detour rather than a UE4SS
    // RegisterHook because:
    //   - LuxUpdateGamePresenceFromSceneData (the scene-transition
    //     driver) calls this DIRECTLY in C++ at 0x14064a191 — never
    //     going through ProcessEvent — so a UFunction-level hook
    //     (which only intercepts ProcessEvent-routed dispatch) would
    //     never fire for the actual user-facing scene changes.
    //   - The exec_ wrapper at 0x140cb8e9c does route through the
    //     ProcessEvent path, but it's only used when Blueprint code
    //     explicitly calls `SetPresence`, which doesn't happen in
    //     practice during normal play.
    // A PolyHook detour on the C++ symbol catches both call paths.
    using LuxUIGamePresenceUtil_SetPresenceFn =
        void(__fastcall*)(uint8_t InPresence);

    class NativeBinding
    {
    public:
        // RVAs verified against the current Steam build (Ghidra image base
        // 0x140000000).  Re-verify after any SC6 patch.
        static constexpr uintptr_t kLuxBattleBuildHitboxLocalMatrixRVA = 0x30BBA0;
        static constexpr uintptr_t kLuxBattleCharaSetStartPositionRVA = 0x301E60;

        // ULuxUIBattleLauncher::Start (the "kick off the configured match"
        // chokepoint) and the 5 BattleRule setters it reads from.  All
        // verified via Ghidra registrar-table walk + decompile of each
        // setter's body.  See horselib/LuxBattleLauncherStartHook.hpp
        // for the hook design that uses these.
        //
        // Each setter writes a bool to BattleRule.<X> in the launcher's
        // data-table cache (this+0x50); Start later reads that cache
        // and converts each row into pushed mission skills via the
        // registrar at 0x5F6D20.  Call signature for all 5 setters:
        //   void __fastcall(void* launcher, bool bEnable)
        // Start signature:
        //   void __fastcall(void* launcher, void* InStartParam)
        static constexpr uintptr_t kLuxUIBattleLauncher_StartRVA              = 0x5EEB50;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetSlipOutModeRVA     = 0x5ED550;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetEndlessModeRVA     = 0x5EC390;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetDamageUpModeRVA    = 0x5EC190;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetNoRingOutModeRVA   = 0x5ECC70;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetBlowUpModeRVA      = 0x5EB7F0;

        // ULuxUIGamePresenceUtil::SetPresence — hook target for the
        // scene-presence tracker (Horse::GameMode).
        static constexpr uintptr_t kLuxUIGamePresenceUtil_SetPresenceRVA      = 0x64F590;

        // LuxMoveVM_GetCharaEffectiveHeight — height-bucket function used
        // by the throw-dispatch gate.  Pure read-only computation; safe to
        // call any tick from the cockpit hook.  RVA from Ghidra @ 0x140309470.
        static constexpr uintptr_t kLuxMoveVM_GetCharaEffectiveHeightRVA      = 0x309470;

        // UActorComponent::MarkRenderStateDirty — called by the line-
        // batcher backend after appending so the proxy rebuilds in time
        // for the next frame.
        static constexpr uintptr_t kUActorComponent_MarkRenderStateDirtyRVA   = 0x1D4E910;

        // Resolve once.  Idempotent; subsequent calls are no-ops.
        static void resolve()
        {
            if (s_resolved) return;
            s_resolved = true;

            HMODULE mod = ::GetModuleHandleW(L"SoulcaliburVI.exe");
            if (!mod)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[HorseMod.NativeBinding] GetModuleHandleW(SoulcaliburVI.exe) "
                        "returned null — cannot resolve native RVAs\n"));
                return;
            }

            s_image_base = reinterpret_cast<uintptr_t>(mod);

            s_build_khit_obb_scratch =
                reinterpret_cast<LuxBattle_BuildHitboxLocalMatrixFn>(
                    s_image_base + kLuxBattleBuildHitboxLocalMatrixRVA);

            s_set_start_position =
                reinterpret_cast<LuxBattleChara_SetStartPositionFn>(
                    s_image_base + kLuxBattleCharaSetStartPositionRVA);

            // Online-rules infrastructure — Start chokepoint + 5 setters.
            s_launcher_start =
                reinterpret_cast<LuxUIBattleLauncher_StartFn>(
                    s_image_base + kLuxUIBattleLauncher_StartRVA);
            s_set_slipout_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetSlipOutModeRVA);
            s_set_endless_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetEndlessModeRVA);
            s_set_damage_up_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetDamageUpModeRVA);
            s_set_no_ringout_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetNoRingOutModeRVA);
            s_set_blowup_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetBlowUpModeRVA);

            // GameMode infrastructure — scene-presence hook target.
            s_set_presence =
                reinterpret_cast<LuxUIGamePresenceUtil_SetPresenceFn>(
                    s_image_base + kLuxUIGamePresenceUtil_SetPresenceRVA);

            s_get_chara_effective_height =
                reinterpret_cast<LuxMoveVM_GetCharaEffectiveHeightFn>(
                    s_image_base + kLuxMoveVM_GetCharaEffectiveHeightRVA);

            s_mark_render_state_dirty =
                reinterpret_cast<UActorComponent_MarkRenderStateDirtyFn>(
                    s_image_base + kUActorComponent_MarkRenderStateDirtyRVA);

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[HorseMod.NativeBinding] image base 0x{:x}  "
                    "BuildKHitObbScratch -> 0x{:x}  "
                    "LuxBattleChara_SetStartPosition -> 0x{:x}  "
                    "Launcher::Start -> 0x{:x}  "
                    "SetSlipOutMode -> 0x{:x}  SetEndlessMode -> 0x{:x}  "
                    "SetDamageUpMode -> 0x{:x}  SetNoRingOutMode -> 0x{:x}  "
                    "SetBlowUpMode -> 0x{:x}  "
                    "SetPresence -> 0x{:x}\n"),
                s_image_base,
                reinterpret_cast<uintptr_t>(s_build_khit_obb_scratch),
                reinterpret_cast<uintptr_t>(s_set_start_position),
                reinterpret_cast<uintptr_t>(s_launcher_start),
                reinterpret_cast<uintptr_t>(s_set_slipout_mode),
                reinterpret_cast<uintptr_t>(s_set_endless_mode),
                reinterpret_cast<uintptr_t>(s_set_damage_up_mode),
                reinterpret_cast<uintptr_t>(s_set_no_ringout_mode),
                reinterpret_cast<uintptr_t>(s_set_blowup_mode),
                reinterpret_cast<uintptr_t>(s_set_presence));
        }

        static bool buildKHitObbScratch(KHitObbScratchBlock& out,
                                        const FVec3& worldP1,
                                        const FVec3& worldP2,
                                        const FVec3& worldP3)
        {
            if (!s_build_khit_obb_scratch) return false;
            bool ok = true;
            __try
            {
                s_build_khit_obb_scratch(&out, &worldP1, &worldP2, &worldP3);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                ok = false;
            }
            return ok;
        }

        // Calls the engine's own teleport helper.  This is the canonical
        // reset-position path used by RoundIntroSetup,
        // PositionCharasSymmetrically, ResetBothCharaPositionsAndFacing,
        // AllocAndInitCharaSlot, etc. — writing all three position triples
        // (+0xA0 / +0xC0 / +0x2090), zeroing post-impulse bookkeeping, and
        // walking the chara's sub-component list at +0x29130.
        //
        // Returns false if either the function pointer or the chara is null.
        // Does NOT set the side-flag at +0x23C — that's a separate write
        // (the engine maintains it via PositionCharasSymmetrically, not
        // SetStartPosition).
        static bool setCharaStartPosition(void* chara, float x, float y, float z)
        {
            if (!s_set_start_position || !chara) return false;
            bool ok = true;
            __try
            {
                s_set_start_position(chara, x, y, z);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                ok = false;
            }
            return ok;
        }

        // ---- Online rules: launcher + setter wrappers --------------------
        // Each Set*Mode setter writes into the launcher's data-table cache;
        // when the launcher's Start() runs later, those values drive which
        // mission skills get pushed.  We call the setters from inside our
        // PolyHook detour on Start (see LuxBattleLauncherStartHook) right
        // before the original runs, so the data table contains our chosen
        // values when Start reads them.
        static void setSlipOutMode(void* launcher, bool bEnable)
        {
            if (s_set_slipout_mode && launcher)
            {
                __try { s_set_slipout_mode(launcher, bEnable); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
        static void setEndlessMode(void* launcher, bool bEnable)
        {
            if (s_set_endless_mode && launcher)
            {
                __try { s_set_endless_mode(launcher, bEnable); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
        static void setDamageUpMode(void* launcher, bool bEnable)
        {
            if (s_set_damage_up_mode && launcher)
            {
                __try { s_set_damage_up_mode(launcher, bEnable); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
        static void setNoRingOutMode(void* launcher, bool bEnable)
        {
            if (s_set_no_ringout_mode && launcher)
            {
                __try { s_set_no_ringout_mode(launcher, bEnable); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
        static void setBlowUpMode(void* launcher, bool bEnable)
        {
            if (s_set_blowup_mode && launcher)
            {
                __try { s_set_blowup_mode(launcher, bEnable); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
        static uintptr_t launcherStartAddress()
        {
            return reinterpret_cast<uintptr_t>(s_launcher_start);
        }
        static bool hasLauncherStart() { return s_launcher_start != nullptr; }

        // ULuxUIGamePresenceUtil::SetPresence — accessor for the
        // GameMode tracker's PolyHook detour.  The function pointer
        // itself is exposed both as a callable (s_set_presence) and
        // as a raw address (setPresenceAddress) since PolyHook needs
        // the latter as its hook target.
        static uintptr_t setPresenceAddress()
        {
            return reinterpret_cast<uintptr_t>(s_set_presence);
        }
        static bool hasSetPresence() { return s_set_presence != nullptr; }

        // Force a scene-proxy rebuild on the given component.  No-op
        // if the binding hasn't resolved or the pointer is null.
        static void markRenderStateDirty(void* component)
        {
            if (s_mark_render_state_dirty && component)
            {
                __try { s_mark_render_state_dirty(component); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }

        // Call LuxMoveVM_GetCharaEffectiveHeight on the given chara.
        // Returns the engine's height bucket (an int — small values are
        // short charas / grounded poses, large values are tall standing
        // charas).  Returns 0 on null/unresolved.  See typedef doc for
        // semantics; the throw-dispatch gate compares this against 5.
        static int getCharaEffectiveHeight(void* chara)
        {
            if (!s_get_chara_effective_height || !chara) return 0;
            int out = 0;
            __try
            {
                out = s_get_chara_effective_height(chara);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                out = 0;
            }
            return out;
        }

        static bool hasGetCharaEffectiveHeight()
        {
            return s_get_chara_effective_height != nullptr;
        }

        static bool isReady()      { return s_image_base != 0; }
        static bool hasSetStartPosition() { return s_set_start_position != nullptr; }
        static uintptr_t imageBase() { return s_image_base; }

    private:
        static inline bool                          s_resolved           = false;
        static inline uintptr_t                     s_image_base         = 0;
        static inline LuxBattle_BuildHitboxLocalMatrixFn s_build_khit_obb_scratch = nullptr;
        static inline LuxBattleChara_SetStartPositionFn s_set_start_position = nullptr;

        // Online-rules infrastructure.
        static inline LuxUIBattleLauncher_StartFn        s_launcher_start       = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_slipout_mode     = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_endless_mode     = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_damage_up_mode   = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_no_ringout_mode  = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_blowup_mode      = nullptr;

        // GameMode infrastructure — only the address is used (PolyHook's
        // hook target).  Storing as a typed pointer mostly for symmetry
        // with the other natives.
        static inline LuxUIGamePresenceUtil_SetPresenceFn s_set_presence        = nullptr;

        // Effective-height function pointer for throw-dispatch gate prediction.
        static inline LuxMoveVM_GetCharaEffectiveHeightFn s_get_chara_effective_height = nullptr;

        static inline UActorComponent_MarkRenderStateDirtyFn s_mark_render_state_dirty = nullptr;
    };

} // namespace Horse
