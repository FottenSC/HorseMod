# Static combo analysis

Attacker/style: `012` / `012`  
Route: slot `372` → `341` → `342`; defender slot `142` (left).

Status: **incomplete**

Training evidence: slot `374` / cell `147` is authored-equivalent to ordinary slot `372` / cell `145`: **true**. The training route was not used as scenario input.

| Character | Reaction | First active tick | Nearest clearance | Contact spacing | Observed | Prediction |
|---|---:|---:|---:|---|---|---|
| `001` | `0x13DB` | 74 | 0.394482 | none | hit | unresolved |
| `005` | `0x13DB` | 74 | 0.098156 | none | hit | unresolved |
| `007` | `0x13DB` | 74 | 0.040343 | (0.1655, 0.4047) | hit | unresolved |
| `00f` | `0x13DB` | 74 | -0.045944 | (0.2605, 0.6293) | hit | unresolved |
| `011` | `0x13DB` | 74 | 0.427802 | none | hit | unresolved |
| `012` | `0x1477` | 74 | 0.145396 | none | hit | unresolved |
| `014` | `0x13DB` | 74 | 0.095590 | none | hit | unresolved |
| `016` | `0x13DB` | 74 | 0.049244 | none | hit | unresolved |
| `028` | `0x1477` | 74 | 0.155399 | none | hit | unresolved |
| `002` | `0x149A` | 74 | 0.193496 | none | escape | unresolved |
| `003` | `0x149A` | 74 | 0.148120 | none | escape | unresolved |
| `006` | `0x149A` | 74 | 0.133950 | none | escape | unresolved |
| `009` | `0x1478` | 74 | 0.114566 | none | escape | unresolved |
| `00b` | `0x149A` | 74 | 0.279239 | none | escape | unresolved |
| `00c` | `0x149A` | 74 | 0.021971 | none | escape | unresolved |
| `00d` | `0x149A` | 74 | 0.112108 | none | escape | unresolved |
| `015` | `0x149A` | 74 | 0.264323 | none | escape | unresolved |
| `017` | `0x1478` | 74 | 0.175955 | none | escape | unresolved |
| `023` | `0x149A` | 74 | 0.100031 | none | escape | unresolved |
| `024` | `0x149A` | 74 | 0.239782 | none | escape | unresolved |
| `030` | `0x149A` | 74 | 0.070441 | none | escape | unresolved |
| `060` | `0x149A` | 74 | 0.592497 | none | escape | unresolved |
| `061` | `0x1478` | 74 | 0.099033 | none | escape | unresolved |
| `062` | `0x149A` | 74 | 0.602524 | none | escape | unresolved |
| `064` | `0x149A` | 74 | 0.305363 | none | escape | unresolved |
| `065` | `0x149A` | 74 | 0.321994 | none | escape | unresolved |

## Reaction-selection partition

Reported catches use only `0x13DB`/`0x1477`; reported escapes use only `0x149A`/`0x1478`. This is the first causal boundary, but it is not accepted as the final classifier until native KHit overlap reproduces the split.

## Unresolved native boundaries

- native tick propagation from the row-1037 terminal state through grounded dispatcher 0x78 to left-ukemi slot 0x8E
- ordered four-lane SolveBonePose controller/IK inputs needed for final KHit world centres
- the displayed contact-spacing intervals linearize spacing after one BODY solve and are diagnostic, not a certified nonlinear close-contact sweep
