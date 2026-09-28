# G1 Niagara zero-hit observation: admission disposition

## Observation and scope

The parent's retained live manifest `a77eaa4b61f4d594dfb7e5f33c76c13b5a965631b837c1ec6426a7698cdc9ad5` passes the paired selected prerequisite mutation, complete B217 recovery and 120 independent continuation ticks. Both dedicated Niagara forwarding hooks installed, the completion status was available with zero records and no persistence failure, and the retained run-owned sidecar contains an empty event array. This is a valid zero-hit observation for the two instrumented helper entries in that run. It establishes neither absence of other Niagara entry routes nor safety under different corrected execution.

This assessment makes no production/test changes and runs no build, native fixture or live experiment. The forwarding observer and existing exact/Unresolved state policy remain unchanged. Native facts are retained in [producer audit](rollback-g1-niagara-spawn-boundary-no-go-2026-09-24.md), [initializer audit](rollback-g1-niagara-init-boundary-no-go-2026-09-24.md) and [observer ABI evidence](rollback-g1-niagara-observation-native-2026-09-24.json).

## Concrete available boundaries

`Sc6ReplayHost::TickApplication` calls `AdmitGroundUpdate` while the application is Idle, before `EnterApplication`. `AdmitGroundUpdate` demonstrates an owned pre-application disposition: wait for the preceding display completion, establish the completed-application hold, and request existing transaction cancellation while complete B is retained. That is a usable location for a future *proved* Niagara admission predicate. Its current ground-state predicate does not establish such a predicate.

`NativeReplayCallbackAdmission::Admit` establishes empty latent-action sets, an empty queued delegate domain, UI admission and quiescent streamable resources; its revisions track those registrations. It has no closed census of synchronous reflected function calls. `ReplayComponentTickConsumerAdmitted` rejects the known Niagara component table/tick target, but absence of a queued Niagara owner cannot exclude a component constructed later inside a different callback.

At `141BCC5A0`/`141BCC730` entry, construction is still ahead, but the native/script caller is already synchronous. The helpers return a component pointer to their caller. Existing task-group continuation does not retain that caller's FFrame, locals, return storage, native stack or all prior effects. Returning null, skipping the helper, waiting indefinitely on the same GT or treating a manager task as its continuation has no verified disposition. Whole-application completion before recovery would admit the uncaptured Niagara birth and callbacks whose undo is precisely unproven.

The existing `BindSessionExit` ProcessInternal pre-callback is not a generic precedent: it defers one parameterless `OnRequestToStop` event whose native flow manager retains scene/completion ownership. Its source explicitly retains no FFrame. The spawn thunks have arguments, pointer results and potentially nested script callers; that verified special-case transfer does not generalize.

## Decision

**No-go for a production admission/continuation change with current evidence.** This is a bounded evidence conclusion, not a claim that no suitable engine mechanism exists.

A versioned supported-scope contract could be legitimate only if a finite, generation-stable set of all executable producers and callbacks for the admitted application is known before it starts, and the contract proves none can reach unowned Niagara creation/init/reset/source refresh. Its invalidation must precede any newly admitted call. `ReplayStatePolicy::Stamp` can version and reject stale contracts; it cannot establish producer reachability. No such closed domain or invalidation protocol exists in the inspected source.

Rejecting every loaded Niagara asset/function would conflate availability with execution and may reject otherwise eligible combat cycles. Checking only current Niagara instances misses future construction. Rejecting at a helper entry is too late for the proposed *pre-application* exclusion. Reusing original-run no-hit data to authorize changed execution would turn observations into assumed behavior. None supplies the requested exclusion without reducing eligible coverage.

No truthful ownership RED/GREEN is presently available: a fixture that invents a resumable script caller or a complete producer census would grant the missing property. Existing forwarding and bounded writer RED/GREEN remain valid for observation only.

## Smallest resolving question

Resolve one finite producer domain before another live run: **which exact registered UFunctions bind reflected thunks `141BEE900` and `141BEEA90`, and which retained callback/function identities in the selected match can reach those functions (including dynamic/reflected dispatch), with what mutation boundary?** Start from the two native registration bindings and the selected match's actual callback roots, not an unrestricted asset census. A negative answer for those two functions alone still must account for other direct init/reset/native construction routes before claiming a Niagara-wide exclusion.

If one enclosing supported callback is found, determine whether its caller already transfers completion ownership without retaining its stack. That specific transfer would make a production-boundary RED possible: mutate its target/binding before application entry, prove rejection before the original callback or constructor executes, preserve the unchanged callback exactly once, and use the existing complete-B cancellation boundary. If no such transfer/domain is established, keep G1 open and the observer forwarding; do not repeat an unchanged zero-hit live diagnostic or weaken exact debris/callback policy.
