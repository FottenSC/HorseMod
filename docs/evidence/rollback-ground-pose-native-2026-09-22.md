# Ground pose preparation boundary

Native reads use the existing SoulcaliburVI.exe Ghidra program. These are static findings, not a reproduction of skipped movement in A617/C624.

The world setter141DAE0A0 delegates through141DACB80,141DABF80,141DA43E0 and141DA2820. The final location/rotation operation can return unchanged for small deltas; it also converts between quaternion and Euler representations. Scale setter141DACA60 can return unchanged. Setter completion therefore does not prove restoration of the captured world transform.

Pending callback14089FF80 enables simulation and then reads root and child translations at280..288 to construct the impulse dispatched through virtual550. Pose admission must occur before this callback becomes executable. Repeating placement initialization is not a repair: it would repeat RNG and lifecycle work.

Native141DA2820,1403D3030 and141DB0600 establish relative location2C0, relative Euler rotation2CC, cached relative quaternion2E0, cached relative Euler rotation2F0, relative scale300 and ComponentToWorld270. Transform flag240 bit0 is the current-cache bit. Parent1D0, socket238 and absolute-channel bits1..3 affect composition.141DB0600 itself uses a near-equality comparison before writing ComponentToWorld.

Propagation141DA79D0 defers through scoped movement3D0/3D8, otherwise invokes bounds refresh141DB03B0. Registered components additionally fan out to physics/navigation/render consumers. Bounds refresh uses virtual448 and writes24C..267; for an unattached static mesh the verified target141DBE830 consumes its mesh asset, ComponentToWorld and bounds scale66C. Root target141D94780 produces zero extent at the supplied world translation. A repair must preserve the relative/cache state and refresh dependent bounds, under private ownership guards, rather than overwrite only ComponentToWorld.

GC cluster behavior is conditional and deferred: material assignment can add a mutable object only when the child has a cluster association. No cluster leak or deadlock is claimed. Duplication callback1403C8F30 must not be interpreted from its existing PostEditChange name; it forwards duplicate mode to virtualC8.

## Owned cold-state installation

The implemented repair captures the same120 auxiliary bytes as Sc6ReplayWorldState: bounds24C(28), world rotation cache2A0(28), relative values2C0(24), relative rotation cache2E0(28), scale300(12). Together with captured ComponentToWorld and low four transform flags, these preserve the native producer state without recomputation, normalization or RNG. Both captured and destination hierarchies must be unattached; installation additionally rejects registered/physics/render ownership flags, cached world, tasks and movement scopes. Captured bounds are restored with the pose, so native bounds recomputation is not needed while the graph remains private. Exact preparation admission checks all copied channels. Future native physics/render publication still needs its separate typed ownership contract.
