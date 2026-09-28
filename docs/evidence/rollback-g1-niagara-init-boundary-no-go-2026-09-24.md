# G1 Niagara initialization boundary: no-go

## Scope

The selected prerequisite mutation, complete B217 recovery and 120 independent continuation ticks passed in retained manifest `38161e181d174def7b5af165d12485aa4e792f84af63cdf774cd0e05bce4da54`. That proves this selected mesh prerequisite disposition. It does not supply Niagara initialization ownership.

This audit inspected current production admission against the September23 consumer-boundaries and component-proof reports. No production or test source, Ghidra database, native hook, build or game changed. The existing SoulcaliburVI.exe program was explicitly selected for every native read; EBOOT.elf was not inspected. [Raw native responses](rollback-g1-niagara-init-boundary-no-go-2026-09-24-raw.json).

## Concrete first-side-effect gap

`ReplayComponentTickConsumerAdmitted` rejects a generic component tick with Niagara component table `image+37fa218` or tick consumer `image+1bcdb90`. Scheduler capture validates an indexed owner before this predicate; actual `Sc6ReplayTaskGroup::DispatchTask` repeats the tick-consumer predicate before the native task wrapper. These are queued tick boundaries.

Niagara initialization `141BC65E0` is referenced at table entry `1437FA498`, offset280 from that Niagara table. Current admission reads the tick slot300, and none of the25 entries in `NativeReplayTraceTaskGuard.Signatures.inl` covers initializer141BC65E0, reinitializer141BC8620, interface rebuild141BC4D90 or source refresh141BC7F90. The selected prerequisite guard forwards task functions outside its retained mesh contracts/trace roots. A direct initialization nested inside another admitted native callback has no demonstrated before-entry ownership boundary. This is a coverage gap; this audit does not claim such a call occurred in the passed combat segment.

The initializer's very first call is `141DA5F50`, before instance allocation. Fresh native decompilation shows that prefix calls141DA6030, writes component+188 bit100000 and, when bit200000 is set, may dispatch virtual+3A0 and141C4F720. The parent independently corroborated that141C4F720 traverses the component+190 owner and dispatches further virtuals/141C54D50. A check after allocation, rebuild or interface refresh is therefore already too late for the complete initialization entry. The initializer then allocates/publishes component+810/+818, weak-binds asset+808, rebuilds the instance and dispatches instance+120 callbacks. Native reinitializer141BC8620 has direct callers141BC65E0 and TickNiagaraSystemInstance141BCC8E0. No void return supplies cancellation.

## Architecture decision

No safe continuation/disposition for the enclosing virtual initializer caller is established. Returning from an init/rebuild callback would suppress required effects. Treating the existing mesh completed-application recovery as permission to drain uncaptured Niagara births would assume missing ownership. Adding terminal entry rejection could diagnose containment but would not make the supported operation recoverable or complete G1.

No speculative code or regression was added. A fixture that merely invokes a new hook or supplies a fabricated owner lease would grant the missing mechanism rather than test it. Existing exact/Unresolved debris and callback rules remain.

## Smallest resolving native question

Which concrete component registration/activation call site dispatches Niagara virtual+280 during an application, and does its enclosing task retain a generation-stable component/world plus a resumable pre-call continuation **before**141DA5F50 changes state?

Resolve that single outer call route first. Direct xrefs show only vtable/data references, so a direct-call search alone cannot answer it. If the outer route has no such continuation, the next valid bounded diagnostic is a read-only first-entry record of caller/task/application epoch/component identity at141BC65E0, installed through the existing process-owned guard architecture and forwarding the required native call while all debris state remains exact. It must not return early or claim recovery. Hook ABI/signature and nested callback ownership would need local production-boundary verification before any build/live test.

G1 remains open beyond the selected prerequisite proof. No local RED/GREEN or new live qualification is claimed by this audit.
