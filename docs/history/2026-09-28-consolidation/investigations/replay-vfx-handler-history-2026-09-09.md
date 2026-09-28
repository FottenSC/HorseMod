> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Repeated seek: VFX handler slot history

The repeated A170 -> 208 -> A170 -> 214 experiment fails because handler-owned slot histories survive manager restoration. Replay `82992899082e43d48d5e5e69233cd2a1` observes effect2005 at native tick176: original [] -> [7], first resimulation [7] -> [7,7], second resimulation appends7, exceeds the native limit2, then destroys the new slot7. Initialization created all10 emitters; destruction precedes the first component tick. The corrected three-argument destruction observer proves this route. The earlier two-argument probe is invalid evidence and is retained explicitly under `host-seek-repeat-invalid-destroy-probe-*`.

Primary native ownership:
- 1403D4D00: embedded handler+400 sparse map, stride32 records, uint key, owned int-array +8, hash chain +18 and bucket +1C. Appends and enforces the native limit through destructive manager calls.
- 1403BA400 (containing3BA5AA): handler+3F0 owns nested int-array groups, compacts missing slots and stops old groups.
- 1403BA780: handler+468 contains tracked integer IDs consumed by manager cleanup.
- 1403CE4C0: handler+450 is the situation-name string consumed by descriptor registration.
- 1403CEDE0: authored setting pointers and +3A0 map are initialization bindings; 1403A5420 copies nine authored flag bytes directly into entries+C (the temporary insertion argument is a pointer, the retained entry is not). Binding changes reject restoration.
- 1403B5530/1403BBA60: BeginPlay/EndPlay own initialization, nested-array destruction and callback membership. A different or expired handler is not revived.

Implementation (both deliberate cancellation paths now independently verified): Sc6ReplayVfxHandlerState captures the runtime allocation graph with independent immutable bytes. Preparation allocates and relocates a complete private A graph while retaining exact B native allocations. Only the root is published; failed or cancelled publication reinstalls B's root and verifies all nested bytes. Commit retires B only after validation. Actor/battle/setting leases, authored-map binding checks, capacity, map-chain and allocation-alias checks remain mandatory. The host includes this participant in checkpoint memory, preparation, undo, commit, dirty-state release and checkpoint validation. Schema58 distinguishes these snapshots. No expected observations, native-limit changes or slot-ID substitutions enter simulation.

The focused storage test covers nested relocation, captured-image survival across later writes, private-target corruption with B intact, hash cycles, allocation alias and capacity failure. This is not a live undo/continuation pass. The next assembled gate cancels after A170 publication and independently requires B220+120 recovery before repeated-seek commit. Full arbitrary seeking and lifecycle/corpus/performance qualification remain unfinished.

Ownership correction: the attempted PSSettingListDataAsset span match rejected A170 before publication.1403A5420 proves entry+C copies8+1 bytes from the insertion argument, with no retained pointer. That speculative span machinery was removed. Authored map1283 entries/capacity2048 fits explicit4096 bounds under the unchanged byte budget. No new authored asset participant is required.

Live cancellation checkpoint: runtime a202e64b0ecb1d3415c08b3433df2eb8035d22cfe36c52774bec3a95136386ad / observer 5f58259180f89621b0f7f4b1b8efc3031a729ee6618c6dca9a09cec99acbcdfe passed before/after publication with independent B220+120 ticks,480 callbacks, native Lux poses and cleanup. See host-seek-handler-cancellation-audit.json for exact controls and raw hashes. Repeated-seek commit remains pending; cancellation success does not establish A continuation or full seeking.

Repeated seek now independently passes120 ticks after214 on the cancellation identities above: replay-2b68268a9d9a420cb1a66611f95e6b21. All three native histories are[]->[7]; particle creation reproduces. First seek468606us, second650572us: performance fails500ms. Full visual coherence remains incomplete. Fixed360 physical observations originally exhausted the continuation;448 includes both replayed prefixes and the unchanged120-tick comparison. Completed spawn probes were removed after retaining raw causal evidence. See host-seek-handler-repeat-audit.json.
