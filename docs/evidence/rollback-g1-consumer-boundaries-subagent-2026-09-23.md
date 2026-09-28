# G1 consumer enforcement and native completion boundaries

Date: 2026-09-23. Existing SoulcaliburVI.exe program, x86-64 Windows, image base 140000000. EBOOT.elf was open/current but never selected, edited or saved. Read AGENTS.md, local-readiness plan, newest rollback-status and linked Niagara/consumer continuation evidence. Native MCP only; no script, hook, game, build or production/test edit. [Retained raw native outputs](rollback-g1-consumer-boundaries-subagent-2026-09-23-raw.json).

## Finding

No safe native cancellation protocol was found in the inspected Niagara/physics chains. This is a bounded negative result, not a proof that no engine facility exists elsewhere. An arbitrary callback returning early does not transfer its incomplete native stack/ownership to the manager-specific Sc6ReplayTaskGroup continuation. Do not close G1 using such a return, zero delta, disabled tick or a Boolean invented as cancellation.

The smallest safe present design is to enforce admission before the owning task/application starts and preserve exact simulation state for any consumer whose immutability cannot be established. A mid-application version check is valuable detection, but is not an implemented recovery protocol. It cannot justify relaxed motion alone.

## Verified boundaries

| Address | Win64 arguments / return | Consequence |
| --- | --- | --- |
| 141BCDB90 TickNiagaraComponent | component RCX, delta XMM1; void | Active bit+188 mask40000 and system+810 gate system update. Tail work still updates proxy and transform/render state. |
| 141BCC8E0 TickNiagaraSystemInstance | instance RCX, delta XMM1; void | Reset/reinitialization can happen inside ordinary component update. Desired-age stepping may call multiple updates. No cancel status. |
| 141BC65E0 | component RCX; void | Initial component entry creates0x3A0 instance and0x18 reference controller, publishes component+810/+818, binds asset weak ref from component+808, rebuilds instance and dispatches instance+120 callbacks. This path is not closed by tick-only inspection. Vtable reference1437FA498. |
| 141BC8620 | instance RCX; void | Reinitializes emitters, calls141BCF750 then141BC4D90 and renderer updates. Entry precedes extensive allocations/refcounted emitter state changes. |
| 141BCE060 | instance RCX, delta XMM1; void | Mutates data buffers, samples owning component transform, propagates interface bindings, rebuilds interfaces on+398, invokes141BCDBF0, then emitter update/parallel work and age+54. The reader is not the first side effect. |
| 141BC4D90 | instance RCX; void | Clears weak interface tick list+388/+390 and walks emitter interface arrays; calls interface+230 and then+240. False initializer status marks emitter validity false; it does not cancel application. |
| 141BC7F90 RefreshNiagaraStaticMeshSource | data interface RCX, instance RDX; bool AL | Clears prior source+AC/mesh+B8, chooses source through explicit actor or component/attachment/owner enumeration, reads transform, updates caches. False means unusable source, not transactional rollback. |
| 141BC7760 TickNiagaraStaticMeshSource | interface RCX, instance RDX, delta XMM2; bool AL | Reads already-selected weak source transform and mutates current/previous matrices. True requests rebuild by141BCDBF0; false does not mean abort. |
| 1420297F0 StartPhysicsSceneSimulation | scene RCX, type EDX, event-reference storage R8; void | Publishes active flag and new event BEFORE callbacks. Callers use scene+130 for type0 and+140 for type2. |
| 1420113E0 DispatchPhysicsStepCallbacks | collection RCX, scene RDX, type R8D, delta XMM3; void | Increments recursion depth+64; iterates entries backwards, stride40; calls callable+68. False AL only requests expired-entry compaction after depth decrement. |
| 142055C40 | substep context RCX; void | Creates event/task and schedules dependent continuation before scene+80 callbacks; increments substep counters; calls native simulate then task virtual+20. Outer physics-start guard cannot substitute for per-substep ownership. |

## Concrete ownership evidence

At142029916, StartPhysicsSceneSimulation writes scene+FE[type]=1. At14202991E it allocates an event, swaps it into caller storage, and releases the prior reference via count+48. Step delegate calls are later at14202999F and1420299C2. At142029B16 completion-task constructor14200CBE0 retains the event at task+28, count at event+48;142029B51 invokes scene virtual+40;142029B5A releases task via virtual+20. The caller142027E40 includes scene+130 and existing async event+158 in prerequisites and retains a joined event+160; caller142027C10 passes scene+140 and schedules another dependent event+158. Submission return is not completed ownership.

Substep142055C40 independently creates event at context+C8, task with event+28, and continuation142055BC0 before delegate dispatch. Returning from a blocked delegate does not cancel that task or remove its prerequisite chain.

Niagara source refresh is also not an atomic admission-only function: it has already cleared caches before source selection, and its caller has changed emitter/list state. Rejecting by returning false would alter required simulation semantics. The safe observation location is before entry; a safe *recovery* location needs an owning continuation that does not presently follow from these signatures.

## Recommended implementation sequence

1. Keep debris motion Unresolved/exact. Add cheap generation/version witnesses at existing owned task dispatch boundaries and at Niagara initialization/reinitialization/rebinding plus physics pre-step/substep boundaries. Observations must identify current application epoch, task owner, active call depth and native event ownership; no UObject::IsReal tick scans.
2. Before dispatching a whole native task, revalidate the current consumer contract. Retain the untouched task payload/completion reference if validation fails; do not call its virtual entry or mark it complete. This requires a separately verified generic task continuation/cancellation owner, since the current manager continuation is not generic. Prove this first with production-boundary tests of task/event counts and absence of native side effects.
3. For changes originating within an already executing call, cover the mutation producer before side effects or preserve exact dependencies and let the complete native application finish, then recover B at the existing completed/idle boundary. The latter is only valid when the full dependency/ownership set is actually reversible; unadmitted Niagara births are not proven reversible. A suspended native stack cannot be discarded or resumed against restored B.
4. Do not enable exclusion until the supported application has either a verified immutable consumer lease over all relevant producers, or a real generic cancellation/drain contract covering begun and pending tasks. A mutex around only validators is not such a lease; it does not govern native writers. Unsupported rejection remains an acceptance failure for an otherwise eligible supported cycle.

This sequence deliberately does not claim that a latch-plus-drain fixes the blocker: allowing an unknown consumer to run on relaxed state then restoring B is too late for before-consumption G1, and may create uncaptured effects.

## Smallest resolving experiments

- Extend existing local scheduler fixture: queue a concrete component task, mutate its owner/interface identity before dispatch, retain it without native entry, and prove completion remains incomplete plus references remain owned. Then establish an explicit resumable or cancellation disposition; do not copy the manager predicates onto arbitrary tasks.
- Instrument one shortest supported application, without changing motion or suppressing calls, recording initialization141BC65E0, reinit141BC8620, rebuild141BC4D90, static source reads141BC7F90/141BC7760 and physics delegate collection counts immediately before dispatch. This tells whether the problematic mutation paths execute, but zero occurrences alone is not permanent enforcement.
- Exercise callback insertion during a synthetic step and between substeps through production admission. Native delegate Boolean should be tested as liveness/compaction, never abort. Live work is required only after local ownership tests establish a valid disposition.

No408 or600 experiment was run or recommended.

## Ghidra changes

1420297F0 renamed StartPhysicsSceneSimulation; verified prototype void __fastcall StartPhysicsSceneSimulation(void *pPhysicsScene, uint dwSceneType, void **ppCompletionEvent); actual multiline ownership/cancellation plate applied, read back, re-decompiled and SoulcaliburVI.exe saved. No competing hook entry changed.

This was a bounded ABI/ownership annotation, not completed function documentation. Completeness score6.80/effective21.80: unresolved struct/local/global/control-flow naming remains. Full type recovery was intentionally outside the blocking dependency scope. One initial get_function_variables call used the wrong address parameter and returned an error; corrected native call succeeded. No tool failure is represented as completed work.
