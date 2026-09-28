# G1 Niagara spawn producer boundary: refined no-go

This refines the [initializer audit](rollback-g1-niagara-init-boundary-no-go-2026-09-24.md). Production/test code remains unchanged; no build, live run, hook installation or Ghidra database edit occurred. [Fresh native reads and parent corroboration](rollback-g1-niagara-spawn-boundary-no-go-2026-09-24-raw.json).

## Proven route and limits

The generic registration function141D58BA0 is not a Niagara identity. Cascade particle and ground/debris producers also call it; their virtual+280 may resolve to different implementations. In particular1408A3920 SpawnLuxParticleSystemComponent constructs a Cascade UParticleSystemComponent. Its existing spawn hook cannot be counted as Niagara coverage.

The concrete Niagara producers are141BCC5A0 and141BCC730. Fresh decompilation confirms both call141BA5A20 before registration. That helper calls class accessor141BD3F90 and StaticConstructObject_Internal. The parent's independent read resolves the class literal to NiagaraComponent, size860. Both producers set asset/template at+808; if already registered they rebind the instance asset, set its reset byte and dispatch instance+120 callbacks before141D58BA0. The location producer then sets absolute transform flags/pose; the attached producer attaches the component and applies its pose. These are required native effects.

Parent read-only evidence maps direct callers141BEE900 and141BEEA90 to reflected UFunction parameter-unpack thunks. Thus script dispatch provides a static route to the producer; it is not inherently a task-group-specific continuation. This static chain does not demonstrate invocation in the passed combat segment or prove a live object's vtable/owner identity.

The common registration path141D58BA0 writes component+1C8 world and can call virtual+368 before141D42EF0. The latter calls component virtual+280 when+188 bit0 is clear, then render/physics registration and callbacks. Consequently an initializer141BC65E0 entry witness is too late to cover the preceding producer's object creation and registration changes. The safe producer boundary must precede construction, not just initializer allocation.

## Existing host ownership audit

| Existing boundary | Actual scope | Missing Niagara guarantee |
| --- | --- | --- |
| Schema::particle_spawn_rva8A3920, DeterministicHookSet::ParticleSpawnDetour | Cascade spawn hook; ordinary path records semantics and forwards original call | Neither Niagara helper nor its reflected thunk enters this hook. Historical suppressed-presentation/shadow paths are not an owned Niagara rollback protocol and must not be repurposed to suppress required callbacks. |
| Sc6ReplayParticleComponentOwner::NativeRegister at1D58BA0 | Private reconstruction owner; surrounding admission and postconditions constrain exact Cascade table335DB28, indexed leases and inactive state | This controlled call does not intercept arbitrary native/script registration and cannot lease a newly constructed Niagara owner. |
| NativeReplayCallbackAdmission | Latent action, queued delegate and streamable-load admission/stamps | Synchronous reflected UFunction execution is not covered by these producer stamps. |
| NativeReplayPrerequisiteGuard and component tick admission | Retained selected roots/mesh prerequisite contracts; queued Niagara ticks rejected | No pre-spawn closure for arbitrary script execution nested inside an otherwise admitted callback. |

Source searches found no production hook/admission at141BCC5A0,141BCC730,141BA5A20 or their reflected thunks. Existing completed-application birth/retirement journals are not a proof that uncaptured Niagara object/instance/interface creation can safely complete and undo.

## Decision and smallest resolving question

**No-go for adding a hold, early return or recovery claim.** There is no established pre-spawn Niagara owner/continuation to exercise in a truthful local ownership fixture. A RED fixture that supplies one would assume the missing mechanism. Exact/Unresolved debris and required callbacks remain unchanged.

The remaining question is now narrower than the initializer caller search: **which enclosing supported application callback invokes either reflected Niagara spawn thunk, and can that callback be retained before141BCC5A0/141BCC730 executes construction, with its real component/world/asset lifetime and completion ownership?**

The smallest diagnostic would record that enclosing caller/task/application identity at the actual Niagara producer entry, without suppressing native work and with exact debris state. Before deployment it needs verified entry ABI/signatures and production tests proving original forwarding and balanced nested-call ownership. An initializer-only sentinel cannot answer the earlier producer question. Absence of calls in one run would be observation, not permanent enforcement. The selected prerequisite B217+120 proof remains valid in its own scope; general G1 remains open.
