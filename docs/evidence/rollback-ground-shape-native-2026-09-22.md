# Ground shape state and publication dependencies

Primary engine evidence and saved Ghidra edits are retained in [the native record](rollback-ground-publication-native-2026-09-22.json). The existing SoulcaliburVI.exe program was used; no second program was imported. The PhysX findings below come from read-only PE/Capstone inspection of the shipped DLL and the actual SDK fixture, not from saved PhysX Ghidra annotations.

## Native identity and ownership

Engine141FF5660, renamed `BuildBodyCollisionFilterData`, builds simulation words at output+4, simple query words at+14 and complex query words at+24. Assembly141FF58C4 reads owning actor+C;141FF58D2 reads component+C. Simulation word2 receives the component index; query word0 receives the owning actor index.141D30940 derives collision masks and channel/filter bits.14200E670 forwards these packets to shape virtual98 andA8. A blanket historical filter copy would install a stale component identity in a new owner.

Engine141FEDEE0, renamed `PublishBodyInitActors`, publishes through an aggregate or scene virtual50. The aggregate scene mismatch branch returns silently. Its caller141FF9100 supplies scene locks330/338 and subsequently calls141FF9700 to initialize body properties. Replaying that initializer would overwrite captured properties. The saved publisher prototype uses verified16-byte actor-array and98-byte partial helper types; only this independently verified function received those new types. Other native entries may also be under concurrent annotation; no exporter was run and no unrelated type was changed.

## Shipped shape consumers

NpShape vtable is DLL+19EE00. Metadata constructor88A0 reads geometry kind at output0,48-byte geometry holder at4, local pose34, simulation filter50, query filter60, contact/rest offsets70/74, flags78, exclusive79, name80, reference count88 and user data90. The packet omits name and reference count. Material-count/getters are55650/55680 (vtableC0/C8); material pointers and user data are dependency witnesses only.

Pose getter55440 reads shape+70 unless buffered-pose bit4 is set. Setter55370 normalizes the quaternion before writing, so it cannot guarantee an exact historical round trip. The implementation installs the captured28-byte pose only after the actor/shape private guard, no buffered shape writes, no scene/BodySim/query handles, and the enclosing engine queue checks. Scalar/filter setters55490,554D0,55800,55830 and558D0 retain native semantics. Geometry, native material pointers and asset user data are never written from historical storage.

The source must contain exclusive simulation-only or disabled shapes. Geometry/material/user-data dependencies must match the newly constructed shape. Geometry comparison excludes the triangle geometry's unknown alignment bytes. Simulation component and query actor identities are separately validated and rebound; collision policy remains captured source state. Native material coefficient changes are not currently separately snapshotted by this shape packet; dependencies are required to remain the leased asset instances. This is not a general mutable-material restoration claim.

## Executable evidence

The existing SDK fixture first failed at reconstructed shape0 property3C after body restoration alone passed. The implemented shape packet passes that case and rejects historical component IDs, wrong owning actor IDs, foreign materials and changed geometry before shape writes. Exact public body continuation matches an independently authored scene for120 ticks through two kinematic targets, simulation enablement, two linear impulses and two angular impulses. The shape packet's immutable receipts, source archive and native logs are in [shape-state evidence](rollback-ground-shape-state-2026-09-22.json).

These are local native state/ownership proofs. They do not establish engine scene/render publication, ground callback execution after A publication, dynamic-phase reconstruction, the particle-event admission gate, complete recovery/reset, visual coherence or sustained rollback performance.
