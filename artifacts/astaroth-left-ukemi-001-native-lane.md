# Static combo analysis

Attacker/style: `012` / `012`  
Route: slot `372` → `341` → `342`; defender slot `142` (left).

Status: **incomplete**

Training evidence: slot `374` / cell `147` is authored-equivalent to ordinary slot `372` / cell `145`: **true**. The training route was not used as scenario input.

| Character | Reaction | First active tick | Nearest clearance | Contact spacing | Observed | Prediction |
|---|---:|---:|---:|---|---|---|
| `001` | `0x13DB` | 53 | 0.069186 | none | hit | unresolved |

## Reaction-selection partition

Reported catches use only `0x13DB`/`0x1477`; reported escapes use only `0x149A`/`0x1478`. This is the first causal boundary, but it is not accepted as the final classifier until native KHit overlap reproduces the split.

## Unresolved native boundaries

- exact playback-cache and lane-end state through the overlapping lane-1 reaction and lane-0 ukemi root writers
- main analytic IK for Seong Mi-na's motion-flag-0x80 left-ukemi descriptor
- remaining controller/IK gate producers needed to prove every other final KHit matrix branch inactive
- the displayed contact-spacing intervals linearize spacing after one BODY solve and are diagnostic, not a certified nonlinear close-contact sweep
