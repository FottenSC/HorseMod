# G1 selected LuxTrace continuation ownership

The first candidate is now the two observed group-5 LuxTrace roots, rather than LineBatch. Existing trace capture already owns their supported state. The world executor completes groups 3 and 4 before group 5; this avoids making group-2 physics independence the first proof. It does not establish complete worker quiescence.

## Verified boundaries

| Reference / resource | Acquisition or native consumer | Required release / protection |
| --- | --- | --- |
| Dequeued graph task | Constructor142156650 transfers completion reference; queue140D2B9F0 removes node | Same native wrapper14215ED20 must eventually complete and recycle it exactly once. No arbitrary FinishManagerTask. |
| Completion event | task+40, constructor-transferred reference | Native140D20A70 closure, reference decrement+48, then TLS task recycle. Neither timeout nor hold signals completion. |
| Trace component and embedded tick | Generic tick owner+50; concrete table143360CA8; tick1408D8BF0 ->1408D93B0 | Earliest contents destructor1408CCAA0 precedes array/controller release; wrapper1408CD7E0 subsequently frees allocation. Task reference does not pin this owner. |
| Generic component's actor owner | Native component constructor finds Outer-chain AActor and writes component+190 | This is ALuxTraceManager, table143361F98, not the battle chara. Physical destructor1408CD120 first frees manager+3B0 map. It does not wait for this task. |
| Logical actor death | World entry141EECDF0(world, actor, ...) marks death and invokes lifecycle callbacks/unregistration | Wrapper141C11C60 alone is insufficient because direct world-entry callers exist. Never clear death flags to resume. |
| Battle chara | BeginTrace1408D5FF0 separately sets component+490 | Existing capture identifies chara, VFX manager and root transform. Its concrete physical lifetime still needs protection for a retained native callback. |
| World | task+28; native payload reads world+930/+934 for time | Earliest physical body1421B4E60; teardown can start before physical destruction, so a destructor-only guard is insufficient. |
| Prerequisites | tick+20/+28; optional payload diagnostic traversal before callback | Add142159B90/remove142164920 can replace backing. Nested references and writer exclusion remain required. |
| Required callbacks | Trace completion+460, VFX completion registration, inherited ReceiveTick/latent processing | Must run with original ordering after resume; no Boolean cancellation reinterpretation or callback suppression. |
| CRI delegates | Independent worker reaches140544470; resolver virtual+8 then row callback+10 under manager+D0 | All20 observed boundaries205..224 had an empty table. A snapshot is not a temporal lease. In-flight entry exclusion is still needed for a hold. |
| Application and engine post | Existing owned world arena detaches after each Advance; framework supports deferred EngineTickPost | A separate strict hold must precede session-exit/retirement/services and defer the initial engine post. Existing manager continuation is not this path. |
| Shutdown | GuardedMain may enter14039B420 without another TickApplication | Must contain shutdown before owner teardown while work is retained. |

Native sources: [selected trace tick/destructor](rollback-g1-selected-trace-native-2026-09-23.json), [owner relationship and retirement signatures](rollback-g1-luxtrace-owner-retirement-native-2026-09-23.json), [CRI entry monitor boundary](rollback-g1-cri-callback-entry-monitor-native-2026-09-23.json), [independent resource callbacks](rollback-g1-cri-independent-callback-surfaces-2026-09-23.json).

## Implemented and tested in this continuation

The existing observer now takes a bounded, nonblocking, locked snapshot of the CRI sparse table before actual LuxTrace task entry. It does not invoke context resolvers, mutate native state, or claim a lifetime lease. At most128 rows are copied; logging happens after native lock release. The existing consumer-census runner requires valid snapshots and retains raw rows and reconstructible sources.

Required build/native checks PASS. Stock360 census PASS, run replay-5308c168b8e843e8bdf9b06b43b5167e: all20 snapshots205..224 valid and empty. No recovery, simulation-equivalence, coherence or performance qualification is claimed. [Immutable-linked receipt and six-file deployment verification](rollback-g1-trace-cri-census-2026-09-23.json). No game remains; Steam98544 remains running.

## G1 feasibility result

Suspension and complete-B recovery remain unproven. No native abandonment protocol was established. The missing mechanism is a synchronized lifetime/execution-domain admission covering the selected trace, its manager/chara/world and prerequisites, followed by a separate strict host continuation. A task/event reference or an empty callback snapshot cannot supply that mechanism. Until it exists, ConsumerHeld must not be enabled and no cancellation/recovery claim is permitted.

The next implementation boundary is now concrete: admission before14215D250, earliest owner/logical-death/teardown and prerequisite protection, then the retained-task record and strict host branch. Only after native ownership passes may unchanged hold/resume run, followed separately by a proven recovery disposition and B+120 comparison. Baseline receipts remain scoped to their identities; no408,600, integration or affected30 rerun occurred. Debris remains exact/Unresolved and state policy is unchanged.
