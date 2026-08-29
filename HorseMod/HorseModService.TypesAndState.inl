    static inline std::atomic<HorseMod*> s_instance{nullptr};

#if HORSE_ENABLE_GEKKONET
    struct OnlineStateToken
    {
        std::uint32_t magic{0x484f5253u};
        std::uint32_t version{1};
        std::int32_t gekko_frame{-1};
        std::uint32_t reserved{};
        Horse::Deterministic::FrameCoordinate coordinate{};
        Horse::Deterministic::CanonicalHash hash{};
    };
    static_assert(sizeof(OnlineStateToken)
        <= Horse::Deterministic::GekkoRollbackSession::maximum_state_bytes);

    enum class OnlineRuntimeKind : std::uint8_t
    {
        Qualification,
        Production,
    };

    enum class OnlineQualificationFault : std::uint32_t
    {
        None,
        PreownershipContractMismatch,
        PreownershipTimeout,
        PostownershipAuthentication,
        PostownershipHash,
        PostownershipRestore,
        PostownershipPeer,
    };

    class QualificationOnlineCoordinator final
    {
    public:
        QualificationOnlineCoordinator(
            Horse::Deterministic::IRollbackTransport& transport,
            const Horse::Deterministic::IOnlineContentAllowlist& allowlist)
            noexcept : core_(transport, allowlist) {}
        Horse::Deterministic::OnlineCoordinator& core() noexcept
        { return core_; }
        const Horse::Deterministic::OnlineCoordinator& core() const noexcept
        { return core_; }
    private:
        Horse::Deterministic::OnlineCoordinator core_;
    };

    class ProductionOnlineCoordinator final
    {
    public:
        ProductionOnlineCoordinator(
            Horse::Deterministic::IRollbackTransport& transport,
            const Horse::Deterministic::IOnlineContentAllowlist& allowlist)
            noexcept : core_(transport, allowlist) {}
        Horse::Deterministic::OnlineCoordinator& core() noexcept
        { return core_; }
        const Horse::Deterministic::OnlineCoordinator& core() const noexcept
        { return core_; }
    private:
        Horse::Deterministic::OnlineCoordinator core_;
    };

    class OnlineCoordinatorRouter final
    {
    public:
        OnlineCoordinatorRouter(
            QualificationOnlineCoordinator& qualification,
            ProductionOnlineCoordinator& production) noexcept
            : qualification_(qualification), production_(production) {}
        void Select(OnlineRuntimeKind kind) noexcept { kind_ = kind; }
        [[nodiscard]] OnlineRuntimeKind kind() const noexcept { return kind_; }
        [[nodiscard]] Horse::Deterministic::OnlineCoordinator& active() noexcept
        {
            return kind_ == OnlineRuntimeKind::Production
                ? production_.core() : qualification_.core();
        }
        [[nodiscard]] const Horse::Deterministic::OnlineCoordinator& active()
            const noexcept
        {
            return kind_ == OnlineRuntimeKind::Production
                ? production_.core() : qualification_.core();
        }
        operator Horse::Deterministic::OnlineCoordinator&() noexcept
        { return active(); }
        auto state() const noexcept { return active().state(); }
        auto failure_disposition() const noexcept
        { return active().failure_disposition(); }
        auto owns_simulation() const noexcept { return active().owns_simulation(); }
        auto active_contract() const noexcept { return active().active_contract(); }
        auto baseline_target() const noexcept { return active().baseline_target(); }
        auto Enable() noexcept { return active().Enable(); }
        auto ObserveLobby(const Horse::Deterministic::OnlinePeerContract& value)
            noexcept { return active().ObserveLobby(value); }
        auto Pump() noexcept { return active().Pump(); }
        auto ReadyBaseline(Horse::Deterministic::FrameCoordinate value) noexcept
        { return active().ReadyBaseline(value); }
        auto ObserveBaselineProgress(
            Horse::Deterministic::FrameCoordinate value) noexcept
        { return active().ObserveBaselineProgress(value); }
        auto FreezeBaseline(Horse::Deterministic::FrameCoordinate coordinate,
            const Horse::Deterministic::CanonicalHash& hash,
            const Horse::Deterministic::CanonicalHash& loaded_map) noexcept
        { return active().FreezeBaseline(coordinate, hash, loaded_map); }
        auto BeginOwnedInputApplication() noexcept
        { return active().BeginOwnedInputApplication(); }
        auto BeginRoundBarrier(std::uint64_t completed_generation,
            std::uint64_t next_generation,
            const Horse::Deterministic::CanonicalHash& hash) noexcept
        {
            return active().BeginRoundBarrier(
                completed_generation, next_generation, hash);
        }
        auto NotifyOwnedTick(Horse::Deterministic::FrameCoordinate coordinate)
            noexcept { return active().NotifyOwnedTick(coordinate); }
        auto SendConfirmedHash(Horse::Deterministic::FrameCoordinate coordinate,
            const Horse::Deterministic::CanonicalHash& hash) noexcept
        { return active().SendConfirmedHash(coordinate, hash); }
        auto PopGameplay() noexcept { return active().PopGameplay(); }
        auto Abort(Horse::Deterministic::FailureCode code) noexcept
        { return active().Abort(code); }
        auto ReturnToLobby() noexcept { return active().ReturnToLobby(); }
        auto NotifyReturnedToLobby(
            const Horse::Deterministic::OnlineSceneExitEvidence& evidence)
            noexcept { return active().NotifyReturnedToLobby(evidence); }
        void Disable() noexcept { active().Disable(); }
        bool IsClearForStock() const noexcept
        { return active().IsClearForStock(); }

    private:
        QualificationOnlineCoordinator& qualification_;
        ProductionOnlineCoordinator& production_;
        OnlineRuntimeKind kind_{OnlineRuntimeKind::Qualification};
    };

    class QualificationOnlineAllowlist final
        : public Horse::Deterministic::IOnlineContentAllowlist
    {
    public:
        void Arm(const Horse::Deterministic::OnlineContentContract& content)
            noexcept
        {
            expected_ = content;
            armed_ = true;
        }
        void Clear() noexcept { expected_ = {}; armed_ = false; }
        [[nodiscard]] bool IsClearForStock() const noexcept { return !armed_; }
        [[nodiscard]] bool IsQualified(
            const Horse::Deterministic::OnlineContentContract& content)
            const noexcept override
        {
            return armed_ && content == expected_;
        }
    private:
        Horse::Deterministic::OnlineContentContract expected_{};
        bool armed_{};
    };
#endif

    // ---- Overlay state ----
    std::atomic<bool> m_enabled{false};

    // Per-player visibility toggles.  Default on-launch layout is
    // "only P2's hurtboxes visible" - the most common starting point
    // for frame-data practice where P2 is the training dummy and you
    // want to see their incoming-damage volumes without the visual
    // noise of P1's own attacks / hurtboxes.  User flips the rest on
    // per-session from the Hitboxes tab.  Each flag indexed by
    // PlayerIndex (0 = P1, 1 = P2).
    std::atomic<bool> m_show_p1_hurt{false};
    std::atomic<bool> m_show_p1_atk {true};
    std::atomic<bool> m_show_p1_body{false};
    std::atomic<bool> m_show_p2_hurt{true};
    std::atomic<bool> m_show_p2_atk {true};
    std::atomic<bool> m_show_p2_body{false};

    // ----------------------------------------------------------------
    //   Box-visibility filter - single master toggle.
    //
    //   m_only_show_active   default ON   active-shape narrow filter
    //                                     applied to both lists:
    //                                       hits  ? selected active-window
    //                                              geometry, ignoring
    //                                              post-hit re-hit lockout
    //                                       hurts ? classifier_addressable
    //                                              && overlap_active
    //                                              && defender_can_react_engine
    //                                     Audit still records the stricter
    //                                     native damage-live predicate.
    //
    //   The two chara-wide gates (attacker_can_strike_engine /
    //   defender_can_react_engine - same boolean, dual-named) cover
    //   the resolver's three early-return sites that disable
    //   reaction processing wholesale:
    //     * Battle running         (DAT_144846410 != 0)
    //     * Not incapacitated/dead (chara+0x20B8 == 0)
    //     * Not in no-react state  (chara+0x19B0 != 6)
    //   When any fails, EVERY hurtbox on this chara is inert
    //   regardless of slot index / +0x14 / category mask.  Examples:
    //   round-end "WIN" cinematic, KO recovery, paused / loading.
    //
    // Engine-truth predicates:
    //   is_per_frame_active = (attack_node[+0x14] != 0) &&
    //                         (slot_bit_mask & chara[+0x44058]) != 0
    //                       - exact predicate of
    //                         LuxBattle_ResolveAttackVsHurtboxMask22
    //                         @ 0x14033C100 before firing damage.
    //   classifier_addressable = (slot < min(chara+0x44494, 22))
    //                       - slot index is within the classifier's
    //                         iteration range.  A box at slot >= cap
    //                         can never deal damage no matter what
    //                         its +0x14 says, because the for-loop in
    //                         ResolveAttackVsHurtboxMask22 won't read
    //                         its PerHurtboxBitmask entry.
    //   overlap_active      = (hurt_node[+0x14] != 0)
    //                       - same byte the engine's overlap loop in
    //                         LuxBattleChara_UpdateAllKHitWorldCenters
    //                         @ 0x14030D6A0 gates iteration on.
    //                         Initialised to 1 by
    //                         Lux_KHitChk_DeserializeLinkedList; can
    //                         be flipped per-frame by MoveVM opcode
    //                         0x13AC (LuxMoveVM_SetHurtboxSlots-
    //                         ActiveMask @ 0x140308D70).
    //   defender_can_react_engine = (DAT_144846410 != 0) &&
    //                                (chara+0x20B8 == 0) &&
    //                                (chara+0x19B0 != 6)
    //                       - the three early-return gates of
    //                         LuxBattle_ResolveAttackVsHurtboxMask22.
    //                         When any fails, the whole resolver
    //                         skips and no slot is read.  Same
    //                         boolean is exposed as
    //                         attacker_can_strike_engine for
    //                         self-documenting attack-side filter
    //                         code (an incapacitated chara doesn't
    //                         deal damage either).
    // ----------------------------------------------------------------
    std::atomic<bool> m_only_show_active{true };

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
    //   ON : re-apply hidden every frame - overrides any game-driven
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
    // ProcessEvent - no native-RVA binding needed.
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
    //        Per-match actual session lifecycle - gated on (1) and (2).
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
    // This runs independent of the F5 overlay toggle - the user said
    // "always allows", so it fires from the hook pre-callback before
    // the enabled / NativeBinding-ready gates.
    std::atomic<bool> m_ansel_always_allowed{true};
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
    // memory" instructions to NOPs - see horselib/CamLock.hpp for the
    // full disassembly walk and the historical note on why the previous
    // CameraCache.POV-write approach didn't work (UMG widget tick runs
    // AFTER the renderer has already consumed the POV).
    //
    // Semantics:
    //   OFF: every store runs as normal - engine owns the camera.
    //   ON : 5 stores at site A and 5 stores at site B are NOPed.  The
    //        camera struct in memory keeps whatever location/rotation/FOV
    //        it had at the moment we toggled ON; nothing in the engine
    //        rewrites those fields for the duration.
    //
    // CamLock owns the live BytePatch state - it must outlive every
    // call site (kept until ~HorseMod) so the patches get cleanly
    // restored on mod unload.  Atomic for ImGui-thread reads against
    // the patch state (the patch-flip itself happens on the same
    // thread, so no race).
    Horse::CamLock    m_cam_lock{};
    std::atomic<bool> m_lock_camera{false};

    // ---- Free-fly camera ----------------------------------------------------
    // Writes the SC6 battle camera's pose (Location, Rotation, FOV) from
    // our own per-cockpit-tick state, driven by WASD + arrow keys.  Uses
    // CamLock internally to freeze engine writes so the input doesn't
    // fight the director-cam.  Independent of Nvidia Ansel - no Ansel
    // session is involved, so our hitbox overlay continues to render.
    //
    // Note: enabling free-camera implicitly enables CamLock; disabling
    // free-camera releases CamLock too.  If the user has ALSO manually
    // enabled "Lock camera", CamLock stays on after free-camera turns
    // off (set() only nudges it if it was internally activated).
    Horse::FreeCamera m_free_camera{};
    std::atomic<bool> m_free_camera_enabled{false};
    // Cached pointer to the local APlayerCameraManager, revalidated
    // each tick via PlayerController.PlayerCameraManager (UObject
    // property chain).  Null until battle has a live PC + PCM pair.
    //
    // HISTORY OF WHY THIS ISN'T "BattleCamera":
    //
    //   Iteration 1 (Gemini):
    //     Wrote to ALuxBattleCamera+0x410..+0x428 (from
    //     LuxBattleManager.BattleCamera) AND called
    //     K2_SetActorLocationAndRotation via ProcessEvent each tick.
    //     Neither moved the camera - the first is the wrong object,
    //     and the ProcessEvent call had a malformed params block
    //     (over-sized FHitResult shifted bTeleport off-offset).
    //
    //   Iteration 2:
    //     Removed the K2 call; kept writing to
    //     LuxBattleManager.BattleCamera+0x410.  Memory-persistence
    //     diagnostics confirmed our writes WERE landing on that
    //     object and nothing was stomping them, but the visual camera
    //     still didn't move.  That proved the write target was wrong.
    //
    //   Iteration 3 (current):
    //     Ghidra trace of UWorld::Tick @ 0x141f02230 shows the engine
    //     per-tick commit path is invoked as
    //       APlayerCameraManager_CommitPOV_NoInterp(plVar15[0x84])
    //     where plVar15 is an APlayerController and [0x84] (=+0x420)
    //     is the PlayerCameraManager field.  The 5 CamLock NOP
    //     targets all write to `this+0x410..+0x428` on that PCM.
    //     APlayerController::GetPlayerViewPoint @ 0x142046410 -
    //     the consumer invoked by ULocalPlayer::CalcSceneView - reads
    //     back from the SAME +0x410..+0x424 block on PCM.
    //
    //     So the renderer-authoritative POV data lives on the
    //     APlayerCameraManager, NOT the ALuxBattleCamera.  The
    //     actor's +0x410..+0x428 is a director-scratch block that
    //     nothing downstream reads - writing there looks like it
    //     works (memory persists) but has zero render-side effect.
    void*             m_cached_player_camera_manager = nullptr;
    // UE4SS reflection-side locators: a cached FindFirstOf handle for
    // APlayerController, revalidated by GlobalPtr::get on level
    // changes.  PCM is read as the "PlayerCameraManager" property
    // on the PC every tick (cheap - hashed FName lookup).
    Horse::GlobalPtr  m_player_controller{};

    // ---- Hide characters ----------------------------------------------------
    // Bytepatch port of somberness's CE "Invisible" cheat - see
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
    // every read of the visibility flag now produces "hidden" - eliminating
    // the race because we're INSIDE the read path, not racing the writes.
    //
    // Useful when you're diagnosing hitbox shapes on a specific move -
    // the character mesh and its skirt/cape/hair occlude the volumes.
    // Hitboxes are part of the gameplay skeleton, not the mesh, so they
    // keep updating fine while the mesh is invisible.
    Horse::CharaInvis m_chara_invis{};
    std::atomic<bool> m_hide_chara{false};

    // ---- Speed control (slow-motion / freeze) -------------------------------
    // Bytepatch port of somberness's CE "Speed control v2" cheat - see
    // horselib/SpeedControl.hpp for the full disassembly walk and the
    // user contract.
    //
    // 5 trampolines hijack every load of the engine's master delta-time /
    // time-dilation float and redirect it to a single user-controlled
    // `speedval` slot in the CodeCave.  Result: the LuxMoveVM simulation
    // (animations, hit timing, opcode-stream execution, motion-object
    // advancement) all scale uniformly with speedval.
    //
    //   speedval = 0.0   ? frozen
    //   speedval = 0.05  ? 20- slow-mo (great for active-frame inspection)
    //   speedval = 0.1   ? 10- slow-mo
    //   speedval = 0.5   ? half speed
    //   speedval = 1.0   ? normal
    //
    // Independent of the GamePause toggle and the F6 step hotkey - they
    // gate different mechanisms and stack cleanly.
    Horse::SpeedControl m_speed_control{};
    std::atomic<bool>   m_speed_enabled{false};
    std::atomic<float>  m_speed_value{1.0f};

    // ---- World-tick gate (PerFrameTick / Site 9 - moved here 2026-05-05) ---
    // Single PerFrameTick gate driving freeze + frame-step semantics
    // independently of the speedval / dt-multiply path.  See
    // horselib/WorldTickGate.hpp for the full plate.  In step+freeze mode
    // we set speedval = 1.0 (so the dt-multiply sites at 1/3/4/5/6/8 are
    // no-ops, eliminating the dt=0 contamination that was breaking multi-
    // hit moves under frame-step) and let this gate be the sole source of
    // "skip this frame" by holding an int32_t step-credit slot:
    //   policy = 0       -> bail every PerFrameTick call (frozen)
    //   policy = N > 0   -> next N PerFrameTick calls atomic-dec and run
    Horse::WorldTickGate m_world_tick_gate{};
    // Sibling gate for the replay master-clock INC instructions
    // (LuxBattleChara_VTable648_TickAndAdvanceReplayClock at 0x1403E1FC0
    // and the GatedBy4404 variant at 0x1403E2000).  Reads from the
    // WorldTickGate policy slot - when frozen, both INCs are skipped so
    // the master clock at ALuxBattleFrameInputLog+0x3A4 stays pinned.
    // Without this gate, match-replay viewing leaks: SimulationLoop's
    // catch-up loop keeps draining recorded inputs into BM input data,
    // round state machine advances, and on unfreeze Stage 3 fast-forwards
    // through the buffered inputs in one tick.  See
    // horselib/ReplayClockGate.hpp for the full plate.
    // Sibling gate for the surrounding Actor::Tick prologues that
    // WorldTickGate's single Site-9 hook misses:
    //   * ALuxBattleChara::TickActor (UE4 anim, hair, weapon mesh, SC
    //     gauge counter)
    //   * ALuxBattleManager::Tick / MainStateMachine_At1461 (round state
    //     machine, SimulationLoop catch-up)
    // Both bare-RET when WorldTickGate's policy slot is 0.  Without these,
    // long match-replay freezes settle the chara to the idle pose because
    // the BM round-over check trips on a wallclock-driven timer and the
    // chara mesh's anim montage plays out via the UE4-side actor tick.
    // See horselib/ActorTickGate.hpp for the full plate.
    Horse::ActorTickGate m_actor_tick_gate{};
    // Sibling gate that forces LuxMoveVM_GetTimeDilationScalar
    // (0x14030A8C0) to return 0.0 when WorldTickGate's policy slot is 0.
    // The function's normal-play fall-through path bypasses VMFreezeByte
    // entirely (returns chara+0x3500 directly), so VMFreezeByte=1 alone
    // doesn't halt P1 in match-replay watching.  This gate's entry-patch
    // returns 0 unconditionally during freeze, which forces every dt-
    // multiply integrator (MoveVM, physics, anim, FX) to produce 0
    // deltas - including UE4 anim instances that scale by the engine's
    // tick dilation.  See horselib/TimeDilationGate.hpp for the full
    // plate.
    Horse::TimeDilationGate m_time_dilation_gate{};
    Horse::WindRngGate m_wind_rng_gate{};
    // Previous-cockpit-tick snapshot of g_LuxBattle_FrameCounter.  Read
    // at the top of frame_step_apply() to detect whether PerFrameTick
    // ran since our last call - drives WorldTickGate's step-credit
    // drain on a real "did the world tick" signal instead of cockpit
    // hook timing.  Read/written exclusively from the cockpit pre-
    // tick (game thread); no atomic needed.
    uint32_t m_prev_frame_counter_value = 0;
    bool     m_prev_frame_counter_seen  = false;
    // Legacy SpeedControl write-dedupe state. Current time controls keep
    // SpeedControl disabled and drive WorldTickGate/ReplayClockGate/
    // ActorTickGate instead.
    float m_last_speed_target =
        std::numeric_limits<float>::quiet_NaN();

    // ---- SC6 NATIVE VM-FREEZE BYTE driver state ----------------------------
    // Tracks whether HorseMod has currently SET the native freeze byte at
    // imageBase + kRVA_LuxBattleVMFreezeRecord
    // (g_LuxBattle_VMFreezeRecord.bVMFreezeByte).
    //
    // Used by frame_step_apply() to:
    //   * Skip touching the byte entirely on the steady-state "freeze
    //     never requested" path (= byte should stay 0, no need to
    //     re-check the page each frame).
    //   * Avoid stomping on SC6's OWN hit-stop / cinematic freeze writes
    //     (when bVMFreezeByte is non-zero because SC6 set it, we don't
    //     want to clear it).
    //   * Recover gracefully from a SEH fault on the byte access by
    //     resetting the flag and falling back to the per-function bare-
    //     RET sites (sites 1..16).
    std::atomic<bool>        m_vm_freeze_byte_we_set{false};

    // BattlePauseRequest REMOVED 2026-04-27 ----------------------------------
    // Discovery: ULuxBattleFunctionLibrary::SetBattlePause is NOT the engine's
    // pause path.  The C++ impl at LuxBattleManager_SetPauseState_OrBattle-
    // Active @ 0x1403F9180 calls LuxBattleChara_SetBitFlag0x394_NotifyMove-
    // Ended which the Ghidra plate explicitly documents as AUDIO STATE
    // ("bit 2 = audio-force-mute"), NOT world-tick pause.  The plate also
    // says: "The REAL world-tick pause is g_LuxBattle_VMFreezeByte @
    // 0x1448462D0 ... HorseMod should be writing to g_LuxBattle_VMFreezeByte
    // directly for the actual freeze."  HorseMod already does that via
    // m_vm_freeze_byte_we_set above.
    //
    // The Soul-Charge break: SC has audio-cue-driven phase transitions
    // (activation glow ? AOE pulse ? recovery).  Muting audio mid-SC by
    // setting bit 2 of chara+0x394 stalls the state machine; the AOE phase
    // never fires, hitboxes never activate, the move "doesn't hit" anymore.
    //
    // Trade-off: without BattlePauseRequest the round timer ticks during
    // long replay freezes, eventually ending the round.  Acceptable for
    // now - the alternative was breaking gameplay-critical mechanics.  If
    // a clean round-timer halt is needed, the next investigation should
    // target the BattleTimeManager's actual tick path (BM+0x4F8) without
    // touching audio state.

    // ---- Suppress VFX -------------------------------------------------------
    // Bytepatch port of somberness's CE "VFX off" cheat - see
    // horselib/VFXOff.hpp for the full disassembly walk.  Replaces the
    // earlier per-frame DestroyAllVFx polling: that approach let each
    // VFX spawn for one frame before tearing it down (1-tick flashes
    // on every hit) and burned a UFunction call per tick.  The
    // bytepatch installs a midfunction trampoline that overrides the
    // engine's per-slot VFX-state writer to plant a sentinel constant
    // the renderer treats as culled - effects never become visible.
    //
    // Same toggle, same ImGui label.  No hot-path work; flip is a
    // single 5-byte JMP install.
    Horse::VFXOff     m_vfx_off{};
    std::atomic<bool> m_suppress_vfx{false};

    // Draws the deterministic J_StgHitChkData terrain/edge/wall triangles and
    // current breakable-stage presentation bounds. Controlled only by the
    // General tab checkbox; deliberately independent of the F5 overlay.
    std::atomic<bool> m_show_stage_boundary{false};
    Horse::StageBoundaryOverlay m_stage_boundary{};

    // Visual-only stage mesh hiding for inspecting hitboxes and the
    // stage boundary wireframe.  Independent of F5 and gameplay
    // collision; StageVisualSuppressor caches actor/component pointers
    // and reapplies at a low cadence instead of scanning every tick.
    Horse::StageVisualSuppressor m_stage_visuals{};
    std::atomic<bool> m_hide_stage_visuals{false};

    // ---- Freeze frame (WorldTickGate-driven) --------------------------------
    // Replaces the broken Horse::GamePause helper (which patched a chara
    // audio-flag bit at +0x394, not a world-pause).  The actual world-
    // tick pause in SC6 is the master VM-freeze byte at 0x1448462D0:
    // when non-zero, LuxMoveVM_GetTimeDilationScalar returns 0.0 and
    // every per-frame integrator (animation, opcode-stream, hit timing)
    // sees dt=0 and halts.  See the plate on g_LuxBattle_VMFreezeByte.
    //
    // Current implementation uses WorldTickGate plus replay/actor/time
    // sibling gates. Legacy SpeedControl remains disabled because several
    // replay AOBs are stale and their duties moved to the dedicated gates.
    //
    // Interaction with the Slow-motion checkbox:
    //   * Freeze ON     -> speedval = 0.0  (highest priority)
    //   * Slow-mo ON    -> speedval = m_speed_value (slider)
    //   * Both OFF      -> SpeedControl disabled (no overhead)
    std::atomic<bool> m_freeze_frame{false};

    // Frame-step state machine, driven from on_cockpit_update_pre.
    // Same shape as the old GamePause::on_tick() machine but the
    // "clear bit" / "set bit" actions are now "set speedval = 1.0" /
    // "set speedval = base".  Two cockpit ticks per advanced game
    // frame - first tick lifts the freeze, second tick re-applies it.
    std::atomic<int>  m_step_pending{0};
    std::atomic<bool> m_step_expecting{false};

    // Defensive frame-step resync: cockpit::Update can fire WITHOUT
    // the world ticking (UMG widget tick is independent of world tick
    // - see comment at the frame_step_apply callsite).  If Step Tick A
    // publishes speedval=1.0 but the world doesn't actually tick before
    // the next cockpit pre-hook (loading reentrance, paused redraw, a
    // doubled cockpit::Update call), pivoting to Step Tick B would
    // silently consume the user's F6 press.
    //
    // Witness: per-lane tick counter at lane+0x04 (int32) - the engine
    // increments this every world tick that processes the chara.  We
    // snapshot it on Step Tick A; on Step Tick B we re-read and only
    // pivot if at least one lane counter advanced.  If none advanced,
    // hold expecting=true and try again next cockpit tick.
    //
    // Cap holds at kStepDwellMax cockpit ticks to recover from a
    // stale or unmappable witness (e.g., chara struct destroyed during
    // a mode transition with pending > 0 - should be cleared by
    // clear_time_features_on_transition but we belt-and-suspender it).
    struct StepWorldTickWitness
    {
        bool    valid = false;
        int32_t p0_lane0_tickctr = 0;
        int32_t p0_lane1_tickctr = 0;
        int32_t p1_lane0_tickctr = 0;
        int32_t p1_lane1_tickctr = 0;
    };
    StepWorldTickWitness m_step_witness {};
    uint32_t             m_step_dwell   = 0;
    static constexpr uint32_t kStepDwellMax = 10;

    // Presence-transition tracker.  Stores the GamePresence we last
    // observed in on_cockpit_update_pre.  Whenever the live presence
    // differs from this value (i.e. SC6 transitioned modes - e.g.
    // training -> ranked -> training), we forcibly clear Freeze and
    // Slow-motion regardless of the "Auto disable online" gate.
    //
    // Why force the clear on EVERY transition (not just into PvP):
    //   1. SC6 destroys the old BattleManager + chara actors and
    //      builds new ones during a mode switch.  If freeze stays
    //      active across the transition, Site 9 (PerFrameTick entry-
    //      RET) blocks the new BattleManager's per-frame tick the
    //      moment its first chara fires the chain - including
    //      UpdateBattleCameraSynthesis, which is what the renderer
    //      reads to set the view matrix.  Result: black screen on
    //      training reload from a previous match.
    //   2. Slow-motion has the same hazard via Sites 1/3/4/5/6
    //      (dt-scale at math sites) - fractional dt during state-
    //      machine init can produce uninitialised camera / VFX
    //      state on the new mode's first frames.
    //   3. Once cleared, freeze/slow-mo STAY cleared (the user must
    //      manually re-engage them) - matching the user's mental
    //      model of "these are temporary debug tools, not persistent
    //      settings".
    //
    // Initialised to Unknown (0xFF) so the first observed presence
    // counts as a transition (Unknown -> something) and triggers a
    // safety clear at session start, in case the previous shutdown
    // somehow left freeze persisted in settings.cfg.
    std::atomic<uint8_t> m_last_seen_presence{
        static_cast<uint8_t>(Horse::GamePresence::Unknown)};


    // Frame-stepped slow-motion accumulator.
    //
    // Old behaviour (dt-scale slow-mo): writes a fractional speedval
    // like 0.5 into the codecave; the dt-multiply patches at sites
    // 1/3/4/5/6 scale dt accordingly.  Visually smooth but breaks
    // multi-hit moves: SC6's MoveVM stores hit cells per integer
    // frame, and a fractional dt accumulator drifts past hit
    // boundaries unpredictably (one tick advances by 0.5, next by
    // 1.0 once accum crosses, but the per-frame-cell hit detector
    // expects to see EACH integer frame exactly once - at fractional
    // dt it sees the same frame twice or skips entirely).
    //
    // New behaviour (frame-stepped slow-mo): each cockpit tick is a
    // hard 1.0 (full game frame) or 0.0 (freeze).  The accumulator
    // adds the slider value S each tick; when it crosses 1.0, that
    // tick is a "go" tick (target = 1.0), accumulator -= 1.0.
    // Otherwise it's a "stop" tick (target = 0.0).  Effective
    // average speed = S, but every game frame the engine sees is a
    // clean native-dt frame - hit cells advance one integer frame
    // at a time, multi-hit moves resolve correctly.
    //
    // Trade-off: slightly choppier visuals at low speeds (1 frame
    // every 4 ticks at S=0.25 = 15 fps effective).  But for analysis
    // and replay-watching, frame accuracy matters more than smooth
    // motion.  The choppiness is identical to repeatedly mashing
    // the Step-1 button at the right cadence - which is exactly
    // what users were asking for when they said "frame stepping
    // works but slow-mo doesn't".
    //
    // Range: only affects S in (0, 1].  S >= 1 produces target=1
    // every tick (full speed, no point slowing past native).  S <= 0
    // collapses to freeze (target=0 every tick), same as the
    // dedicated freeze toggle.
    //
    // Reset on slow-mo OFF -> ON edges so the cadence starts clean
    // (otherwise an in-flight accumulator could produce a one-tick
    // glitch at the resume).
    float m_slow_mo_accumulator {0.0f};

    // Most recent cockpit-tick decision from frame_step_apply().
    // Read by render_time_tab() to show a live cadence indicator
    // that flickers between "GO" (green) and "STOP" (red) so the
    // user can see the frame-step cadence at a glance - useful for
    // confirming the slider is actually doing what they expect at
    // very low speeds (e.g., 0.001x = one go-tick every ~1000
    // cockpit ticks - 17 seconds; without a live indicator the user
    // would have no visual confirmation the system is alive).
    //
    // Atomic because it's written from the cockpit hook thread and
    // read from the render thread.  uint8_t enum values:
    //   0 = inactive    (slow-mo off / native speed)
    //   1 = stop tick   (target == 0.0)
    //   2 = go tick     (target == 1.0)
    enum class TickKind : uint8_t { Inactive = 0, Stop = 1, Go = 2 };
    std::atomic<uint8_t> m_last_tick_kind{
        static_cast<uint8_t>(TickKind::Inactive)};

    // Red "just got hit" sticky flash duration, in GAME FRAMES.
    //
    // The underlying PerHurtboxReactionState signal is a ~1-frame pulse
    // (~16ms at 60fps) - too short to see.  We extend it by holding the
    // hot state for `m_flash_frames` game frames before fading.
    //
    // KHitWalker drains the sticky by tracking g_LuxBattle_FrameCounter
    // (imageBase+0x470D0C4), which is incremented exactly once at the
    // end of LuxBattle_PerFrameTick.  Since Horse::WorldTickGate gates
    // PerFrameTick at its entry, the counter halts under freeze and
    // advances once per gate-released game frame under step / slow-mo
    // - the flash is held during freeze, drains one unit per F6 step,
    // and drains in lockstep with the slowed game clock during slow-mo.
    //
    // Default 15 frames - 250ms of native-speed gameplay.
    std::atomic<int> m_flash_frames{15};

    std::atomic<float> m_thickness{1.5f};

    // Per-feature line-batcher slot.  Hitboxes (Attack list) draw via
    // m_backend_hit; hurtboxes draw via m_backend_hurt.  When a feature
    // is set to Persistent, only engine-live hit/hurt boxes are routed
    // there; inactive boxes in the broad inspection view fall back to
    // fixed Foreground backends so they show once instead of smearing.
    // Body boxes are not hit-resolution volumes, so they never trail.
    std::atomic<Horse::LineBatcherSlot> m_slot_hit {Horse::LineBatcherSlot::Foreground};
    std::atomic<Horse::LineBatcherSlot> m_slot_hurt{Horse::LineBatcherSlot::Foreground};

    // Trail length in game frames for whichever backend is in the
    // Persistent slot.  Pushed to the backend each cockpit tick as
    // m_trail_frames / 60.0 seconds.  HorseMod advances that lifetime
    // from g_LuxBattle_FrameCounter, not wall-clock time, so freeze and
    // F6 step hold/drain the trail in game-frame units.  Range matches the slider 1..300
    // = 1 frame to 5 seconds of trail at 60 Hz.  Default 30 - 0.5 s,
    // long enough to be visibly useful for tracing a move without
    // drowning the screen in line history.  Used for both backends
    // when their slot == Persistent - Normal-slot backends ignore
    // this and stick to LineBatcherBackend::kDefaultLifetime.
    std::atomic<int> m_trail_frames{30};
    static constexpr int kKHitPersistentTrailLineBudget = 12000;
    static constexpr int kKHitPersistentTrailLineHeadroom = 4096;

    // Persistent trail cadence follows the game frame counter, not the
    // cockpit/render tick.  When HorseMod freeze holds PerFrameTick, this
    // counter stops, so persistent lines are neither duplicated nor aged.
    bool     m_have_trail_game_frame{false};
    uint32_t m_last_trail_game_frame{0};
    bool     m_have_trail_filter_state{false};
    bool     m_last_trail_only_active{true};

    // Diagnostic-only.  Logs attack spheres once per game frame so
    // externally-edited spheres can be compared against the rendered
    // centre/radius without permanently noisy UE4SS logs.
    //
    // Scuffle clue captured from hdr030_TEST.khd, move 328: the edited
    // test hitbox is on attack entry 1's General_1 / General_2 masks,
    // i.e. native category slots 56 and 57, not HitMisc::Big_Sphere.
    std::atomic<bool> m_khit_sphere_audit{false};
    std::atomic<bool> m_khit_sphere_audit_filter_move{false};
    std::atomic<int>  m_khit_sphere_audit_move{328};
    std::atomic<bool> m_khit_sphere_audit_filter_slots{false};
    std::atomic<int>  m_khit_sphere_audit_slot_a{56};
    std::atomic<int>  m_khit_sphere_audit_slot_b{57};
    bool     m_have_sphere_audit_frame{false};
    uint32_t m_last_sphere_audit_frame{0};
    int      m_khit_audit_attack_logs_this_frame{0};
    int      m_khit_audit_hurt_logs_this_frame{0};
    int      m_khit_audit_pair_logs_this_frame{0};
    int      m_khit_audit_calib_logs_this_frame{0};
    int      m_khit_audit_cluster_logs_this_frame{0};
    static constexpr int kMaxKHitAuditAttackLogsPerFrame = 96;
    static constexpr int kMaxKHitAuditHurtLogsPerFrame = 96;
    static constexpr int kMaxKHitAuditPairLogsPerFrame = 128;
    static constexpr int kMaxKHitAuditCalibLogsPerFrame = 64;
    static constexpr int kMaxKHitAuditClusterLogsPerFrame = 16;

    struct KHitRenderCalibrationPoint
    {
        bool native_ok = false;
        bool actor_ok = false;
        bool delta_ok = false;
        Horse::FVec3 native_root{};
        Horse::FVec3 converted_root{};
        Horse::FVec3 actor_root{};
        Horse::FVec3 delta{};
    };

    struct KHitRenderCalibrationFrame
    {
        KHitRenderCalibrationPoint point[2]{};
        bool has_common_delta = false;
        bool consistent = false;
        bool applied = false;
        float delta_distance = 0.0f;
        Horse::FVec3 common_delta{};
        Horse::FVec3 active_offset{};
        const wchar_t* status = L"missing";
        int samples = 0;
    };

    struct KHitRenderCalibrationState
    {
        bool valid = false;
        int samples = 0;
        Horse::FVec3 offset{};
    };

    KHitRenderCalibrationState m_khit_render_calibration{};
    Horse::Fn m_fn_khit_actor_location[2];

    // ---- Retrack-event overlay ----------------------------------------
    // When ON, watches each chara's facing yaw every cockpit tick and
    // prints a transient "Player N retrack event" line on screen
    // whenever the engine rotated that chara during a move (i.e. the
    // chara's facing changed appreciably while a move was active).
    //
    // -------------------------------------------------------------------
    // History - what was tried first and why it was wrong
    // -------------------------------------------------------------------
    // Initial implementation watched chara+0x16E6 / chara+0x16E1 for
    // an "active retrack" gate equivalent to:
    //   active = (chara[+0x16E6] != 0) && (chara[+0x16E1] != 0)
    // based on a misreading of LuxBattleChara_RetrackFacingTowardOpponent
    // @ 0x140369450, which uses those two bytes as its early-return
    // gate.  That gate IS real, but the SEMANTICS of the two flags is:
    //
    //   chara+0x16E6 = motion-input flag #0x16  (set during most moves;
    //                  caller writes are widespread, not specifically
    //                  "move-locks-facing")
    //   chara+0x16E1 = motion-input flag #0x11  (part of the
    //                  fall-reaction cluster {0x0c..0x11, 0x29, 0x35} -
    //                  toggled by LuxBattle_ComputeHitReactionParams
    //                  @ 0x140343b90 case 0xd, which is a SPECIFIC
    //                  knockback / recovery type)
    //
    // The retrack gate's actual meaning is therefore:
    //
    //   gate-blocks = (in-some-non-walk-state) && (NOT-in-fall-reaction)
    //
    // i.e. retracking RUNS during idle/walk, AND during fall-reactions
    // mid-move; it's BLOCKED during normal mid-move animation.  There
    // is NO "homing override" flag - moves that track the opponent
    // (homing throws, certain supers) implement that through some
    // other mechanism (likely the SLERP-weight system at
    // chara+0x971ac..+0x971b8 set up at move-start, or by the move
    // script writing chara+0x94 directly).
    //
    // So watching the gate flag-pair fired the overlay during knockback
    // and fall recoveries - which the user reported as "triggers in
    // unexpected places".  Confirmed: my interpretation was wrong.
    //
    // -------------------------------------------------------------------
    // Current implementation - direct yaw-delta detection
    // -------------------------------------------------------------------
    // Read chara+0x94 (facing yaw, written by ApplyFacingRotationDelta)
    // every cockpit tick, compute the per-tick delta against last
    // tick's snapshot, and fire when:
    //
    //   |yaw_delta| > kRetrackYawThresholdNorm   (= ~0.7- per tick)
    //   AND chara is in some move state          (chara+0x16E6 != 0)
    //
    // This catches "the engine rotated my chara appreciably during a
    // move" regardless of which internal mechanism produced the
    // rotation - homing-throw retrack, hit-reaction realignment, or
    // a move script's direct yaw write.  False positives are limited
    // by the threshold; brief sub-degree adjustments don't fire.
    //
    // The yaw value at chara+0x94 is normalised in [0, 1) where 1.0
    // == 360-.  See the plate on RetrackFacingTowardOpponent for the
    // unit convention; the integrator at +0x94 uses the same scale.
    //
    // Off by default - diagnostic feature, not gameplay-affecting.
    std::atomic<bool> m_show_retrack_events{false};

    // Yaw threshold in normalised units (1.0 == 360-).  ~0.002 == 0.72-.
    // Below this we treat the rotation as "noise" / fine-tune adjustment
    // and don't fire; above it we treat it as a real retrack event.
    // Tuned empirically - natural facing-maintenance during idle/walk
    // produces sub-millidegree fluctuations; homing moves and hit
    // reactions produce multi-degree-per-tick rotations that easily
    // clear this bar.
    static constexpr float kRetrackYawThresholdNorm = 0.002f;

    // Per-player state for edge detection: previous tick's yaw, and
    // whether we were in a "retracking" state last tick (so we fire
    // ONE event per movement burst, not one per tick of it).  Indexed
    // by PlayerIndex (0 = P1, 1 = P2).
    float m_prev_yaw[2]        = {0.0f, 0.0f};
    bool  m_have_prev_yaw[2]   = {false, false};   // have we sampled yet?
    bool  m_was_retracking[2]  = {false, false};

    // Small ring buffer of recent on-screen text events.  Each entry
    // carries a fixed-size text payload and the ImGui::GetTime()
    // timestamp it fired at; the renderer iterates the buffer every
    // frame and draws every entry whose age is < kHudTextEventLifetime.
    //
    // Used by:
    //   - Retrack-event detector - formats "Player N retrack event" and
    //     pushes a string when an in-move yaw burst exceeds threshold.
    //   - Test button (General tab) - pushes "Hello World" to verify
    //     the overlay path is alive without needing a fight.
    //   - Any future C++ feature that wants to surface a transient
    //     diagnostic line on top of the game viewport.
    //
    // 8 slots - 1.5s lifetime is enough to show ~5 events per second
    // (an upper bound for human-perceivable distinct events) without
    // truncation.  Older entries get overwritten FIFO-style; the
    // renderer skips entries older than the lifetime cap, so
    // wraparound is invisible.
    //
    // text_len < 0 marks an empty slot (initial state and post-clear).
    // The fixed 56-byte text buffer avoids any heap allocation in the
    // push hot path, which keeps the per-tick retrack-detection code
    // allocation-free.  56 chars covers messages like
    // "Player 2 retrack event" (22 chars) and "Hello World" (11 chars)
    // with plenty of headroom for future formatting.
    //
    // No atomic / mutex because the writer (m_lux.forEachChara on the
    // game thread, plus render_tab_impl from the test button on the
    // same game thread) and the reader (render_tab_impl) are all the
    // SAME thread per Horse::GameImGui's threading docs.
    struct HudTextEvent
    {
        char   text[56] = {};       // null-terminated; empty when len < 0
        int    text_len = -1;       // -1 = empty slot, else strlen(text)
        double time     = 0.0;      // ImGui::GetTime() at push moment
    };
    static constexpr size_t kHudTextEventCount    = 8;
    static constexpr double kHudTextEventLifetime = 1.5;   // seconds
    HudTextEvent m_hud_text_events[kHudTextEventCount]{};
    size_t       m_hud_text_event_head = 0;

    // Push a transient text line onto the overlay queue.  Truncates
    // strings longer than the slot capacity, which is fine - these
    // are user-facing diagnostic banners, not log lines.  Safe to
    // call from any game-thread code (cockpit hook, button handler,
    // detector).
