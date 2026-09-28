# G1 PhysX substep completion edge (2026-09-24)

Bounded read-only trace in the existing `SoulcaliburVI.exe` Ghidra program. No hook, source change, build or live experiment was made for this audit.

`142028440` creates a 0x40-byte completion task with vtable `143976490`. It retains the scene's `+0x130` event in task `+0x28`, stores scene type at `+0x30`, and stores at task `+0x10` the result of native scene virtual `+0x318`. Its initial delegate `1420555E0` stores that completion task in substep context `+0xB0`, initializes its counters and tail-jumps to `142055C40`.

`142055C40` allocates/retains context `+0xC8`, schedules a repeat delegate, invokes scene `+0x80` callbacks through `1420113E0`, then submits native simulation. `142055BC0` releases `+0xC8`; while more substeps remain it calls a native scene virtual and enters `142055C40` again. In its terminal branch it invokes context `+0xB0` virtual `+0x20`.

Completion-task vtable `143976490` has `+0x20 = 14202E4E0` and `+0x30 = 14202E450`. `14202E4E0` calls the task's `+0x10` object's virtual `+0x90`, passing the task. `14202E450` first invokes the task's `+0x18` object virtual `+0x20`, then passes the retained event at task `+0x28` to `140D20A70`, and finally retires the task. `140D20A70` may release prerequisites immediately or schedule dependent task-graph work; calling it is not itself proof that all dependents have finished.

The first unresolved edge is the concrete target and completion ordering of the native object's virtual `+0x90`. Static game code here does not show when it invokes `14202E450`, whether that occurs after all required substep callbacks and simulation work, or when the retained `scene+0x130` graph event becomes complete. The task graph wait `14202DE80` also collects `+0x160` and per-type `+0x130/+0x148` events, but transitive closure for both scene types is unproved. Astra high found no safe pre-publication recovery owner from this chain.

Next resolving test: identify the concrete returned object from native scene virtual `+0x318`, resolve its virtual `+0x90` implementation, and trace actual invocation of completion-task `+0x30` through the retained event's completion state. Stop at an unresolved external native edge; do not infer completion from reference release or task submission. Writer exclusion and an enclosing recoverable tick disposition remain separate blockers.
