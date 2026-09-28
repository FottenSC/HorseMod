> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# DotVanisher: general-fix investigation — 2026-09-13

Implementation follow-up: a bounded watch-assignment recovery candidate is now built and offline-tested. See [implementation and validation](dotvanisher-recovery-implementation-2026-09-13.md). The investigation below records the preceding static findings and design limits; references to no behavioral patch describe that earlier checkpoint.

DotVanisher cannot be treated as a general fix for spectator connection/loading failures. Its only intervention delays host-side retirement of watchers collected when a battle ends. Native watch admission and client route validation have independent failure paths. The host-only installation described by the user leaves remote clients' code unchanged, but there is no evidence that the remote players' lack of the mod caused this incident.

The user was host, with DotVanisher installed; the two players did not have it. They think the players had not connected yet, but could not see their screens. This does not identify which endpoint failed, which phase it reached, or which branch fired. No claim below identifies the incident's cause.

## Evidence and scope

Static analysis used the existing SoulcaliburVI.exe Ghidra program through native MCP, DotVanisher source, and existing local exports. [Retained native decompilation](../../../investigations/evidence/native/dotvanisher-general-fix-native-2026-09-13.json) contains 27 relevant functions and watch-connect assembly. Decompiled scratch variables and inherited partial types are not authoritative complete layouts. Control flow, accesses, constants and observed callers support the conclusions. Hermes independently challenged the interpretation.

No game launch, process attachment, deployment, build, live test or shared configuration change was performed during this follow-up. The earlier audit's successful build is only a build result. This investigation changes documentation and scoped Ghidra annotations, not the mod binary.

## 1. The patched list belongs to battle-end retirement

`142E6EFA0`, now named `QueueHostLinkedSpectatorsForBattleEnd`, resolves a player through the host transport's peer lookup, then enumerates that player's linked watcher IDs through peer vtable `+0x128`. For each watcher, an absent peer or peer state other than 1 causes an append through `142E4F0A0`. State-1 watchers are skipped. The player's vector is then cleared through `+0x140`.

The only observed static code caller of the append helper is `142E6F0BF` in this collector. The collector's observed static callers are battle-result event 4 (`142E65580`, calls at `142E65905` and `142E65934`) and battle-end-to-lobby (`142E68120`, calls at `142E684E8` and `142E6850F`). These are static call-graph observations; unknown indirect paths are not ruled out.

The list is at full HostSys `+0xB8` with count `+0xC0`. The tick function receives full host `+8`, explaining DotVanisher's relative sentinel `+0xB0`, count `+0xB8`, and timer `+0xC0`. At `142E61A04`, the host tick examines the count; it accumulates delta and retires nodes when elapsed time exceeds 30 seconds. Float bytes at `143E8A474` are `00 00 F0 41` (30.0). It queues watch-end work and emits event 10.

DotVanisher suppresses that timer for 90 wall-clock seconds, after which the native 30-second countdown proceeds. This can delay forced retirement. It neither creates a connection nor guarantees loading-data delivery. Increasing this grace, or fixing its stale-epoch detection, does not cover initial admission.

## 2. Client requests have a separate 20-second deadline

`142E5E6C0` (`ConnectSys_RequestWatchConnect`) requires local ready-channel state 5 and queues event `0x13` with 20 seconds. The native float at `143E8A454` is `00 00 A0 41` (20.0); the request assembly loads it at `142E5E7AB`.

`142E53310` (`QueueLuxorActiveConnectRequest`) maintains an independent list/count at active-connect `+0x268/+0x270`. Each `0x120`-byte node contains:

| Offset | Meaning | Consumer |
| --- | --- | --- |
| `+0x10` | Event opcode | Response opcode matching |
| `+0x20` | Response delegate | `142E6B060`, message `0x05` handler |
| `+0x70` | Start/send delegate | Queue start/advance |
| `+0xC0` | Timeout delegate | `142E6E480`, state-update task |
| `+0x110` | Remaining seconds | Decremented by update delta |

An empty queue starts the request synchronously before inserting its node. The update task invokes the timeout delegate when the head's remaining time becomes negative. For event `0x13`, start is `142E54020`, response is `142E53BA0`, and timeout is `142E53B40`; timeout reaches `142E69EC0` with sender code 10 and failure state/substate 5/9.

Older Ghidra annotations had response and timeout roles reversed. The prototype, local delegate names, node field names and plate comment were corrected from both consumers and read back. This matters when choosing a repair hook: intercepting the response callback under the old failure label would change the wrong transition.

## 3. Assignment success still requires a local peer route

These stages are distinct:

1. Host event `0x13`, `142E69100`, handles watch-connect eligibility and queues its response. It does not append the battle-end retirement list.
2. Client `142E5EDF0` requests a specific player using event 7 and a 20-second queued request. A missing local peer lookup fails before submission.
3. Host event 7 reaches `142E695C0`. It resolves requester and requested player, rejects missing/self assignments with status `0xB` and ID `0xFFFF`, or links the watcher to a selected player. When the requested player has a linked opponent, vector counts can select the opponent with fewer watchers.
4. The host's deferred callback at `142E53B90` is a jump to `142E51A00`. That function serializes message `0x0C`, a status dword and assigned ushort player ID, then sends it through the connect sender interface. This closes the static path from assignment to the client's response handler.
5. Client `142E6C540` (`HandleConnectSysWatchResponseMessage`) returns immediately for nonzero status. For status zero, it must resolve both the assigned peer and its route through `GetLuxorConnectRouteService` vtable `+0x30`.
6. With peer and route present, it sets local role 3/substate 7, stores the watch-end routing tag, and dispatches ready `(5,0)`. With either absent, it resets local state, sets substate 9 and, unless its state accessor returns 6, queues event 10 and dispatches `(0xD,7)`.

Thus an assignment packet reporting success is insufficient to establish a watch connection. There is no wait/retry for a missing route in this response handler. A route-publication race is a concrete hypothesis to test, not a proven defect: a missing route could instead correctly reflect departure or a failed underlying connection. Suppressing this failure indiscriminately would hide that distinction.

The response-handler plate was corrected: its previous description incorrectly grouped nonzero status with missing-route failure and characterized the function too broadly as propagation of host watch-end removal.

## 4. Loading and match watchdogs are also separate

`14050DD60` initializes a scene-owned BattleSync object and installs weak-object receive callback `140511CF0` on channel 6. The callback dispatches messages including `ReadyToConnect`, profile, `CharacterSet`, `Stage`, `GuestAll`, `All`, and `AllComplete`. `ReadyToConnect` is gated on substate 1; `All` can be suppressed once character and stage flags are both set; `AllComplete` sets substate `0xC`. The common tail broadcasts a script multicast. Request handlers send their data only when the corresponding received flag is set.

These gates show that transport admission and loading completion are different boundaries. They do not prove packet loss, a missed Blueprint subscriber, or stale flags caused this incident. Repairing the loading path requires following the scene's subscriptions and native/Blueprint state transitions, using Kismet as primary evidence before changing generated pseudocode.

`142E6DFE0` implements another watchdog driven by active-connect flag bits 6/7 and a three-second timer. It is installed by retry/delay tasks `142E6ECF0`/`142E6EEE0`; `142E5D730` initializes relevant match-transition paths. The flag producers and full ownership were not resolved in this investigation. This watchdog is not an appropriate blanket timer-extension target.

## General repair boundary

### Follow-up: concrete route-service ownership

Further native tracing verified the actual vtable rather than relying on inherited function descriptions. `142E07390`/`142E080B0` install `143D06478` at connect-root `+8`. Its `+0x30` entry is `142E06520` (`GetLuxorPacketRouteWriter`), which locks the registry and performs lookup only. Its `+0x10` entry is `142E04C60` (`AcquireLuxorConnectRouteWriter`), which can assign an available writer. `142E0AF00` invokes that acquisition separately through a callback registered by the root constructor. Watch-response handling calls lookup, not acquisition. The refresh-task plate previously used misleading acquisition wording and was corrected.

Acquisition can return empty: `142E04460` requires a reusable writer satisfying `142E02310` (nonreplacement route, vtable `+0x10` false and `+8` zero). Therefore neither assigning a route unconditionally nor waiting indefinitely for one is justified. The incident has not established slot exhaustion or delayed publication. [Additional native evidence](../../../investigations/evidence/native/dotvanisher-route-ownership-2026-09-13.json) retains these functions.

The concrete generalization candidate is to keep a successful assignment pending during a recoverable route transition, then complete it exactly once when the matching route exists. It must retain decoded assignment data, not a packet cursor or unowned peer pointer; key it to the current room/watch generation; and execute on the native owner thread. Native response acknowledgment, queue completion, timeout, callback thread, cancellation and teardown ownership must be established before implementing this deferral. Otherwise the queued request could already be expired or disposed when the deferred success runs. This candidate covers a class of admission-ordering failures, not all loading failures, and applies at the endpoint executing ConnectSys. Hermes independently reviewed these constraints. No behavioral patch is implemented by this design.

The durable repair must preserve admission, route readiness and loading completion as separate operations owned by a particular room/watch attempt. A single host retirement timer cannot implement that contract. The smallest implementation should remain within the existing native request/scene owners; a new networking subsystem is not justified by this evidence.

Before selecting a behavioral patch, a bounded trace must distinguish: request submission/start/response/expiry; host assignment result and selected peer; client peer/route lookup and native failure branch; BattleSync receive/completion and subscriber lifetime; explicit cancel/end and scene teardown. Correlate room/attempt generation and peer ID, rather than pointer identity alone. Record timestamps and state changes without payload dumps or per-frame logging.

The evidence supports the following conditional repairs, not unconditional patches:

| Observed failure | Repair to investigate | Required boundary |
| --- | --- | --- |
| Valid assignment arrives before a route that subsequently becomes usable | Retain assignment and resume once route readiness is published, with a bounded deadline | Same live request, peer and room generation; retain references correctly; do not re-enter a disposed callback |
| Request expires because native admission cannot make progress within its deadline | Repair request start/progress ownership, then evaluate a bounded phase-specific deadline | Modify the owning endpoint; do not fabricate responses or treat stale traffic as progress |
| Loading data precedes a listener, or duplicate state gates suppress necessary completion | Make readiness/completion derivable from current phase state, with verified idempotent delivery | Scene-owned subscriber lifetime, reset on exit/re-entry, verified Kismet/native consumer ordering |
| Actual battle-end retirement grace is reused across distinct attempts | Track the verified battle-end episode instead of host/sentinel addresses | Explicit insertion/retirement lifecycle, including clear-and-refill between ticks |

Cancel event 9 (`142E69310`), watch end 10 (`142E69B40`), incoming watch end/reset (`142E6C3B0`/`142E6C4A0`), peer departure and room/scene teardown must remain authoritative. Retry cannot revive a cancelled operation or transfer one peer's state to another. Cleanup must release any retained callbacks/references before re-entry.

A host-only fix is possible only for failures the host can actually prevent through native protocol behavior. It cannot directly extend an unmodified remote client's local countdown or change that client's missing-route branch. The code does not yet prove that a host-side ordering change suffices for every endpoint. Installing this same timer mod on all machines is therefore not an established solution either.

## Validation required before claiming a fix

Once game use is available, first capture one bounded failing attempt with endpoint roles recorded. Choose a specific repair from that evidence, then validate entry, active connection/loading, controlled failure, explicit cancel, exit, re-entry, scene change and process cleanup. Include late route availability, permanent route failure, concurrent watchers, repeated battles, delayed completion and host-only versus relevant-client coverage. A successful connection alone is insufficient; both bounded recovery and genuine failure/cancellation must terminate cleanly.

Current result: the old fix's narrow coverage is established, and the initial admission/assignment path is mapped. The actual incident cause and a runtime-validated general fix remain unresolved. No speculative timer patch was added. Ghidra changes were saved and read back; these were scoped naming/type/comment corrections, not a complete function-documentation certification.
