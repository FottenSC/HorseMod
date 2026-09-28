# PhysX copied body flag and writer reachability — 2026-09-28

Read-only audit for the existing changed-free-body regression. The checkout was
`8bec85ddfd76add9d7031c8a81651414c153767d`; dirty and untracked work was
preserved. The installed `SoulcaliburVI.exe` SHA-256 was
`f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`.
The installed `PhysX3_x64.dll` SHA-256 was
`c3c7bd6e13260ee5f850552b6e9a15f69e58e0b41cd4f4bcb48e1b419719b5a2`.
The game program inspected through native Ghidra MCP was the existing
`/SoulcaliburVI.exe` at image base `140000000`; no second program was imported
and no database edit was made. The DLL instructions below were disassembled
read-only from the installed PE with Capstone, using file RVAs. No game was run.

## Copied flag provenance

`PhysX3_x64.dll+0x7810` calls `+0x74D0` at `+0x7820`. In the latter routine,
`+0x7570` places the output buffer's `+0x9C` address in RDX and `+0x757A`
calls the body's virtual `+0x178`. Thus the byte tested by
[`physics_domain`](../../HorseMod/horselib/deterministic/ReplayGroundCollisionDomain.inl)
at copied-value `+0x9C` comes from the native body-flags getter, rather than a
stable admission token. The relevant disassembly is:

```text
PhysX3_x64+0x7820  call PhysX3_x64+0x74D0
PhysX3_x64+0x7570  lea rdx, [rdi + 0x9c]
PhysX3_x64+0x757a  call qword ptr [rax + 0x178]
```

In the game program, `141FE97F0` reads the same virtual `+0x178` flags,
modifies bit 0 according to the requested simulation state, and writes them
through virtual `+0x170`. `141FE6730` reaches that setter while taking and
releasing scene virtual locks `+0x330/+0x338`. The lock serializes this
operation; it does not protect the interval after application-entry admission.
`142007730` invokes this route from body instance state. `142001C40` can
detach the component and call another helper *before* it changes body-instance
`+0x74` bit 2 and invokes `142007730`; a guard after that point would miss
earlier effects of the enclosing operation.

Direct callers of `142001C40` in the existing game program are `142053F50`,
`1420F72B0`, `14210D1A0`, `14210DFC0`, `14210E8F0`, `14210FB20`, `142111C00`
and `1430F9C20`. The reflected thunk `1428199E0` forwards a boolean to
`14210DFC0`, which iterates skeletal body instances and passes that boolean to
`142001C40`. `142818150` and `142819E10` reach other skeletal routes.
`1430F9C20` passes simulation-disabled, so its presence alone does not show a
kinematic-to-free transition. None of these static callers establishes an
invocation, body binding, thread or phase at A617.
Registration data at `143B1E500` points to the
`SetAllBodiesSimulatePhysics` name at `143B19718`, immediately before the
`1428199E0` thunk pointer at `143B1E508`. This identifies that reflected
route, but not a call from an active cooked Blueprint. A raw exact-name scan
of the available Battle/Stage files found no hit and is not bytecode or
runtime reachability evidence.

## Ownership consequence

[`ValidateUpdate`](../../HorseMod/horselib/deterministic/Sc6ReplayHost.GroundDebris.inl)
checks the collision inventory at application entry and releases each scene
lock before returning. [`DispatchTask`](../../HorseMod/horselib/deterministic/Sc6ReplayTaskGroup.cpp)
checks start-task callback/vehicle identity, not that inventory; the substep
branch bypasses consumer hooks, and retained-task resume does not repeat the
ordinary dispatch predicates. The current selected
[`exit-136 regression`](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/50f76bfae511e4fd9e28a250afb3937cb964abf9d2b57b4e5c9fc6560a2248bb.log)
and [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/99622bfa5aaf01f8a57bfd6f1a16040a100298604ffc943cb723c82a3a2aeaa9.json)
remain the compatible **pre-documentation** production-boundary result; its
[source snapshot](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-789cfc29917b00bb94befba780201422e7e021d7f850548cbbb4bb760dc68a36.json)
retains the tested dirty/untracked source. The fixture proves a changed flag
can pass entry admission then reach the native-entry probe; it does not prove
that this transition occurs at A617 or model native completion and B recovery.

The bounded Astra Xhigh review with Daybreak disabled independently checked
the production paths and the game's task/event ownership. The normal tick
binding (`142163BA0`), completion (`14215D250`/`14215ED20`), world start
(`142018570`/`1420297F0`) and substep B0/C8 chain
(`1420555E0`/`142055C40`/`142055BC0`) still provide no verified
abort-to-quiescence operation for a rejected popped task. Destructor
`142158AF0` only releases an event reference. Existing trace and prerequisite
guards apply to different task contracts. Holding, dropping, terminally
vetoing or falsely completing this task would not recover complete B.

## Smallest resolving check

Bind one retained unrelated actor to its real component/body and test whether
an enabling writer or actor insertion/replacement can run between entry
admission and the last affected start/substep/resume consumption. Start with the
reflected `1428199E0 -> 14210DFC0 -> 142001C40` path: inspect its actual
receiver, invocation and enclosing task phase before its first effect. One
feasible post-admission invocation with the matching body falsifies structural
exclusion. Absence in a static name scan is insufficient, and closing this one
route would leave other writers to check. If exclusion fails, a native
task/event/application settlement path must be proved before production change.

**Implemented:** no new behavior. **Locally tested:** no new test; the selected
regression remains RED on production/test source unchanged by this audit.
**Demonstrated live:** nothing new. **Unproven:** A617 writer reachability and
actual effect lifecycle, safe task settlement, complete B, debris-motion
consumer closure, A617→C624, rolling qualification, render coherence, memory
and full-update cost. No build, deployment or live experiment occurred.
