# Ground body construction dependencies

Read from the existing SoulcaliburVI.exe program (x86:LE:64:default, Windows), with explicit program selection. These findings guide implementation; they do not establish reconstructed physics or qualification.

## Configuration

Native struct registration1422A9300 and constructor141FE7A50 establish a 0x230-byte FBodyInstance embedded at primitive+430. Registration1422AC760 describes a 0x30-byte FCollisionResponse: channels at0 and an owned array at20. Registration1423BE720 establishes ResponseChannel stride16, FName at0 and response byte at8. Native destructor1403AF000 frees the body+60 response array separately from callbacks/shared storage.

The configuration image owns only these verified fields:

| Body offset | Extent | Meaning |
| --- | --- | --- |
| 14 | 20 | ResponseToChannels |
| 38 | 8 | CollisionProfileName |
| 40 | 20 | CollisionResponses channels |
| 60 | 10 | Owned ResponseChannel array header; never a borrowed installation |
| 74 | 4 | Reflected configuration flags, mask3BFFFF |
| 78 | 4 | MaxDepenetrationVelocity |
| 84 | 28 | Mass override, damping, custom DOF plane, COM nudge, mass scale |
| C0 | 10 | WalkableSlopeOverride |
| D0 | 8 | Physical-material UObject dependency, separately leased |
| D8 | 14 | Angular limit, sleep/stabilization multipliers, blend weight, position iterations |
| 128 | 4 | Velocity iterations |
| 22C | 4 | Sleep family, DOF mode, collision enabled, object type |

Flag setters14228CDB0..14228D4C0 and1423C56B0 independently establish reflected bits0..17 and19..21. Bit18 is the initial-owner-velocity state set by141FF1790; bit22 is assigned during body construction. Neither is configuration. Actor pointers F0/F8, aggregate100, opaque identifiers118/120, weak owners138/140, callbacks, shared state and user-data/self binding200/208 are not copied from historical storage. Body+210 is the optional 2D body, not a weld-parent pointer.

## Native construction split

- Primitive create141DA5CE0 obtains BodySetup via virtual618, copies component transform270, resolves world+1C8 and calls141FF9480.
-141FE7C90 builds four spawn-option bytes. Byte0 selects static bodies unless component mobility318 equals2. Ground reconstruction therefore captures mobility explicitly.
-141FF9480 uses native temporary body/transform arrays and constructs a helper through141FE7680, then calls141FF9100 and the separate 2D path141FF85C0.
- Helper141FE7680 has extent98. It stores body/transform arrays at0/8, BodySetup10, component18, FPhysScene20, aggregate28, collection30, spawn options80 and selected native scenes88/90.
-141FF9100 calls141FF1790 to create native actors/shapes, then141FEDEE0 to add them to scenes, then141FF9700 to initialize dynamic properties. The direct factory is a potential private-acquisition boundary; the wrapper is not private.
-141FF1790 creates fresh owner/BodySetup weak bindings, assigns typed user data to fresh BodyInstance+200, calls actor factory141FF0680 and shape factory141FF2370, and returns owned actor arrays. It writes scene IDs from the real FPhysScene; nulling that pointer is not a supported shortcut. Its input arrays can shrink/reallocate on failure and therefore cannot use stack backing.
- BodySetup create142014460 returns immediately when byte390 is set. Otherwise it may load/cook physical meshes; a private acquisition must prove that readiness before calling it.

The static-mesh BodySetup getter at141DC4E30 is not defined as a function in the current database. Raw bytes independently show `mov rax,[rcx+920]; test rax,rax; je return; mov rax,[rax+80]; ret`. The child vtable36CEFB0 slot618 points there. No successful decompilation or new annotation is claimed for that entry.

Helper cleanup in141FF9480 releases its shared debug-name owner at50 and FString allocation38. The private construction path must retain these, and all native input/output array allocations, across any partial failure.141FF1790 returning true alone does not establish a body: failed shape construction can release actors and continue. Admission must inspect the actual typed body/actor/shape bindings and absence of scene publication. Initial owner velocity is read by the helper from the current owning actor; it is not historical-state restoration and must not silently supply A's dynamic state.

## Retirement dependencies

Native TermBody142005C40 calls142005D80 for both actors and cleans other ownership.142005D80 can call1420289D0, which updates engine publication queues/maps before native actor release. A scene-less actor must not be treated as proof that all engine publication consumers are empty.

Read-only disassembly of the shipped PhysX3_x64.dll confirms dynamic vtable19B7C0: release slot0 ->396A0, scene getter slot30 ->3C8E0 ->11590. The getter resolves Scb control-state/scene ownership; release also invokes SDK deletion listeners. Private body construction, retirement and typed scene publication still require implementation and tests against those consumers.
