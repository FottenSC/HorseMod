# SC6 movement strength - player-readable report

Latest static analyzer run: 2026-05-23.

## Short Answer

The analyzer now measures authored root movement from the game files and shows every character, but it only ranks movement when the static route is trusted as basic movement.

Current trusted-and-ranked movement rows: 97. Current trusted selected routes: 97. Internal move-bank bucket links audited: 384; resolved bucket links: 384.

For backsteps, the trusted group is still small. Xianghua has the strongest early retreat in that group, Sophitia creates the most total space, and Siegfried/Nightmare share the same clean retreat curve.

## What The Labels Mean

Trusted basic means direct movement from a neutral-like state with decoded movement and no active offensive timing on the route.

Trusted stance movement means the movement is trusted inside a stance or mode, not as universal neutral movement.

Measured, route unclear means the movement curve decodes, but the route source is not proven as the normal movement state.

Unknown means the static data does not yet prove whether this is basic movement, usually because recovery or offensive timing is unresolved.

Special or attack movement means the route has offensive behavior during the movement window.

## Trusted Basic Backsteps

Frame 4 and frame 8 matter most for making attacks miss. Total retreat matters for resetting range, but late movement is less valuable if the attack is already active.

| Character | Grade | Frame 4 | Frame 8 | Frame 12 | Frame 16 | Total | Player read |
|---|---:|---:|---:|---:|---:|---:|---|
| Xianghua | S | 0.306 | 0.562 | 0.655 | 0.705 | 0.826 | Creates space early, which is the part most likely to make fast attacks miss. |
| Sophitia | A | 0.006 | 0.502 | 0.943 | 1.185 | 1.526 | Creates space early, which is the part most likely to make fast attacks miss. |
| Nightmare | B | 0.026 | 0.104 | 0.231 | 0.399 | 0.819 | The space gain builds later, so it is more useful for resetting range than beating fast active frames. |
| Siegfried | A | 0.026 | 0.104 | 0.231 | 0.399 | 0.819 | The space gain builds later, so it is more useful for resetting range than beating fast active frames. |
| Hwang | B | 0.027 | 0.090 | 0.188 | 0.267 | 0.297 | Clean route with modest retreat. |
| Maxi | B | 0.048 | 0.066 | 0.025 | 0.011 | 0.069 | Trusted route, but this selected motion barely retreats. |
| Mitsurugi | B | 0.027 | 0.048 | 0.127 | 0.167 | 0.180 | Clean route with modest retreat. |
| Cervantes | C | 0.018 | 0.040 | 0.048 | 0.042 | 0.089 | Trusted route, but this selected motion barely retreats. |
| Amy | C | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | Trusted route, but this selected motion barely retreats. |
| Haohmaru | D | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | Trusted route, but this selected motion barely retreats. |

## Backsteps We Can Measure But Not Rank Yet

These are not a tier list. They stay out of ranking because the route is not proven as plain neutral movement, or recovery and offensive timing are not proven enough.

| Character | Label | Frame 8 | Frame 16 | Why it is not ranked |
|---|---|---:|---:|---|
| Hilde | Unknown | 0.650 | 0.701 | The route points at attack timing with an invalid time window, so we cannot prove when movement ends. |
| Ivy | Unknown | 0.643 | 1.159 | The route includes attack timing, and static recovery is not proven yet. |
| Geralt | Unknown | 0.616 | 1.129 | The route includes attack timing, and static recovery is not proven yet. |
| Seong Mi-na | Unknown | 0.452 | 0.918 | The route includes attack timing, and static recovery is not proven yet. |
| Cassandra | Unknown | 0.354 | 0.845 | The route includes attack timing, and static recovery is not proven yet. |
| Kilik | Unknown | 0.308 | 0.476 | The route includes attack timing, and static recovery is not proven yet. |
| Taki | Unknown | 0.258 | 0.635 | The route includes attack timing, and static recovery is not proven yet. |
| Voldo | Unknown | 0.248 | 1.110 | The route includes attack timing, and static recovery is not proven yet. |
| 2B | Unknown | 0.225 | 0.413 | The route includes attack timing, and static recovery is not proven yet. |
| Raphael | Unknown | 0.225 | 0.393 | The route includes attack timing, and static recovery is not proven yet. |
| Yoshimitsu | Unknown | 0.208 | 0.541 | The route includes attack timing, and static recovery is not proven yet. |
| Setsuka | Unknown | 0.173 | 0.921 | The route includes attack timing, and static recovery is not proven yet. |
| Unknown (cid 066) | Unknown | 0.151 | 0.357 | The route includes attack timing, and static recovery is not proven yet. |
| Talim | Unknown | 0.127 | 0.740 | The route includes attack timing, and static recovery is not proven yet. |
| Tira | Unknown | 0.123 | 0.432 | The route includes attack timing, and static recovery is not proven yet. |
| Astaroth | Unknown | 0.007 | 0.107 | The route includes attack timing, and static recovery is not proven yet. |
| Azwel | Unknown | 0.000 | 0.000 | The route includes attack timing, and static recovery is not proven yet. |
| Groh | Unknown | 0.000 | 0.000 | The route includes attack timing, and static recovery is not proven yet. |
| Zasalamel | Unknown | - | - | Static evidence does not yet prove this as basic movement. |

## Sidestep Read

| Character | Route | Label | Frame 8 | Frame 16 | Total | Player read |
|---|---|---|---:|---:|---:|---|
| Mitsurugi | Sidestep down-side | Trusted basic | 0.191 | 0.190 | 0.197 | Clean side movement; compare the early value to judge evasiveness. |
| Nightmare | Sidestep down-side | Trusted basic | 0.099 | 0.138 | 0.139 | Clean side movement; compare the early value to judge evasiveness. |
| Hwang | Sidestep up-side | Trusted basic | 0.089 | 0.024 | 0.128 | Clean side movement; compare the early value to judge evasiveness. |
| Sophitia | Sidestep up-side | Trusted basic | 0.089 | 0.077 | 0.090 | Clean side movement; compare the early value to judge evasiveness. |
| 2B | Sidestep up-side | Trusted basic | 0.062 | 0.014 | 0.080 | Clean side movement; compare the early value to judge evasiveness. |
| Xianghua | Sidestep up-side | Trusted basic | 0.061 | 0.174 | 0.248 | Clean side movement; compare the early value to judge evasiveness. |
| Kilik | Sidestep down-side | Trusted basic | 0.044 | 0.059 | 0.110 | Clean side movement; compare the early value to judge evasiveness. |
| Xianghua | Sidestep down-side | Trusted basic | 0.034 | 0.053 | 0.056 | Clean side movement; compare the early value to judge evasiveness. |
| Ivy | Sidestep down-side | Trusted basic | 0.033 | 0.019 | 0.081 | Clean side movement; compare the early value to judge evasiveness. |
| Haohmaru | Sidestep down-side | Trusted basic | 0.029 | 0.096 | 0.111 | Clean side movement; compare the early value to judge evasiveness. |
| Cassandra | Sidestep up-side | Trusted basic | 0.028 | 0.003 | 0.031 | Clean side movement; compare the early value to judge evasiveness. |
| Taki | Sidestep down-side | Trusted basic | 0.021 | 0.056 | 0.080 | Clean side movement; compare the early value to judge evasiveness. |
| Amy | Sidestep down-side | Trusted basic | 0.018 | 0.041 | 0.054 | Clean side movement; compare the early value to judge evasiveness. |
| Zasalamel | Sidestep down-side | Trusted basic | 0.016 | 0.049 | 0.078 | Clean side movement; compare the early value to judge evasiveness. |
| Voldo | Sidestep down-side | Trusted basic | 0.015 | 0.038 | 0.059 | Clean side movement; compare the early value to judge evasiveness. |
| 2B | Sidestep down-side | Trusted basic | 0.014 | 0.010 | 0.106 | Clean side movement; compare the early value to judge evasiveness. |
| Yoshimitsu | Sidestep down-side | Trusted basic | 0.013 | 0.037 | 0.099 | Clean side movement; compare the early value to judge evasiveness. |
| Cassandra | Sidestep down-side | Trusted basic | 0.008 | 0.022 | 0.040 | Clean side movement; compare the early value to judge evasiveness. |
| Maxi | Sidestep down-side | Trusted basic | 0.008 | 0.023 | 0.040 | Clean side movement; compare the early value to judge evasiveness. |
| Sophitia | Sidestep down-side | Trusted basic | 0.008 | 0.022 | 0.040 | Clean side movement; compare the early value to judge evasiveness. |
| Talim | Sidestep down-side | Trusted basic | 0.008 | 0.022 | 0.047 | Clean side movement; compare the early value to judge evasiveness. |
| Unknown (cid 066) | Sidestep down-side | Trusted basic | 0.008 | 0.022 | 0.040 | Clean side movement; compare the early value to judge evasiveness. |
| Seong Mi-na | Sidestep down-side | Trusted basic | 0.007 | 0.073 | 0.442 | Clean side movement; compare the early value to judge evasiveness. |
| Tira | Sidestep down-side | Trusted basic | 0.007 | 0.007 | 0.040 | Clean side movement; compare the early value to judge evasiveness. |
| Astaroth | Sidestep down-side | Trusted basic | 0.003 | 0.012 | 0.069 | Clean side movement; compare the early value to judge evasiveness. |
| Azwel | Sidestep down-side | Trusted basic | 0.000 | 0.000 | 0.000 | Clean side movement; compare the early value to judge evasiveness. |
| Groh | Sidestep down-side | Trusted basic | 0.000 | 0.000 | 0.000 | Clean side movement; compare the early value to judge evasiveness. |
| Hilde | Sidestep down-side | Trusted basic | 0.000 | 0.000 | 0.000 | Clean side movement; compare the early value to judge evasiveness. |
| Hwang | Sidestep down-side | Trusted basic | 0.000 | 0.000 | 0.000 | Clean side movement; compare the early value to judge evasiveness. |
| Raphael | Sidestep down-side | Trusted basic | 0.000 | 0.000 | 0.000 | Clean side movement; compare the early value to judge evasiveness. |
| Raphael | Sidestep up-side | Trusted basic | 0.000 | 0.000 | 0.058 | Clean side movement; compare the early value to judge evasiveness. |
| Setsuka | Sidestep up-side | Trusted basic | 0.000 | 0.000 | 0.000 | Clean side movement; compare the early value to judge evasiveness. |
| Siegfried | Sidestep down-side | Trusted basic | 0.000 | 0.000 | 0.000 | Clean side movement; compare the early value to judge evasiveness. |
| Voldo | Sidestep up-side | Trusted basic | 0.000 | 0.000 | 0.000 | Clean side movement; compare the early value to judge evasiveness. |

Not ranked in this group: 24 rows (20 unknown, 4 measured but route unclear).

| Character | Route | Label | Frame 8 | Frame 16 | Why it is not ranked |
|---|---|---|---:|---:|---|
| Unknown (cid 066) | Sidestep up-side | Unknown | 0.418 | 0.492 | The route includes attack timing, and static recovery is not proven yet. |
| Amy | Sidestep up-side | Unknown | 0.095 | 0.008 | The route includes attack timing, and static recovery is not proven yet. |
| Talim | Sidestep up-side | Unknown | 0.095 | 0.086 | The route includes attack timing, and static recovery is not proven yet. |
| Zasalamel | Sidestep up-side | Unknown | 0.082 | 0.082 | The route includes attack timing, and static recovery is not proven yet. |
| Geralt | Sidestep up-side | Unknown | 0.074 | 0.022 | The route includes attack timing, and static recovery is not proven yet. |
| Astaroth | Sidestep up-side | Unknown | 0.058 | 0.089 | The route includes attack timing, and static recovery is not proven yet. |
| Cervantes | Sidestep up-side | Unknown | 0.048 | 0.008 | The route includes attack timing, and static recovery is not proven yet. |
| Tira | Sidestep up-side | Unknown | 0.027 | 0.029 | The route includes attack timing, and static recovery is not proven yet. |
| Maxi | Sidestep up-side | Unknown | 0.020 | 0.187 | The route includes attack timing, and static recovery is not proven yet. |
| Hilde | Sidestep up-side | Unknown | 0.019 | 0.120 | The route includes attack timing, and static recovery is not proven yet. |
| Ivy | Sidestep up-side | Unknown | 0.019 | 0.038 | The route includes attack timing, and static recovery is not proven yet. |
| Mitsurugi | Sidestep up-side | Unknown | 0.013 | 0.054 | The route includes attack timing, and static recovery is not proven yet. |
| Groh | Sidestep up-side | Unknown | 0.009 | 0.020 | The route includes attack timing, and static recovery is not proven yet. |
| Setsuka | Sidestep down-side | Measured, route unclear | 0.009 | 0.025 | The movement curve decodes, but the source state is not proven as the normal movement state. |
| Taki | Sidestep up-side | Unknown | 0.007 | 0.163 | The route includes attack timing, and static recovery is not proven yet. |
| Haohmaru | Sidestep up-side | Unknown | 0.003 | 0.001 | The route includes attack timing, and static recovery is not proven yet. |
| Azwel | Sidestep up-side | Unknown | 0.000 | 0.000 | The route includes attack timing, and static recovery is not proven yet. |
| Cervantes | Sidestep down-side | Measured, route unclear | 0.000 | 0.000 | The movement curve decodes, but the source state is not proven as the normal movement state. |
| Geralt | Sidestep down-side | Measured, route unclear | 0.000 | 0.000 | The movement curve decodes, but the source state is not proven as the normal movement state. |
| Kilik | Sidestep up-side | Measured, route unclear | 0.000 | 0.000 | The movement curve decodes, but the source state is not proven as the normal movement state. |
| Nightmare | Sidestep up-side | Unknown | 0.000 | 0.000 | The route includes attack timing, and static recovery is not proven yet. |
| Seong Mi-na | Sidestep up-side | Unknown | 0.000 | 0.000 | The route includes attack timing, and static recovery is not proven yet. |
| Siegfried | Sidestep up-side | Unknown | 0.000 | 0.000 | The route includes attack timing, and static recovery is not proven yet. |
| Yoshimitsu | Sidestep up-side | Unknown | 0.000 | 0.000 | The route includes attack timing, and static recovery is not proven yet. |

## Forward Movement Read

| Character | Route | Label | Frame 8 | Frame 16 | Total | Player read |
|---|---|---|---:|---:|---:|---|
| Nightmare | Forward movement | Trusted basic | 0.658 | 0.557 | 0.659 | Clean route; frame 8 shows how quickly it gains ground. |
| Sophitia | Forward movement | Trusted basic | 0.502 | 1.185 | 1.526 | Clean route; frame 8 shows how quickly it gains ground. |
| Xianghua | Forward movement | Trusted basic | 0.392 | 0.541 | 0.551 | Clean route; frame 8 shows how quickly it gains ground. |
| Yoshimitsu | Forward movement | Trusted basic | 0.279 | 0.533 | 1.020 | Clean route; frame 8 shows how quickly it gains ground. |
| Raphael | Forward movement | Trusted basic | 0.120 | 0.307 | 2.617 | Clean route; frame 8 shows how quickly it gains ground. |
| Voldo | Forward movement | Trusted basic | 0.066 | 0.352 | 1.746 | Clean route; frame 8 shows how quickly it gains ground. |
| Siegfried | Forward movement | Trusted basic | 0.065 | 0.136 | 0.500 | Clean route; frame 8 shows how quickly it gains ground. |
| Mitsurugi | Forward movement | Trusted basic | 0.060 | 0.028 | 0.278 | Clean route; frame 8 shows how quickly it gains ground. |
| Ivy | Forward movement | Trusted basic | 0.010 | 0.062 | 0.087 | Clean route; frame 8 shows how quickly it gains ground. |
| Hwang | Forward movement | Trusted basic | 0.007 | 0.054 | 0.078 | Clean route; frame 8 shows how quickly it gains ground. |
| Amy | Forward movement | Trusted basic | 0.000 | 0.000 | 0.000 | Clean route; frame 8 shows how quickly it gains ground. |

Not ranked in this group: 18 rows (18 unknown, 0 measured but route unclear).

| Character | Route | Label | Frame 8 | Frame 16 | Why it is not ranked |
|---|---|---|---:|---:|---|
| Cassandra | Forward movement | Unknown | 0.665 | 1.298 | The route includes attack timing, and static recovery is not proven yet. |
| Geralt | Forward movement | Unknown | 0.373 | 0.976 | The route includes attack timing, and static recovery is not proven yet. |
| 2B | Forward movement | Unknown | 0.338 | 0.927 | The route includes attack timing, and static recovery is not proven yet. |
| Cervantes | Forward movement | Unknown | 0.333 | 0.698 | The route includes attack timing, and static recovery is not proven yet. |
| Talim | Forward movement | Unknown | 0.266 | 0.724 | The route includes attack timing, and static recovery is not proven yet. |
| Tira | Forward movement | Unknown | 0.236 | 0.472 | The route includes attack timing, and static recovery is not proven yet. |
| Astaroth | Forward movement | Unknown | 0.216 | 0.619 | The route includes attack timing, and static recovery is not proven yet. |
| Azwel | Forward movement | Unknown | 0.200 | 0.508 | The route includes attack timing, and static recovery is not proven yet. |
| Haohmaru | Forward movement | Unknown | 0.173 | 0.211 | The route includes attack timing, and static recovery is not proven yet. |
| Hilde | Forward movement | Unknown | 0.154 | 0.680 | The route points at attack timing with an invalid time window, so we cannot prove when movement ends. |
| Kilik | Forward movement | Unknown | 0.149 | 0.210 | The route includes attack timing, and static recovery is not proven yet. |
| Seong Mi-na | Forward movement | Unknown | 0.124 | 0.225 | The route includes attack timing, and static recovery is not proven yet. |
| Unknown (cid 066) | Forward movement | Unknown | 0.124 | 0.485 | The route includes attack timing, and static recovery is not proven yet. |
| Taki | Forward movement | Unknown | 0.103 | 0.447 | The route includes attack timing, and static recovery is not proven yet. |
| Setsuka | Forward movement | Unknown | 0.100 | 0.699 | The route includes attack timing, and static recovery is not proven yet. |
| Zasalamel | Forward movement | Unknown | 0.087 | 0.129 | The route includes attack timing, and static recovery is not proven yet. |
| Groh | Forward movement | Unknown | 0.036 | 0.083 | The route includes attack timing, and static recovery is not proven yet. |
| Maxi | Forward movement | Unknown | 0.000 | 0.000 | The route includes attack timing, and static recovery is not proven yet. |

## How To Use This

If you are asking who escapes fastest, read frame 4 and frame 8 first.

If you are asking who creates the most space, read total distance, but remember that late movement may not save you from fast active frames.

If a character is unknown, do not read that as bad movement. It means the movement is mixed with state, recovery, or offensive timing that needs stronger proof before ranking.

## Why Distance Is Not The Whole Answer

The curve tells us how much space the route tries to create. It does not prove a real whiff by itself.

Practical evasiveness also depends on hurtbox pose, the attacker's reach, body collision with the opponent, pushback, walls, ring edge, terrain, and when the defender can block or punish.

## Information We Do Not Currently Have

We do not yet have unresolved internal move-bank bucket routing; 384 of 384 audited bucket links are resolved.

We do not yet have confirmed static recovery for 203 selected movement rows.

We do not yet have complete guard, cancel, and punish timing for every movement route.

We do not yet have complete left-versus-right sidestep trust for the whole cast.

We do not yet have hurtbox movement over time during every movement option.

We do not yet have wall, ring edge, terrain, and opponent collision adjustment modeled statically.

We do not yet have matchup-specific whiff results against common attacks.
