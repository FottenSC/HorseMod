# Static combo analysis

Attacker/style: `012` / `012`  
Route: slot `372` → `341` → `342`; defender slot `142` (left).

Status: **incomplete**

Training evidence: slot `374` / cell `147` is authored-equivalent to ordinary slot `372` / cell `145`: **true**. The training route was not used as scenario input.

| Character | Reaction | First active tick | Nearest clearance | Contact spacing | Observed | Prediction |
|---|---:|---:|---:|---|---|---|
| `001` | `0x13DB` | 54 | -0.236944 | (0.0000, 1.4874) | hit | unresolved |
| `005` | `0x13DB` | 54 | -0.206154 | (0.0000, 1.4787) | hit | unresolved |
| `007` | `0x13DB` | 54 | -0.226944 | (0.0000, 1.4932) | hit | unresolved |
| `00f` | `0x13DB` | 54 | -0.209270 | (0.0000, 1.4688) | hit | unresolved |
| `011` | `0x13DB` | 54 | -0.228063 | (0.0000, 1.4954) | hit | unresolved |
| `012` | `0x1477` | 54 | -0.266719 | (0.6491, 1.9747) | hit | unresolved |
| `014` | `0x13DB` | 54 | -0.234718 | (0.0000, 1.4597) | hit | unresolved |
| `016` | `0x13DB` | 54 | -0.212392 | (0.0000, 1.4588) | hit | unresolved |
| `028` | `0x1477` | 54 | -0.257736 | (0.6775, 1.8529) | hit | unresolved |
| `002` | `0x149A` | 54 | -0.279851 | (0.0000, 1.5227) | escape | unresolved |
| `003` | `0x149A` | 54 | -0.168327 | (0.0000, 1.3489) | escape | unresolved |
| `006` | `0x149A` | 54 | -0.125142 | (0.0000, 1.2944) | escape | unresolved |
| `009` | `0x1478` | 54 | -0.261952 | (0.7287, 2.0000) | escape | unresolved |
| `00b` | `0x149A` | 54 | -0.109929 | (0.0000, 1.2894) | escape | unresolved |
| `00c` | `0x149A` | 54 | -0.109479 | (0.0000, 1.2461) | escape | unresolved |
| `00d` | `0x149A` | 54 | -0.106263 | (0.0000, 1.3060) | escape | unresolved |
| `015` | `0x149A` | 54 | -0.120931 | (0.0000, 1.2689) | escape | unresolved |
| `017` | `0x1478` | 54 | -0.267158 | (0.7014, 2.0000) | escape | unresolved |
| `023` | `0x149A` | 54 | -0.167547 | (0.0000, 1.3987) | escape | unresolved |
| `024` | `0x149A` | 54 | -0.108657 | (0.0000, 1.4774) | escape | unresolved |
| `030` | `0x149A` | 54 | -0.168365 | (0.0000, 1.3384) | escape | unresolved |
| `060` | `0x149A` | 54 | -0.060077 | (0.0000, 1.2847) | escape | unresolved |
| `061` | `0x1478` | 54 | -0.341040 | (0.6525, 2.0000) | escape | unresolved |
| `062` | `0x149A` | 54 | -0.124479 | (0.0000, 1.2705) | escape | unresolved |
| `064` | `0x149A` | 54 | -0.098363 | (0.0000, 1.2878) | escape | unresolved |
| `065` | `0x149A` | 54 | -0.099555 | (0.0000, 1.2942) | escape | unresolved |

## Reaction-selection partition

Reported catches use only `0x13DB`/`0x1477`; reported escapes use only `0x149A`/`0x1478`. This is the first causal boundary, but it is not accepted as the final classifier until native KHit overlap reproduces the split.

## Unresolved native boundaries

- exact playback-cache and lane-end state through the overlapping lane-1 reaction and lane-0 ukemi root writers
- main analytic IK for Seong Mi-na's motion-flag-0x80 left-ukemi descriptor
- remaining controller/IK gate producers needed to prove every other final KHit matrix branch inactive
- the displayed contact-spacing intervals linearize spacing after one BODY solve and are diagnostic, not a certified nonlinear close-contact sweep
