> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Retained rolling600 coverage audit

Audited the existing independent pass; no game was launched for this audit.
[Original pass and immutable raw logs](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/124bed6cff7c7c23b67d0099978a031559c1cb01eeaf66662b3a499838bbcb4e.json).
[Repackaged evidence with scoped inventories](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/4df5f7a70e1863612bf39f5b86704154af5acdde776894708068048259e10207.json).

The existing evidence runner now includes these inventories for candidate and stock reports. The focused runner group passed 135 tests, including the new scope/retirement/manifest tests and the previously unmapped integration-reuse tests. [Test/source receipt](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/7eecc515f747cf31b024a7952bf9a716c296af61f18d6f2d36009f8863cc76c4.json). [RED/GREEN logs](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6774fd8dd7c0ca9a8087a88286cc788546f9040267c0b88d34dcdf6ea1443e7f.json). No native build or integration-suite rerun was needed for this reporting-only change; the historical 529-test receipt is not a new full-suite claim for these edits.

The active window is completed ticks 211–816. The candidate contains 606 original-forward intervals and 4,200 regenerated intervals across 600 transactions. Counts below inventory observed events; the retained independent comparator establishes agreement. Counts alone never promote an acceptance gate.

| Observation | Stock / candidate original-forward | Candidate regenerated | Coverage conclusion |
| --- | ---: | ---: | --- |
| Native simulation callbacks | 606 / 606 | 4,200 | Observed and independently compared |
| Input cache publications | 606 / 606 | 4,200 | One publication per observed combat tick |
| GPU initialization | 196 / 196 | 1,366 | GPU births observed; first at tick 214 |
| GPU retirement | 25 / 25 | 175 | Native peer notification/destructor return and cleared slot recorded; first at tick 217 |
| CPU sprite retirement | 185 / 185 | 1,280 | Same completed-retirement receipt; first at tick 211 |
| CPU mesh retirement | 25 / 25 | 175 | Same completed-retirement receipt; first at tick 284 |
| RNG seed events | 196 / 196 | 1,366 | Seeds observed; these do not independently identify CPU construction |
| Multiple native ticks per interval | 0 / 0 | 0 | Missing in rolling combat |
| Repeated ticks sharing one publication | 0 / 0 | 0 | Missing in rolling combat |
| Zero-tick intervals | 0 / 0 | 0 | Missing |
| Restoration of pending native work | Unavailable | Unavailable | No corresponding restoration receipt; generic `event=1` lines are startup callbacks |
| CPU construction | Unavailable | Unavailable | Requires construction-specific observation at an existing native owner |

Both runs do contain three multi-tick intervals, but they finish at ticks 3, 23 and 34. Repeated input consumption occurs at ticks 23 and 34. These are startup observations, not rolling-window coverage. Normal rendering was enabled; no visual-coherence review is inferred from that setting.

Emitter kind classification follows `Sc6ReplayCpuEmitterState::Vtable`: sprite `0x3949b60`, mesh `0x3949d88`, GPU `0x394c100`. The inventory preserves traversal boundaries, ignores process-local owner addresses for counts, and rejects incomplete retirement receipts. It does not choose controls or supply simulation state.

## Next missing-case diagnostics

Do not rerun the full 600-cycle campaign for these gaps. First verify native reachability for repeated input, multi-tick intervals and pending work in the supported combat domain. Use an existing bounded pending-work case where its setup and identities apply; otherwise add one short named profile for the verified path. Do not force extra engine callbacks or claim unreachable behavior without native-code evidence. CPU construction needs a bounded receipt from the existing construction/lifetime owner, without competing hooks.

## Changed-input capture dependency

At the start of this audit, the host could not safely rebuild checkpoints while retaining B by enabling ordinary capture:

- `CaptureOperation(Begin)` and `Capture(Checkpoint&)` reject an active historical restore.
- `AdvanceCaptureOperation` finishes and resets the single `particle_copy_` before starting another capture.
- `Sc6ReplayParticleCopy::Finish` releases undo/staging/image resources, coordinate references, wrappers and A/B/C histories, and resets execution/recovery state. That is the same owner used by the retained-B transaction.

This required a separate transaction-owned corrected-capture context, with explicit ownership and completion routing. That context is now implemented and locally tested; its first live scheduled run stopped before A at replacement capacity. See [current status](../../../rollback-status.md). Native MCP access was restored on 2026-09-21 and the existing SoulcaliburVI.exe program was inspected. [Retained decompilations and verified plates](../../../evidence/rollback-capture-native-2026-09-21.json).

- `0x141fa0e50`, now `DropCompiledParticleResourceReference(void*) -> int`, atomically decrements compiled-resource `+0x230`. Last release either invokes resource release/deletion directly or dispatches a task. `RebuildTileEmitterCoordinateResources` confirms the corresponding retain and render `+0x48` owner. A zero return does not imply GPU completion.
- `0x1415b83f0`, existing `AssignRenderResourceReference`, retains the new resource before releasing the old one. Last release can enqueue deletion. Its existing annotation was preserved.
- `0x14146b380`, now `DropPooledRenderTargetReference(void*) -> int`, decrements non-atomic `+0x48` unless `+0xA0` bypasses refcounting. Zero cleans nested render references through `0x14146c150` and deletes the target. The paired retain at `0x14144ac80` confirms the count/flag behavior. Nested wrapper deletion can remain deferred. Precise pool-policy field meanings remain untyped.

The two new prototypes/comments were re-read and saved through native MCP. No database scripts or new program copies were used. These are ownership findings, not new live rollback evidence.

Before the change, `ParticleCopyExperiment`, `ExecuteParticleCopyCommand` and `CaptureCheckpointUnchecked` routed through the single `particle_copy_`. Corrected capture now has an explicit separate owner and command/failure routing. A separate allocation alone is insufficient: corrected capture needs explicit routing, separate failure/completion state, checkpoint assembly from that context, and simultaneous accounting. Do not temporarily exchange the transaction owner, relax capture guards, or manufacture completed-application boundaries. The corresponding production-boundary regression must prove that corrected checkpoint capture leaves B's particle owner, undo images and pending completion pins intact, followed by cancellation restoring B's revision and window.
