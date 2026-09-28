# Ground controller consumers

Read from the existing SoulcaliburVI.exe Ghidra program on 2026-09-22. These are native findings, not proof of restored execution.

- 140895CC0 operates on ground component+0x820 (not the VFX manager). It admits mode1/4, places/activates meshes and ends by binding 14089F9E0. It consumes placement RNG and cannot be replayed as a private reconstruction shortcut.
- 14089F9E0 raw bytes `48 8b d1 c6 41 10 02 48 83 c1 70 4c 8d 05 8e 05 00 00 e9 09 93 7d 02`: writes mode2 and binds14089FF80.
- 14089FF80 enables each mesh physics, applies the configured radial impulse, clears elapsed+0x60 and binds1408A0230. Mode2 therefore includes both pending and completed impulse phases.
- 1408A0230 tests fade-request+0x58; when set, writes mode3 and selects14089FC70 for zero duration, otherwise14089FAE0. The first target is FC70, not FC80.
- 14089FAE0 clears elapsed+0x60 and fade-request+0x58, switches to translucent MIDs with Opacity1 if duration+0x5C is positive, and binds14089FE30.
- 14089FE30 advances/clamps elapsed, writes Opacity=(duration-elapsed)/duration, and binds14089FC70 when complete.
- 14089FC70 disables physics/collision on children, sets duration=-1/elapsed=0/mode4 and destroys the ring delegate. Root tick then performs normal deactivation/callback/autodestruction; do not suppress it.
- 143078D00 constructs a48-byte bound delegate with context+8, callback+0x10, nonzero sequence+0x20 and vtable3510A68. Capture must preserve callback phase; fresh construction needs a fresh context and ownership.

Streaming ownership: SetStaticMesh141DD8790 calls collection+0x68 (141DD0D50); texture manager+0x68 (14214B4C0) calls142141460 when manager+0x1B8 is enabled. The latter appends even an unregistered component to manager+0x170/count+0x178 and ORs component+0x3F8 with6. Native manager+0x70 (14214B520) removes queued dynamic membership and any built dynamic record; verify completion before release. The audio streaming manager slot is the no-op1402D2BC0. No setter was added to the cold factory yet.

Relevant Ghidra plate comments at140895CC0 and142141460 were updated, read back and saved through native MCP.
