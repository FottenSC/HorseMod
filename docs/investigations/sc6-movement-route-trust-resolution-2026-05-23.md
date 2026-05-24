# SC6 Movement Route Trust Resolution - 2026-05-23

## Short Answer

I found the major route-resolution bug.

The analyzer was treating packed move ids like this:

```text
bank = high nibble
slot = low 12 bits
bank 1/2/3 = another KHD/MOT bank
```

That was wrong.

Ghidra shows `LuxMoveVM_ResolveBankSlot @ 0x1402FC400` uses:

```text
bucket = (packedMoveId >> 12) & 0xF
slotInBucket = packedMoveId & 0x7FF
linearSlot = FLuxMoveBank.bucket[bucket].start + slotInBucket
```

So the high nibble is an internal `FLuxMoveBank` bucket, not an external character/common file. The current parser now resolves bank `1`, `2`, and `3` as internal buckets in the same character move bank.

## What Changed

The static analyzer now:

- resolves all 384 formerly "cross-bank" direction links,
- uses the engine's `0x7FF` slot mask instead of the incorrect `0xFFF` mask,
- feeds resolved bucket routes into candidate selection,
- allows bucketed routes to participate in recovery analysis,
- keeps report values generated from CSV evidence instead of hand-maintained markdown.

Current generated counts:

| Metric | Count |
|---|---:|
| Character KHD banks analyzed | 29 |
| Direction transitions | 3,076 |
| Movement candidates | 3,337 |
| Nonzero-bucket direction links | 384 |
| Nonzero-bucket links resolved | 384 |
| Nonzero-bucket links unresolved | 0 |
| Selected movement rows | 203 |
| Trusted basic rows | 97 |
| Unresolved rows | 98 |
| Measured but not basic rows | 8 |
| Ranked movement rows | 97 |

## Ghidra Evidence

`LuxMoveVM_ResolveBankSlot @ 0x1402FC400` is the key function. It takes a packed move id and returns a pointer into the slot table:

- bank/bucket index: `(packedMoveId >> 12) & 0xF`
- slot index inside bucket: `packedMoveId & 0x7FF`
- valid buckets: `0..3`
- bucket table: `FLuxMoveBank + 0x1C..0x2A`
- slot table: `FLuxMoveBank + 0x30`, stride `0x48`

`LuxMoveVM_TransitionToMove @ 0x1402FE350` uses the same resolver when the engine actually changes movement state. That proves this is not just a helper for nested scripts; it is the real route dispatch path.

`LuxMoveVM_DecodeVariadicStreamArgs @ 0x1402FC930` writes the packed move id into the active lane transition target. The bytecode transition author calls `0x05..0x08` all flow through this writer.

## What This Means

The old unresolved "cross-bank" bucket was not a gameplay mystery. It was a parser/modeling mistake.

Those routes are now usable static evidence. They do not automatically improve the number of ranked backsteps because the remaining blocker is different: offensive cell timing and recovery proof.

## Trusted Basic Backsteps

These are the currently ranked neutral/basic backsteps. Distances are decoded authored root motion, not runtime post-collision position.

| Character | Frame 4 | Frame 8 | Frame 12 | Frame 16 | Total | Read |
|---|---:|---:|---:|---:|---:|---|
| Xianghua | 0.306 | 0.562 | 0.655 | 0.705 | 0.826 | Best early retreat in the trusted group. |
| Sophitia | 0.006 | 0.502 | 0.943 | 1.185 | 1.526 | Slow first few frames, then the most total space. |
| Nightmare | 0.026 | 0.104 | 0.231 | 0.399 | 0.819 | Same clean curve as Siegfried. |
| Siegfried | 0.026 | 0.104 | 0.231 | 0.399 | 0.819 | Same clean curve as Nightmare. |
| Hwang | 0.027 | 0.090 | 0.188 | 0.267 | 0.297 | Moves early, but total retreat is modest. |
| Maxi | 0.048 | 0.066 | 0.025 | 0.011 | 0.069 | Starts moving, then gives most of it back. |
| Mitsurugi | 0.027 | 0.048 | 0.127 | 0.167 | 0.180 | Clean but modest. |
| Cervantes | 0.018 | 0.040 | 0.048 | 0.042 | 0.089 | Very small spacing gain. |
| Amy | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | Trusted route, but no authored root retreat in the selected route. |
| Haohmaru | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | Trusted route, but no authored root retreat in the selected route. |

## Strong-Looking But Not Ranked Backsteps

These routes decode to real movement, but the static model does not yet prove they are clean basic backsteps because the route contains offensive cell timing or unresolved recovery.

| Character | Frame 8 | Frame 16 | Why not ranked |
|---|---:|---:|---|
| Hilde | 0.650 | 0.701 | Damage-bearing cell has invalid `999-999` timing, so active/recovery timing is not trustworthy yet. |
| Ivy | 0.643 | 1.159 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Geralt | 0.616 | 1.129 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Seong Mi-na | 0.452 | 0.918 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Cassandra | 0.354 | 0.845 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Kilik | 0.308 | 0.476 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Taki | 0.258 | 0.635 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Voldo | 0.248 | 1.110 | Includes offensive cell timing; no proven static recovery/return point yet. |
| 2B | 0.225 | 0.413 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Raphael | 0.225 | 0.393 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Yoshimitsu | 0.208 | 0.541 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Setsuka | 0.173 | 0.921 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Unknown (cid 066) | 0.151 | 0.357 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Talim | 0.127 | 0.740 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Tira | 0.123 | 0.432 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Astaroth | 0.007 | 0.107 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Zasalamel | - | - | Root motion did not decode with high confidence for the selected route. |
| Groh | 0.000 | 0.000 | Includes offensive cell timing; no proven static recovery/return point yet. |
| Azwel | 0.000 | 0.000 | Includes offensive cell timing; no proven static recovery/return point yet. |

Do not read these as bad. Read them as "the movement curve looks interesting, but we have not proven the route is a clean basic backstep."

## Recovery Is Now The Real Blocker

After fixing bucket resolution, the remaining large unknown is recovery/cancel semantics.

The recovery model currently inspects frame-gated outgoing edges from the movement destination and classifies them as:

- return to neutral/control,
- stance return,
- movement loop,
- attack follow-up,
- unresolved destination/cell semantics,
- unknown frame edge.

Current result:

| Recovery result | Count |
|---|---:|
| Confirmed static recovery rows | 0 |
| Unknown recovery rows | 203 |

That is why several characters with large decoded retreat are still unranked. Their movement routes contain damage-bearing cells, and without a proven return/control frame we cannot safely say "this is basic movement followed by a late optional attack" instead of "this is a special/attack route with movement."

## Player-Relevant Conclusion

For the currently trusted group:

- Fastest early retreat: Xianghua.
- Most total retreat: Sophitia.
- Shared clean big-body curve: Siegfried and Nightmare.
- Small but clean retreats: Mitsurugi, Hwang, Cervantes.
- Clean selected route with no authored retreat: Amy and Haohmaru.

For the untrusted group:

- Hilde, Ivy, Geralt, Seong Mi-na, Cassandra, Taki, and Voldo have movement numbers worth investigating.
- They are not ranked because the engine route still mixes movement evidence with offensive/recovery evidence.
- The next static target is not distance. It is proving recovery/control return and offensive-cell activation timing.

## Files Updated

- `tools/moveset_parser/luxformats.py`
- `tools/moveset_parser/stackvm_emulate.py`
- `tools/moveset_parser/move_graph.py`
- `tools/moveset_parser/bank_resolver.py`
- `tools/moveset_parser/recovery_trust.py`
- `tools/moveset_parser/analyze_movement_system_static.py`
- `tools/moveset_parser/route_trust.py`
- `tools/moveset_parser/render_movement_player_report.py`
- generated evidence under `docs/investigations/generated/`
- `docs/investigations/sc6-character-movement-player-readable-2026-05-21.md`
- `docs/investigations/sc6-character-movement-static-2026-05-21.md`

## Validation

Latest focused validation after the bucket fix:

```powershell
python -m py_compile tools\moveset_parser\bank_resolver.py tools\moveset_parser\recovery_trust.py tools\moveset_parser\render_movement_player_report.py tools\moveset_parser\validate_movement_outputs.py tools\moveset_parser\analyze_movement_system_static.py tools\moveset_parser\route_trust.py tools\moveset_parser\move_graph.py tools\moveset_parser\stackvm_emulate.py tools\moveset_parser\luxformats.py
python -m pytest -q tools\moveset_parser\tests\test_bank_resolver.py tools\moveset_parser\tests\test_recovery_trust.py tools\moveset_parser\tests\test_move_graph.py tools\moveset_parser\tests\test_analyze_movement_system_static.py
python tools\moveset_parser\analyze_movement_system_static.py --dump-battle E:\myMods\dump\Battle --full-dump-battle "C:\Users\prest\Documents\SoulcaliburModding\SCVI Sound Tools\dump\Battle" --out docs\investigations\generated
python tools\moveset_parser\render_movement_player_report.py --generated docs\investigations\generated --out docs\investigations\sc6-character-movement-player-readable-2026-05-21.md
```

Result: focused tests passed, analyzer regenerated, player report regenerated.

Full parser test suite also passed:

```text
153 passed
```
