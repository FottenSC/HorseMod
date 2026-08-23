# Static combo analysis

Attacker/style: `012` / `012`  
Route: slot `372` → `341` → `342`; defender slot `142` (left).

Status: **incomplete**

Training evidence: slot `374` / cell `147` is authored-equivalent to ordinary slot `372` / cell `145`: **true**. The training route was not used as scenario input.

| Character | Reaction | First active tick | Nearest clearance | Contact spacing | Observed | Prediction |
|---|---:|---:|---:|---|---|---|
| `001` | `0x13DB` | 74 | 0.446008 | none | hit | unresolved |
| `005` | `0x13DB` | 74 | 0.495911 | none | hit | unresolved |
| `007` | `0x13DB` | 74 | 0.421808 | none | hit | unresolved |
| `00f` | `0x13DB` | 74 | 0.418846 | none | hit | unresolved |
| `011` | `0x13DB` | 74 | 0.430055 | none | hit | unresolved |
| `012` | `0x1477` | 74 | 0.212525 | none | hit | unresolved |
| `014` | `0x13DB` | 74 | 0.428701 | none | hit | unresolved |
| `016` | `0x13DB` | 74 | 0.396201 | none | hit | unresolved |
| `028` | `0x1477` | 74 | 0.324392 | none | hit | unresolved |
| `002` | `0x149A` | 74 | 0.534328 | none | escape | unresolved |
| `003` | `0x149A` | 74 | 0.424211 | none | escape | unresolved |
| `006` | `0x149A` | 74 | 0.481711 | none | escape | unresolved |
| `009` | `0x1478` | 74 | 0.298609 | none | escape | unresolved |
| `00b` | `0x149A` | 74 | 0.509636 | none | escape | unresolved |
| `00c` | `0x149A` | 74 | 0.530525 | none | escape | unresolved |
| `00d` | `0x149A` | 74 | 0.485252 | none | escape | unresolved |
| `015` | `0x149A` | 74 | 0.495128 | none | escape | unresolved |
| `017` | `0x1478` | 74 | 0.297312 | none | escape | unresolved |
| `023` | `0x149A` | 74 | 0.422992 | none | escape | unresolved |
| `024` | `0x149A` | 74 | 0.509840 | none | escape | unresolved |
| `030` | `0x149A` | 74 | 0.456159 | none | escape | unresolved |
| `060` | `0x149A` | 74 | 0.581914 | none | escape | unresolved |
| `061` | `0x1478` | 74 | 0.259119 | none | escape | unresolved |
| `062` | `0x149A` | 74 | 0.534592 | none | escape | unresolved |
| `064` | `0x149A` | 74 | 0.568884 | none | escape | unresolved |
| `065` | `0x149A` | 74 | 0.562794 | none | escape | unresolved |

## Reaction-selection partition

Reported catches use only `0x13DB`/`0x1477`; reported escapes use only `0x149A`/`0x1478`. This is the first causal boundary, but it is not accepted as the final classifier until native KHit overlap reproduces the split.

## Unresolved native boundaries

- exact playback-cache and lane-end state through the overlapping lane-1 reaction and lane-0 ukemi root writers
- main analytic IK for Seong Mi-na's motion-flag-0x80 left-ukemi descriptor
- remaining controller/IK gate producers needed to prove every other final KHit matrix branch inactive
- the displayed contact-spacing intervals linearize spacing after one BODY solve and are diagnostic, not a certified nonlinear close-contact sweep
