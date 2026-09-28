> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Retained replay end: native finish side effects

Read-only native inspection,2026-09-12. No runtime hook, state participant or behavior change was added for this investigation. Exact historical renderer history is outside the acceptance scope.

## Observed code

Existing BattleScene.script.json and ReplayBattleScene.script.json exports show ReplayBattleScene.OnFinishMatch invoking the parent finish callback and then opening the result menu. The parent checks IsStylePlayCount for each player before AddStylePlayCount. IsStylePlayCount calls LuxBattleFunctionLibrary.IsLocalUserControl. These cached exports must still be tied to the admitted cooked assets before treating them as a general supported-format guarantee.

Ghidra SoulcaliburVI.exe native registration data at14337CD10 pairs the ASCII IsLocalUserControl string143377A28 with thunk140944F80. The thunk parses WorldContextObject and PlayerIndex and calls1403F22F0. The existing function name LuxBattleChara_IsRoundReadyForPlayer_CheckFlags is not sufficient evidence of its semantics.

The actual1403F22F0 listing resolves the world context. A qualifying input-sync owner with its player bit set returns false. Otherwise1403F00B0 resolves the move provider; a player parameter type other than1 returns false, and a nonzero provider byte+220 also returns false. Missing context/provider can return true. The decompiler currently labels+220 fBattleReplayEnabled; that label alone is not proof of the installed replay value.

## Remaining bounded questions

- Establish that the admitted replay world/provider is the owner used by this native call and that its+220 replay flag remains set through the finish callback. A retained live binding/value witness plus the setter/consumer trace closes this; do not replace the function result or import expected observations.
- Verify the cached finish graph against the supported installed asset before relying on absence of other persistent side effects. If the replay branch makes IsLocalUserControl false, the observed style-count branch is skipped natively; no suppression hook is justified.
- Indexing tail traversals is not proof that seeking into them safely repeats finish/UI work. A bounded last/near-last seek must verify callback counts, native session bindings, coherent HUD/actors, final hold and complete B recovery. Repeated endings and changed-history endings remain unqualified.

The current full-index/backward208 check does not replay a second finish callback. Its independent native control covers the initial tail. Keep these proof scopes separate.


## Causal visual failure, 2026-09-13

`indexed-controls-obstructed-panel-failure-stage.json` retains candidate0db808d6e2dd484385f257d467e5a247, runtime58a789f8/observere6f66594. The actual game-window screenshot at resumed tick212 shows the result menu over combat. This is materially stale native UI, not optional shading variation. Native source completion was11501; OnFinishMatch arrived about78ms later, and the host held11506. Independent gameplay matches from earlier binaries do not validate this visual state.

The immediate proposed replacement is a pre-armed retained gameplay endpoint, not result-menu restoration. This is NOT implemented or live-proven yet:

* Reuse the existing bounded display arm during the final replay round. It already retains the native Present image without diagnostic readback. No historical renderer participant is required. Its current surface/output allocations remain charged to the512MiB total.
* At a completed application, require the actual inactive final-round source and existing native mode10/round-state10 witness. Select the actual completed tick, preserve all completed traversals in the map, and hold before another application. If the native finish callback already ran, reject this session's seek admission rather than pretending the result-menu lifecycle was restored.
* Index metadata must distinguish the retained gameplay range from the deferred native UI/callback tail. Do not set native_finish synthetically or keep the old all-tail-complete claim. The independent native control still executes the full callback tail; compare the candidate's actual retained prefix and resumed suffix without copying observations into the game.
* A pre-armed surface is owned work: cancellation must convert at an already completed application to the existing retained hold, retire checkpoint/GPU owners, release the surface, and acknowledge completion before playback. Manual Pause during a pre-armed completed-index replay must reuse that arm instead of rejecting an otherwise valid request. Pending arm completion, scene exit and release failure must retain their existing owners.

Remaining facts to resolve in the assembled attempt: whether OnFinishMatch can execute in the same application as the final simulation traversal (safe rejection is required if so); whether the final Present is available/ordered at the selected boundary; and whether B undo plus120 independent continuation ticks and held/resumed HUD images remain coherent. Test cancellation while pre-armed locally across actual host methods before live use. Keep the strict-seek entry point and non-managed diagnostic protocol until replacements are qualified. No new live attempt is justified solely to observe another stale result menu.


## Source-stop policy supersedes the proposed mode-10 prearm

The flat-inventory retained raw log observes source mode10 at tick11501, then native OnFinishMatch in the same application before the completed-application hold could arm. The timer runs after gameplay tasks. Prearming mode10 therefore does not guarantee an intact replay UI lifecycle. The latest independently matching indexed-controls run visually fails with the result menu over combat.

Use the earlier observed final native source stop (11147, mode5, round-state5, inactive source, cursor3310) for the managed retained range. Do not infer exhaustion from raw record sizes (the final record stores3318 samples) or suppress native Finish callbacks. Verify manager1403F2840 returns false both before arming and after the next completed application, with source/session membership unchanged. This predicate includes manager12F4/12F1 overrides and rule consumers, not only the global mode fallback. Unexpected finish, source reactivation or transition rejects admission. Legacy explicit indexing still exercises the native-finish path.

Current implementation records all actual ticks through this supported held boundary and explicitly marks the subsequent native victory/finish tail unsupported. It does not set native_finish or final_tail, create synthetic observations, restore result widgets or classify victory state as cosmetic. Independent controls still run to the true native endpoint. Compare every retained tick and the restored suffix; report excluded tail separately. Implementation and185 Python tests are not live proof. The next bounded indexed208+240 controls run must demonstrate no result menu, complete B ownership and independent gameplay/HUD continuation.


## Missing target proxy: bounded publication plan

Runtime4ca6b796/observerda761a19 candidate40b372db158148c6b3c2d16419dd15cb held11148 after source-stop11147, then rejected lighting preparation before A publication with complete B retained. Cleanup completed/zero games; no control launched. The retained-source-stop-missing-proxy-failure report links the raw log and exact reconstructible sources. First absent target is primitive441, STG009_FallenTreeA.BaseMesh, static-mesh proxy36BD1A8; its component weak lease still resolves and its live proxy is null.

Classification: the missing scene-info/proxy/uniform destination is reconstructible presentation ownership, not a simulation snapshot or required historical shading. The stage actor's source visibility/fade and physics remain authoritative participants. Existing PrepareStageRenderOwners already proves matching A/B stage membership and retains B. StageRenderBinding currently admits dormant stage destinations only after execution starts; the initial target path therefore rejects before existing PublishStageVisibility can enqueue native reconstruction.

The bounded extension is a deferred target destination for an already owned static stage mesh: retain the original owner plus A's six visibility words, validate the actual weak/member/parent/asset/render-data/primitive-ID bindings, require hidden B and visible A, and permit only either unchanged hidden B or A's verified queued recreate state. No historical proxy is revived and no uniform is written through a missing slot. Existing world render-work validation owns the actual queue; native end-frame work creates the proxy after execution handoff. Particle target recreation remains outside this extension.

Focused proof: actual-memory admission tests cover before/after visibility publication, complete B visibility recovery, changed generation/member/asset/ID/flags, missing queue flags and unsupported particle type. The assembled retry must show native target reconstruction and independently matching continuation/coherent held images. Then publication and post-execution cancellation must recover B through existing native stage retirement and GPU completion. No completion claim precedes those results.

Impact: no new checkpoint images or GPU readbacks; operation-local plan adds six uint32 visibility words per retained stage actor (at most3072 additional bytes for128 actors, plus vector alignment if any). No additional fence or capture pass is planned. Existing native visibility/recreate work remains charged to seek completion; measure its actual latency and retirement. Current reconstruction guards and private B allocation/reference ownership remain mandatory.


Runtime27cb23a4/observer0c65bff4 candidate d8286cc94cc6445980abd58f46671b96 passes deferred tree-proxy lighting preparation, then rejects the same primitive441 main occlusion query because PublishVisibility handles DormantCreation but not PendingStageTarget. Still before A publication; complete B retained, cleanup complete/zero games. stage-target-visibility-admission-failure-stage.json links raw evidence. The correction routes only that explicit binding through the same TargetDestination ownership admission; query subindex, RHI type/reference and pool guards remain unchanged. No cache pruning, extra state, new native work or weaker unknown-owner admission.
