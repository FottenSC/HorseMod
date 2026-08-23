# Static combo analysis

Attacker/style: `012` / `012`  
Route: slot `372` → `341` → `342`; defender slot `142` (left).

Status: **incomplete**

Training evidence: slot `374` / cell `147` is authored-equivalent to ordinary slot `372` / cell `145`: **true**. The training route was not used as scenario input.

| Character | Reaction | First active tick | Nearest clearance | Contact spacing | Observed | Prediction |
|---|---:|---:|---:|---|---|---|
| `001` | `0x13DB` | 54 | -0.136489 | (0.0000, 0.9250) | hit | unresolved |
| `005` | `0x13DB` | 54 | -0.122348 | (0.0000, 0.9175) | hit | unresolved |
| `007` | `0x13DB` | 54 | -0.139999 | (0.0000, 0.9300) | hit | unresolved |
| `00f` | `0x13DB` | 54 | -0.122666 | (0.0000, 0.9075) | hit | unresolved |
| `011` | `0x13DB` | 54 | -0.137738 | (0.0000, 0.9300) | hit | unresolved |
| `012` | `0x1477` | 54 | -0.409749 | (0.0800, 1.4025) | hit | unresolved |
| `014` | `0x13DB` | 54 | -0.112939 | (0.0000, 0.8975) | hit | unresolved |
| `016` | `0x13DB` | 54 | -0.119519 | (0.0000, 0.8975) | hit | unresolved |
| `028` | `0x1477` | 54 | -0.341196 | (0.1100, 1.2925) | hit | unresolved |
| `002` | `0x149A` | 54 | -0.180053 | (0.0000, 0.9525) | escape | unresolved |
| `003` | `0x149A` | 54 | -0.019781 | (0.0000, 0.7875) | escape | unresolved |
| `006` | `0x149A` | 54 | 0.007014 | (0.0000, 0.7325) | escape | unresolved |
| `009` | `0x1478` | 54 | -0.305594 | (0.1650, 1.6525) | escape | unresolved |
| `00b` | `0x149A` | 54 | 0.007454 | (0.0000, 0.7275) | escape | unresolved |
| `00c` | `0x149A` | 54 | 0.022590 | (0.0000, 0.6850) | escape | unresolved |
| `00d` | `0x149A` | 54 | 0.001825 | (0.0000, 0.7450) | escape | unresolved |
| `015` | `0x149A` | 54 | 0.015176 | (0.0000, 0.7075) | escape | unresolved |
| `017` | `0x1478` | 54 | -0.340891 | (0.1325, 1.5925) | escape | unresolved |
| `023` | `0x149A` | 54 | -0.051528 | (0.0000, 0.8375) | escape | unresolved |
| `024` | `0x149A` | 54 | -0.097001 | (0.0000, 0.9125) | escape | unresolved |
| `030` | `0x149A` | 54 | -0.013733 | (0.0000, 0.7775) | escape | unresolved |
| `060` | `0x149A` | 54 | 0.009961 | (0.0000, 0.7225) | escape | unresolved |
| `061` | `0x1478` | 54 | -0.385723 | (0.0850, 1.6475) | escape | unresolved |
| `062` | `0x149A` | 54 | 0.015845 | (0.0000, 0.7100) | escape | unresolved |
| `064` | `0x149A` | 54 | 0.008355 | (0.0000, 0.7275) | escape | unresolved |
| `065` | `0x149A` | 54 | 0.006199 | (0.0000, 0.7325) | escape | unresolved |

## Reaction-selection partition

Reported catches use only `0x13DB`/`0x1477`; reported escapes use only `0x149A`/`0x1478`. This is the first causal boundary, but it is not accepted as the final classifier until native KHit overlap reproduces the split.

## Unresolved native boundaries

- exact playback-cache and lane-end state through the overlapping lane-1 reaction and lane-0 ukemi root writers
- main analytic IK for Seong Mi-na's motion-flag-0x80 left-ukemi descriptor
- remaining controller/IK gate producers needed to prove every other final KHit matrix branch inactive
- the displayed contact-spacing intervals linearize spacing after one BODY solve and are diagnostic, not a certified nonlinear close-contact sweep
