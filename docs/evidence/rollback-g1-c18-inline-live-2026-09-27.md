# G1 C18 entry and listener diagnostic live result — 2026-09-27

This was one bounded ordinary-forward `c18-diagnostic` run after the separate
zero-serial diagnostic gained native-supported inline count-one/capacity-one
handling and distinct entry failure codes. It selected and returned a C18
callback. The separate diagnostic rejected at `listener_identity`, after its
entry checks and before retaining any material rows. This establishes a new
investigation boundary; it does **not** establish that the live collection used
inline storage or that its weak listener was valid. G1 and G2 remain open.

## Local admission and exact run

Root ran `python tools/replay_test.py build`: PASS, both CTests, with [build
manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/0b954b79f68e4685150ff4d252f15f53b589a71eb630fd508edd2dfe2b907043.json)
and [console](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/da643fd0a6e26ac6c0eeb24192cec7d557411512ad206acca9416b4b26252051.log).
The full `python tools/replay_test.py local` gate passed **1,061 tests**:
378 unit, 447 native contract and 236 workflow. Its [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/8b598c58cda698bf30980e408e3ff0191b289018e619a5d82cf1a60a3d2d19d0.json)
retains the reconstructible source and its [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c7c19fb7d34dc25effd95765b46b7e853295f4e5a5a674b6919ac6c857a1011a.log).
Preflight was ready, with no game process, a clean seven-entry deployment
journal and Steam PID 9240.

Exact live command:

```powershell
python tools/replay_test.py c18-diagnostic --replay ReplayExample/REPLAY_12744704008398858106.bin --flush-startup-loading --no-async-loading-thread
```

The run ID was `replay-88afec7f229c4df5a594ad54bc4c1ab3`. Its
[stage manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/77b4bbb6f0a7571ecf296342c14eaca411c6399c7c1bc2ee0685d873b35ecc1e.json),
[captured report](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/0a3f74abeda093bf6e2494cd54e1665b9ba5f05983cfb9a58cd48300748c34e0.json),
[native game log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/64308ada0370894dcedd43a5ffdc65ff6b400a6225cddb7045e2de88d79035ed.log),
[raw observer sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/1ef05c575e77e3d709666a30a4b9164733950ed443283a4273f9a59842ea28c4.json)
and [runner console](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/97495393297b30af1a4db38a3501603324b39d6495b500bc12eb63abe73908b5.log)
are immutable. Startup flush completed seven calls at first tick 3; the native
loading state reports no async thread. The runner stopped on a run-bound
selected-return marker without reading the sidecar during publication.

SHA-256 identities: runtime `abd072c55086c3a9cf837fb849af5ca7ef26938c3dd118711a45bc2b68f64072`,
observer `6a278b8f8658ed0daf48075a071b2a15805023ef81af73637c820bb0a82a9ce8`,
framework `ca1b3d151efac51c70a4162abe59109d7973e35548bb5d79ed91122098bd132f`,
game `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`,
replay `95e12e394d35c13d5e0dd3dce692f9e0a4022e2a84205a9ec75f2fa6726d7879`,
UCRT `5c52e3a303baaac0e0af8bd9b96134993da34bc9d834a31ef37e1d2cdc7fe192`.
The report verifies the owned PID 448 mapped runtime, observer, framework and
UCRT at these hashes.

## First failure and investigation

The selected occurrence has entry sequence 5071540, return sequence 5071541,
thread 32580, collection 2212527630992 and descriptor 317237019216. The
reader bound those coordinates to the native stop marker. The raw game log
places its return in the runner's **setup phase at frame 23**, round frame
zero, between the frame-23 actor tail and next input publication. Thus this
run is an observer-shape probe, not a representative active-combat material
sample. The indexed hub
sample has serial zero. The ordinary provider census remains invalid/empty;
the separate diagnostic is `rejected` with failure `listener_identity`, no
header, no hub detail and no rows. Its `generation_unknown=true`,
`provider_census_valid=false`, `ownership_permission=false`,
`resource_completion_proven=false`, `rollback_admission=false` and native
retained-byte total null remain intact. A zero-row rejection is not an empty
material collection.

The ordinary witness displays count/capacity zero because its `serial>0`
identity condition short-circuits before header reads. These default zeros are
**not** a diagnostic header measurement. `listener_identity` occurs inside the
diagnostic listener loop, so the new entry checks passed and a listener row was
visited, but the accepted entry could be inline or heap backed. The failure
currently groups weak serial, indexed lookup/current serial, sampled receiver
identity and receiver vtable checks.

Root rechecked the existing `SoulcaliburVI.exe` Ghidra program. Binder
`14043D210` constructs a weak UObject one-argument callback with finalized
vtable `14374B760`, using `FWeakObjectPtr_FromUObject`. The vtable's checked
dispatch slot `+0x68` is `TryExecuteWeakUObjectCallbackOneArg` `1403EF060`.
That function resolves the weak target and returns false if stale; executor
`141D38300` can then compact a stale entry. The live `listener_identity`
failure may therefore be a stale listener rather than a bad layout. The raw
rejection does not distinguish them. No positive weak serial or lifetime rule
was relaxed. Root updated and saved directly relevant Ghidra capacity/storage
comments to reflect the diagnostic and this uncertainty.

The next falsifiable experiments are an exact listener failure discriminator
in the same separate observation branch and a combat-targeted selection while
the observer still starts before replay loading. Both require production-boundary
RED/GREEN evidence before another bounded live run, plus build and full local
checks. They must not treat a stale or unresolved listener as a routed material
provider or claim writer coverage from a later arm point.

## Outcomes and cleanup

Observer validity: the local gates and selected-return association pass;
listener/material sampling rejected. Simulation: no independent comparison.
Coherence: not measured. Recovery: no changed-consumer complete-B recovery.
Performance: no qualifying rollback update measurement. The process working
set peak is not a production-owned-memory accounting. The live stage's
`diagnostic_observed` result means only bounded observation and cleanup.

The owned game process exited with code zero. Root separately checked the
deployment journal: state clean, all seven recorded prior file hashes or
absences restored, no mismatches. The stage reported zero games remaining;
Steam PID 9240 was still running. No unchanged live retry followed.
