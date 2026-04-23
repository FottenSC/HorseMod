// ============================================================================
// HorseMod — SoulCalibur VI hit-volume visualiser (UE4SS C++ mod).
//
// What this shows
// ---------------
// Every frame, read the SC6 legacy Namco KHit linked lists on both charas
// and draw their contents as wire-frame volumes in world space using
// UWorld's ULineBatchComponent.
//
//   HURTBOXES  (KHit list at chara+0x444B8) — entries that RECEIVE
//              damage from opponent attacks.  On by default.  Flashes red
//              when that slot's PerHurtboxReactionState[i] is non-zero
//              (just got hit).  The hurtbox list holds every "receive"
//              volume — damage hurtboxes, throw-reach receivers,
//              proximity/auto-turn cages.  The engine's own classifier
//              doesn't sub-bucket these from the defender side; reactions
//              are decided per-slot based on which attacker category bits
//              are set in the incoming mask.  We no longer invent
//              size-based sub-buckets for this reason.
//
//   ATTACK     (KHit list at chara+0x44498) — entries that DEAL damage
//   BOXES      (or initiate a grab).  On by default.  Dim amber for
//              strikes; a node whose per-frame active gate (node+0x14) is
//              non-zero is drawn bright yellow (the "hot" attack for the
//              current frame — the tick sets this bit to
//              `(hotMask >> node[+0x17]) & 1`, meaning the MoveVM's hot
//              bitmap has this bone slot marked active).  Grab/throw
//              attacks are drawn magenta — the engine distinguishes them
//              from strikes via bits 31 and 55 of the CategoryMask at
//              node+0x08 (see LuxBattle_ResolveAttackVsHurtboxMask22
//              @ 0x14033C100).
//
//   BODY /     (KHit list at chara+0x44478) — entries that neither deal
//   PUSHBOX    nor receive damage.  Used by
//              LuxBattle_SolvePhysBodyCollision @ 0x14030CCF0 for
//              character-to-character physical pushing only.  OFF by
//              default (visually noisy — spacing context only).
//              Enable per-player from the ImGui tab.
//
// Historical note: earlier versions of this mod had the three list heads
// rotated (Attack↔Body↔Hurtbox off by one in the old plate on
// LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0).  That's why
// "attack" boxes used to appear to not make contact with the opponent —
// they were actually the pushbox list.  Corrected above.
//
// Visibility is per-player: P1 and P2 each get independent hurtbox /
// attack / body toggles, so e.g. you can hide P2's pushboxes while
// keeping P1's visible.
//
// All geometry comes from Pipeline 2 (legacy KHit chain) — the same data
// the hit resolver uses.  Weapon-trail Pipeline 1 (ActiveAttackSlot via
// TraceManager) is not drawn in this build; most hit volumes of interest
// (kicks, hurtboxes, bodies) are in Pipeline 2 anyway.
//
// Hotkeys
//   F5  toggle overlay on / off
//   F6  pause + step one frame.  First press lazily enables the
//       Freeze-frame bytepatches; subsequent presses queue additional
//       frames (held F6 yields ~30fps slow-motion via UE4SS key
//       auto-repeat).  See horselib/GamePause.hpp.
//
// ImGui tab ("HorseMod")
//   • overlay enable toggle (mirrors F5)
//   • per-list visibility toggles (hurtbox / attack / body)
//   • line thickness slider
//   • LineBatcher slot selector (Default / Persistent / Foreground)
//
// Everything else the earlier prototype had — predicate hooks, bounds
// traces, yarare watchers, process-event spies, cockpit-widget backend,
// capsule walker — has been removed.  If you want any of that back, pull
// it out of git history; it's all on dead paths for the "draw the real
// hitboxes" goal we're pursuing now.
//
// Ghidra references
//   LuxBattle_TickHitResolutionAndBodyCollision  @ 0x14033CCA0  (full plate)
//   ALuxBattleChara_GetBoneTransformForPose      @ 0x140462760
//   LuxSkeletalBoneIndex_Remap                   @ 0x140898140
//   KHitBase / KHitArea / KHitSphere / KHitFixArea — 0xA0 bytes each
// ============================================================================

#include "horselib/HorseLib.hpp"
#include "horselib/KHitWalker.hpp"
#include "horselib/LineBatcherBackend.hpp"
#include "horselib/NativeBinding.hpp"
#include "horselib/CamLock.hpp"
#include "horselib/VFXOff.hpp"
#include "horselib/GamePause.hpp"
#include "horselib/CharaInvis.hpp"

#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Input/KeyDef.hpp>
#include <Input/Handler.hpp>

#include <imgui.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

using namespace RC;
using namespace RC::Unreal;

// ----------------------------------------------------------------------------
class HorseMod final : public CppUserModBase
{
private:
    // Static live-instance pointer so the cockpit hook lambda can safely
    // no-op after destruction (game thread could fire after Restart All).
    static inline std::atomic<HorseMod*> s_instance{nullptr};

    // ---- Overlay state ----
    std::atomic<bool> m_enabled{false};

    // Per-player visibility toggles.  Hurtbox + Attack default ON, Body
    // defaults OFF (it's visually noisy — spacing context only).  Each
    // player indexed by PlayerIndex (0 = P1, 1 = P2).
    std::atomic<bool> m_show_p1_hurt{true};
    std::atomic<bool> m_show_p1_atk {true};
    std::atomic<bool> m_show_p1_body{false};
    std::atomic<bool> m_show_p2_hurt{true};
    std::atomic<bool> m_show_p2_atk {true};
    std::atomic<bool> m_show_p2_body{false};

    // Attack-role filters — shared across P1 and P2 (same engine-defined
    // partition on both sides).  Derived from the 64-bit CategoryMask at
    // node+0x08, the same split the classifier at 0x14033C100 uses:
    //
    //   Strike  — mask bits in 0xFF7FFFFF7FFFFFFF  (all bits except 31, 55).
    //   Throw   — mask bits in 0x0080000080000000 (bits 31, 55) = grabs.
    //
    // Both default ON — throws and strikes are equally useful to see.
    std::atomic<bool> m_show_atk_strike{true};
    std::atomic<bool> m_show_atk_throw {true};

    // "Live attacks only" filter — geometry gate.  When true, skip
    // drawing any attack node whose engine-side per-frame gate at
    // node+0x14 is zero (i.e. not participating in hit resolution
    // this tick).  The engine's own OR aggregation loop inside
    // LuxBattleChara_UpdateAllKHitWorldCenters @ 0x14030D6A0
    // short-circuits on exactly this condition:
    //
    //     for (atk in AttackList)
    //       if (*(short*)(atk + 0x14) != 0)     // ← attacker live-gate
    //         for (hurt in HurtList)
    //           if (*(short*)(hurt + 0x14) != 0) // ← defender live-gate
    //             if (overlap_test(atk, hurt))
    //               defender.PerHurtboxBitmask[bone] |= atk.CategoryMask;
    //
    // IMPORTANT caveat discovered later: the tick writes
    //     *(short*)(node+0x14) = (hotMask >> node[+0x17]) & 1
    // and hotMask is built as
    //     hotMask = 0x3FFFD | animCellMask | ownActiveCellMask
    // where 0x3FFFD = slots {0, 2..17} forced ON every single frame.
    // Most "body proximity" attack volumes are authored to those
    // slots so their +0x14 is pinned at 1 during neutral.  This
    // filter will NOT cull them — it only culls nodes authored at
    // slots 1 or >=18.  For a tighter "actually causing damage"
    // filter use m_hide_not_damage_active below.
    std::atomic<bool> m_hide_cold_atk  {false};

    // "Damage-active only" filter — damage gate.  When true, skip
    // any attack node whose +0x17 slot bit is NOT set in this
    // chara's own active-attack-cell mask at *(u64*)(chara+0x44058).
    // That mask is written per-frame by the MoveVM from the active
    // move's authored timeline and is the same mask the classifier
    // at LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100 ANDs
    // against defender PerHurtboxBitmask entries to decide
    // reactions.
    //
    // Unlike the geometry gate above, this is NOT affected by the
    // 0x3FFFD always-on floor — during neutral the active-cell is
    // typically null (mask=0) and every attack box gets filtered
    // out, which is the behaviour you want for "show me only what's
    // ACTIVELY trying to hit right now".  Default OFF so the
    // overlay still lights up visibly on first launch; users flip
    // it on to answer "which of these is dealing damage this
    // frame?".
    std::atomic<bool> m_hide_not_damage_active{false};

    // "Per-frame damage-active only" filter — strictly tighter than
    // m_hide_not_damage_active.  When ON, hide every attack node whose
    // +0x17 slot bit is NOT set in the chara's PER-FRAME damage mask
    // (computed by mirroring the engine's pMoveVMCell lookup at
    // 0x14033cca0; see KHitWalker::readPerFrameDamageMask).
    //
    // Why this matters: m_hide_not_damage_active reads
    // **(chara+0x44058), which is set ONCE PER MOVE-SLOT (in
    // LuxMoveVM_SetActiveMoveSlot @ 0x140300c70) and stays constant
    // through startup / active / recovery.  As a result hitboxes
    // appear "damage-active" through the entire move duration even
    // though damage is only dealt during the active-frame window.
    //
    // The per-frame mask follows the move's authored sub-frame
    // timeline: it turns OFF during startup, ON during damage frames,
    // OFF during recovery — exactly what hitbox-data analysis wants.
    // Default OFF (additive over the existing toggles); typically
    // used together with m_hide_cold_atk to slice down to "only the
    // currently-dealing-damage volume".
    std::atomic<bool> m_hide_not_per_frame_active{false};

    // "Addressable hurtboxes only" filter — hurtbox classifier gate.
    //
    // Correction to an earlier attempt at a +0x14-based "live hurtboxes"
    // filter: the per-frame update loop in
    // LuxBattle_TickHitResolutionAndBodyCollision (0x14033CCA0) only
    // walks chara+0x44498 = AttackListHead.  It does NOT touch the
    // hurtbox or body lists.  Hurtbox +0x14 is therefore pinned at its
    // deserialize-time default of 1 forever, and a +0x14-based filter
    // is a no-op for hurtboxes.
    //
    // The real mechanism that makes a hurtbox un-hittable from the
    // classifier's perspective is the HurtboxSlotCount bound:
    //
    //     LuxBattle_ResolveAttackVsHurtboxMask22 iterates
    //         for (slotIndex = 0; slotIndex < chara+0x44494; ++slotIndex)
    //             test PerHurtboxBitmask[slotIndex] & attackerMask & ...
    //
    // UpdateAllKHitWorldCenters still OR's bits into
    // PerHurtboxBitmask[hurt->+0x17] regardless of slot, but if
    // hurt->+0x17 >= HurtboxSlotCount those bits land in an index the
    // classifier never reads — no reaction, no damage, visually alive
    // but engine-dead.
    //
    // When this toggle is ON, we hide every hurtbox whose
    // bone_id_internal (+0x17) is outside the classifier range.  These
    // are typically body-wide "meta" volumes authored at high slots
    // (counter-hit detection, throw-whiff zones, state-specific
    // detection geometry) — exactly the kind of giant sphere a user
    // notices "doesn't get hit by anything".  Default OFF: the
    // unaddressable hurtboxes are still interesting data for modders.
    std::atomic<bool> m_hide_unaddressable_hurt{false};

    // ---- Weapon visibility override ----------------------------------------
    // When ON, force every ALuxBattleChara's weapon meshes hidden each
    // frame by calling SetWeaponVisibility(false) via UFunction reflection.
    // This is useful when inspecting hitboxes on characters with bulky
    // weapons (Nightmare's sword, Astaroth's axe) that otherwise occlude
    // the volumes we're drawing.
    //
    // Semantics:
    //   OFF: do nothing.  The game manages weapon visibility normally
    //        (cinematic cues, ring-out states, etc.).
    //   ON : re-apply hidden every frame — overrides any game-driven
    //        visibility change, so weapons stay gone while the toggle is
    //        held.
    //   ON -> OFF transition: call SetWeaponVisibility(true) once per
    //        chara so weapons return to visible; after that we stop
    //        touching the state and let the game run.
    //
    // We apply via the BlueprintCallable UFunction declared on
    // ALuxBattleChara.h:80:
    //     UFUNCTION(BlueprintCallable)
    //     void SetWeaponVisibility(bool bVisible);
    // which is a registered UFunction and therefore reachable through
    // ProcessEvent — no native-RVA binding needed.
    std::atomic<bool> m_hide_weapons{false};
    // Tracks the last state actually pushed to the game; lets us detect
    // the ON->OFF edge so we restore visibility exactly once.
    std::atomic<bool> m_last_applied_hide_weapons{false};

    // ---- Ansel "always allow photography" override -------------------------
    // SC6 gates NVIDIA Ansel (the in-engine freeze-frame / free-camera
    // photo mode) via three layers:
    //
    //   1. UAnselFunctionLibrary::SetIsPhotographyAllowed(bool)
    //        The bottom-layer switch the UE4 AnselIntegration plugin
    //        checks before it will even accept a capture hotkey.
    //        Declared BlueprintCallable in
    //        include/SoulCaliburVI/Ansel/Public/AnselFunctionLibrary.h:35.
    //
    //   2. ULuxGameInstance::SetLuxorAnselEnabled / SetAnselEnabled /
    //      SetAnselIsInPauseMenu
    //        SC6-level permission flags read by the battle manager and
    //        HUD.  When these are off the game masks Ansel even if the
    //        UE4 layer allows it.
    //
    //   3. ALuxBattleManager::RequestStartAnselSession / RequestEndAnselSession
    //        Per-match actual session lifecycle — gated on (1) and (2).
    //
    // Empirically the community-reliable way to un-gate Ansel in SC6
    // is to force layer (1) on every frame.  The SC6-layer (2) stubs
    // are either no-ops or always-false in the binary we have; the
    // UE4 layer is the real cliff.  When this toggle is ON we
    // re-apply SetIsPhotographyAllowed(true) every frame so any game
    // code that tries to disable it (menu transitions, ring-out,
    // cinematic cams) gets overridden back on before its effects are
    // visible.
    //
    // ON -> OFF transition: we call SetIsPhotographyAllowed(false)
    // exactly once so the game resumes managing the flag itself, then
    // stop touching it.  This matches the weapon-visibility semantics
    // above.
    //
    // This runs independent of the F5 overlay toggle — the user said
    // "always allows", so it fires from the hook pre-callback before
    // the enabled / NativeBinding-ready gates.
    std::atomic<bool> m_ansel_always_allowed{false};
    // Last state we actually pushed; used to detect the ON -> OFF
    // transition so we restore engine control exactly once.
    std::atomic<bool> m_last_applied_ansel_allowed{false};
    // Cached CDO of /Script/Ansel.Default__AnselFunctionLibrary.
    // The CDO is a persistent UObject that stays valid for the life
    // of the process, so we keep a raw pointer and revalidate with
    // UObject::IsReal before each use.  Same pattern as
    // Horse::Screen's GameplayStatics CDO cache.
    RC::Unreal::UObject* m_ansel_cdo = nullptr;
    // Cache slot for the UFunction* itself; shared across all frames.
    Horse::Fn m_fn_set_photo_allowed;

    // ---- Camera lock --------------------------------------------------------
    // Freeze the battle camera at its current pose while the toggle is
    // held.  Implemented by patching SC6's per-frame "commit POV to
    // memory" instructions to NOPs — see horselib/CamLock.hpp for the
    // full disassembly walk and the historical note on why the previous
    // CameraCache.POV-write approach didn't work (UMG widget tick runs
    // AFTER the renderer has already consumed the POV).
    //
    // Semantics:
    //   OFF: every store runs as normal — engine owns the camera.
    //   ON : 5 stores at site A and 5 stores at site B are NOPed.  The
    //        camera struct in memory keeps whatever location/rotation/FOV
    //        it had at the moment we toggled ON; nothing in the engine
    //        rewrites those fields for the duration.
    //
    // CamLock owns the live BytePatch state — it must outlive every
    // call site (kept until ~HorseMod) so the patches get cleanly
    // restored on mod unload.  Atomic for ImGui-thread reads against
    // the patch state (the patch-flip itself happens on the same
    // thread, so no race).
    Horse::CamLock    m_cam_lock{};
    std::atomic<bool> m_lock_camera{false};

    // ---- Hide characters ----------------------------------------------------
    // Bytepatch port of somberness's CE "Invisible" cheat — see
    // horselib/CharaInvis.hpp for the full disassembly walk.
    //
    // Replaces the earlier per-frame SetCharacterVisibility(false) UFunction
    // re-apply loop.  That approach worked for normal moves but flickered
    // visibility ON for one frame on certain moves (Critical Edges, super
    // intros, transformations) because:
    //   * The cockpit hook fires during Slate tick (BEFORE world tick).
    //   * Engine's chara-tick during world tick would re-set the visibility
    //     flag back to "visible" as part of the move's state machine.
    //   * Render then drew the chara visible for that one frame.
    //   * Our next cockpit-tick re-hid it, producing the flicker.
    //
    // The new approach inverts the engine's own visibility-compare
    // instructions inside ALuxBattleChara_SyncMoveStateVisibility so that
    // every read of the visibility flag now produces "hidden" — eliminating
    // the race because we're INSIDE the read path, not racing the writes.
    //
    // Useful when you're diagnosing hitbox shapes on a specific move —
    // the character mesh and its skirt/cape/hair occlude the volumes.
    // Hitboxes are part of the gameplay skeleton, not the mesh, so they
    // keep updating fine while the mesh is invisible.
    Horse::CharaInvis m_chara_invis{};
    std::atomic<bool> m_hide_chara{false};

    // ---- Suppress VFX -------------------------------------------------------
    // Bytepatch port of somberness's CE "VFX off" cheat — see
    // horselib/VFXOff.hpp for the full disassembly walk.  Replaces the
    // earlier per-frame DestroyAllVFx polling: that approach let each
    // VFX spawn for one frame before tearing it down (1-tick flashes
    // on every hit) and burned a UFunction call per tick.  The
    // bytepatch installs a midfunction trampoline that overrides the
    // engine's per-slot VFX-state writer to plant a sentinel constant
    // the renderer treats as culled — effects never become visible.
    //
    // Same toggle, same ImGui label.  No hot-path work; flip is a
    // single 5-byte JMP install.
    Horse::VFXOff     m_vfx_off{};
    std::atomic<bool> m_suppress_vfx{false};

    // ---- Freeze frame (game pause) ------------------------------------------
    // Bytepatch port of somberness's CE "Game Pause" cheat — see
    // horselib/GamePause.hpp for the full disassembly walk and the
    // rationale for the trampoline (we need to capture rbx, the battle-
    // context pointer, to write the pause byte ourselves; reflection
    // can't reach it).
    //
    // Two flavours of use:
    //   * Hold "Pause" — bit 0 of [rbx+0x394] is set, world stops
    //     ticking, UMG/render keeps going (that's how we can still
    //     interact with ImGui and how the frame-step button works).
    //   * Click "Step 1 frame" while paused — clears the bit for one
    //     cockpit tick, then re-sets it.  Net: simulation advances by
    //     exactly one frame.  The state machine lives in GamePause and
    //     is driven from on_cockpit_update_pre.
    //
    // Atomic mirrors the bool just so the ImGui callback gets a clean
    // load; the actual pause state is the patched memory byte.
    Horse::GamePause  m_game_pause{};
    std::atomic<bool> m_freeze_frame{false};

    // Red "just got hit" sticky flash duration, in milliseconds.
    //
    // The underlying PerHurtboxReactionState signal is a ~1-frame pulse
    // (~16ms at 60fps) — too short to see.  We extend it by holding the
    // hot state for `m_flash_ms` ms (converted to frames at 60fps in
    // KHitWalker::setStickyFrames below).  0 = disable the sticky (raw
    // 1-frame pulse only — effectively no visible flash).
    //
    // Default 250ms matches the previous hardcoded behaviour.
    std::atomic<int> m_flash_ms{250};

    std::atomic<float> m_thickness{2.0f};
    std::atomic<Horse::LineBatcherSlot> m_slot{Horse::LineBatcherSlot::Foreground};

    // Look up the current show-flag for (player, list).  Inlined in the
    // hot path; pi outside [0,1] falls through to P1's settings.
    bool shouldShow(int pi, Horse::KHitList list) const
    {
        const bool is_p2 = (pi == 1);
        switch (list)
        {
            case Horse::KHitList::Hurtbox:
                return (is_p2 ? m_show_p2_hurt : m_show_p1_hurt).load();
            case Horse::KHitList::Attack:
                return (is_p2 ? m_show_p2_atk  : m_show_p1_atk ).load();
            case Horse::KHitList::Body:
                return (is_p2 ? m_show_p2_body : m_show_p1_body).load();
        }
        return false;
    }

    // Secondary gate for Attack-list entries: after the master Attacks
    // toggle passes, consult the engine-derived role filter.  NotAttack
    // always passes (used for Hurtbox / Body — handled at the outer
    // switch, never reaches this gate in practice).
    bool shouldShowAttackRole(Horse::KHitAttackRole r) const
    {
        switch (r)
        {
            case Horse::KHitAttackRole::Strike: return m_show_atk_strike.load();
            case Horse::KHitAttackRole::Throw:  return m_show_atk_throw .load();
            case Horse::KHitAttackRole::NotAttack:
            default:                             return true;
        }
    }

    // ---- Hook / backend ----
    bool                         m_hook_registered = false;
    std::pair<int32_t, int32_t>  m_hook_ids{};
    StringType                   m_hook_path;
    int                          m_poll_counter = 0;
    int                          m_update_calls = 0;
    int                          m_diag_tick    = 0;

    Horse::Lux                 m_lux;
    Horse::LineBatcherBackend  m_backend;

    // One-shot log flags so UE4SS.log doesn't fill with repeats.
    bool m_logged_native_missing = false;

public:
    HorseMod() : CppUserModBase()
    {
        ModName        = STR("HorseMod");
        ModVersion     = STR("0.10.0");
        ModDescription = STR("SC6 KHit hitbox / hurtbox / body visualiser.");
        ModAuthors     = STR("horse");

        Input::ModifierKeyArray no_mods{};
        no_mods.fill(Input::ModifierKey::MOD_KEY_START_OF_ENUM);

        register_keydown_event(Input::Key::F5, no_mods, [this]() {
            bool s = !m_enabled.load();
            m_enabled.store(s);
            Output::send<LogLevel::Verbose>(STR("[HorseMod] overlay {}\n"),
                s ? STR("ON") : STR("OFF"));
            if (!s) m_backend.hideAll();
        });

        // F6 — single-frame step.  Lazily resolves+enables the pause
        // patches on first press so the user doesn't need to touch the
        // ImGui tab to use frame-step.  Holding F6 produces ~30 fps
        // slow-motion thanks to UE4SS's keyboard auto-repeat (the
        // step counter just queues frames; on_tick drains them one
        // per two cockpit ticks).  See GamePause::on_tick comment.
        register_keydown_event(Input::Key::F6, no_mods, [this]() {
            if (!m_game_pause.is_enabled())
            {
                if (!m_game_pause.is_resolved()) m_game_pause.resolve();
                if (!m_game_pause.enable()) return;  // patch failed; gave up
            }
            // First press while running: pause and queue one frame so
            // the user sees an immediate freeze at the next frame
            // boundary.  Mirrors what set_paused(true) + step would do
            // but in one keystroke.
            if (!m_game_pause.is_paused() && !m_freeze_frame.load())
            {
                m_freeze_frame.store(true);
                m_game_pause.set_paused(true);
            }
            m_game_pause.step_one_frame();
        });

        register_tab(STR("HorseMod"), [](CppUserModBase* inst) {
            static_cast<HorseMod*>(inst)->render_tab_impl();
        });

        s_instance.store(this);
        Output::send<LogLevel::Verbose>(
            STR("[HorseMod] ctor v0.10.0 (KHit walker)\n"));
    }

    ~HorseMod() override
    {
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor ENTER\n"));

        // Zero instance pointer FIRST so any in-flight hook sees null.
        s_instance.store(nullptr);

        if (m_hook_registered && !m_hook_path.empty())
        {
            UObjectGlobals::UnregisterHook(m_hook_path, m_hook_ids);
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor unregistered cockpit hook pre={} post={}\n"),
                m_hook_ids.first, m_hook_ids.second);
        }
        // m_cam_lock will restore any active patches via its own dtor
        // when our member destruction runs after this body returns.
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor EXIT\n"));
    }

    auto on_ui_init() -> void override { UE4SS_ENABLE_IMGUI() }

    auto on_unreal_init() -> void override
    {
        // Resolve SC6 native function RVAs now that the game image is loaded:
        //   ALuxBattleChara_GetBoneTransformForPose @ image + 0x462760
        //   LuxSkeletalBoneIndex_Remap              @ image + 0x898140
        // These are used by KHitWalker to transform KHitArea OBBs into
        // world space via the owning chara's bone pose.
        Horse::NativeBinding::resolve();

        // Push the default hit-flash duration (ms -> frames @ 60fps) into
        // the walker so it's correct on frame 0 without the user having
        // to touch the slider.
        const int frames = (m_flash_ms.load() * 60 + 500) / 1000;
        Horse::KHitWalker::setStickyFrames(frames);
    }

    auto on_update() -> void override
    {
        if (m_hook_registered) return;
        if (++m_poll_counter < 60) return;
        m_poll_counter = 0;
        try_register_cockpit_hook();
    }

private:
    void try_register_cockpit_hook()
    {
        Horse::Obj cockpit = m_lux.cockpit();
        if (!cockpit) return;

        UClass* klass = cockpit.raw()->GetClassPrivate();
        if (!klass) return;

        m_hook_path = klass->GetPathName() + STR(":Update");
        Output::send<LogLevel::Verbose>(STR("[HorseMod] Registering hook: {}\n"), m_hook_path);

        UnrealScriptFunctionCallable pre_cb =
            [](UnrealScriptFunctionCallableContext& ctx, void*) {
                if (auto* self = s_instance.load(std::memory_order_acquire))
                    self->on_cockpit_update_pre(ctx.Context);
            };
        UnrealScriptFunctionCallable post_cb =
            [](UnrealScriptFunctionCallableContext&, void*) {};

        m_hook_ids        = UObjectGlobals::RegisterHook(m_hook_path, pre_cb, post_cb, nullptr);
        m_hook_registered = true;
        Output::send<LogLevel::Verbose>(STR("[HorseMod] hook pre={} post={}\n"),
            m_hook_ids.first, m_hook_ids.second);
    }

    // ---- Ansel "always allow photography" apply-per-frame helper -------
    // Pushes UAnselFunctionLibrary::SetIsPhotographyAllowed(bVisible)
    // via ProcessEvent when either the toggle is ON or we're on the
    // ON -> OFF edge (one-shot restore).  Called from the top of the
    // cockpit pre-hook so it runs every frame independent of the F5
    // overlay state.
    //
    // Safe to call before NativeBinding is resolved — this path is
    // pure UE4 reflection and does not touch SC6 RVAs.  Safe when no
    // battle chara exists (menu / loading) because the CDO always
    // exists once the Ansel module is loaded.
    void apply_ansel_override_if_needed()
    {
        using namespace RC;
        using namespace RC::Unreal;

        const bool now  = m_ansel_always_allowed.load();
        const bool last = m_last_applied_ansel_allowed.load();
        // Nothing to do while both the toggle and last-applied are off —
        // let the engine manage the flag itself.
        if (!now && !last) return;

        // Resolve / re-resolve CDO.  UObject::IsReal catches the case
        // where UObjectArray was rebuilt (rare, but survivable).
        if (!m_ansel_cdo || !UObject::IsReal(m_ansel_cdo))
        {
            m_ansel_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr,
                STR("/Script/Ansel.Default__AnselFunctionLibrary"));
            if (!m_ansel_cdo)
            {
                // Ansel plugin not loaded in this build / run; silently
                // skip.  (One-shot log would be nice but is low value
                // — the toggle is visibly inert in that case.)
                return;
            }
        }

        UFunction* f = m_fn_set_photo_allowed.on(
            m_ansel_cdo, STR("SetIsPhotographyAllowed"));
        if (!f) return;

        // `now` = desired visibility state; `last` captures whether
        // we're on the restore edge (last=true, now=false → push false
        // once).
        struct { bool bIsPhotographyAllowed; } p{ now };
        m_ansel_cdo->ProcessEvent(f, &p);

        m_last_applied_ansel_allowed.store(now);
    }

    // ------------------------------------------------------------------
    // CockpitBase_C::Update pre-hook.  Game thread, one call per frame.
    // ------------------------------------------------------------------
    void on_cockpit_update_pre(UObject* raw_cockpit)
    {
        ++m_update_calls;

        // Apply Ansel override first — independent of the overlay F5
        // gate and the NativeBinding-ready gate below, because the user
        // asked for it to be "always" on while the toggle is held.
        apply_ansel_override_if_needed();

        // Camera lock has NO per-frame helper here — it's implemented as
        // a runtime bytepatch (Horse::CamLock) that's flipped on/off
        // from the ImGui toggle.  The patch is a property of the
        // process, not the cockpit tick.

        // VFX suppression: same bytepatch story as camera lock — the
        // toggle is a process-state property, not a per-frame action.
        // No call needed here.

        // Frame-step state machine — drives the GamePause helper's
        // "clear pause bit -> let world tick once -> re-set pause bit"
        // sequence.  Cheap when idle (single atomic load).  Must run
        // here (not from the ImGui callback) because cockpit::Update
        // ticks even while the world simulation is paused, while the
        // ImGui tab callback only runs when the user has the menu
        // open.
        m_game_pause.on_tick();

        // Always-on heartbeat: once every ~2s print a single line so we can
        // confirm the hook is ticking even while the overlay is off.  This
        // separates "hook not firing" from "F5 not toggled".
        // DISABLED — noisy; re-enable if diagnosing "is the hook firing?".
#if 0
        if ((m_update_calls & 0x7F) == 1)
        {
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod.Tick] calls={} enabled={} pivot=0x{:x} "
                    "backend_ready={} native_ready={}\n"),
                m_update_calls,
                m_enabled.load() ? 1 : 0,
                reinterpret_cast<uintptr_t>(raw_cockpit),
                m_backend.isReady() ? 1 : 0,
                Horse::NativeBinding::isReady() ? 1 : 0);
        }
#endif

        if (!m_enabled.load() || !raw_cockpit) return;
        if (!Horse::NativeBinding::isReady())
        {
            if (!m_logged_native_missing)
            {
                Output::send<LogLevel::Warning>(
                    STR("[HorseMod] NativeBinding not ready — KHit draw disabled\n"));
                m_logged_native_missing = true;
            }
            return;
        }

        Horse::Obj pivot{raw_cockpit};

        // Sync LineBatcher slot with the ImGui toggle.
        if (m_backend.slot() != m_slot.load())
            m_backend.setSlot(m_slot.load());
        m_backend.primeFrom(pivot);
        if (!m_backend.isReady()) return;

        m_backend.beginFrame();

        const float T = m_thickness.load();

        int charas_seen = 0;
        int nodes_drawn = 0;

        // ---- Weapon visibility snapshot ---------------------------------
        // Compute once per frame.  `apply_weapons` is true when we need to
        // actively push SetWeaponVisibility into the game this frame:
        //   * when the toggle is ON — re-apply every frame to overwrite
        //     any game-driven re-show (the engine can flip visibility as
        //     part of cinematic cues; we fight it back to hidden).
        //   * on the ON->OFF transition — call once with true to restore
        //     visibility, then stop touching it.
        const bool hide_weapons_now = m_hide_weapons.load();
        const bool was_hiding       = m_last_applied_hide_weapons.load();
        const bool apply_weapons    = hide_weapons_now || was_hiding;
        // Cache the UFunction resolution once.  ALuxBattleChara's
        // SetWeaponVisibility is declared BlueprintCallable, so it's a
        // regular reflection-reachable UFunction shared across all
        // instances of the class.
        static Horse::Fn s_fn_set_weapon_vis;

        // ---- Character-mesh visibility ----------------------------------
        // Now handled by Horse::CharaInvis bytepatches (see ImGui block
        // for the toggle).  No per-frame UFunction call here — the
        // patch lives inside the engine's own visibility-getter so it
        // works invariantly across all move states without flicker.

        m_lux.forEachChara([&](int i, Horse::Obj chara) {
            if (i >= 2) return;  // only P1 / P2; ignore spectators
            ++charas_seen;
            int32_t pi = chara.getValueOr<int32_t>(L"PlayerIndex", i);
            if (pi < 0 || pi > 1) pi = i;

            // Push the weapon-visibility state for this chara.  Done
            // first so it runs even if all list toggles are off below.
            if (apply_weapons)
            {
                struct { bool bVisible; } p{ !hide_weapons_now };
                chara.callRaw(s_fn_set_weapon_vis,
                              L"SetWeaponVisibility", &p);
            }

            // Snapshot this player's three toggles once per chara so the
            // inner-loop gate is branch-free.
            const bool show_hurt       = shouldShow(pi, Horse::KHitList::Hurtbox);
            const bool show_atk        = shouldShow(pi, Horse::KHitList::Attack );
            const bool show_body       = shouldShow(pi, Horse::KHitList::Body   );
            const bool hide_cold_atk   = m_hide_cold_atk.load();
            const bool hide_not_dmg    = m_hide_not_damage_active.load();
            const bool hide_not_pf     = m_hide_not_per_frame_active.load();
            const bool hide_unaddr_hurt = m_hide_unaddressable_hurt.load();
            if (!show_hurt && !show_atk && !show_body) return;

            // KHit +0x60 values are already in absolute UE4 world space
            // (Z-up, cm) — the game's per-frame update bakes chara position,
            // yaw, and bone-local into the matrix that wrote them.  The
            // walker just reads them raw; no xform parameter needed.
            Horse::KHitWalker::forEachKHit(
                chara.raw(),
                static_cast<uint32_t>(pi),
                [&](const Horse::KHitDraw& d) {
                    // Gate by list kind using THIS chara's per-player flags.
                    switch (d.list)
                    {
                        case Horse::KHitList::Hurtbox:
                            if (!show_hurt) return;
                            // "Addressable only" — skip hurtboxes whose
                            // +0x17 slot is outside ResolveAttackVs
                            // HurtboxMask22's iteration range
                            // (chara+0x44494 clamped to 22).  The engine
                            // will happily OR attacker bits into the
                            // PerHurtboxBitmask index corresponding to
                            // these nodes, but the classifier never
                            // reads those slots so no reaction is
                            // written and no damage lands — they're
                            // effectively engine-invisible.  See
                            // m_hide_unaddressable_hurt comment for the
                            // full derivation.
                            if (hide_unaddr_hurt && !d.classifier_addressable)
                                return;
                            break;
                        case Horse::KHitList::Attack:
                            if (!show_atk) return;
                            // Engine-derived sub-gate: strikes vs throws.
                            if (!shouldShowAttackRole(d.attack_role)) return;
                            // "Live only" filter — drop authored-but-cold
                            // attack nodes whose per-frame gate at +0x14
                            // is zero.  `is_current_attack` already holds
                            // (activeGate != 0) as set in walkList().
                            //
                            // NOTE: +0x14 is the GEOMETRY gate, not the
                            // damage gate.  The MoveVM hotMask has a
                            // permanent floor of 0x3FFFD (slots {0, 2..17}
                            // forced on every frame), so many boxes pass
                            // this filter even during neutral.  For the
                            // tighter engine-truth filter see hide_not_dmg
                            // below, which tests the per-slot bit in
                            // *chara[+0x44058] (the damage-active cell).
                            if (hide_cold_atk && !d.is_current_attack) return;
                            // Damage gate — the actor-level own-attack
                            // mask that ResolveAttackVsHurtboxMask22 AND-s
                            // against the opponent's hurtbox mask before
                            // it fires a hit.  Unlike +0x14 this is empty
                            // during neutral and only lights up on real
                            // active frames.
                            if (hide_not_dmg && !d.is_damage_active) return;
                            // Per-frame damage gate — strictest of the
                            // three "is this attack live" filters.
                            // Hides the box during startup AND recovery
                            // within the same move-slot, leaving only
                            // the engine-authored damage-window frames
                            // visible.  Independent of the other two,
                            // so users can stack them or use just this.
                            if (hide_not_pf && !d.is_per_frame_active) return;
                            break;
                        case Horse::KHitList::Body:
                            if (!show_body) return;
                            break;
                    }

                    const Horse::FLinColor col = colourFor(d, pi);
                    Horse::DrawKHitDraw(m_backend, d, col, T);
                    ++nodes_drawn;
                });
        });

        // Commit the state we actually pushed to the game this frame.
        // Only update last-applied when we had at least one chara to push
        // to; otherwise we'd "lose" the pending transition (e.g. toggle
        // flips OFF between rounds while no chara exists — we'd never
        // get a chance to call SetWeaponVisibility(true) and weapons
        // would stay hidden).
        //
        // On next frame:
        //   * If hide_weapons_now is still true we keep re-hiding.
        //   * If it transitions true -> false we'll detect (was_hiding
        //     was true, now false) and apply once to restore.
        //   * If false -> false we skip entirely.
        if (charas_seen > 0)
        {
            m_last_applied_hide_weapons.store(hide_weapons_now);
        }

        m_backend.endFrame();

        // Once per ~2s: dump a summary so we can tell whether each stage
        // fired.  Parallel to the KHitWalker's shouldLog() throttle.
        // DISABLED — noisy; re-enable for end-to-end health checks.
#if 0
        if ((++m_diag_tick & 0x7F) == 1)
        {
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] frame pivot=0x{:x} backend_ready={} charas={} drawn={}\n"),
                reinterpret_cast<uintptr_t>(raw_cockpit),
                m_backend.isReady() ? 1 : 0,
                charas_seen,
                nodes_drawn);
        }
#else
        (void)charas_seen; (void)nodes_drawn; (void)raw_cockpit;
        ++m_diag_tick;
#endif
    }

    // ------------------------------------------------------------------
    // Colour scheme (engine-role driven, not size-heuristic)
    //   Hurtboxes — green (receive volumes).  Bright red when reaction_hot
    //               (this slot just got hit — sticky-extended).
    //   Attacks   — amber (strike) / magenta (throw/grab).  Hot (the
    //               currently-active cell) overrides to bright yellow for
    //               strikes or bright pink for throws so you can still see
    //               which one is live.
    //   Body/push — dim blue.  These are not involved in damage.
    // A subtle per-player hue nudge keeps P1 / P2 distinguishable when
    // they overlap visually.
    // ------------------------------------------------------------------
    static Horse::FLinColor colourFor(const Horse::KHitDraw& d, int pi)
    {
        const float player_tint = (pi == 1) ? 0.80f : 1.0f;

        switch (d.list)
        {
            case Horse::KHitList::Hurtbox:
            {
                if (d.reaction_hot)
                    return Horse::FLinColor{ 1.0f, 0.15f, 0.15f, 1.0f };

                // Unified green for all hurtbox entries — the engine
                // doesn't sub-categorise these from the defender side.
                return Horse::FLinColor{ 0.25f * player_tint,
                                         0.95f,
                                         0.35f * player_tint, 1.0f };
            }

            case Horse::KHitList::Attack:
            {
                const bool is_throw =
                    (d.attack_role == Horse::KHitAttackRole::Throw);

                if (d.is_current_attack)
                {
                    // Hot (live this frame) — max-saturation colour so it
                    // pops out of the other attack boxes on the same chara.
                    return is_throw
                        ? Horse::FLinColor{ 1.0f, 0.30f, 0.85f, 1.0f }  // hot throw = pink
                        : Horse::FLinColor{ 1.0f, 1.0f, 0.25f, 1.0f }; // hot strike = yellow
                }
                // Cold.
                return is_throw
                    ? Horse::FLinColor{ 0.85f * player_tint,
                                        0.15f,
                                        0.70f * player_tint, 0.6f }   // cold throw = magenta
                    : Horse::FLinColor{ 1.0f * player_tint,
                                        0.55f * player_tint,
                                        0.10f, 0.6f };                // cold strike = amber
            }

            case Horse::KHitList::Body:
            default:
                return Horse::FLinColor{ 0.25f,
                                         0.45f * player_tint,
                                         1.0f * player_tint, 0.5f };
        }
    }

    // ------------------------------------------------------------------
    // ImGui tab
    // ------------------------------------------------------------------
    void render_tab_impl()
    {
        bool enabled = m_enabled.load();
        if (ImGui::Checkbox("Overlay enabled (F5)", &enabled))
        {
            m_enabled.store(enabled);
            if (!enabled) m_backend.hideAll();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("ticks:%d  hook:%s  lbc:%s  native:%s",
            m_update_calls,
            m_hook_registered                ? "OK" : "pending",
            m_backend.isReady()              ? "OK" : "idle",
            Horse::NativeBinding::isReady()  ? "OK" : "miss");

        ImGui::Separator();
        ImGui::TextUnformatted("KHit lists — per player");
        ImGui::TextDisabled(
            "Hurtboxes + Attacks default ON.  Body / pushbox default OFF "
            "(noisy — spacing context only).");

        auto per_player_row = [](const char* label,
                                 std::atomic<bool>& hurt,
                                 std::atomic<bool>& atk,
                                 std::atomic<bool>& body,
                                 const char* id_suffix)
        {
            ImGui::PushID(id_suffix);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(80.0f);
            {
                bool h = hurt.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Hurtboxes##%s", id_suffix);
                if (ImGui::Checkbox(tag, &h)) hurt.store(h);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "KHit HurtboxListHead (chara+0x444B8).\n"
                    "Volumes that RECEIVE damage.  Each node's BoneId byte\n"
                    "at +0x17 indexes PerHurtboxBitmask[22] (+0x44078) and\n"
                    "PerHurtboxReactionState[22] (+0x1C74).  Green; red\n"
                    "flash on reaction-state non-zero (just got hit),\n"
                    "sticky-extended per the 'Hit-flash duration' slider.");
            }
            ImGui::SameLine();
            {
                bool a = atk.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Attacks##%s", id_suffix);
                if (ImGui::Checkbox(tag, &a)) atk.store(a);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "KHit AttackListHead (chara+0x44498).\n"
                    "Volumes that DEAL damage (or initiate grabs).\n"
                    "Strike: amber cold, yellow when active.\n"
                    "Throw : magenta cold, pink when active.\n"
                    "\"Active\" = engine's per-frame gate at node+0x14\n"
                    "(set by the tick to (hotMask >> node[+0x17]) & 1).\n"
                    "When active, the node's CategoryMask is OR'd into\n"
                    "the opponent's PerHurtboxBitmask this tick.");
            }
            ImGui::SameLine();
            {
                bool b = body.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Body##%s", id_suffix);
                if (ImGui::Checkbox(tag, &b)) body.store(b);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "KHit BodyListHead (chara+0x44478).\n"
                    "Character-to-character physical pushing only — used\n"
                    "by LuxBattle_SolvePhysBodyCollision @ 0x14030CCF0.\n"
                    "Not involved in damage resolution.  Dim blue.");
            }
            ImGui::PopID();
        };

        per_player_row("P1",
                       m_show_p1_hurt, m_show_p1_atk, m_show_p1_body, "p1");
        per_player_row("P2",
                       m_show_p2_hurt, m_show_p2_atk, m_show_p2_body, "p2");

        // Quick-action buttons for the common cases.
        if (ImGui::SmallButton("All ON"))
        {
            m_show_p1_hurt.store(true); m_show_p1_atk.store(true); m_show_p1_body.store(true);
            m_show_p2_hurt.store(true); m_show_p2_atk.store(true); m_show_p2_body.store(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("All OFF"))
        {
            m_show_p1_hurt.store(false); m_show_p1_atk.store(false); m_show_p1_body.store(false);
            m_show_p2_hurt.store(false); m_show_p2_atk.store(false); m_show_p2_body.store(false);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Defaults (no Body)"))
        {
            m_show_p1_hurt.store(true); m_show_p1_atk.store(true); m_show_p1_body.store(false);
            m_show_p2_hurt.store(true); m_show_p2_atk.store(true); m_show_p2_body.store(false);
        }

        // --- Attack role filters (engine-derived) -----------------------
        // Source: LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100
        // partitions the 64-bit CategoryMask at node+0x08 into two disjoint
        // bit regions.  We expose the same partition here.
        ImGui::Spacing();
        ImGui::TextUnformatted("Attack roles (shared)");
        ImGui::TextDisabled(
            "Engine-derived from CategoryMask bits at node+0x08.\n"
            "Strike = normal attacks (all category bits except 31, 55).\n"
            "Throw  = grabs / throws (mask bits 31 or 55).");
        {
            bool s = m_show_atk_strike.load();
            if (ImGui::Checkbox("Strike##atkrole", &s)) m_show_atk_strike.store(s);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Normal attacks that damage on contact with a hurtbox.\n"
                "Mask bits in 0xFF7FFFFF7FFFFFFF (all except bit 31/55).\n"
                "Drawn amber (cold) / yellow (currently active).");
            ImGui::SameLine();
            bool t = m_show_atk_throw.load();
            if (ImGui::Checkbox("Throw / Grab##atkrole", &t)) m_show_atk_throw.store(t);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Grab attacks (throws, command grabs).  The classifier\n"
                "pre-scans for bits 31 or 55 in the CategoryMask and\n"
                "handles them as an all-or-nothing gate before the\n"
                "per-hurtbox strike loop runs.\n"
                "Mask: 0x0080000080000000.\n"
                "Drawn magenta (cold) / pink (currently active).");
        }
        if (ImGui::SmallButton("Strikes only"))
        {
            m_show_atk_strike.store(true);
            m_show_atk_throw .store(false);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Throws only"))
        {
            m_show_atk_strike.store(false);
            m_show_atk_throw .store(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Both roles"))
        {
            m_show_atk_strike.store(true);
            m_show_atk_throw .store(true);
        }

        // --- Live attacks only (engine-side per-frame gate) -------------
        // Toggle between "authored" view and "live" view.  The engine's
        // own OR-aggregation loop in UpdateAllKHitWorldCenters @
        // 0x14030D6A0 gates participation on `*(short*)(node+0x14) != 0`,
        // both on the attacker and on the defender's hurtbox.  This
        // filter mirrors that gate: authored-but-cold attack nodes are
        // hidden so the overlay shows only what's actually making hits
        // this tick.
        ImGui::Spacing();
        {
            bool lo = m_hide_cold_atk.load();
            if (ImGui::Checkbox("Live attacks only (node+0x14 != 0)", &lo))
                m_hide_cold_atk.store(lo);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "When on, hide attack-list nodes whose per-frame 'active'\n"
                "bit at node+0x14 is zero.  The engine itself gates on\n"
                "this bit in its OR-aggregation loop at 0x14030D6A0 — a\n"
                "node with +0x14 == 0 can never OR its CategoryMask into\n"
                "the opponent's PerHurtboxBitmask this tick.\n\n"
                "CAVEAT: the MoveVM hotMask has a permanent floor of\n"
                "0x3FFFD (slots {0, 2..17} forced ON every frame), so\n"
                "many attack nodes will pass this geometry gate even\n"
                "during neutral.  For the tighter engine-truth filter\n"
                "use the damage-gate checkbox below.\n\n"
                "Off: draw every authored attack volume (amber cold +\n"
                "yellow live).  Useful for seeing a move's full authored\n"
                "coverage.\n"
                "On : draw only currently-live attack volumes (yellow).\n"
                "Useful for answering 'which of these is actually hitting\n"
                "right now?'.");
        }

        // --- Damage-active only (actor-level own-attack cell) -----------
        // Tighter than +0x14.  Reads *chara[+0x44058] (the own-attack
        // active cell) and tests bit (node[+0x17] & 0x3F) — the same
        // per-slot bit the engine uses in ResolveAttackVsHurtboxMask22
        // when deciding whether to fire a hit against the opponent's
        // hurtbox mask.  Unlike the +0x14 geometry gate this cell is
        // zero during neutral — so "Damage-active only" gives the cleanest
        // "is this box doing anything RIGHT NOW?" view.
        {
            bool dg = m_hide_not_damage_active.load();
            if (ImGui::Checkbox("Damage-active only (chara+0x44058 bit)", &dg))
                m_hide_not_damage_active.store(dg);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "When on, hide attack-list nodes whose slot bit is not\n"
                "set in the actor's own-attack-active cell at\n"
                "*chara[+0x44058].  This is the mask that\n"
                "ResolveAttackVsHurtboxMask22 AND-s against the opponent's\n"
                "PerHurtboxBitmask — if the bit is clear, the node\n"
                "physically cannot damage this tick, regardless of\n"
                "geometry.\n\n"
                "Distinction from the +0x14 filter above:\n"
                " • +0x14 is the per-node GEOMETRY gate.  Subject to the\n"
                "   MoveVM 0x3FFFD floor — slots {0, 2..17} force-on every\n"
                "   frame, so ~20 cold boxes still pass it in neutral.\n"
                " • +0x44058 is the ACTOR-level DAMAGE gate.  Empty\n"
                "   during neutral; lights up only on real active frames.\n\n"
                "On : strongest 'currently hitting' filter.\n"
                "Off: show all geometry-live attacks (respects +0x14 if\n"
                "     that filter is also on).");
        }

        // --- Per-frame damage-active only (move sub-frame cell) ----------
        // STRICTEST of the three "is this attack live" filters.  Mirrors
        // the engine's pMoveVMCell lookup at 0x14033cca0: walks
        // chara+0x44dc2 (current sub-frame id) + chara+0x455c0 (move
        // bank base) to fetch the per-sub-frame damage bitmask, then
        // tests this node's slot bit in it.
        //
        // Difference vs. "Damage-active only" above:
        //  • +0x44058 (above) is set ONCE per move-slot in
        //    LuxMoveVM_SetActiveMoveSlot (0x140300c70) and stays put
        //    through startup/active/recovery — so a hitbox shows
        //    "damage-active" through the entire move duration.
        //  • The per-frame mask follows the move's authored sub-frame
        //    timeline, switching off during pre-active and post-active
        //    frames just like the engine's hit-detection does.
        //
        // Use this to answer "exactly which frames does this hitbox
        // deal damage on?" — which is what frame-data analysis wants.
        {
            bool pf = m_hide_not_per_frame_active.load();
            if (ImGui::Checkbox("Per-frame damage-active only (sub-frame cell)", &pf))
                m_hide_not_per_frame_active.store(pf);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "When on, hide attack-list nodes whose slot bit is not\n"
                "set in the engine's PER-FRAME damage mask for this\n"
                "chara's currently-active move sub-frame.\n\n"
                "Strictly tighter than 'Damage-active only' above:\n"
                " • Damage-active reads *chara[+0x44058], a per-move\n"
                "   mask set on slot transition — stays on through the\n"
                "   whole move including startup and recovery.\n"
                " • Per-frame reads the move-data sub-frame cell,\n"
                "   which advances each frame and turns off outside\n"
                "   the authored damage window.\n\n"
                "On  : answers 'does this hitbox deal damage on THIS\n"
                "      frame?'.  Useful for frame-data analysis —\n"
                "      step the game with F6 and watch the box appear\n"
                "      only during the move's active frames.\n"
                "Off : ignore this filter (the other gates still run).");
        }

        // --- Addressable hurtboxes only (classifier-range gate) ----------
        // Hide hurtboxes whose +0x17 slot is outside the classifier's
        // iteration range at ResolveAttackVsHurtboxMask22 — i.e.
        // bone_id_internal >= HurtboxSlotCount (chara+0x44494, capped
        // at 22).  Those hurtboxes participate in overlap testing and
        // the engine does OR attacker bits into their corresponding
        // PerHurtboxBitmask slot, but the classifier's for-loop bound
        // means it never reads that slot — so the hurtbox can never
        // produce a reaction, flash red, or cause damage.  This is
        // the real reason "body-wide sphere never gets hit" — it's
        // authored at an unreachable slot.
        //
        // (An earlier revision of this UI exposed a +0x14-based "live
        // hurtboxes only" filter — that was a mistake; hurtboxes
        // don't go through TickHitResolution's per-frame +0x14
        // update, so their +0x14 is pinned at 1 forever and such a
        // filter is a no-op.)
        ImGui::Spacing();
        {
            bool uh = m_hide_unaddressable_hurt.load();
            if (ImGui::Checkbox("Addressable hurtboxes only (+0x17 < count)", &uh))
                m_hide_unaddressable_hurt.store(uh);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "When on, hide hurtboxes whose per-node slot byte at\n"
                "+0x17 is >= HurtboxSlotCount (chara+0x44494, capped at\n"
                "22 to match the classifier's hard bound).\n\n"
                "Why this matters: LuxBattle_ResolveAttackVsHurtboxMask22\n"
                "iterates only slots 0..HurtboxSlotCount-1 when reading\n"
                "PerHurtboxBitmask.  A hurtbox authored at slot >= count\n"
                "will have attacker bits OR'd into its PerHurtboxBitmask\n"
                "entry by UpdateAllKHitWorldCenters, but the classifier\n"
                "never looks at that entry — no reaction is written, no\n"
                "damage is applied.  Visually the hurtbox is alive but\n"
                "functionally it's engine-invisible.\n\n"
                "This is the most likely reason for a large, always-\n"
                "drawn body sphere that appears immune to every attack\n"
                "(counter-hit detection zones, throw-whiff volumes, and\n"
                "reaction-state probes are often authored at high\n"
                "slots).\n\n"
                "Off: show every hurtbox including unaddressable ones.\n"
                "On : show only hurtboxes the classifier actually reads\n"
                "     — the 'can this hurtbox ever produce a reaction?'\n"
                "     view.");
        }

        // --- Weapon visibility override ---------------------------------
        // Force hide both charas' weapons so they stop occluding the
        // hitbox overlay.  Calls SetWeaponVisibility(false) every frame
        // while on (so the game's own show-triggers don't sneak weapons
        // back in); calls SetWeaponVisibility(true) once on OFF to
        // restore.  Applies only while the overlay is enabled — if F5
        // turns the mod off, weapons stay in whatever state the engine
        // last set (typically visible).
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Scene");
        {
            bool hw = m_hide_weapons.load();
            if (ImGui::Checkbox("Hide weapons", &hw))
                m_hide_weapons.store(hw);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Hide both characters' weapon meshes while the overlay\n"
                "is enabled.  Useful when a bulky weapon (Nightmare's\n"
                "sword, Astaroth's axe) occludes the hitbox volumes you\n"
                "want to inspect.\n\n"
                "Uses ALuxBattleChara::SetWeaponVisibility(bool) — a\n"
                "BlueprintCallable UFunction on the battle chara class.\n"
                "Re-applied every frame while on, so any game-driven\n"
                "visibility change (cinematic cues) is overridden.\n"
                "Turning the toggle off calls SetWeaponVisibility(true)\n"
                "once per chara and then leaves the state alone.");

            // --- Always allow Ansel camera -----------------------------
            bool aa = m_ansel_always_allowed.load();
            if (ImGui::Checkbox("Always allow Ansel camera", &aa))
                m_ansel_always_allowed.store(aa);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Force NVIDIA Ansel (the free-camera photo mode) to be\n"
                "available at all times by calling\n"
                "UAnselFunctionLibrary::SetIsPhotographyAllowed(true)\n"
                "every frame.  SC6 normally blocks Ansel outside of\n"
                "specific states (menus, cinematics, ring-out); with\n"
                "this on you can trigger the Ansel hotkey any time.\n\n"
                "Runs independent of the F5 overlay toggle.  Turning\n"
                "this off calls SetIsPhotographyAllowed(false) exactly\n"
                "once so the engine resumes managing the flag itself.");

            // --- Lock camera position -----------------------------------
            // Bytepatch-based: NOPs the engine's per-frame stores into
            // the camera struct, so whatever pose the camera is in at
            // toggle-ON time stays put until OFF.  See
            // horselib/CamLock.hpp for the disassembly walk and the
            // history of why the previous CameraCache.POV-write
            // approach didn't work.
            bool lc = m_lock_camera.load();
            if (ImGui::Checkbox("Lock camera position", &lc))
            {
                m_lock_camera.store(lc);
                m_cam_lock.set(lc);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Freeze the camera at its current pose (location,\n"
                "rotation, and FOV).  Patches SC6's per-frame camera-\n"
                "commit instructions to NOPs while held; turning OFF\n"
                "restores the originals.\n\n"
                "To re-frame: toggle off, move the camera to where you\n"
                "want it, toggle back on.\n\n"
                "Runs independent of the F5 overlay toggle.  Ported\n"
                "from somberness's CE table (the 'Cam' cheat).");

            // Status line — useful for "is this even resolving?".
            if (!m_cam_lock.is_resolved() && lc)
            {
                ImGui::TextDisabled(
                    "(camera-lock AOB did not resolve — check log)");
            }
            else if (m_cam_lock.is_enabled())
            {
                ImGui::TextDisabled("(camera frozen — 10 stores NOPed)");
            }

            // --- Freeze frame (game pause) -----------------------------
            // Bytepatch + trampoline: capture battle-context rbx via a
            // tiny midfunction hook, NOP the engine's pause-bitfield
            // writers, then flip bit 0 ourselves.  Frame-step works by
            // briefly clearing the bit for one cockpit tick.
            bool ff = m_freeze_frame.load();
            if (ImGui::Checkbox("Freeze frame", &ff))
            {
                m_freeze_frame.store(ff);
                // First ON: resolve+enable patches.  After that the
                // patches stay live — toggling the checkbox just flips
                // the byte.
                if (ff && !m_game_pause.is_enabled())
                {
                    if (!m_game_pause.is_resolved()) m_game_pause.resolve();
                    m_game_pause.enable();
                }
                m_game_pause.set_paused(ff);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Freeze the battle simulation in place.  World stops\n"
                "ticking; UMG/render keeps going so you can rotate the\n"
                "free-camera (with Ansel) or step through frames.\n\n"
                "Implemented as a 3-patch bytepatch: a midfunction\n"
                "trampoline captures the battle-context pointer, two\n"
                "NOPs prevent the engine from clearing our pause bit.\n"
                "Ported from somberness's CE 'Game Pause' cheat.\n\n"
                "Caveat: don't return to the main menu while a freeze\n"
                "is active — the captured pointer goes dangling.");

            // Step-frame controls.  step_n_frames() queues frames so
            // mashing the button (or holding F6) is lossless.  Buttons
            // are gated on "patches live + battle active" — without a
            // captured rbx there's no pause byte to flip.
            const bool can_step =
                m_game_pause.is_enabled() && m_game_pause.has_captured();
            ImGui::BeginDisabled(!can_step || !ff);
            if (ImGui::Button("Step 1 (F6)"))
            {
                m_game_pause.step_one_frame();
            }
            ImGui::SameLine();
            // Step-N: small int-input + button.  Static so the value
            // persists across frames; common values for SC6 hitbox
            // analysis are ~10 (skip startup) or ~30 (skip recovery).
            static int s_step_n = 10;
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::InputInt("##stepn", &s_step_n, 0))
            {
                if (s_step_n < 1)   s_step_n = 1;
                if (s_step_n > 600) s_step_n = 600;
            }
            ImGui::SameLine();
            if (ImGui::Button("Step N"))
            {
                m_game_pause.step_n_frames(s_step_n);
            }
            ImGui::EndDisabled();

            // Tooltips — split between "why disabled" and "what it does".
            if (!can_step || !ff)
            {
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    !ff ? "Enable Freeze frame first."
                        : !m_game_pause.has_captured()
                            ? "Waiting for battle context — start a "
                              "match to populate the captured pointer."
                            : "Patches not active.");
            }

            // Status / debug line.
            if (!m_game_pause.is_resolved() && ff)
            {
                ImGui::TextDisabled(
                    "(game-pause AOB did not resolve — check log)");
            }
            else if (m_game_pause.is_enabled())
            {
                if (!m_game_pause.has_captured())
                {
                    ImGui::TextDisabled(
                        "(patches live; awaiting first battle tick)");
                }
                else if (const int q = m_game_pause.step_pending(); q > 0)
                {
                    ImGui::TextDisabled("(stepping… %d frame%s queued)",
                                        q, q == 1 ? "" : "s");
                }
                else if (m_game_pause.is_paused())
                {
                    ImGui::TextDisabled("(paused — F6 to step)");
                }
            }

            // --- Hide characters (bytepatch, no flicker) ---------------
            // Inverts the engine's own visibility-compare instructions
            // inside ALuxBattleChara_SyncMoveStateVisibility — the
            // chara stays hidden through every move state including
            // critical edges and transformations that previously caused
            // 1-frame flickers.  See horselib/CharaInvis.hpp.
            bool hc = m_hide_chara.load();
            if (ImGui::Checkbox("Hide characters", &hc))
            {
                m_hide_chara.store(hc);
                m_chara_invis.set(hc);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Hide both characters' meshes (and attached components —\n"
                "hair tassels, skirts, accessories) while leaving\n"
                "simulation fully active.  Hitboxes are part of the\n"
                "gameplay skeleton, not the mesh, so they keep updating\n"
                "fine while the model is invisible.\n\n"
                "Implemented as two single-byte patches inside\n"
                "ALuxBattleChara_SyncMoveStateVisibility — flips the\n"
                "engine's own visibility-compare instructions so the\n"
                "renderer reads 'hidden' regardless of move-state\n"
                "writes.  No flicker on critical edges, supers, or\n"
                "transformations.\n\n"
                "Ported from somberness's CE 'Invisible' cheat.");

            if (!m_chara_invis.is_resolved() && hc)
            {
                ImGui::TextDisabled(
                    "(chara-invis AOB did not resolve — check log)");
            }

            // --- Suppress VFX ------------------------------------------
            // Bytepatch port of somberness's CE "VFX off" cheat.
            // Patches the engine's per-slot VFX-state writer to plant a
            // sentinel constant the renderer culls — effects never
            // become visible.  See horselib/VFXOff.hpp.
            bool sv = m_suppress_vfx.load();
            if (ImGui::Checkbox("Suppress VFX", &sv))
            {
                m_suppress_vfx.store(sv);
                m_vfx_off.set(sv);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Disable hit-effect / spark / particle VFX by patching\n"
                "the engine's per-frame VFX-slot writer to plant a\n"
                "sentinel value the renderer treats as culled.\n\n"
                "Strictly stronger than the previous DestroyAllVFx\n"
                "polling approach: effects don't get a 1-frame flash on\n"
                "first spawn, and there's no per-tick UFunction call.\n\n"
                "Toggle OFF to restore.  Runs independent of the F5\n"
                "overlay toggle.  Ported from somberness's CE table.");

            if (!m_vfx_off.is_resolved() && sv)
            {
                ImGui::TextDisabled(
                    "(VFX-off AOB did not resolve — check log)");
            }
        }

        // --- Hit-flash duration -----------------------------------------
        // The raw PerHurtboxReactionState signal is a ~1-frame pulse
        // (~16ms at 60fps) — too short to see.  This slider extends the
        // visible red flash by holding the "hot" state for N ms.  0 ms
        // disables the sticky entirely (raw 1-frame pulse only).
        ImGui::Spacing();
        ImGui::TextUnformatted("Hit-flash duration");
        {
            int ms = m_flash_ms.load();
            if (ImGui::SliderInt("ms##flashdur", &ms, 0, 1000, "%d ms"))
            {
                if (ms < 0)    ms = 0;
                if (ms > 1000) ms = 1000;
                m_flash_ms.store(ms);
                // Convert ms -> frames at 60 fps and push into the walker.
                // (SC6 runs locked 60; no point in making this fancier.)
                const int frames = (ms * 60 + 500) / 1000;
                Horse::KHitWalker::setStickyFrames(frames);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "How long to hold the red 'just got hit' flash after the\n"
                "engine's PerHurtboxReactionState signal fires.\n"
                "0 ms = no sticky (1-frame pulse, usually invisible).\n"
                "Assumes 60fps; converted to frames internally.");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Render");
        {
            float t = m_thickness.load();
            if (ImGui::SliderFloat("Thickness", &t, 0.5f, 8.0f, "%.1f"))
                m_thickness.store(t);

            const char* slot_names[3] = {
                "Default (depth-tested)",
                "Persistent",
                "Foreground (on top)",
            };
            int slot_idx = static_cast<int>(m_slot.load());
            if (ImGui::Combo("LineBatcher slot", &slot_idx, slot_names, 3))
                m_slot.store(static_cast<Horse::LineBatcherSlot>(slot_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Which of UWorld's three ULineBatchComponents to append to.\n"
                    "  Default     — depth-tested, occluded by world geometry\n"
                    "  Persistent  — depth-tested, identical behaviour per-frame\n"
                    "  Foreground  — NO depth test, always on top (recommended)");
        }
    }
};

// ----------------------------------------------------------------------------
#define HORSE_MOD_API __declspec(dllexport)
extern "C"
{
    HORSE_MOD_API CppUserModBase* start_mod()                    { return new HorseMod(); }
    HORSE_MOD_API void             uninstall_mod(CppUserModBase* m) { delete m; }
}
