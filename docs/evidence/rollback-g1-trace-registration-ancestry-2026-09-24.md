# G1 trace registration: pre-payload ancestry witness

GPT-6 Astra performed all production/test edits in E:/myMods. No delegated coding, project build, game-native checks, deployment, live run or Ghidra database mutation. Existing checkout/build and unrelated work were preserved.

**Decision: local observation GREEN; positive writer ownership still unproved.** The existing registration hook now associates the writer with an earlier task-scope identity snapshot and its eventual return. It does not admit a particle task, grant a lease, defer callbacks or establish complete-B recovery.

## Immutable evidence

- [Source/native/log index](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/9a8fec6ff5f79843a5f28271c60901878fcfa793ffd730f0444f07c69ab02662.json): individual before/after source hashes, exact diff, native transcript, intermediate failures and sidecars.
- [Final nine fixture sidecars, reproducible RED, journal/process inventory](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/a643d8f89ed64d66f5212cae2b7d6a246492cec4235ac7bad0b196f47229169e.json).
- [Raw native MCP transcript](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/ab6212a11ea126987d3ee1a7db5d63b1ab4432eb65b47e387f7afd1de558b415.json): decompilation, assembly and exact 48-byte entry reads in the existing SoulcaliburVI.exe. No snapshot endpoints, second import or database-edit scripts.
- [Initial RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/97b5d8dbfe8e78698ff902246b4ddcb93b448726ba1dd169984ad87fd7f2bafa.log); [final fixture against immutable old observer RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3a28bca0bd1e80ab210b9f550386946c7f8d9d09a876f6aadc131602fdeec707.log). Both reach native registration, particle completion and reflected tail, then fail specifically for missing registration ancestry.
- [Final existing local-group GREEN: 70 tests](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/8393d9b1411934e79d9591d16b5824d8c9dee4ba494481b9d038da5f98fd74e9.log), via `python tools/replay_test.py local --group vfx-observation`.
- [Final reconstructible source archive](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-21501f2655d6e82582081e9074277fbed7a61aa34690f7ee526ab5f82282acd8.zip), SHA-256 `55fb6f458eff0a99f813830530e55ec4d93d6aac84c4c1d22efc5fffc0abed93`. Verified manifest/payloads; 16,622 files including dirty/untracked inputs.

The read-only game-file SHA-256 is `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`, matching the retained positive game. This does not verify newly built or mapped runtime/framework/observer DLLs; parent owns those checks.

## Hook ownership and native ordering

`NativeReplayVfxCompletionObservation` remains the sole source owner of registration **1408C9120** and task body **14215D250**, with the same ten detours and unchanged signatures. `ReplayBoundaryObserver` separately owns **14215ED20**; it was not changed. Existing kind2/kind3 declarations in Sc6ReplayTaskGroup and Resume were reused without modifying those files. No competing hook or new public API was added.

Registration ABI: RCX=collection, RDX=UObject receiver, R8=unused descriptor bits, R9=full FName; void return. Assembly saves R8 without consuming it; R9 becomes the stored name. All arguments and GetLastError forwarding remain intact. **14215ED48** calls the task body with RCX=task+0x10, EDX=native thread, R8=task+0x40. Body return precedes outer event completion/release/recycling. Return is therefore synchronous scope completion only.

| Route | Verified native ordering |
|---|---|
| ActivateLuxTraceManagerRequest 1408CD940 | CALL Start **1408CD975**, return **1408CD97A**; Start precedes active-slot insertion |
| BeginTrace 1408D5FF0 | first component store **1408D6016**, +0x4A8; CALL Start **1408D64AF**, return **1408D64B4**; topology/owner work precedes Start |
| Start 1408D8C40 | first component store **1408D8C65**, +0x43C; material calls and tick registration precede **tail JMP 1408CDBB0 at 1408D92E2** |
| Binder 1408CDBB0 | registration CALL **1408CDC87**, return **1408CDC8C** |

Activate's callers include event callback 1403C5360, infinity settings 1408CEB40, 14048A2D0 and reflected 140C3DA20. BeginTrace's direct caller 1408D5D10 can construct/register components before BeginTrace. Static callers do not identify the retained positive execution owner. The pre-payload scope must enclose the entire relevant chain; registration entry is too late for admission. Start's frame can be absent from the stack because of its verified tail call.

## Implemented boundary and limitations

The private [registration include](../../HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Registration.inl) uses the existing scope constructor/destructor through a once-installed private observer callback. Kind1/native-body and kind2/custom-manager entry snapshot task/function, exact actor/component tick table, scheduled-task binding, world/completion pointers, caller, native/OS thread, indexed UObject address/index/serial and observed application epoch. Kind3/resume remains distinct and cannot substitute for original admission.

Registration copies the earlier snapshots, captures up to 32 stack PCs, compares current task/owner metadata and records current owner index/serial and epoch value. A scope ID is an **invocation serial**, not a native task-allocation generation. Epoch observations are not simulation ticks or generations. Selection uses the actual thread-local scope, never a recent task on another thread. Matching reads do not establish synchronization or lifetime protection.

Entry/registration/return ordinals preserve nesting under the existing observer lock; registration event timestamps remain available. Scope return updates copied receipts by ID without task/event/UObject reads after native execution. No observer lock spans native callbacks. Custom scope return can follow task retirement; it creates no post-return lease.

Explicit reasons are `missing_scope`, `identity_changed`, `continuation_not_entry`, `overlapping_or_nested_scope`, `scope_capacity`, `stack_prefix_only`, and `correlation_only`. Missing/changed/nested ancestry can be valid observation of a contract failure. Capacity loss or pending/incomplete scope at closure makes observer completeness false. Stack saturation stays a labelled prefix. Every witness keeps `ownership_proven=false` and `before_trace_effects_proven=false`.

The real hook/start/close/serialization/retention boundaries run in the local fixture with controlled native dependencies. The fixture does not execute trace-start topology/material effects or manufacture a live owner. Its callers are fixture addresses. Positive selected-route evidence remains the historical 16 callers at 8CDC8C, not a fabricated fixture return address.

## Regression and accounting

The production observation defect is original registration forwarding without available pre-payload ancestry. The final fixture reproduces the RED against the immutable old observer header without reverting the checkout. GREEN covers generation changes, another thread, custom entry/resume, nested calls, capacity exhaustion, closure while active, task-memory overwrite on return, native ABI/error forwarding, and corrupt-sidecar rejection.

Storage attempts stopped at the unchanged 3 MiB static assertion. The original one-byte detour stub could conceal real object cost, so the existing fixture now reserves **1 KiB per detour**. Final bounds: **16 registration witnesses, two copied task ancestors each, four active entry snapshots, 32 PCs per registration**. Two ancestors already falsify a single-owner hypothesis; deeper/pool-exhausted ancestry remains incomplete. All existing record/serialization scratch and ten 64 KiB trampoline reserves stay charged simultaneously. No event-path heap allocation or retained native/UObject owner was added. `ProcessOwnedBytes()` remains **3,145,728 bytes** and host production accounting remains **1 GiB**. Parent's real-dependency build must still enforce the actual type-size assertion.

Other stopped failures concerned fixture setup for the existing custom hub and an overflow assertion incorrectly expecting a receipt after scratch exhaustion. Retained logs show the corrections; none is native ownership evidence. The final test requires missing receipts on exhaustion while all native work forwards.

## Parent live discrimination

The shortest **already positively demonstrated runner configuration** remains the original same-setup A210/B217 prerequisite diagnostic. After parent build/native checks, exact identities and preflight:

```powershell
python tools/replay_test.py combat-restore --replay ReplayExample/REPLAY_12744704008398858106.bin --host-seek --combat-anchor-tick 210 --combat-advanced-tick 217 --host-seek-target 217 --combat-case after --combat-exact-advance --consumer-mutation --replay-budget-gib 1
```

The existing recipe applies include-setup/intro skipping, fixed seed and serial particles. Preserve them in the independent control; reuse a control only through existing identity/setup validation. Do not switch to committed candidate-only restore to skip a control. This command is **not run here** and does not test changed-particle recovery.

The actual discriminating prefix ends at the **first exact 8CDC8C registration and enclosing scope return**, earlier than the full validation leg. Historically this was **pair2**, 44.860 s after observer start, manager index 310119/serial 8362 and listener index 311114/serial 8104. These are historical lookup coordinates, not future identity constants or a wall-clock stop contract. No simulation-tick coordinate is established for this registration, so a smaller numerical tick bound would be invented. Pair14/producer746 remains the subsequent concrete particle-completion lookup target; its kind1 task does not retroactively own pair2.

Resolve captured PCs against the verified image: require binder writer plus Activate return **8CD97A** or BeginTrace return **8D64B4**, allowing Start's tail call. Require one readable kind1/kind2 scope, unchanged valid indexed actor/component generation and scheduled binding, matching OS/native thread, and entry-before-registration-before-return ordinals. Inspect the outer caller chain from that scope for any earlier unowned effect. Correlation only supports the next admission/B-contract investigation.

**Stop/falsifiers:** absent/different/recycled owner, stale/unreadable generation/binding, continuation-only entry, nested/reentrant/other-thread writer, missing relevant outer ancestry, entry after first effects, or pending/truncated/overflowed evidence. Parent must stop at the first unsupported observation and retain the raw sidecar/log prefix with journaled cleanup of only owned processes. The observer always forwards; it neither cancels a native task nor automatically stops the game. Later matching rows cannot repair the failed prefix.

If the prefix cannot establish the relevant outer call before effects, retain that precise no-go; do not claim a lease or write an admission fixture that supplies one. No unchanged Niagara/408 retry or 600-cycle run is justified.

## Separate outcomes

| Dimension | Result |
|---|---|
| Observer validity | Local 70-test GREEN and forwarding RED; new positive live writer correlation not yet observed |
| Simulation | No new game execution or determinism result |
| Recovery | No admission/B/settlement changes; changed particle complete-B recovery unproved |
| Coherence | No new normal-rendering actors/effects/poses/HUD evidence |
| Performance | No live measurement; added indexed reads/diagnostic locking have unmeasured cost |
| Memory | Unchanged3 MiB observer and 1 GiB production ceiling; conservative local fixture passes |
| Cleanup | No deployment; final inventory only Steam 98544 among game/compiler/linker/fixture/Steam names; journal retained read-only, not preflight recertified |
| Gates | G1/G2 OPEN; no rolling qualification advancement |
