> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Ground-debris ownership: bounded integration plan

The late matrix commits 5574 and 6417, then cannot capture complete B6538 for
the next seek. Ordinary playback reproduces the same rejection. The first
new body belongs to `LuxGroundDebrisComponent_7.StaticMeshComponent_35`.
Triangle geometry admission alone cannot repair this ownership difference.

## Evidence and classification

| Dependency | Proven evidence | Remaining question / smallest observation |
|---|---|---|
| Component lifecycle | Native 14085C7E0 constructs a fresh component, class size A20. 1408A4860 advances fixed-step controller/lifetime state, dispatches deactivation, and can destroy the component. Earlier descriptions of a pooled actor are inaccurate. | Verify actual root/child membership and registration before admitting a private B root. Native destruction must remain a rejection unless a reversible lifecycle owns it. |
| Controller | 143078D00 replaces the type-erased callback. Vtable 143510A68 slot60 is 1430F4C60: invoke stored function+10 with owner+8. 1408A47E0 invokes it on a subsequent component tick. 14089F9E0 selects 14089FF80; the latter enables simulation and applies impulses. | Retain the controller owner and native clone/cleanup contract, including replacement during resumed execution. Do not replay completed activation or RNG draws when publishing B. |
| Motion / collision | Candidate cbb4bb1ded104fa89c80ff997d84b501: rigid flags2, convex flags9, disabled triangle flags0, no query handles. Simulation filter ffff/25/43f2a/a00000 requests no notification itself. | The retained complete B6538 inventory has70 shapes/53 actors, five free debris bodies and no simulation-enabled notification requester. Actual native filter accepts85 eligible pairs with401/no notification. This closes the observed packet only; production admission must enforce the same consumer exclusions. |
| Filter consumers | 141FE93C0 builds simulation data and publishes through shape slot98; slotA0 reads it. Query data uses A8/B0. 14204CF60 combines both partners' notification bit8 and collision masks. Kinematic-only suppression is not universal. | If any accepted contact can reach gameplay callbacks or another authoritative dynamic body, trace that consumer and preserve its future state. No cosmetic exemption based on owner name. |
| RNG / logical slot | 140895CC0 consumes CRT RNG for ring placement. VFX allocation paths can bind deactivation and publish persistent slot IDs. A separate reflected construction route also exists. | Establish which route owns this root and whether its delegate is empty or bound. Keep slot/delegate/clock state authoritative even if motion is reconstructible. |
| Physics removal | Native scene removeActor (408C0/46160) removes scene/query membership and BodySim work. flushSimulation (42250/4CAD0/E5800) clears reports and other scene-wide state. | No global flush bypass. A reconstruction path needs quiescent preconditions, exact unaffected owners and complete B recovery. Private lifetime retention does not preserve destroyed solver state. |
| Render ownership | Existing transaction retains B render references through final GPU completion and deferred retirement. | Apply that ownership to actual debris meshes only after native component/body quarantine is understood. No historical lighting capture or pixel-equality requirement. |

## Next transaction

1. Finish the bounded collision-feedback inventory. This is diagnostic-only
   storage, not a snapshot participant or permission to admit geometry5.
2. Choose native reconstruction for physical motion only if the consumed-input
   audit proves no gameplay feedback. Otherwise implement the identified
   physics ownership; do not silently omit it. Keep logical root lifecycle,
   controller, RNG and slot ownership in either design.
3. Integrate one private B root with the existing transaction: retain B owners
   and storage, exclude their scheduling and physics safely, publish A, execute
   C, then undo to B before irreversible commit. Local controlled-native tests
   must cover partial acquisition, publication failure, repeated cleanup and
   poisoned operations. Do not free resources on uncertain native completion.
4. Run the existing A170/B6538 to C2510 after-drain cancellation. Require full B
   recovery plus 120 independent continuation ticks, poses and HUD; inspect
   coherent actors/effects separately. Only then resume remaining late matrix
   cases and compare identical targets under 1 GiB and diagnostic 2 GiB.

No captured bytes have been added for debris. The current failure diagnostic
uses bounded stack scratch and native getters only; it must be removed after
closing this question. Any production owner must charge simultaneous retained
state, B undo, operation scratch and deferred retirement. No completed-seek or
rollback performance claim follows from these diagnostic runs.

Retained evidence:
`build_cmake_LessEqual421__Shipping__Win64/replay-tests/native-B6538-debris-active-physics-failure-audit.json`
links the actual raw log, report, exact binaries and reconstructible sources.
Cleanup completed with zero games. A was never published; complete B capture
and recovery were not demonstrated.

## Native retirement result and remaining admission

Shipped-native 1389D0 then138A70 completes queued island-node retirement synchronously. The existing physics fixture now uses a real Foundation and tracked allocator: removal first leaves one pending node, both passes preserve the other owner, clear the removed owner and allow its handle to be reused. Native destruction releases every allocation (125116 peak bytes). This does not exercise complete NpActor/body/broadphase removal, solver execution or the replay transaction. See debris-native-island-retirement-audit.json and its retained raw regression log.

Native 142028750 publishes body poses through non-sweeping component MoveComponent, then owner CheckStillInWorld. Before treating debris motion as reconstructible, require the verified plain mesh virtual routes, physics-only collision mode, empty previous overlap array, no attached children or physics-volume route, and an unchanged owner root distinct from the debris mesh. The observed filter packet alone does not establish these facts. Native controller/lifetime/deactivation delegates and VFX slot state remain authoritative regardless of motion classification.

Native removeActor queues node and ID retirement. Global flushSimulation clears unrelated scene outputs and is not a substitute for ownership. The next integrated path must retain B root/children/body/render ownership, prove selective native removal and re-add with unaffected core membership, and preserve B until C completion/commit. If any feedback or lifecycle guard cannot be satisfied, keep the rejection and identify the consumed state rather than enlarging snapshots speculatively.

## B6538 consumer inventory, 2026-09-14

Candidate889c793e81c841e097a954065366ca1a (runtime47003a5f/observere085d436) verifies all five child meshes use vtable1436CEFB0, move141DA43E0, overlap141DB1200, update141DA6A20 and collision getter142043340. They have collision mode2, no attached children/parent, zero overlap and transform-delegate counts, no movement scopes, and WorldSettings owner143499188 with null root/CheckStillInWorld141C0FE90. These observations support the guarded motion-reconstruction route; production must enforce them, including collision-partner exclusions. They do not themselves implement a reconstruction transaction.

The root has autoDestroy1, mode2, lifetime4, elapsed0.05, one deactivation delegate and a live controller. Its native controller/clock/delegate and secondary VFX slot cannot be omitted. Native14089F5F0 searches manager+3F8 by exact component pointer, broadcasts the matching slot ID to+388 listeners, then removes matching records. The shared thunk1408954B0 is used by unrelated classes and was incorrectly typed as exclusively FrameInputLog; the neutral Ghidra type/name now reflects its virtual+600 dispatch.

Native root destruction140898E30 invokes140898A40 on the ring: destroys every mesh, clears material root flags, frees nested arrays and destroys the controller, then invokes native scene-component destruction. It is an irreversible lifecycle operation, not a pause/undo primitive. Native PhysX disable-simulation3CD70 ->3B380 also resets velocity/wake/force state; toggling the flag back is insufficient for complete B recovery. Prefer the smallest proven native body removal/re-add transaction, preserving B values and original root lifecycle until irreversible commit.

Retained native-B6538-debris-consumer-inventory-audit.json includes actual raw lines and exact sources; cleanup complete/zero games. No A publication, complete B capture or120-tick recovery proof.

## Selective body retirement result

The existing shipped-native fixture now creates a real scene with two dynamic boxes contacting opposite sides of a static box. Native removal of one dynamic body, synchronous island passes1389D0/138A70, AABB update150290 with private completion callbacks, postprocess150CF0, scratch retirement156BB0 and ID retirementE75D0/FDB00 remove its pair and endpoints without entering simulation. The other contacting body's native owner and public values remain unchanged. Re-add preserves the removed body's public values and a subsequent native update recreates its pair. All tracked allocations retire:718388 peak bytes, zero outstanding, zero native errors.

The fixture defers native task dispatch: submission returns with completion pending, remains pending for three polls, then both continuations release exactly once after dispatch drains. This is controlled single-threaded completion evidence, not concurrent task or application responsiveness proof. A first contact attempt exposed an incorrect C-style filter return ABI; native125790 passes a hidden ushort result pointer. The corrected callback is exercised by actual collisions. The failure, bounded fault trace, successful raw regression logs and reconstructible sources are retained in debris-native-selective-retirement-audit.json. The completed fault probe was removed.

Production integration remains blocked at the complete-B boundary. Preparation currently assumes failed CPU capture performed no CPU mutation. Body quarantine must therefore own pre-capture B state and its pending retirement, restore it on preparation cancellation/failure, and remain accounted until recovered or explicitly committed. The root controller, clock, deactivation delegate and manager slot remain authoritative; retaining the Np body alone is insufficient. Scheduler exclusion and retained render owners must cover the same root/children. No production rejection has been bypassed.

## Preparation cancellation and remaining integration boundary

The actual production preparation prefix did not inspect an accepted cancellation before CPU acquisition. The local regression fails on that implementation, then passes with cancellation routed to its existing retirement path before new acquisition. Pending render work still drains; wind undo failure remains pending and retryable. Full build/native suites and271 runner tests pass on runtime5361c0e1/observere085d436. See debris-preparation-cancel-audit.json for retained raw logs, executable production-prefix RED/GREEN fixtures and reconstructible build sources. No live restoration is claimed.

ReplayPhysicsTaskCompletion now supplies the shipped task prefix and virtual ordering to the native fixture. Its atomics distinguish partial completion from the final reference, prevent reference underflow/reopening, and keep a premature release from reporting completion. It is not yet connected to host body quarantine. The enclosing future owner must retain an invalid or pending receipt and every referenced body/allocation; the receipt alone is not lifecycle recovery.

Further native facts needed by that integration: root constructor860E30 calls UPrimitive base constructor141D8DF80, which constructs a separate +7B0 tick with CanEverTick cleared. Root primary +110 is enabled by the derived constructor. Admission must check both actual tick prefixes, not assume primary exclusion covers the inherited tick. Ground deactivation binding533F40 stores a16-byte reflected delegate (weak object8 plus FName8) in root+810; the supplied native thunk argument is unused there. Validate the actual reflected manager binding and retain its owner. No new historical snapshot participant or state admission follows from these findings.

## Causal correction: pending unrelated physics work

The earlier public-body comparison was insufficient: full AABB150290 advances the peer SAP endpoint values while retiring one body. The retained endpoint RED demonstrates this without a native simulation step. The corrected removal-only packet calls SAP1569C0 with NpScene+730 context itself, as forwarded by the single-worker150290/150880 path. Passing context+7B0 instead produced invalid scratch frees; that failure is retained. Corrected execution preserves unrelated pending update bits and endpoint values, recreates contact after re-add, and retires all tracked allocations.

Native1570E0 stores packet.removed at SAP+90. The native fixture proves that pointer remains after task completion/postprocessing/scratch retirement and is replaced by the next actual native simulation update. Therefore the immovable packet owner must remain retained beyond completion; timeout is not cancellation or release. Native scene destruction is the alternate lifetime end. The future enclosing transaction must account this fixed storage through deferred retirement.

A controlled read fault exposed duplicate unaffected actor membership accepted by the production guard. The retained RED and fixed GREEN are linked by debris-removal-only-ownership-audit.json. This is admission/retirement proof only: complete B6538 root, clock, controller, reflected delegate, VFX slot, scheduler and render ownership remain the integration boundary. Do not use the earlier full AABB path as selective quarantine; it remains only fixture teardown.

## Independent physical continuation and assembled boundary

The existing native fixture now constructs a second real scene from identical authored initial conditions. Only the candidate removes/re-adds one body. The unaffected body matches120 subsequent native metadata observations (pose/body/dynamic fields28..DF; process-local actor bindings and concrete type pointer excluded). No observations are installed. Two scenes peak1391544 native allocator bytes and release every allocation. debris-removal-native-continuation-audit.json links the actual positive120-tick marker, build/source archive and raw log. This remains below the game replay qualification boundary.

Root vtable1433566F8+300 points to native1408A4860. That tick calls base UActorComponent TickComponent, advances the ring controller through1408A47E0 and auxiliary state, updates lifetime, broadcasts deactivation and may invoke destructive component teardown. Static mesh child vtable1436CEFB0+300 uses base TickComponent. These routes explain the required primary scheduling exclusion and dormant child/secondary admission; they do not establish those live tick flags. Existing VFX PrepareTopology rejects live secondary suffix owners absent from its particle delta. Existing AppendQuarantinedTracePrimitive accepts a trace-owned skeletal proxy only, so static debris needs explicit native binding through the existing render owner, not a relaxed trace check.


## Assembled queue failure and bounded correction

Candidate251f08412a5d48b59c6abefee02375b0 (runtime0830d8aa/observer723dc50e) rejected engine_physics_queues atB6538 before body removal or A publication. Read-only inspection of the same paused process identified five already-consumed body-transform outputs, empty deferred/substep maps and disabled async storage containing an uninitialized pointer. Cleanup incorrectly required the rejected domain despite no native mutation; local actual-method RED/fix/GREEN now permits untouched-B recovery while preserving mutated/poisoned guards. Process cleanup completed with zero games; no game B recovery was demonstrated.

Native142024D10 calls142029B70 after successful fetch;142029B70 clears/rebuilds FPhysScene+310/+330, and142028750 consumes them before the completed application boundary. The new bounded owner privately retains original B arrays, exposes empty native C storage and restores original B arrays after completed C work. It rejects foreign B bodies, duplicates, aliases and partial publication; uncertain native retirement remains poisoned. The two C buffers reserve131072 bytes, plus original B capacity. This does not enlarge historical lighting or allow missing proxies. Native root commit still rejects pending EndPlay/material-root/lifetime disposal admission. Ground-publication-transaction-audit.json links raw live failure, exact reconstructible build, read-only headers and local regressions.


### Assembled B capture and retirement admission, 2026-09-14

Candidate81530575530a4e0c8d5fc96a01a78f6e (schema100) passes complete B6538 capture after native debris removal. CPU checkpoint11688550 bytes, shared scratch5314954 bytes, admission total282801630 at the complete-B receipt. These are conservative tracked snapshots, not full native allocation peak or completed-seek cost. The next guard rejects birth_retirement before A publication; cleanup completed with zero games. Independent recovery is not demonstrated. See retained ground-birth-retirement-failure.json/raw log.

The preceding emitter failure is resolved at capture: Ice_Crack_01 has native Velocity_Seeded8 + Collision4 payloads.141FD7E90 computes the seed extent,141FD3D90/141FA0850 initialize two scalar words,141FDB3A0/141FDD210 consume the stream for particle velocity, and141FBDD10 resets on looping. Both module identities now have explicit leases. The existing full instance buffer supplies restore bytes; initialization is not replayed. Native offsets must give disjoint complete coverage. External collision impulses remain rejected. Local production RED/fix/GREEN is retained in seeded-collision-admission-audit.json.

A separate actual-code local regression confirms that PrepareHistoricalRestore's inner failure step published Failed while its outer requested preparation still owned detached B. The fix preserves Preparing until outer GPU/wind/ground recovery and release finish. The original failure identity remains recorded. A new terminal receipt follows resource recovery; it is not an independent continuation oracle. cpu-preparation-terminal-audit.json contains exact source and RED/GREEN evidence.

Current retry only distinguishes the first birth-retirement check and verifies preparation recovery reporting. No birth-lifecycle guard has been relaxed, no additional captured state was added for retirement, and irreversible private-debris disposal remains unsupported. After that blocker is resolved, the same assembled drained-cancellation must recover B and match120 independent ticks before commit/durability/performance expansion.


Element-ID tail checkpoint: see ../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/element-id-tail-audit.json. PhysX FF080 adds delayed IDs and deletion bits; FDB00 drains the list without clearing bits; F1ED0 clears the element bitmap after FDB00. The selective operation now proves all four complete bitmap/list sets belong exclusively to selected removals after joined tasks, then performs that missing tail. It does not invoke F1ED0 active-body callbacks, other world work or scene epoch increment. Native fixture reproduces omission before fix; unowned bits/lists still reject. A partial-acquisition retry separately reproduced stale local ID witnesses and now resets them before fresh capture. Native GPU/render/recovery proof remains the assembled experiment, not this local fixture.


Dirty-node causal checkpoint: native14A890 marks removed-edge endpoints in bitmap178 and node bit10;1389D0/138A70 run149070 with dirty traversal false. Native third-pass task137D60 uses149070 with traversal true. Local selected-body contact removal reproduces retained kinematic5->15 in both graphs, matching the class of live6->16. The guarded operation now requires initially quiescent dirty/input queues, exact retained kinematic or owned deleted endpoints, no queued edge/node work and no ready-to-sleep dynamic peers before native149070(traverse=true,deactivate=false,limit1000). No simulation/world epoch advances. Local production RED/fix/GREEN and120 independent native continuation pass; native allocator peak1401688/zero outstanding. No new snapshot fields. Current game proof remains blocked until the assembled retry; see kinematic-node-retirement-audit.json.


## Assembled C2510 cancellation: damage-player backing

Candidate20e38e96c76047a7be53464f4f3875e2 confirms the first recovery rejection at damage effect3: widget/image/slot/animation and current C backing all match; C active capacity is0, while retained B6538 requires1 player. Complete B is retained and cleanup completes, but recovery and continuation fail. `ground-hud-capacity-failure.json` points to the retained raw log and reconstructible diagnostic build.

Native141764AC0 is the already signature-verified pointer-array allocator: it quantizes pointer capacity and calls FMemory_Realloc; zero frees backing without animation evaluation or callbacks. Keeping the original B array private is smaller than reconstructing animation state. Existing B player0x780 images remain read-only witnesses and UObject leases; no expected state is installed. The native C list may shrink independently. Recovery admits only an empty, validated C list, releases that list synchronously, and returns B's exact original allocation. Commit releases the private pointer backing once; the existing player lease/GC lifecycle is unchanged. A malformed free result poisons the operation and retains the allocation/leases instead of retrying a free. Active discarded C damage evaluation remains unsupported on this recovery path until it has its own native retirement proof.

Schema102 implements this bounded backing owner. Native capacity is reserved before publication (at most2048 bytes across16 slots, plus fixed metadata); no new player image, GPU resource or readback is added. Actual production installation and ownership-method regression first fails against the diagnostic production source after native shrink, then passes with the change. It exercises repeated recovery/commit, foreign identity/player membership, alias and partial acquisition rejection, active-C rejection and poisoned free behavior. Local evidence does not establish game recovery; the same assembled drained-cancellation retry and independent B+120 remain required.

## Native debris commit ownership, 2026-09-14

The assembled Render::Drained cancellation now independently passes B6538+120 gameplay/callback/Lux/HUD continuation (ground-hud-recovered-audit.json). This proves recovery for that case, not irreversible disposal or visual coherence.

The commit implementation uses native root destruction140898E30(false) only after target application/render tails complete and the explicit seek-wide commit decision. Its native898A40 ring cleanup destroys child meshes, removes MID roots, frees material arrays/ring and destroys controller storage without dispatching the old deactivation delegate. The auxiliary898940 branch is admitted only when empty. Native component ownership, EndPlay script emptiness, nav/callback/weld absence, detached BodySim state, exact vtables/signatures and allocation disjointness are preflight requirements. Private B output buffers remain unchanged through preflight. A rejection retains B recovery; uncertain irreversible calls poison retirement and cannot be retried as successful cleanup.

Retirement releases joined physics input bindings and B output backing before native component destruction. UObject leases remain until the host's existing ordered CompleteRetirement GPU drain and render-thread Finish have completed. No additional historical snapshot is introduced. Fixed4096-byte retirement scratch is reserved; this does not establish total native allocation high-water. Disposal may enqueue native proxy/RHI work, whose completion is required before lease release.

Build fd613578/observer907b8c58 and278 runner regressions pass, including actual production-method admission/partial acquisition/poison/retry and actual host GPU-release ordering with controlled native edges. Two shipped-native physics fixtures independently reproduce re-added and unaffected bodies for120 ticks. These are local results. The bounded committed A170/B6538->C2510 live attempt is in progress with coherent-image capture; its readbacks preclude production performance conclusions. Direct game debris trajectories, coherent images, committed continuation and broader checkpoint coverage remain separate evidence gaps.


Native empty-root admission: candidate d3fb44063b8b4c1c8ecd474d89d07fd0 reaches completed C2510 tails and rejects commit_root_physics before irreversible commit. Root and mesh vtable entries were re-read from the native binary and match.141DA5E40 calls670/678, tests BodyInstance F0/F8/210 through141FFB640, then clears bit4 through141D4FD70.1420431D0(false) returns the inline BodyInstance430. The root may therefore carry created-state bit4 with no body. The replacement requires empty root actor/2D slots plus the common no-weld/constraint/nav/child/callback guards; it does not clear flags or copy expected state. Actual production regression fails on the old blanket flag rejection and passes with empty-root admission, while live body slots and each dangerous dependency still reject. Live admission/disposal remains pending. The retained held2510 image is visually coherent; no resumed-native visual proof yet. See ground-root-physics-preflight-audit.json.

Navigation admission: candidate2cf96fe1535e45d3b49854f9ebd81158 passes empty-root physics admission and rejects a navigation flag before commit. The earlier retained five-child inventory already records scene_flags=a2c0607. Native141DA66A0 unregister calls141C4F7D0, which resolves NavRelevantInterface and the actor world, then returns when world+E8 NavigationSystem is null. WorldSettings virtual138 is independently verified141C204E0. Current admission retains every native flag and requires this exact getter to resolve the retained current world with null navigation owner; changed/live navigation state still rejects. Local actual-production RED/fix/GREEN passes, with no additional checkpoint bytes/GPU work. Live replacement and irreversible native disposal remain unproven; see ground-navigation-preflight-audit.json.
