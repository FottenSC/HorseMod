# G1 C18 material memory-shape census — 2026-09-27

Implemented and locally tested observation only. G1 remains open. No MID
reference, producer exclusion, actor-death survival, complete material B undo,
native resource bound or GPU completion is provided.

The existing C18 provider owner now samples exact indexed override MIDs with
vtable `base+0x391ee70`. It records the actor/provider/MID indexed identities,
provider ordinal and slot, vector header at MID `+0xB8`, and occupancy of the
three proxies `+0xF0/+0xF8/+0x100` and twelve caches `+0x150..+0x1A8`.
The verified layouts and limits come from the
[native evidence](rollback-g1-material-task-cpu-guard-2026-09-27.md#actor-teardown-and-upstream-activation-boundary).

Production scope is `C18ProviderAt` under the existing real `C18Before` double
capture. Non-MID overrides and asset-fallback materials keep their existing
provider census behavior and receive no detailed memory row. Ordered aliases
are retained as occurrences, not treated as distinct owned allocations.

Only guarded header/pointer reads and indexed identity observations were added.
The vector header is rechecked after reading the shape and revalidating all
three identities; the two entry captures compare the complete sampled pointer
values, including proxy/cache pointers whose output exposes only occupancy.
Matching samples do not establish an atomic snapshot, ABA protection, writer
exclusion or any future invariant. Vector backing, proxies and cached resources
are never dereferenced. Pointer-shape validity proves count/capacity, nullness,
alignment and arithmetic checks only, not backing readability or ownership.
No resource-size virtual, material getter/setter, retain or new hook is used.

The shape has its own validity, separate from the provider census. A shape read,
header or shape comparison failure suppresses detailed rows without invalidating
an otherwise valid provider census. A changed provider/MID generation between
captures still invalidates the existing provider census and suppresses shape
output. Scratch is not serialized before the selecting call publishes it, and
native return bookkeeping still reads no borrowed input.

Detailed rows are capped at 16. An overflow retains the first 16 matching rows,
reports the omitted occurrence count and `detail_capacity`, and marks the shape
incomplete. The compiled fixture reports census ownership **156,738 bytes**, below
its existing 256 KiB sub-bound. Both additional capture buffers and metadata are
included in that charge. Serialization uses the existing bounded buffer, and
the observer-wide compile-time accounting still fits the unchanged **4 MiB**
reservation. The production ceiling remains **1 GiB**. No native resources are
newly retained or allocated by this observation.

Changed source files:

- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`
- `tools/replay_vfx_completion_observation_selftest.cpp`
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`
- This evidence document.

## Retained RED and GREEN

The pre-fix regression compiled, reached the real callback owner, captured the
existing provider census and forwarded once. It failed specifically at
`missing production collection18 material memory-shape receipt`.
[RED manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/74cdf579ebb086e5bd698702e098a34e81a577d381b952e04893252f4f816135.json),
[raw RED log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/5e2bd31cbaa97e200cdf36d32cbded6bbefce998831d0753923f6f6ecf672dda.log).

Exact pre-fix command (the test was not yet parametrized):

```text
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape
```

After implementation, **11 selected native-contract cases passed**: seven new
shape cases and four existing provider/registry cases.
[GREEN manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/efb454e851dc38afe8067ff2f29f01b2993026d60ffda848114032811e9d6abf.json),
[raw GREEN log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3bf260a8d4b6a6d093e32e1361a193d57b4a7458ad2866e18a6893de660dd260.log),
[full runner console](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/d115db8e06379ee16dd5eaa4ec7796165c53938a3021d6fd3b2fe4f3d253c827.log).

Exact post-fix command:

```text
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census
```

The following unmodified production JSON outputs from that same pytest run
(`pytest-3258`, shape cases 0–6) were subsequently retained with the existing
`replay_evidence.store_bytes` helper. Their combined size is 89,339 bytes.
The GREEN pytest log records assertion success; these files retain the actual
shape values rather than relying on temporary fixture paths.

| Case | Asserted production result | Retained JSON |
| --- | --- | --- |
| Normal | Four ordered override occurrences, vector count 1/capacity 8, payload capacity 320, proxy occupancy `[true,true,false]`, first/last caches occupied; asset MID excluded | [output](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/741b770740ea0c4e24e96a35ab410b0a929e313a404d29d8b5858d2fce175499.json) |
| Header changed between captures | `mid_shape_entry_changed`, no detail rows; provider census remains valid | [output](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6742def62638de026df24e434147ec39f57e388eb648395dbb624605471895e0.json) |
| Header changed during first sample | `vector_header_changed`, no detail rows; immediate recheck detects the change | [output](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/699d4feeabd84144e9764f5829f122bedfe9fddb5c79d80be2c938adea836689.json) |
| Indexed serial changed between captures | Existing provider census rejects `entry_changed`; shape says `provider_census_invalid`, no rows/count claim | [output](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/5ecbf4b5665939f64fd1d1d58f4fedabaacc6af950b2a3dffb5786b0f4743812.json) |
| Unreadable header | Actual PAGE_NOACCESS interval makes the guarded read fail; `mid_shape_unreadable`, provider census remains valid | [output](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7a01e481100be8a7dcb183c36731d055646aec143fb824fee727c59473c64f49.json) |
| Null backing with nonzero capacity | `vector_pointer_shape`, no detail rows | [output](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b31361fe386d60f9038cdaf8ea36e8d4f860729b3258460b3ba523eb49e60413.json) |
| Detail capacity | 20 matching occurrences, 16 rows, 4 omitted, `complete=false`, `detail_capacity` | [output](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6302461d1e0d6cca1150a687eb2963ea4da0b1880ccba2be75cb0a1812d6e19a.json) |

Every new case asserts callback forwarding exactly once, incoming/outgoing
LastError `0x66c/0x77c`, zero vector/proxy/cache target reads and zero observer
reads after native return. The fixture revokes graph and callback pages before
return. Its interventions change external memory/indexed-service inputs only;
there is no fixture-side receipt, census implementation or ownership grant.
Normal input bytes remain unchanged; mutation cases compare against only their
explicitly changed expected input. The existing reader retains the additive
shape object with the provider census; no reader admission logic was changed.

Reconstructible sources:
[pre-fix](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-4b81cbc90ff1ee88a81c7c3da6073e3f8575fa30afa26fe3e5b4ff781d4a9e66.json),
[post-fix](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-563a9a5ab264cdf62abec4a20d1a993e71a08bf908147ea33a43b8530b76f51d.json).
Post-fix workspace fingerprint:
`6e8e0abf419524e8bdb4e93e6e4033263f1890b218b1957e557e759a4e77c9b8`.
Comparing those archives found only the three source files above changed;
their on-disk contents still match the tested post-fix hashes. This document
was added afterward. Recorded DLL hashes belong to root's preceding build,
not a production build of this slice.

## Output contract and remaining limits

The additive object is
`collection18_witness.material_provider_census.memory_shape`, version 1,
scope `sampled_override_mid_shapes`. `complete` means only that the selected
entry's scoped detail samples matched with no failure or omitted occurrence.
`matched_count` and `omitted_count` are null if the provider census is invalid.
`entry_samples_match` can remain true on detail overflow, while `complete` is
false. Provider ordinals bind rows to the enclosing provider census.

`vector_payload_capacity_bytes = capacity * 0x28` excludes allocator overhead,
proxy/cache allocations, sharing, deferred resources and GPU ownership.
`total_retained_native_bytes` is always null. `ownership_permission`,
`resource_completion_proven`, `writer_exclusion_proven`, and
`covers_native_start_births` are always false. These observations are never
consumed as admission, publication or retirement permission.

All six material guard hooks, `HistoricalRestoreSupported()==false`, the C-only
`ProducerUncovered` veto, complete B, lifetime/membership/GPU/deferred-retirement
guards and the opt-in positive-admission RED remain unchanged. No proxy
ownership, material retention, callback completion, B recovery or G1 pass is
claimed. Native simulation, lifecycle, normal-render coherence and performance
were not measured. Root owns production build, full local and any live test.
No build, broad suite, game, deployment, Ghidra/AGENTS/status edit or commit ran.
Both invoked test commands returned; runner cleanup remains reported unknown.

After root's required build/full-local and exact-identity checks, the next useful
diagnostic is one known active-combat C18 occurrence with a live selected MID.
A nonnull secondary proxy falsifies a primary-proxy-only restriction for that
sample. Zero eligible MIDs is inconclusive; primary-only samples still do not
establish an ownership or memory bound.
