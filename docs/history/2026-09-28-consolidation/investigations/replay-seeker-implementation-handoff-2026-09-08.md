> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Replay seeker implementation handoff — static fork review, 2026-09-08

## Scope and evidence rules

Testing is paused by the user. This review reads current source, retained reports/logs and the existing `SoulcaliburVI.exe` Ghidra program. It launches no game, build, test, deployment or new capture. It changes no runtime code. Recommendations below are implementation proposals, not live proof. The checkout is heavily dirty and includes untracked implementation files; preserve it. Git HEAD alone cannot identify this implementation.

Pixel-identical shading/history is optional. Exact future-affecting state, complete B undo, safe native resource lifetime and coherent actors/effects/HUD remain required. Retained expected observations must never be installed. Saved A display is not native rendering proof. This review does not reopen the stopped per-draw investigation.

Read alongside `replay-checkpoint-static-audit-2026-09-06.md`, `replay-checkpoint-static-followup-2026-09-06.md`, and `replay-world-ownership.md`. Their native facts remain useful; their older implementation status and pixel completion gates may be superseded.

Further native/cooked-asset investigation during the extended testing pause is in [replay-seeker-static-deepening-2026-09-08.md](replay-seeker-static-deepening-2026-09-08.md). Start with its six implementation chunks. It now covers partial-write/undo defects, immutable checkpoint ownership, no-birth integration, exact input/publication semantics, trace lifetime, HUD/latent/async callbacks, stage root motion and events, physics, RNG/audio feedback, held controls, GPU simulation/vector fields and the memory/latency tradeoff. Native facts, current source behavior and missing live evidence are distinguished. Read its newer evidence before acting on an unresolved item below.

The final static [source fingerprint inventory](../../../investigations/evidence/replay-fork-static-source-identities-2026-09-08.json) covers the deterministic source tree, runner, observer and overlay headers. These are source identities, not refreshed binary qualification. No runtime file, build or live test was changed/run by this fork. Ghidra annotations and bounded cooked-resource evidence are retained for implementation. The review explicitly retracts its earlier mistaken32-versus16 physics-array claim; the real remaining issue is public count validation and bounded failure diagnostics, not mismatched array capacities.

## What is actually demonstrated

Extended static review closed at approximately20:47UTC. The newest causal result is the verified renderer1412C694E→FX141F7CBC0→mode2 GPU particle collision argument/binding chain. Scene depth/GBuffer inputs can therefore belong to particle simulation rather than optional final shading; actual admitted mode, compiled bindings and resource generations remain to be checked. See the deepening report's GPU section. Testing remains paused until separately resumed; passage of the four-hour research window is not test authorization.

The current bounded implementation is substantially beyond a storage experiment. Runtime `d95e0113d69dfc99d949cdbee52364b962e66002e43016bb04db94d684003485` has an assembled completed-application A205→B210→A205 transaction. Retained unchanged-input, corrected-history and both cancellation reports support their own bounded comparisons. They do not establish arbitrary historical interior restoration, general lifecycle recovery or a finished seeker.

Latest `combat-restore-stage.json` binds runtime above, observer `210d6fbe554cc83e7410f6a842fd9d461757d332da487032f605e32cf0d550a1`, framework `f5fe7c823d208a4b407106d3e0f47904464d3b0703a654901d1ff9efa02c8023`, game `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`, replay `95e12e394d35c13d5e0dd3dce692f9e0a4022e2a84205a9ec75f2fa6726d7879`. Corrected input samples161–190 change publications inside206–210, and120 resumed ticks/480 callbacks plus native Lux poses match the altered native control. `visual_coherence` remains incomplete. Do not substitute earlier observer identities for this one.

The latest corrected run reports59.309 TPS and four observation gaps over20ms, maximum29,323us. That is neither constant60 pacing nor rollback-cost proof. Earlier readback-free measurements are useful measured costs, not a guarantee: latest corrected capture74,575us, restore85,902us, accounted233,662,524 bytes; paced restore-plus-five194,049us excludes deliberate held dwell and final application/render retirement. Zero Maps does not mean zero GPU work or stalls.

## Immediate code defects and hardening work

### 1. Validate public operation state before touching participant storage

`Sc6ReplayHost.Restore.inl:RestoreOperation(Commit)` dereferences `transaction.target` and `*transaction.physics_observation`, calls native physics observation, and only afterward tests `phase == Held`. `PrepareHistoricalRestore` creates `historical_restore_` and assigns target before `CaptureCheckpointUnchecked(undo)`; if that capture fails, it leaves a Failed operation with no physics observation allocation. A caller issuing Commit after failed Begin can therefore reach a null dereference. This is a concrete static invalid-action path, not a reproduced crash.

Fix: phase/target/storage/owner-thread checks must precede all dereferences and native reads. Define accepted actions for every phase. Begin failure must return a fully initialized witness, plus either an explicitly releasable preparation-failure object or no operation. An invalid action must leave state unchanged. Unknown enum values currently fall through with success unless another branch rejects; reject them explicitly.

`PublishHistoricalRestore` does check Prepared before reads. Preserve that ordering. Cancel after Failed is accepted but `AdvanceHistoricalRestore` returns immediately on Failed; this is not recovery. Make that outcome explicit instead of acknowledging a request that cannot advance.

### 2. Exception safety is not covered by the outer catch

`RestoreOperation` catches C++ exceptions and sets only its local success=false. It does not necessarily set witness failure, select recovery or clean a partly prepared transaction. Several inner functions are `noexcept`; an escaping exception there terminates before the outer catch can run. Raw native accesses are not made safe by a C++ catch under the normal MSVC exception model. Trace Install has leaf SEH for writes/comparisons, but Capture and many pointer/native calls do not.

Fix: all fallible allocation/preparation before publication, explicit failure statuses at participant boundaries, and a transaction scope that records phase and attempted writes before invoking a mutator. Do not globally catch access violations and resume. An unexpected native fault requires preserved owners and controlled session termination if B cannot be independently validated. Distinguish allocation failure, invalid action, invalid binding, submission failure, completion timeout and undo failure.

### 3. Commit is a real irreversible boundary

`AdvanceHistoricalRestore(Committing)` completes GPU commit, reconstructs physics queries, then sequentially commits CPU emitter, GPU emitter, pools, world render work, world state, scheduler and manager storage, and retires the B-only particle. A failure partway sets Failed/InteriorFailed; earlier retirements cannot be undone by pretending B still exists. Current happy-path cancellation evidence deliberately ends before this boundary.

Keep this design distinction. Preflight every retirement while complete B exists; move fallible allocation out of commit; make each retirement idempotent with a recorded cursor. If a post-decision failure is irrecoverable, terminate only the replay session after native work is safe to release. Do not advertise cancellation after the commit decision. A UI cancellation accepted before that decision must recover B; one after it must report that commit won and arrange a later safe stop.

`Release` checks checkpoint_restoring, witness.pending, game/physics dirty and published participants, but not a terminal phase or every event/dirty flag. Trace/HUD/rendering flags are left set on successful undo/commit. That is presently an ambiguous bookkeeping invariant, not proof that a normal successful Release corrupts state. Replace implicit assumptions with one releasability predicate covering terminal phase, no queued command/event, no in-flight GPU references, all participants settled and no borrowed checkpoint access remaining.

### 4. Borrowed checkpoint lifetime is an API constraint

HistoricalRestore stores `const Checkpoint* target`, including pointers used by asynchronous render progress and commit. General sparse retention must pin that checkpoint until the transaction is terminal and released. Timeline eviction cannot free it because another checkpoint has become preferable. Use an owned immutable checkpoint handle with session/generation, and charge pins to the shared memory budget. Do not clone the entire A image just to repair an API lifetime problem.

## Exact execution and arbitrary interior seeking

`Sc6ReplayExecutor` has useful explicit phases: OuterEntry, WorkerEntry, MoveEntry/MoveTick/MoveTail, WorkerCounters, PublishInput, SimulationTick, RepeatDecision, InputTail, RoundDecision and OuterTail. Its continuation separates tick, interval, publication count, remaining inputs, cache offset, delta and entry timer decisions. Preserve this work.

The host API is still much narrower. `Sc6ReplayHost.Checkpoint.inl:AdvanceToTick` admits only Holding/EngineWorld, target=current+1, RepeatDecision and manager+1462 requesting a repeat. It preserves the same manager task. Ordinary Restore rejects CompletedApplication and requires the same physical task/event/epoch/continuation shape. HistoricalRestore admits only completed application/idle execution. Combining these two experiments does not implement historical interior restore.

`Sc6ReplayHost.Interior.inl` externally holds unfinished work; Resume then drains `while (!engine_idle()) AdvanceEngine()` before completing deferred engine post. `ArmPause` generally requires idle ownership and cannot be naively reused to request a second interior pause in that drain. A seek loop built out of Resume/ArmPause would overrun some targets.

Smallest coherent extension: retain completed-application checkpoints initially, restore a preceding admissible checkpoint, then drive the existing engine/world/executor state machine until the exact target traversal, leaving its remaining task/interval/world/application phases pending. Add a target request into that owner, not a second coordinator or nested native stack. This can address interior targets without serializing a historical native task object. It still requires proof that A's completed checkpoint is valid and that the subsequent external hold freezes all consumers. A later rollback checkpoint at an interior boundary needs explicit reconstruction of task/arena ownership; it is a separate optimization.

Required transition accounting: input publication once per sample, repeat ticks without republishing, every MoveTail/InputTail/OuterTail exactly once, native callbacks and round updates included in the completed tick, pending manager event incomplete while held, and engine post completed exactly once after its enclosing engine work. Zero-tick intervals advance scheduling/source work without inventing ticks. Finite completion detection must include the final outer tail and replay-scene finish witness; source length is not simulation-tick count.

Do not restore the global physical engine epoch to A. Timers, animation admission and other engine clients consume it. Maintain logical replay scheduling coordinates separately and admit native application work through the existing owner. Arena isolation preserves scratch lifetime but cannot establish scheduling correctness on its own.

## Particle lifecycle: generalization required, not another byte participant

`Sc6ReplayVfxState.cpp:PrepareParticleBirth` requires B primary slots=A+1, equal secondary count and exactly one additional component. Previous slot identities/weak generations must match. **Even a zero-birth interval is unsupported by this path.** Counts alone cannot safely generalize it.

Generalize to an explicit identity-delta plan: unchanged, B-only, A-only, replaced slot generation, emitter topology change and shared-pool change. First support unchanged membership and multiple independently validated B-only births using the existing reversible birth operation. Preserve ordering/slot identities, scheduler registration and pool allocation ownership. Reject duplicate component aliases and overlapping pool claims. A-only/death cases require native retirement/recreation ownership; do not revive a weak pointer or copy a dead UObject address.

For B-only exclusion, retain component and render owners, original VFX slot, emitter storage, scheduler membership, shared tile allocation and world render registration until commit. Undo restores the complete B relation, not just actor visibility. For A-only reconstruction, identify the exact native constructor inputs and all side effects (RNG, registration, allocation, callback/event) before replaying creation. Constructors may consume deterministic inputs; replaying them outside their original simulation traversal can double-consume RNG. Prefer replay from an earlier valid checkpoint across a native birth/death when feasible.

Coordinate buffers are derived only through the checked native reconstruction path (`141F95B10`) with retained compiled resource and ordered tile inputs. Persistent simulation textures remain state. A GPU copy command is submission; its event completion is the write/retirement boundary. No timeout frees those owners. Existing zero-coordinate-row evidence does not prove reconstruction for changed tile lists.

## Trace ownership and newly resolved evidence

`Sc6ReplayTraceState.hpp` captures109 native source states/77 attachments in the retained bounded run. It retains source history/material rows, animation destinations, selected scene transforms and manager payloads; captures complete A/B images; refuses changed topology/storage. These restrictions are intentional admission guards, not things to delete to make a test pass.

### Native dependency map

| Native address | Verified responsibility | Consequence |
|---|---|---|
|1408D1C00|Allocates controller+runtime state, mesh actor/attachment, establishes trace mesh scheduling prerequisite|Native payload and UObject generations are different owners|
|1408CED30|Frees state arrays at10/70/88 and releases cached weak controllerA8|UObject GC lease cannot keep these arrays alive|
|1408D2B40|Destroys owned mesh actors and attachment components, then component teardown|Strong state retention alone cannot prevent explicit UObject destruction|
|1408D47F0|Initializes geometry/history/lifetime from current bones, publishes rows and hides actor/mesh|Not a harmless history invalidation; changes retained source state|
|1408D5450|Waits/completes existing parallel animation evaluation, resolves proxy nodes, deep-copies rows|Publisher can mutate old evaluation state and allocate; not a free repair call|
|141C93E00|Resolves reverse reflected property index using type ancestry and property offset|Node pointer alone is not a stable binding across reinitialization|
|1408D9C20|Advances life/fade and stops VFX slots|Trace timing affects lifecycle|
|1408DA260 /1408DAC40|Advances retained spline/interpolation state and attachment transforms|History/transform feedback cannot be removed as cosmetic|
|1408D95A0|Samples curves into retained state before material publication|Whole function is not presentation-only|
|1408D5590|Writes incoming actor delta+444 and group scale+448|Checked fixed-step pipeline consumes448;444 remains retained but excluded only from its clock comparison|

Read-only Ghidra decompilation in this review reconfirmed1408CED30,1408D2B40,1408D5450 and1408D47F0. Earlier annotations are not substitutes for the function bodies. In particular,1408D4E10 initializes owned geometry; its obsolete clear-hash-buckets interpretation is wrong.1408D5FF0 builds topology from assets, not a general activation of one retained trace.

### Retained per-state comparison, no new capture

Parsed `combat-restore-executor-corrected.UE4SS.log` and `combat-restore-native-corrected.UE4SS.log`, joining `(tick, player, parts, kind)` and selecting candidate generation0 through205, generation1 for206–325. There are254 candidate records/249 native records, with no duplicate full candidate keys in this parse.

The bounded analysis, source/raw-log SHA-256 values and all108 differing logical keys are retained in [the static evidence manifest](../../../investigations/evidence/replay-fork-static-trace-analysis-2026-09-08.json). Its source hashes identify the files inspected by this fork; they are not compiled binary identities and do not refresh live proof.

At185 all109 logical identities join.108 differ, and each of these differs only in `rows` (and the aggregate hash); scalar, history, filtered, life, fade and active fields match. Across the entire selected prefix121 records join, with no other mismatches. All118 available resumed active-state records join and match every compared field. No selected candidate record lacks a native partner. This is a new analysis of existing logs, not a new equivalence run and not a raw-log certificate replacement.

Therefore aggregate prefix inequality is not solely native collection order. It is localized to per-state material-row content. It is not yet localized to individual row fields. The observer logs all states only at185 and then active weak collections;118 records are not240 player/tick observations or full dormant-state coverage. Corrected input suffix is intentionally different from the original candidate suffix, so do not compare those two as unchanged-input execution.

Next causal step, when observation resumes: log only the differing row fields for a small logical key subset before first activation, then their last writer and first consumer at activation. Native1408D47F0 overwrites several geometry fields from current bones but does not prove every retained row field is overwritten before every consumer. Determine whether difference comes from setup bone state, asset inputs, partial initialization or another writer. Do not zero all rows, install native-control rows, weaken gameplay checks or add GPU bone snapshots. The active-state equality suggests narrowing this work rather than another broad capture campaign.

### Storage and lifetime limitations

The snapshot acquires UObject leases but no native strong/weak controller lease. It binds collection headers and live reference pairs, then state backing. If topology changes it should reject. It cannot preserve history across controller destruction or allocation reuse. VirtualQuery writable memory is not ownership proof. ABA reuse can satisfy address/vtable/count checks without being the captured lifetime.

A general trace participant needs explicit native reference lifetimes or a checkpoint-validity horizon ending before retirement. Acquiring references must use the verified native count/destructor contract, on its admitted thread, and must not silently prevent semantic actor retirement. UObject lease and shared-state lease remain distinct. Retaining payloads may retain significant nested capacity; charge it. Initially prefer invalidating a checkpoint and replaying from an earlier supported anchor over resurrecting dead objects.

Additional native consumer verified in this review:1408DAC40 promotes the cached controller at state+A8, and when it is live with a nonnull cached state+A0, jumps past history generation to copy that other state's row30..47 endpoint data. Thus capture must establish transitive coverage of cached trace pairs. Current Capture records A0/A8 as binding bytes but does not explicitly require that the referenced state/controller is among `states_`, or recursively capture an external cached state. The109-state run may already contain the entire closure; no closure witness establishes it. Add a preflight closure check first. If it fails, identify the external owner before adding capture. A raw cached pointer is not a reason to manufacture a strong reference or revive it.

This same native path explains why matching an active trace's own history is insufficient: cached source rows are independent inputs. It also narrows the row question to real consumed endpoint fields30..47 before considering other fields. The function publishes derived rows to the animation graph and releases its temporary cached strong reference afterward. Do not skip that promotion/release work during resimulation.

Current image bindings also require unchanged array address/capacity, manager sparse-map topology, component activation/parent flags, reflected property list, proxy pointer and node base. Metadata property offsets/type ancestry are consumed by the native resolver but are not individually rebound by this participant. Determine whether class metadata is immutable for the admitted session; if it is not, bind metadata generation and reject/rebuild. Do not claim a hot-reinitialized animation instance is supported just because the UObject serial stayed constant.

`Add` deduplicates exact starting addresses and lengths, not arbitrary overlapping ranges. Review aliasing before supporting new node layouts. Full-capacity images include inactive bytes/padding; they can support same-process B undo but are not canonical deterministic serialization. Keep semantic comparison separate from exact process-local restoration.

## Physics, world callbacks and animation

### Physics

`Sc6ReplayWorldState::PreparePhysicsProjection` admits unchanged scene membership, selected kinematic actors, unchanged body/shape scale and storage, and a narrow inactive interaction set. It rejects articulations, particle/fluid/cloth solver kinds, active contact/trigger state, changed convex/static actors, unsupported query handles and changed shape geometry. It is a kinematic projection, not a full PhysX snapshot. These guards prevent silent omissions.

Inventory supported corpus actors by native kind/interaction/consumer, not by the visual description of the stage. For a rejected actor establish whether queries/contact callbacks can affect Lux/game state; if so it is mandatory state. Required next ownership work for dynamic bodies includes velocity/wake state, solver/island/contact caches, buffered writes, constraints and callbacks, scene-query data and pending completion. Reconstructing broad-phase/query caches is acceptable only when semantic query results and callback ordering are preserved. Current query timestamp treatment deliberately preserves physical invalidation epochs rather than rewinding them.

Do not attempt generic byte copying of the PhysX heap. Either implement a supported native reconstruction transaction for the observed actor class or use native replay from a valid earlier anchor. A general seeker must report unsupported content or provide an exact fallback, not silently truncate the timeline.

### Timers and callback-owned world state

The existing world participant owns manager/container state, but a copied timer is not all state that its callback changes. Prior native audit identifies script-delegate set141ED3D90, lookup14217F550 and lower setter14217E8E0. Lookup searches the executing timer before active/paused/pending arrays; self-rearm can preserve its handle. General reconstruction must retain delegate ownership, handle references in callback objects, executing/pending phase, callback mutations and timer clock/admission. Do not create a new timer per rearm or merely offset all deadlines.

Make a list from the currently captured delegate identities and latent actions, then trace only those callbacks into owner fields and scene lifecycle. Unclassified delegates are an admission failure. Include replay exit/round finish callbacks; never invoke them during a visual-only reconstruction. A scene-generation mismatch must abort restoration before any historical pointer use.

Current implementation detail: ValidateRecord rejects function captures (record+A0 or+90), admits only two native delegate vtables for allocated native callbacks, clones their48-byte payloads, and preserves script delegates. FingerprintCallbackBindings checks resolved owner identities/live-dead status, not callback-owned mutable fields. Read rejects an executing callback. PreparedRestore translates timer admission to the current physical epoch (or epoch-1 according to the captured admission bit), while restoring the captured logical timer clock. This is useful explicit ownership already implemented; replacing it with blanket global-epoch rewind would regress correctness. The remaining gap is owner-state completeness and broader callback classes, not absence of any timer transaction.

### Animation and render publication

Prior static audit proves worker event, completed evaluation, publication and enclosing tick completion are separate. Native141D96320 does not wait; it checks captured instance/mesh/count, swaps owning arrays and publishes. It has no replay generation check. Old work can be structurally valid against restored objects.141DA6840 handles animation eventEC8 and separate clothing eventD40; do not collapse those owners. URO tracker state/time offsets and admission stamps are separate from the global epoch.

Completed-application checkpoints reduce this hazard but historical interior extension must explicitly prove which mesh/cloth tasks and publications are pending. Complete old work before capture only when that is an admitted boundary; doing so after choosing an interior target changes its state. Native trace row publication also completes existing evaluation, so reconstructing it at the wrong phase can violate the target. Restore source/solver state, schedule coherent native pose publication once, and retain B ownership until publication has completed or B recovery is verified.

## HUD and visual coherence

`Sc6ReplayHudState.hpp` restores seven Cockpit fields, two current damage-effect references and the `DmgEff` pool's colors/layout/admission counts. A must have no active damage sequence. B sequence players are GC-retained and byte-validated but not rewound. A general target during a damage animation is unsupported. This intentionally avoids incorrect Play/Stop callback replay.

Do not generalize by calling PlayAnimation at the guessed elapsed time. Trace sequence player timing, evaluated widget properties, active/stopped lists, delegates and completion effects. Preserve native B players until undo closes. Further static investigation resolved the missing `DmgTypeEff` pool's role: Cockpit `CreateDamageTypeEffect` reparents a selected pooled widget into a player's canvas, changes its render transform, plays `DamgeEffect`, and increments `TypeEffCount`. Capturing only the counter omits hierarchy and animation state. Its cooked tracks animate transforms, color/opacity and visibility; no event track was found. Native sequence completion still dispatches delegates and widget events, so this is not permission to omit callback ownership. The retained corrected runs have one active damage-type widget at185 but none at205/210: this is a general seeker coverage gap, not a demonstrated cause of the bounded restore's discrepancy. See the deeper findings below. Other HUD values may rebuild natively, but identify their producer and first display boundary before relying on that.

Required visual checks are actor/effect membership, correct current poses, HUD freshness and no persistent corruption across hold/resume/seek. Full Lux pose equality is strong evidence for queried evaluations, not a complete actor/effect inventory. A saved framebuffer can conceal missing native actors. Once history is classified presentation-only, prefer native invalidation and a declared bounded settling interval. Keep simulation-fed particle history; do not blanket-reset effects or exposure-linked gameplay state.

`PresentHook::retain_replay_surface` shares `m_replay_image`; it does not clone pixels. The current arm/hold/release sequence can keep A intact by detaching live capture ownership before further native captures. General indexing must make that transfer explicit: seal a checkpoint image, allocate/reuse a different live capture target, and never CopyResource into a texture still owned by a checkpoint. Reference counting prevents deletion, not writes. Same-device/dimensions/format checks currently reject resize/replacement; define checkpoint surface invalidation independent of deterministic checkpoint validity.

## Memory and performance

The512MiB limit needs one reserving ledger, not only summed final sizes. Historical preparation captures B and allocates observation/surface metadata before whole-transaction accounting. Trace Add checks an image's payload before vector growth and checks total after allocation; temporary owner vectors, allocation overhead and retained native capacities complicate peak accounting. A per-participant budget equal to the whole checkpoint budget does not reserve space for simultaneous A/B/GPU/scratch/index data.

Reserve before allocations/submission. Charge unique owned allocations, retained displaced native allocations where replay prevents release, temporary peaks, GPU A/B images, completion/quarantine objects, CPU scratch, pinned checkpoints and index entries. Avoid double-charging shared handles; do not omit native backing just because it is represented by a borrowed pointer. A rejected allocation must leave existing checkpoints usable. Timeout quarantine remains charged until actual completion, even if a seek request is cancelled. Eviction must exclude transaction pins and in-flight GPU storage.

Read-only decompilation of142189040 confirms max-rate control is mixed with delta/time calculation. Raising engine+64C to a huge value changes the fixed-frame branch's time progression; it is not a safe unpaced mode. The old plate comment claiming1:1 lockstep is not proof. Benchmark/fixedtime globals14416A943/944 select a different clock branch and have other consumers; toggling them without audit is not a solution.

Implement a scoped unpaced admission path that retains verified logical deltas and timer/animation/render publication semantics while omitting only wall pacing. Preserve physical epochs and restore ambient pacing ownership on exit. Measure request-to-held, capture, undo reservation, restore publication/completion, resimulation per tick, final world/application/render retirement and total request-to-ready. Report production renderer cadence separately from viewport updates. At74ms capture/86ms restore the demonstrated path is already far above a16.67ms frame budget; suitability of architecture is not production rollback readiness.

## Full replay index, controls and lifecycle

The index should map absolute completed traversal→round/local tick/source sample/interval/phase, plus a preceding checkpoint or native round/setup anchor. It must cover native final replay completion, not stop when winner/source cursor looks finished. Repeated ticks share an input sample. Authored replay/customization/stage/round records must come from the verified native loader. Expected trajectory rows belong only to validation.

Use sparse checkpoints with explicit validity domains (scene generation, binding generations, supported participants, immutable resource ownership). Cross-round operation should run native lifecycle and bind a new domain. A checkpoint containing old raw pointers cannot be transplanted into a recreated world. Native round/setup replay is the conservative fallback; include its full latency in the0.5s seek requirement. Whether that requirement is achievable for distant unsupported domains remains unknown, not an excuse to report a partial timeline complete.

Controls should submit coalesced target requests with monotonic request IDs to the single owner. A later drag target supersedes an unstarted request; it cannot cancel an already submitted GPU command. Define when supersession becomes B undo versus committed-A advancement. Seek ends paused; Resume executes pending tails once; Step advances one completed traversal, possibly across zero-tick intervals or round lifecycle. Same-target seek should be idempotent without needless restoration. Invalid targets must fail before mutation.

The held application path currently draws responsive overlay frames but does not generally pump native Windows/Slate lifecycle. Comments in Interior explicitly leave resize/exit event admission unfinished. Host/Application/Surface contain process `__fastfail` guards. They are appropriate evidence that unsafe continuation is blocked, not proof of recoverable cleanup. Do not merely remove them.

Define a session shutdown owner: stop accepting seeks, freeze gameplay admission, settle/recover any reversible transaction, retain uncertain GPU/native resources until completion, finish or safely retire owned pending tasks, restore hooks/pacing/input/overlay ownership, then exit/rebind. Scene destruction cannot race with a checkpoint dereference. Device loss and an event that never completes require a documented terminal resource policy; elapsed timeout is not cancellation. No queued callback may outlive its host.

Concrete containment path: `Sc6ReplayObjectLease::~Sc6ReplayObjectLease` calls Release and, on failure, invokes `control_.release()` to preserve a possibly registered native callback object. That avoids freeing registry-reachable memory, but drops the only owning handle and its accounting/retry path. Do not replace it with unconditional deletion. Transfer failed retirement to a session-owned quarantine with bytes, registry identity and retry admission; prevent module unload while its callback vtable can remain registered. Acquire also refuses shared registry growth when existing capacity is full, so sparse checkpoint retention must account for registry slots as well as bytes. Explicitly release checkpoint leases while the owner thread and GC admission boundary still exist.

## Implementation order and proof obligations when testing resumes

1. Harden public action/exception/release semantics and pin checkpoint lifetimes. Review every failed Begin/invalid Commit path before any new live experiment.
2. Add unchanged-membership restoration alongside the working one-birth operation; preserve its cancellation and corrected-input protocol. Then generalize identity deltas only with audited lifetime support.
3. Add exact target driving from a restored completed checkpoint into a genuinely held interior boundary. Reuse the executor phases; do not drain and snap. Independently compare its continuation and pending task/callback/tail witnesses.
4. Resolve unsupported participant domains needed by the chosen short interval: trace lifetime/activation, active HUD, physics and callback owners. Keep restrictions explicit. Use the retained trace analysis above to avoid an unnecessary new aggregate campaign.
5. Implement sparse indexing and native anchor fallback, then controls and recoverable stop/re-entry. Memory reservations and cancellation ownership belong in these changes, not a late optimization.
6. Resume targeted tests only after the user lifts the pause. First invalid-action and transaction failure checks, then unchanged/corrected/cancel continuation on changed identities, interior historical landing, exhaustive short-interval ticks, round boundaries, required strict sentinel, lifecycle faults and corpus. Do not repeat a failing live run without a distinguishing change/hypothesis.
7. Measure unpaced restore-plus-resimulation separately, then optimize the proven hot path. Keep the strict-seek entry point until equivalent coverage actually passes.

For every experiment report simulation_correctness, visual_coherence and optional pixel_diagnostics separately. Passing one bounded experiment must not set full execution_gate or seeker completion. Preserve exact binary/framework/observer/replay/protocol identities and bounded raw-log hashes. The current stage report correctly leaves visual coherence incomplete; top-level prose should do the same and remove duplicated obsolete checkpoint paragraphs at the main agent's next status update.

## Remaining unknowns and the evidence that closes them

| Unknown | Evidence needed; no broad campaign required |
|---|---|
|Which dormant trace row fields differ and whether activation consumes them|Selected row-field bytes and last-writer/first-reader trace for matched logical keys; activation continuation under corrected inputs|
|Whether current trace bindings survive useful checkpoint spacing|Native lifecycle/reference/reallocation inventory; reject boundaries or retain verified distinct owners|
|Complete active HUD restoration and damage-type effects|Blueprint/native sequence and callback ownership plus one active-animation restore with B undo|
|General physics needed by replay corpus|Actor/interaction/query-consumer inventory tied to actual rejected content; semantic query/contact continuation|
|Callback-owned world state completeness|Captured delegate/latent identities traced to mutated owners and ordering, including self-rearm and scene finish|
|Historical external interior continuation|Restored earlier completed anchor→exact pending boundary held across app updates→independent unchanged/corrected continuation|
|Visual coherence beyond Lux poses|Native actor/effect inventory, HUD target values and resumed display without saved-image substitution|
|Failure recovery beyond deliberate cancellation|Fault at each publication/commit phase, invalid binding, resize/scene exit, late GPU completion, cleanup/re-entry|
|512MiB peak rather than final accounted total|Preallocation reservations and high-water tracking including retained/quarantined/native/scratch allocations|
|Rollback and0.5s whole-seek latency|Unpaced logical-equivalent resimulation measurements through final completion, including cross-round work|

This handoff establishes bounded implementation constraints and identifies concrete hazards. Static analysis cannot certify runtime ownership, future content coverage or timing. Those remain explicit evidence obligations rather than promises that the next change will be the last.
