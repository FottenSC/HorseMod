# Rollback Netcode Implementation Plan - 2026-07-01

Branch: `rollback`

Source investigation:
`E:\DevShitPosts\SC6Mods\SC6ModdingDocs\docs\cookbook\rollback-netcode-investigation.md`

Scope:

- Build rollback state save, restore, prediction, correction, resimulation, and
  verification in `HorseMod`.
- Add testing functionality that can prove same-input restore/resim equality and
  delayed-input correction.
- Continue Ghidra MCP cleanup while implementing: function names, prototypes,
  variable names, variable types, labels, comments, and structs for directly
  touched battle/input/snapshot code.
- Maintain a rollback evidence table that maps each copied field/range, hash
  field, hook boundary, and exclusion to Ghidra/runtime proof.
- Do not remove local input delay in this phase. That is phase two after rollback
  correctness is proven.

## Implementation Status - 2026-07-03

Implemented and validated on branch `rollback`:

- Developer-gated rollback lab, request-file automation, and JSONL/UE4SS trace
  events.
- Explicit global snapshot manifest for rollback extras not covered by
  HgCpuDirect.
- Native HgCpuDirect snapshot/restore wrapper with dynamic used-byte hashing,
  dynamic P2 stream base handling, and documented native-canonicalization policy
  ranges.
- KHit topology capsule for the state native HgCpuDirect walks but does not
  serialize: per-chara list-control bytes plus reachable `0xA0` KHit node
  images.
- Direct `LuxBattle_PerFrameTick` resim harness with a bypass trampoline,
  deterministic input streams, baseline same-input equality, and delayed-input
  correction.
- Delayed-input tests now require a real divergent prediction path before
  accepting corrected resim success.
- Lifecycle epoch binding for the lab manifest. The controller waits for live
  P1/P2 chara pointers, records them in the manifest hash, resets probes when
  they change, and restore refuses with `lifecycle-epoch-mismatch` if the live
  chara pointers no longer match the captured state.
- Rollback resim matrix test case covering rollback windows `1,2,8,15,60` for
  both baseline-oracle and delayed-input correction. The matrix emits only
  context-wait summaries before the battle epoch is bound, then emits exactly
  one result per case/window.
- Read-only InputLog/cache ownership probe. The probe snapshots
  `BM+0x478 -> ALuxBattleFrameInputLog`, the `InputLog+0x394..+0x4418`
  ownership window, the `+0x3C0` 0x4000-byte cache, the `+0x3B8/+0x3BC`
  current-input mirror, the `+0x3A4` master clock, and the `+0x4410` drain
  cursor before and after delayed-input rollback resim. It fails if direct
  local resim mutates stock InputLog/cache state.
- Scoped rollback RNG caller tracing around baseline, predicted, and corrected
  resim phases. The trace proved warmed cache failures were caused by visual
  stage-wind calls into the shared `LuxMoveVM_GetRandU32` LFSR, not by explicit
  snapshot restore failure.
- Rollback resim now gates visual stage-wind RNG paths with `WindRngGate`
  during hidden baseline/predicted/corrected stepping. Ghidra comments were
  added to the wind update/init/schedule functions that appeared in the caller
  trace.
- Post-review motion-bank remediation now treats CMatrixBank physical provider
  buffers as presentation cache, while requiring restored CMatrixBank control
  bytes and a new per-chara MoveVM motion-tail capsule
  (`chara+0x96490..+0x9748F`) to match. This closes the earlier false pass
  where `hgcpu_motion_bank_match=false` could still report policy success.
- Replay seek harness support for rollback lab request files:
  `tools\replay_seek_test_run.py --rollback-lab-case ...` writes
  `Saved\rollback_lab_request.txt` after any requested game shutdown and before
  launch, and `--require-rollback-cache-ownership` fails if the selected trace
  does not contain `rollback_cache_ownership ok=true`.
- Phase-8 transport groundwork in `horselib\RollbackTransport.hpp`. This is a
  model and packet contract only, not a live Steam/SC6 transport hook. It uses
  absolute frame ids, confirmed-frame acknowledgements, input/state-hash/resend
  metadata, prediction age, rollback depth, duplicate/conflict/reorder metrics,
  over-window late-input refusal, and explicit game-thread cache ordering
  decisions.
- Same-machine fault-injected transport validation now lives in
  `RollbackFaultInject.hpp` and `RollbackFaultInjectSelfTest`. It runs two
  deterministic local peers through seeded HRB1 transport profiles
  (`clean_0ms`, `wifi_50ms_jitter`, `bad_wifi_120ms_5pct_loss`,
  `overseas_180ms_2pct_loss`, `spike_every_10s`, `burst_loss_500ms`, and
  `corrupt_probe`), exercises latency/loss/reorder/duplicate/corruption plus
  ack/resend recovery, hashes each peer's actually accepted per-frame payload
  history, and requires both peers to match the expected deterministic checksum.
  Loss/corruption/burst profiles must also show a first-send fault later
  accepted from a resend.
  The validation runner accepts `--profile <name>` for targeted profile runs.
  This is intentionally a Horse-owned local model, not Steam/SC6 launch bypass
  work and not live packet injection.
- Harness gates now assert the strong rollback proof fields directly instead
  of trusting only `ok=true`: cache ownership requires cache/drain equality,
  corrected resim equality, HgCpu policy match, motion-bank control match,
  MoveVM motion-tail match, zero motion-bank mismatches, and zero unignored
  mismatches; matrix log parsing requires the same policy/control/tail fields.
- Second low-context review found a blocking transport-model acknowledgement
  bug: `0xFFFFFFFF` was being used both as "no ack" and as a real maximum frame.
  Remediation added `kRollbackTransportNoFrame`, a
  `peer_confirmation_known` bit, monotonic real-ack handling, sentinel no-op
  handling, regression counting for lower real acknowledgements, and expanded
  invalid-packet/field roundtrip self-tests.
- Phase-8 session-policy groundwork now includes `RollbackPeerHandshake`,
  `ValidateRollbackHandshake`, and `CheckRollbackStateHash`. The offline model
  rejects protocol/build/HorseMod/gameplay-manifest/window/hash-policy
  mismatches, supports warn-only versus enforced state-hash mismatch behavior,
  and has a loopback delay/reorder path that proves contiguous confirmation and
  diagnostic metrics still advance when frames arrive out of order.
- `RollbackOnlineSession.hpp` now adds the packet-to-correction adapter model:
  it builds local absolute-frame packets with peer confirmations, predicts
  remote inputs by held-last input, accepts delayed/out-of-order confirmed Horse
  packets, reports correction start/depth, rejects duplicate/conflict/late
  packets, enforces cache-order gates, and never permits live InputLog cache
  writes. This is still a tested model, not a live Steam/SC6 transport hook.
- `RollbackLiveBoundaryHook` now includes a lab-only cache-injection probe at
  the stock consumer boundary. The probe computes the same cache entry consumed
  by `LuxBattleChara_UpdatePlayerInputData_FromRoundCache @ 0x1403FCD10`,
  performs an idempotent game-thread `SafeWriteBytes`, verifies the stock
  consumer copied the value into the `BM+0x1498` current-input slot, and restores
  the cache cell. This proves mechanical write/read/restore access at the live
  cache boundary.
- `RollbackLiveBoundaryHook` also includes a stronger lab-only
  `cache-prediction` probe. It writes a non-idempotent predicted cache value,
  verifies the stock consumer observed it through `BM+0x1498` and `BM+0x14A8`,
  then restores the cache entry, current-input slot, and input-pair slot before
  the detour returns. This proves non-idempotent cache prediction mechanics at
  the replay consumer boundary, but still deliberately does not claim live peer
  transport or real online-match scheduling.
- GekkoNet is now integrated as the first live-session candidate, pinned to
  commit `02c447c9d6a8a478070b2c162d15370dfad34482`, built as a no-ASIO static
  dependency, and exercised through `RollbackGekkoSession.hpp`,
  `RollbackGekkoSelfTest`, and the in-game `gekko-session` lab case. This proves
  Horse can drive GekkoNet save/load/advance/rollback-advance/desync events, but
  does not yet connect GekkoNet to Steam/SC6 packets or live online matches.
- `RollbackGekkoAdapter.hpp` adds the first Horse-owned `GekkoNetAdapter`
  bridge in deterministic loopback form. It pumps two `GekkoGameSession`
  instances through custom send/receive/free callbacks, proves GekkoNet's
  malloc/free ownership contract, bidirectional payload movement, no desync,
  and matching final checksums. Its advance-event path now decodes all gameplay
  inputs through `RollbackGekkoGameplayInputBridge` and advances deterministic
  state from those decoded values. It deliberately uses fixed queued packets
  rather than Steam or SC6 stock online traffic.
- `RollbackGekkoTransportBridge.hpp` now wraps each GekkoNet adapter payload in
  a Horse-owned `HRG1` v2 envelope with source/destination peer ids, a 64-bit
  session id, sequence, payload length/hash, and embedded
  `RollbackTransportPacket` metadata. The adapter decodes and accepts that
  metadata through `RollbackTransportPeerModel` before exposing only validated
  payload bytes back to GekkoNet. Decode now requires the embedded metadata
  input/hash fields to match the HRG1 payload hash, closing the low-context
  review finding where metadata for payload A could accompany payload B; strict
  decode paths also reject wrong source peer and wrong session id.
- `RollbackGekkoUdpAdapter.hpp` moves the Horse-owned Gekko adapter from an
  in-memory delayed queue to real localhost UDP socket I/O. It starts WinSock,
  binds nonblocking IPv4 loopback sockets, proves a manual HRG1 UDP roundtrip,
  pumps two `GekkoGameSession` instances through the custom adapter callbacks,
  rejects wrong UDP endpoint plus wrong source/destination/session identity
  before payload delivery, and still decodes Gekko advance-event gameplay
  buffers through
  `RollbackGekkoGameplayInputBridge`. This is a socket-backed lab adapter, not
  Steam/SC6 live packet injection and not local input-delay removal.
- `RollbackLiveTransportQueue.hpp` adds the next guarded live-transport model:
  network-side HRG1 decode validates source peer, destination peer, session id,
  and metadata before queueing, while game-thread drain into
  `RollbackOnlineSessionModel` requires stock-drain completion unless an
  explicit drain-bypass policy is selected. This proves the cache-order and
  peer/session identity contract for the next bridge seam, but still does not
  replace Steam or SC6 live peer packet I/O.
- `RollbackGekkoGameplayInputBridge.hpp` now makes the Gekko advance-event input
  boundary explicit: it decodes `GekkoAdvanceEvent` input buffers as
  `uint32_t[player_count]` gameplay values, rejects malformed/null/unsupported
  buffers, and composes with `RollbackLivePeerPipeline` to prove HRG1 metadata
  `local_input` remains a payload hash while only decoded gameplay input can
  reach the cache shadow. It now exposes a named `bad_frame=1` rejection gate
  and pins the actual Gekko decoded event-stream checksum to `0x79B1B776`.
  Ghidra plate notes on
  `GetCachedInputForFrameInputLogSlot @ 0x1403F0720` tie this width to the
  stock cache entry's `uint dwInputValue`.
- `RollbackEndToEndHarness.hpp` composes the local rollback/Gekko/live-peer
  model in one gate. It decodes a two-player `uint32_t[2]` Gekko-style payload,
  seeds metadata predictions, writes divergent gameplay predictions, enqueues
  source/destination/session-bound HRG1 packets both directions, requires stock
  drain before metadata acceptance, proves metadata correction is needed, keeps
  HRG1 metadata/hash separate from gameplay cache values, applies decoded
  confirmed gameplay inputs on the game thread, consumes confirmed cache entries
  by player/frame identity, rejects wrong identity and network-thread cache
  writes, and verifies predicted checksums diverge while confirmed checksums
  converge.
- `RollbackStockTransportSurface.hpp` codifies the Ghidra-backed native online
  transport surface before any live send hook is attempted: the shared transport
  session pointer is `16` bytes, stock input uses vtable slot `+0x20`/channel 5,
  BattleSync uses slots `+0x18/+0x28`/channel 6, and channel 7 remains a
  high-level KV mirror. The guard rejects HRG1 on all stock surfaces before
  considering any Horse-owned adapter marker and allows HRG1 only through the
  distinct Horse-owned adapter path with the Horse adapter provenance cookie,
  actual nonzero source peer, distinct nonzero destination peer, and nonzero
  session id values. Ghidra plate/PRE cleanup at
  `AcquireLuxOnlineTransportSessionSharedPtr @ 0x1403F0CC0` records that native
  stock transport acquisition is not Horse adapter provenance; the Horse-owned
  HRG1 route must carry the provenance cookie plus values matching the
  activation request.
- `RollbackStockTransportObserveHook.hpp` adds the first runtime live
  transport-observability seam for this surface: observe-only x64Detours on
  `AcquireLuxOnlineTransportSessionSharedPtr`, stock opcode 0, stock opcode 1,
  BattleSync `RequestStage`, and the receive-side
  `LuxOnline_PushToRingBuffer_WithCriticalSection` enqueue boundary. The hook
  records counts and last-seen pointers, frames, channel/message type, and
  receive wrapper identity, then immediately calls the original. It is
  deliberately not an injection hook and does not mutate stock payloads, packet
  wrappers, or cache cells.
- `RollbackLiveOnlineCapture.hpp` combines the stock transport observe report
  with the live drain/cache-consumer boundary report. The readiness lab now
  proves all observe-only stock send/receive hooks and drain/consumer hooks are
  installed and active without requiring live traffic. The stricter live gate is
  opt-in and requires a real online match to produce nonzero native transport
  acquisition, input send, BattleSync send, receive enqueue, drain enter/exit,
  cache consumer observation, and `live_order_proven=true` before any guarded
  HRG1 injection work is allowed.
- `RollbackLiveActivationGate.hpp` adds the final pure policy gate before any
  future HRG1/Gekko live path can leave observe-only mode. It requires explicit
  operator arming, direct capture readiness (`observe_only`, stock observe
  hooks/trace, and boundary hooks/trace), completed live traffic/order proof, no
  boundary violation, stock input plus BattleSync liveness, receive enqueue,
  drain/consumer proof, nonzero native session and InputLog pointers, HRG1
  payload, Horse adapter route provenance, strict route identity, an allowed
  Horse-owned route, route source/destination/session values matching the
  activation request, distinct nonzero peers, and a nonzero session id. It
  deliberately sends no packets and writes no stock cache cells.
- `RollbackController` now emits a `rollback_live_activation_candidate` event
  beside every `live-online-capture` verdict. The candidate evaluates the
  current capture through `EvaluateRollbackLiveActivation` using the configured
  Horse-owned HRG1/Gekko route identity. `tools\rollback_lab_test_run.py` and
  `tools\rollback_live_online_capture_analyze.py` can require this verdict with
  `--require-live-activation-candidate`; the request-file path carries the
  explicit operator arm, source peer, destination peer, and session id. This
  remains a pure gate: it does not send packets and does not write stock cache
  cells.
- Low-context review by Linnaeus found a P2 stale-evidence risk in the direct
  UE4SS log watcher: same-process live-online lines could be accepted without
  matching the current request id, and candidate route fields were only checked
  for nonzero values. Remediation added `request_id` to the human
  `live_activation_candidate` log line, filters both capture and candidate log
  lines by current `request_id`, and requires candidate source/destination/
  session fields to equal the requested route values. The durable JSONL
  analyzer already had case/request-id filtering and now also requires stable
  readiness when `--require-live-activation-candidate` is used directly.
  Follow-up review by Hypatia found the same stable-readiness requirement was
  still missing from the direct process exit path; remediation added a shared
  strict helper so a candidate can pass only when an accepted live-online
  readiness verdict exists and no later violation/regression was seen.
  Final review by Boyle found the durable JSONL analyzer still accepted
  internally self-consistent but wrong activation route values; remediation
  added expected source/destination/session CLI arguments, wires the full
  validation live-only path to pass those values to both the lab request and
  analyzer, and adds a wrong-route analyzer rejection self-test.
- `RollbackLiveActivationExecutor.hpp` adds the first activation-owned HRG1
  executor model. It refuses enqueue, metadata drain, prediction, confirmed
  gameplay apply, and cache consume operations unless
  `EvaluateRollbackLiveActivation` is ready. Once armed, it accepts only the
  activation request's Horse-provenance source/destination/session route,
  queues HRG1 metadata without cache side effects, requires stock drain before
  metadata acceptance, keeps metadata payload hashes separate from decoded
  gameplay inputs, applies prediction/confirmation only on the game-thread
  cache-order path, and rejects wrong source/destination/session,
  decoded-gameplay route mismatches, and network-thread cache writes.
- Final-review remediation hardened `tools\rollback_lab_test_run.py` so strict
  human-log gates parse exact whitespace-delimited `field=value` tokens instead
  of raw substrings. This prevents aliases such as `session=1` passing because
  `zero_session=1` appears in the same line, while preserving the durable JSONL
  analyzer for live-online capture.
- Third low-context review found a blocking handshake validation bug: matching
  invalid `RollbackHashPolicy` enum values could be accepted. Remediation added
  `RollbackHashPolicyValid`, rejects invalid local/remote policy values through
  `RollbackHandshakeValid`, and extended the transport self-test with invalid
  local, invalid remote, and matching-invalid policy cases.
- Lifecycle boundary refusal coverage now includes deterministic snapshot-copy
  validation for inactive snapshot/live epochs, changed chara/input-log
  identity, changed round number, changed round-start hash, and changed stage
  context hash. Live same-process resim restore remains guarded by active
  presence plus matching live chara pointers; broader BattleManager/InputLog/
  stage live epoch binding is still a future online gate.
- Low-context UDP review remediation closed two P2 gaps: the UDP adapter
  self-test now injects a valid HRG1 packet from the wrong UDP source endpoint
  and requires `wrong_endpoint=1`, and the full validation runner now builds and
  runs the CMake target `RollbackSnapshotSelfTest` instead of accepting a stale
  lower-case snapshot executable.

Latest validation:

- Gekko gameplay-input bridge validation on 2026-07-02 passed:
  `RollbackGekkoGameplayInputSelfTest` reported `enabled=1`, `raw=1`,
  `raw_p0=1`, `raw_p1=1`, `null=1`, `bad_frame=1`, `bad_size=1`,
  `bad_players=1`, `bad_slot=1`, `pipeline=1`, `payload_separate=1`,
  `advance_decode=1`, `rollback_decode=1`, `no_desync=1`,
  `decoded_events=49`, `decoded_inputs=98`, `rollback_advances=33`,
  `checksum=0x79B1B776`, and `checksum_expected=1`.
  `RollbackInputCacheAdapterSelfTest` now also reports
  `not_game_thread=1`, separately from `net_reject=1`.
  `build_and_deploy.bat` finished at `02-Jul-26 16:12:13.92`.
  In-game request-file lab trace
  `replay_trace_20260702_161220_pid51660.jsonl` emitted
  `rollback_gekko_gameplay_input ok=true` with all strict gates true and no
  rollback command-line parameters.
  Required strict replay regression
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-161258-seek.json`,
  trace `replay_trace_20260702_161258_pid83888.jsonl`, passed with
  `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and 0 state
  mismatches.
- Gekko adapter gameplay-decoder integration validation on 2026-07-02 passed:
  `RollbackGekkoSelfTest` reported `adapter_gameplay_decode=1`,
  `adapter_gameplay_slots=1`, `adapter_gameplay_state=1`,
  `adapter_gameplay_events=87`, `adapter_gameplay_inputs=174`,
  `bridge_encoded=345`, `bridge_decoded=339`, `bridge_bad=0`, and matching
  adapter checksums `0x68FC89EA/0x68FC89EA`.
  `build_and_deploy.bat` finished at `02-Jul-26 16:19:10.00`.
  In-game request-file lab trace
  `replay_trace_20260702_161916_pid79700.jsonl` emitted
  `rollback_gekko_adapter ok=true` with `gameplay_decode=true`,
  `gameplay_slots=true`, `gameplay_state=true`, `gameplay_events=87`,
  `gameplay_inputs=174`, and no rollback command-line parameters.
  Required strict replay regression
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-161954-seek.json`,
  trace `replay_trace_20260702_161954_pid81448.jsonl`, passed with
  `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and 0 state
  mismatches.
- Gekko UDP adapter validation on 2026-07-03 passed:
  `RollbackGekkoUdpSelfTest` reported `enabled=1`, `wsa=1`, `sockets=1`,
  `loopback=1`, `nonblocking=1`, `manual=1`, `wrong_endpoint=1`,
  `wrong_source=1`, `wrong_dest=1`, `wrong_session=1`, `sent=1`,
  `received=1`, `freed=1`, `bidirectional=1`, `bridge=1`, `bridge_meta=1`,
  `gameplay_decode=1`, `slots=1`, `state=1`, `checksums=1`, `save=1`,
  `load=1`, `advance=1`, `rollback=1`, `no_desync=1`, `frames=48`,
  `packets_sent=20`, `packets_recv=20`, `bridge_encoded=20`,
  `bridge_decoded=20`, `bridge_bad=0`, `endpoint_bad=0`,
  `gameplay_events=5`, `gameplay_inputs=10`, and matching checksums
  `0xF0AD2560/0xF0AD2560`.
  In-game request-file lab trace
  `replay_trace_20260703_012507_pid52308.jsonl` emitted
  `rollback_gekko_udp_selftest ok=true` with socket, manual-roundtrip,
  endpoint/source/destination/session rejection, bridge, gameplay decode,
  rollback, and checksum gates true.
- The pinned local validation bundle command is now
  `python tools\rollback_full_validation_run.py`. It runs build/deploy, the
  standalone rollback self-tests, request-file in-game lab gates, and the
  strict replay seek regression, writing a JSON report under
  `reports\rollback_validation`. Live peer proof is intentionally opt-in
  because it requires an actual SC6 online peer/match to generate stock observe
  traffic; use `--live-online-only --require-live-activation-candidate` for
  that operator-driven attach run.
  Full bundle run `E:\myMods\reports\rollback_validation\rollback_validation_20260703-082341-584926.json`
  passed on 2026-07-03: preflight SC6 close, build/deploy, self-test target
  build, seventeen standalone self-tests including `RollbackGekkoUdpSelfTest`,
  `RollbackFaultInjectSelfTest`, and the rebuilt `RollbackSnapshotSelfTest`,
  eleven request-file in-game lab gates
  including `lab-gekko-udp`,
  `lab-end-to-end`, `lab-live-activation`,
  `lab-live-activation-executor`, `stock-observe`, and
  `live-online-capture`, the post-capture
  `analyze-live-online-capture-trace` gate, and strict replay
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260703-082621-seek.json`
  all passed. The validation report records no warm-process strict replay
  retry; the final strict replay report has `final_passed=true`,
  `summary_passed=true`, empty `failed_case_labels`, empty `failure_groups`,
  `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, 0 state
  mismatches, and trace `replay_trace_20260703_082622_pid10756.jsonl`.
  The new fault-injection target reported `profiles=7/7`, `clean=1`,
  `wifi=1`, `bad_wifi=1`, `overseas=1`, `spike=1`, `burst=1`,
  `corrupt=1`, and `converged=1`. The targeted post-review
  `--profile corrupt_probe` runner report
  `E:\myMods\reports\rollback_validation\rollback_validation_20260703-082337-047586.json`
  additionally proves accepted payload history and causal resend recovery with
  `accepted_a=120`, `accepted_b=120`, `payload_match=1`,
  `first_faults_ab=6`, `first_faults_ba=9`,
  `resend_recovered_ab=6`, `resend_recovered_ba=9`, and matching
  `expected/checksum_a/checksum_b=0xE5977A35`.
- Live-online-capture readiness validation on 2026-07-03 passed:
  `RollbackLiveOnlineCaptureSelfTest` reported `ready=1`, `live=1`, `recv=1`,
  `violation_case=1`, `acquire=1`, `input=1`, `battle=1`, `receive=1`,
  `drain=1/1`, `consumer=1`, and `total=7`. In-game request-file lab trace
  `replay_trace_20260703_082451_pid58636.jsonl` emitted
  `rollback_live_online_capture ok=true`, `capture_ready=true`,
  `live_capture_complete=false`, `observe_only=true`,
  `stock_hooks_installed=true`, `stock_trace_active=true`,
  `boundary_hooks_installed=true`, `boundary_trace_active=true`,
  `boundary_violation=false`, `request_id=20260703-082341-584926`, and
  `failure=waiting-for-live-online-traffic`.
  The validation bundle also wrote
  `E:\myMods\reports\rollback_validation\live_online_capture_readiness_20260703-082341-584926.json`
  and the post-capture ReplayTrace analyzer output
  `E:\myMods\reports\rollback_validation\live_online_capture_trace_readiness_20260703-082341-584926.json`.
  The analyzer saw 8 `rollback_live_online_capture` JSONL events with
  `readiness_ok=true`, `stable_readiness_ok=true`, `live_traffic_ok=false`,
  `stable_live_traffic_ok=false`, `boundary_violation_seen=false`,
  `readiness_regression_seen=false`, `bad_event_count=0`, and
  `selected_source=readiness`, `require_case=live-online-capture`,
  `require_request_id=20260703-082341-584926`, `filtered_request_count=0`, and
  `filtered_event_count=0`. The same analyzer summary now includes
  `activation_candidate.observed=true`, `activation_candidate.ready=false`,
  and `activation_candidate.failure=operator-not-armed` for the default
  unarmed readiness run; the route-specific durable analyzer summary records
  expected route `160/176/0x4C495645414354` and
  `selected_route_matches=true`. An armed blank-start smoke run
  `E:\myMods\reports\rollback_validation\live_activation_candidate_remediate_candidate-remediate-20260703-020923.json`
  with trace `replay_trace_20260703_020924_pid16112.jsonl` passed readiness
  and proved the candidate fails closed as `live-traffic-not-proven` when no
  online peer traffic is present, while the human log line carries the matching
  request id and route provenance plus strict route identity gates are true. The
  full
  bundle now fails closed if `trace_file=`
  is missing from the lab output or the analyzer summary JSON is not produced.
  This is intentionally readiness-only; the live-traffic gate is
  `python tools\rollback_full_validation_run.py --live-online-only --require-live-activation-candidate --live-online-watch-seconds <seconds>`
  after deploying the branch and staging/running a real SC6 online peer/match,
  which writes both
  `live_online_capture_live_<run>.json` and
  `live_online_capture_trace_live_<run>.json`, and requires stable live traffic
  and live ordering plus activation-candidate readiness in both the log watcher
  and the durable JSONL trace.
- First operator-facing live-only attach attempt on the already-running SC6
  process failed closed as intended:
  `E:\myMods\reports\rollback_validation\rollback_validation_20260703-024759.json`.
  It used `--live-online-only --require-live-activation-candidate` for 120s,
  wrote `live_online_capture_live_20260703-024759.json` and
  `live_online_capture_trace_live_20260703-024759.json`, and cleaned up the
  request file afterwards. Both the human log watcher and durable analyzer saw
  `stable_readiness_ok=true`, `selected_route_matches=true`, and no boundary
  violation/regression, but correctly failed `live_traffic_ok=false` and
  `activation_candidate.ready=false` with `failure=live-traffic-not-proven`.
  Missing live-match evidence was explicit: `stock_send=0`, `receive=0`,
  `drain_consumer=0`, `live_order=0`, `session_ptr=0`, and `input_log=0`.
  Follow-up reporting hardening adds machine-readable
  `missing_live_traffic_gates` and `missing_activation_candidate_gates`; the
  regenerated analyzer summary
  `live_online_capture_trace_live_20260703-024759_missing_gates.json` reports
  missing live gates `live_capture_complete`, `session_acquired`,
  `stock_input_send`, `battle_sync_send`, `receive_enqueue`,
  `stock_drain_enter`, `stock_drain_exit`, and `live_order`, plus activation
  gaps `stock_send_observed`, `receive_observed`,
  `drain_consumer_observed`, `session_pointer_bound`, and `input_log_bound`.
  A follow-up non-destructive state snapshot
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260703-025623-seek.json`
  confirmed the running process was still `presence=Replay`
  (`world=/Game/Stage/STG009/Maps/STG009.STG009`, `bm=LuxBattleManager`,
  `rp=LuxBattleReplayPlayer`), not an online peer match.
  This proves the live gate is armed and fail-closed, not that live peer
  rollback is complete.
- Live-activation gate validation on 2026-07-02 passed:
  `RollbackLiveActivationSelfTest` reported `ready=1`, `readiness_only=1`,
  `stock=1`, `identity=1`, `boundary=1`, `session=1`, `input_log=1`,
  `self_peer=1`, `zero_session=1`, `operator=1`, `receive=1`, and
  `non_hrg1=1`, plus the hardened gates `route_provenance=1`,
  `direct_ready=1`, and `route_identity=1`. In-game request-file lab trace
  `replay_trace_20260702_233002_pid51932.jsonl` emitted
  `rollback_live_activation_selftest ok=true`, `case=live-activation`,
  `request_id=20260702-232726`, `activation_ready=true`,
  `route_provenance_rejected=true`, `direct_readiness_rejected=true`,
  `route_identity_rejected=true`, and all refusal gates true. This is the
  guarded readiness/activation policy for a future live path; it is still pure
  and does not send packets or write stock cache cells.
- Live-activation executor validation on 2026-07-03 passed:
  `RollbackLiveActivationExecutorSelfTest` reported
  `activation_required=1`, `readiness_only=1`, `stock=1`, `provenance=1`,
  `route_identity=1`, `ready=1`, `enqueue=1`, `queued_only=1`,
  `stock_drain=1`, `metadata=1`, `metadata_not_gameplay=1`, `predict=1`,
  `apply=1`, `consume=1`, `net_cache_reject=1`, `wrong_source=1`,
  `wrong_dest=1`, `wrong_session=1`, `decoded_route=1`, `enqueued=1`,
  `drained=1`, `rejected=3`, and `cache_writes=2`. In-game request-file lab
  trace `replay_trace_20260703_023833_pid18836.jsonl` emitted the matching
  `rollback_live_activation_executor_selftest` gate for
  `request_id=20260703-023756`, including `wrong_destination_rejected=true`,
  `decoded_route_rejected=true`, `rejected_packets=3`, and
  `cache_write_sequence=2`. This remains a model/lab executor: it proves
  activation-gated queue/drain/cache-order behavior, not Steam/SC6 live packet
  injection.
- Low-context route-provenance remediation after the first
  activation-executor review added strict decoded gameplay input provenance:
  `RollbackDecodedGameplayInput` now carries source peer, destination peer, and
  session id, `RollbackLivePeerPipeline::require_decoded_input_route` rejects
  mismatched decoded cache writes after activation, and
  `RollbackLiveActivationExecutorSelfTest`/the in-game lab require
  `decoded_route=1`. The same remediation makes failed lab cleanup a hard
  failure and gives the native Windows process helpers explicit `ctypes`
  HANDLE-return signatures.
- Low-context review by Anscombe accepted the current live-online-capture slice
  with no blocking findings. Nonblocking notes: the opt-in live-online command
  assumes an actual online match is already being orchestrated, and the
  `RollbackLiveOnlineCaptureSelfTest` console field `violation_case=1` is a
  synthetic proof that the bad-order case is detected, not an active violation.
- Follow-up low-context reviews accepted the live-online summary artifact and
  sticky violation/regression hardening. The final no-require summary fix now
  treats `ok` as `stable_readiness_ok` instead of merely "saw any line"; Anscombe
  reported no blocking or nonblocking findings on that final remediation.
- Follow-up hardening after Anscombe's nonblocking notes added explicit
  `--require-case live-online-capture` filtering to the durable JSONL analyzer,
  switched strict replay retry eligibility to prefer the replay report JSON, and
  added the `preflight-close-game` validation step so deployment cannot fail
  open on a leftover SC6 process. The hardened full bundle above passed.
- Live-match validation orchestration was split out into `--live-online-only`
  so the real online capture can attach to an already staged/running match
  without running the full disruptive build/lab/replay bundle first. The mode
  still requires live traffic and the durable trace analyzer, records
  `mode=live-online-only`, and uses `--cleanup-request-after` so failed attach
  attempts do not leave a stale request file. A no-game smoke run
  `reports\rollback_validation\live_online_only_probe\rollback_validation_20260702-190750.json`
  failed closed with `live-traffic-not-proven`, missing `trace_file=`, and no
  stale `rollback_lab_request.txt`.
- Request-id hardening after live-attach review now writes a per-validation
  `request_id` into the lab request file, live summary, validation report, and
  durable ReplayTrace events. `tools\rollback_live_online_capture_analyze.py`
  requires that id in full/live-only orchestration, so a same-case trace from a
  nearby previous run cannot satisfy the current run. The request-file writer
  uses same-directory temp-file + flush/fsync + atomic replace, the C++ reader
  retries transient read failures instead of deleting the request, and the gate
  can be consumed repeatedly in one SC6 process for operator-friendly retries.
  The live-online capture verdict is also deferred until the native image base
  is resolved, so early startup cannot masquerade as a boundary-hook failure.
  No-game smoke
  `reports\rollback_validation\live_online_only_probe_atomic2\rollback_validation_20260702-195800.json`
  failed closed with `request_id=20260702-195800` and cleaned up the request
  file. Full validation
  `reports\rollback_validation\rollback_validation_20260702-232726.json`
  passed with `request_id=20260702-232726`; the post-capture analyzer reported
  `require_request_id=20260702-232726`, `filtered_request_count=0`, and
  `filtered_event_count=0`.
- Strict replay retry hardening now also retries timing-only strict failures
  where the replay summary passed, `failed_cases=0`, `failed_case_labels=[]`,
  `failure_groups={}`, and `state_mismatches=0`. The earlier
  `rollback_validation_20260702-210157.json` bundle exercised that retry path
  with a timing-only cold-start miss. The latest
  `rollback_validation_20260702-232726.json` bundle finished with strict replay
  `PASS` on `replay_seek_e2e_20260702-233209-seek.json`, trace
  `replay_trace_20260702_233209_pid84476.jsonl`: 4/4 watch cases, 2400/2400
  observed frames, 0 state mismatches, and max seek elapsed `0.44s`.
- Stock transport surface guard validation on 2026-07-02 passed:
  `RollbackStockTransportSelfTest` reported `failure=ok`, `shared_ptr=1`,
  `slots=1`, `channels=1`, `input_reject=1`, `battle_reject=1`, `kv_reject=1`,
  `unknown_reject=1`, `provenance=1`, `identity=1`, `identity_values=1`,
  `horse_allow=1`, `stock_native=1`, `stock_no_hrg1=1`, `flag_override=1`,
  and `bridge_v2=1`.
  Full validation `rollback_validation_20260702-232726.json` rebuilt and
  deployed the branch.
  In-game `stock-transport` lab trace
  `replay_trace_20260702_233028_pid13252.jsonl` emitted
  `rollback_stock_transport_selftest ok=true` with `failure=ok` and all guard
  gates true, including `adapter_provenance_required=true`,
  `strict_identity_values_required=true`, and
  `adapter_flag_cannot_override_stock=true`.
  Required strict replay regression
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-233209-seek.json`,
  trace `replay_trace_20260702_233209_pid84476.jsonl`, passed with
  `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and 0 state
  mismatches.
- Stock transport observe validation on 2026-07-02 passed:
  `RollbackStockTransportObserveSelfTest` reported `hooks=1`, `trace=1`,
  `acquire=1`, `opcode0=1`, `opcode1=1`, `battle=1`, `recv=1`, `totals=1`,
  and `observed=5`.
  Full validation `rollback_validation_20260702-232726.json` reran the
  blank-start `stock-observe` lab.
  In-game trace `replay_trace_20260702_233055_pid19816.jsonl` emitted
  `rollback_stock_transport_observe ok=true`, `request_id=20260702-232726`,
  `observe_only=true`, `hooks_installed=true`, `trace_active=true`,
  `receive_enqueue_hook_installed=true`, all five hook gates true, and
  `total_observed_calls=0`.
  Required strict replay regression
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-233209-seek.json`,
  trace `replay_trace_20260702_233209_pid84476.jsonl`, passed with
  `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and 0 state
  mismatches.
- HRG1 v2/source-session validation on 2026-07-02 passed:
  `RollbackLiveTransportSelfTest` reported `enqueue=1`, `bad=1`,
  `wrong_source=1`, `wrong_dest=1`, `wrong_session=1`, `queued_only=1`,
  `stock_drain=1`, `drain=1`, `correction=1`, `duplicate=1`, `late=1`,
  `bypass=1`, `capacity=1`, `enqueued=7`, `drained=5`, `rejected=5`, and
  `queued=2`. `RollbackGekkoSelfTest` still passed with `bridge=1`,
  `bridge_meta=1`, `bridge_reject=1`, `bridge_encoded=345`,
  `bridge_decoded=339`, `bridge_bad=0`, and matching adapter checksums
  `0x68FC89EA/0x68FC89EA`.
  `build_and_deploy.bat` finished at `02-Jul-26 14:20:28.40`.
  In-game `live-transport` lab trace
  `replay_trace_20260702_142036_pid82452.jsonl` emitted
  `rollback_live_transport_selftest ok=true` with
  `wrong_source_rejected=true`, `wrong_session_rejected=true`,
  `network_receive_queued_only=true`, `stock_drain_required=true`,
  `game_thread_drain_accepts=true`, `correction_required=true`,
  `over_window_rejected=true`, and `drain_bypass_ok=true`.
  In-game `gekko-adapter` lab trace
  `replay_trace_20260702_142114_pid41116.jsonl` emitted
  `rollback_gekko_adapter_selftest ok=true` with
  `bridge_roundtrip=true`, `bridge_metadata_accepted=true`,
  `bridge_rejections_ok=true`, `bridge_packets_encoded=345`,
  `bridge_packets_decoded=339`, and `bridge_packets_rejected=0`.
  Required strict replay regression
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-142152-seek.json`,
  trace `replay_trace_20260702_142152_pid86112.jsonl`, passed with
  `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and 0 state
  mismatches.
- Post-bridge validation on 2026-07-02 passed:
  `RollbackGekkoSelfTest` reported `bridge=1`, `bridge_meta=1`,
  `bridge_reject=1`, `bridge_encoded=345`, `bridge_decoded=339`,
  `bridge_bad=0`, and matching adapter checksums `0x68FC89EA/0x68FC89EA`.
  The bridge self-test includes corrupt magic/length/hash, wrong destination,
  null payload, empty payload, and oversize rejection.
  `build_and_deploy.bat` finished at `02-Jul-26 13:47:08.84`.
  Strict in-game `gekko-adapter` lab trace
  `replay_trace_20260702_134714_pid46804.jsonl` emitted
  `rollback_gekko_adapter_selftest ok=true` with
  `bridge_roundtrip=true`, `bridge_metadata_accepted=true`,
  `bridge_rejections_ok=true`, `bridge_packets_encoded=345`,
  `bridge_packets_decoded=339`, and `bridge_packets_rejected=0`.
  Required strict replay regression
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-135010-seek.json`,
  trace `replay_trace_20260702_135014_pid87376.jsonl`, passed with
  `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and 0 state
  mismatches.
- `python -m py_compile tools\rollback_lab_test_run.py` passed.
- `tools\rollback_snapshot_selftest.cpp` compiled with MSVC and passed. The
  latest rerun on 2026-07-02 reported
  `rollback snapshot self-test passed hash=0x919285B01B9C0EF3 bytes=32`
  (hash includes process-local capture details and is not used as a fixed
  golden value).
- `build_horse_mod.bat` passed after lifecycle-epoch changes.
- `build_and_deploy.bat` passed at `02-Jul-26 02:51:51.03`.
- Baseline rollback probe passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-020112-seek.json`.
- Delayed-input rollback proof passed inside the strict trace:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-020355-seek.json`.
  The event has `predicted_differs_from_baseline=1`,
  `corrected_matches_baseline=1`, `hgcpu_topology_match=1`, and
  `hgcpu_unignored_mismatch_count=0`.
- Clean rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-022833-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_022846_pid76804.jsonl`.
  The trace has 10 `rollback_resim_window` events, 0 failures, epoch presence
  `3`, and delayed-input prediction divergence for every tested window.
- Required strict replay seek regression passed all 4 watch cases with
  `2400/2400` observed frames and `0` state mismatches after the matrix and
  lifecycle changes:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-023138-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_023159_pid62992.jsonl`.
  The same strict trace also contains the clean 10-event rollback matrix with
  0 rollback failures.
- Cache ownership replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-025219-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_025231_pid78868.jsonl`.
  The `rollback_cache_ownership` event reports `ok=true`,
  `same_battle_manager=true`, `same_input_log=true`, `full_hash_match=true`,
  `cache_hash_match=true`, `current_input_match=true`,
  `master_clock_match=true`, `drain_cursor_match=true`,
  `resim_predicted_differs_from_baseline=true`, and
  `resim_corrected_matches_baseline=true`.
- Required strict replay seek regression was rerun after the InputLog probe:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-025842-seek.json`
  passed all 4 watch cases with `2400/2400` observed frames, `0` state
  mismatches, and `strict: PASS`.
- Combined cache-ownership plus strict replay seek run passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-030446-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_030503_pid56272.jsonl`.
  The trace contains `rollback_cache_ownership ok=true` and the analyzer
  reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- One-command cache ownership native audit passed using the replay harness
  request-file support:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-031028-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_031052_pid87744.jsonl`.
- One-command cache ownership plus strict replay seek run passed using
  `--rollback-lab-case cache-ownership --require-rollback-cache-ownership`:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-031631-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_031654_pid46184.jsonl`.
  The trace contains `rollback_cache_ownership ok=true` at line 1145 and the
  analyzer reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames,
  and `0` state mismatches.
- Post-wind-gate window-30 warmed cache audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-061155-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_061207_pid13512.jsonl`.
  The `rollback_cache_ownership` event has `ok=true`,
  `resim_wind_rng_gate_enabled=true`, all LFSR indices remained `24`, and the
  three rollback `rng_u32_callers` phase events reported `total_calls=0`.
- Post-wind-gate default window-60 warmed cache audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-061421-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_061444_pid26916.jsonl`.
  It reports `rollback_cache_ownership ok=true`,
  `resim_corrected_matches_baseline=true`, `resim_explicit_match=true`,
  `resim_hgcpu_policy_match=true`, `resim_hgcpu_unignored_mismatch_count=0`,
  `full_hash_match=true`, and `cache_hash_match=true`.
- Post-wind-gate rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-061932-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_061952_pid84788.jsonl`.
  The trace has 10 context-ready `rollback_resim_window` events, all `ok=true`,
  all delayed windows have `predicted_differs_from_baseline=true`, all cases
  have `explicit_match=true`, `hgcpu_policy_match=true`,
  `hgcpu_unignored_mismatch_count=0`, `wind_rng_gate_enabled=true`, and no
  rollback RNG caller events with nonzero calls.
- Combined cache-ownership plus strict replay seek passed after the wind gate:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-062254-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_062317_pid38664.jsonl`.
  The analyzer reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed
  frames, `0` state mismatches, and the same trace contains
  `rollback_cache_ownership ok=true`.
- The exact required strict replay seek command, without rollback extras, was
  rerun after the wind-gate changes and passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-062537-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_062605_pid83784.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- First low-context review found a blocker: warmed cache probes could pass with
  `resim_hgcpu_motion_bank_match=false`. Remediation split gameplay rollback
  state from presentation cache: CMatrixBank control and MoveVM motion tail are
  required, while physical provider buffers remain diagnostic presentation
  cache under the deferred visible-side-effect phase. Ghidra cleanup typed and
  renamed `LuxMoveVM_InitMotionPlayback @ 0x140300400` parameters, documented
  the rollback note, and saved the program.
- Post-remediation build and deploy passed at `02-Jul-26 07:02:23.72`.
  `python -m py_compile tools\rollback_lab_test_run.py tools\replay_seek_test_run.py`
  passed, and `git diff --check` reported only CRLF normalization warnings.
- Post-remediation window-30 warmed cache audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-070229-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_070240_pid35784.jsonl`.
  The `rollback_cache_ownership` event has `ok=true`,
  `resim_hgcpu_policy_match=true`, `resim_hgcpu_motion_bank_match=true`,
  `resim_hgcpu_motion_tail_match=true`,
  `resim_hgcpu_motion_bank_mismatch_count=0`, and
  `resim_hgcpu_unignored_mismatch_count=0`.
- Post-remediation default window-60 warmed cache audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-070445-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_070506_pid34456.jsonl`.
- Post-remediation rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-070755-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_070822_pid80840.jsonl`.
  The trace has 10 `rollback_resim_window` events, all `ok=true`, with
  `hgcpu_policy_match=true`, `hgcpu_motion_bank_match=true`,
  `hgcpu_motion_tail_match=true`, `hgcpu_motion_bank_mismatch_count=0`,
  `hgcpu_unignored_mismatch_count=0`, and `wind_rng_gate_enabled=true`.
- Post-remediation combined cache-ownership plus strict replay seek passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-071115-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_071135_pid84436.jsonl`.
  The analyzer reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed
  frames, `0` state mismatches, and the trace contains
  `rollback_cache_ownership ok=true`.
- The exact required strict replay seek command, without rollback extras, was
  rerun after the motion-bank remediation and passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-071350-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_071417_pid85648.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- Final rebuilt-DLL validation after adding explicit
  `hgcpu_motion_bank_control_match` / `resim_hgcpu_motion_bank_control_match`
  trace fields passed. `build_horse_mod.bat` passed and `build_and_deploy.bat`
  finished at `02-Jul-26 07:20:36.48`; `python -m py_compile
  tools\rollback_lab_test_run.py tools\replay_seek_test_run.py` passed;
  `tools\rollback_snapshot_selftest.cpp` compiled with MSVC and passed; and
  `git diff --check` reported only CRLF normalization warnings on existing
  tracked files.
- Final rebuilt-DLL window-30 warmed cache audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-072051-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_072103_pid57124.jsonl`.
  The `rollback_cache_ownership` event has `ok=true`,
  `resim_hgcpu_policy_match=true`,
  `resim_hgcpu_motion_bank_control_match=true`,
  `resim_hgcpu_motion_tail_match=true`,
  `resim_hgcpu_motion_bank_mismatch_count=0`, and
  `resim_hgcpu_unignored_mismatch_count=0`.
- Final rebuilt-DLL default window-60 warmed cache audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-072309-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_072333_pid43956.jsonl`.
  The event has `resim_hgcpu_motion_bank_control_match=true`,
  `resim_hgcpu_motion_tail_match=true`,
  `resim_hgcpu_motion_bank_mismatch_count=0`, and
  `resim_hgcpu_unignored_mismatch_count=0`.
- Final rebuilt-DLL rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-072623-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_072651_pid31612.jsonl`.
  The trace has 10 `rollback_resim_window` events, all `ok=true`, with
  `hgcpu_policy_match=true`, `hgcpu_motion_bank_control_match=true`,
  `hgcpu_motion_tail_match=true`, `hgcpu_motion_bank_mismatch_count=0`,
  `hgcpu_unignored_mismatch_count=0`, `wind_rng_gate_enabled=true`, and
  delayed-input prediction divergence for windows `1,2,8,15,60`.
- Final rebuilt-DLL combined cache-ownership plus strict replay seek passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-072931-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_072952_pid81008.jsonl`.
  The analyzer reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed
  frames, `0` state mismatches, and the trace contains
  `rollback_cache_ownership ok=true` with motion-bank control and motion-tail
  matches.
- The exact required strict replay seek command, without rollback extras, was
  rerun on the final rebuilt DLL and passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-073206-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_073229_pid61196.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- Final low-context review by Anscombe accepted the current same-process
  rollback core/lab slice with no blocking faults. Non-blocking caveats:
  standalone HgCpu roundtrip should not be used by itself as acceptance proof;
  Python harnesses can later assert the key motion-control/tail/unignored trace
  fields directly; online transport/protocol, local input delay removal,
  broader visible-side-effect gating, online InputLog drain/read ordering, and
  round-boundary refusal tests remain deferred.
- Phase-8 transport groundwork validation passed. `build_horse_mod.bat` passed,
  `build_and_deploy.bat` finished at `02-Jul-26 07:53:35.27`,
  `python -m py_compile tools\rollback_lab_test_run.py tools\replay_seek_test_run.py`
  passed, and `tools\rollback_transport_selftest.cpp` compiled with MSVC and
  passed:
  `rollback transport self-test passed contiguous=2 accepted=3 duplicates=1 reordered=1 conflicts=1 over_window=1 max_prediction_age=1 max_rollback_depth=1`.
- Stricter cache ownership replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-075341-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_075354_pid88056.jsonl`.
  The harness required `full_hash_match=true`, `cache_hash_match=true`,
  `drain_cursor_match=true`, `resim_corrected_matches_baseline=true`,
  `resim_hgcpu_policy_match=true`,
  `resim_hgcpu_motion_bank_control_match=true`,
  `resim_hgcpu_motion_tail_match=true`,
  `resim_hgcpu_motion_bank_mismatch_count=0`, and
  `resim_hgcpu_unignored_mismatch_count=0`.
- Stricter rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-075704-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_075725_pid70812.jsonl`.
  The trace has 10 `rollback_resim_window` events, 0 failures, and 5
  delayed-input prediction-divergence cases; UE4SS log parsing also required
  `hgcpu_policy=1`, `motion_bank_control=1`, `motion_tail=1`, and
  `unignored=0`.
- The exact required strict replay seek command, without rollback extras, was
  rerun after the transport-model and harness-hardening changes and passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-080026-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_080050_pid72152.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- Transport acknowledgement remediation validation passed. `build_horse_mod.bat`
  passed, `build_and_deploy.bat` finished at `02-Jul-26 08:11:52.95`,
  `python -m py_compile tools\rollback_lab_test_run.py tools\replay_seek_test_run.py`
  passed, and `git diff --check` reported only CRLF normalization warnings.
  `tools\rollback_transport_selftest.cpp` compiled with MSVC and passed:
  `rollback transport self-test passed contiguous=2 accepted=3 duplicates=1 reordered=1 conflicts=1 over_window=1 max_prediction_age=1 max_rollback_depth=1 ack_monotonic=1 invalid_packets=1`.
- Post-remediation stricter cache ownership replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-081202-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_081216_pid69052.jsonl`.
  The harness again required full/cache/drain equality, corrected resim equality,
  HgCpu policy, motion-bank control, motion tail, zero motion-bank mismatches,
  and zero unignored mismatches.
- Post-remediation stricter rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-081508-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_081533_pid61168.jsonl`.
  The trace has 10 `rollback_resim_window` events, 0 failures, and 5
  delayed-input prediction-divergence cases.
- The exact required strict replay seek command, without rollback extras, passed
  after the acknowledgement remediation:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-081813-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_081844_pid41372.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- Phase-8 handshake/session-policy validation passed. `build_horse_mod.bat`
  passed, `build_and_deploy.bat` finished at `02-Jul-26 08:30:45.96`,
  `python -m py_compile tools\rollback_lab_test_run.py tools\replay_seek_test_run.py`
  passed, and `git diff --check` reported only CRLF normalization warnings.
  `tools\rollback_transport_selftest.cpp` compiled with MSVC and passed:
  `rollback transport self-test passed contiguous=2 accepted=3 duplicates=1 reordered=1 conflicts=1 over_window=1 max_prediction_age=1 max_rollback_depth=1 ack_monotonic=1 invalid_packets=1 handshake=1 handshake_reject=1 hash_policy=1 loopback=1`.
- Post-handshake stricter cache ownership replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-083056-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_083110_pid12712.jsonl`.
- Post-handshake rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-083418-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_083442_pid35356.jsonl`.
  The trace has 10 `rollback_resim_window` events, 0 failures, and 5
  delayed-input prediction-divergence cases.
- One exact required strict replay attempt after the handshake slice,
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-083724-seek.json`,
  passed all 4 watch cases and `2400/2400` state compares but failed strict on a
  single native replay tick gap spike (`0.200s > 0.100s`). It was treated as a
  timing flake and rerun.
- The exact required strict replay seek command then passed cleanly:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-083958-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_084019_pid81908.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- Invalid hash-policy remediation validation passed. `build_horse_mod.bat`
  passed, `build_and_deploy.bat` finished at `02-Jul-26 08:48:49.69`,
  `python -m py_compile tools\rollback_lab_test_run.py tools\replay_seek_test_run.py`
  passed, and `git diff --check` reported only CRLF normalization warnings.
  `tools\rollback_transport_selftest.cpp` compiled with MSVC and passed:
  `rollback transport self-test passed contiguous=2 accepted=3 duplicates=1 reordered=1 conflicts=1 over_window=1 max_prediction_age=1 max_rollback_depth=1 ack_monotonic=1 invalid_packets=1 handshake=1 handshake_reject=1 handshake_invalid=1 hash_policy=1 loopback=1`.
- Post-invalid-policy-remediation cache ownership replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-084859-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_084912_pid23600.jsonl`.
- Post-invalid-policy-remediation rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-085218-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_085242_pid63928.jsonl`.
  The trace has 10 `rollback_resim_window` events, 0 failures, and 5
  delayed-input prediction-divergence cases.
- The exact required strict replay seek command passed after the invalid-policy
  remediation:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-085528-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_085552_pid55072.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- Lifecycle boundary-refusal validation passed. `tools\rollback_snapshot_selftest.cpp`
  compiled with MSVC and passed:
  `rollback snapshot self-test passed hash=0x14C21A698A24CFB bytes=32`
  and `rollback snapshot boundary refusals passed count=9`. The nine refusal
  cases cover inactive snapshot epoch, missing snapshot chara epoch, inactive
  live epoch, changed BattleManager pointer, changed chara pointer, changed
  InputLog pointer, changed round number, changed round-start hash, and changed
  stage-context hash, and each case verifies the destination byte range remains
  untouched after refusal.
- Post-boundary-refusal build and deploy passed. `build_horse_mod.bat` passed,
  `build_and_deploy.bat` finished at `02-Jul-26 09:06:29.81`,
  `python -m py_compile tools\rollback_lab_test_run.py tools\replay_seek_test_run.py`
  passed, and `git diff --check` reported only CRLF normalization warnings.
- Post-boundary-refusal cache ownership plus strict replay seek passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-090658-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_090712_pid7440.jsonl`.
  The analyzer reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed
  frames, `0` state mismatches, and the trace contains
  `rollback_cache_ownership ok=true`.
- Post-boundary-refusal rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-090948-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_091012_pid2128.jsonl`.
  The trace has 10 `rollback_resim_window` events, 0 failures, windows
  `1,2,8,15,60`, and 5 delayed-input prediction-divergence cases.
- The exact required strict replay seek command passed after the
  boundary-refusal slice:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-091242-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_091307_pid79912.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- Online-session adapter model validation passed. Native MSVC self-tests passed:
  `rollback_snapshot_selftest` with boundary refusals count `9`,
  `rollback_transport_selftest` with ack/invalid-packet/handshake/hash/loopback
  gates, and `rollback_online_session_selftest` with ack, prediction,
  correction, reorder, duplicate, conflict, over-window late, cache-write
  rejection, stock-drain requirement, drain-bypass, enforced hash rejection, and
  warn-only hash correction gates all `=1`.
- Post-adapter build and deploy passed. `build_horse_mod.bat` passed,
  `build_and_deploy.bat` finished at `02-Jul-26 09:23:38.04`,
  `python -m py_compile tools\rollback_lab_test_run.py tools\replay_seek_test_run.py`
  passed, and `git diff --check` reported only CRLF normalization warnings.
- Post-adapter cache ownership plus strict replay seek passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-092346-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_092358_pid45732.jsonl`.
  The analyzer reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed
  frames, `0` state mismatches, and `rollback_cache_ownership ok=true`.
- Post-adapter rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-092610-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_092631_pid81516.jsonl`.
  The trace has 10 `rollback_resim_window` events, 0 failures, windows
  `1,2,8,15,60`, and 5 delayed-input prediction-divergence cases.
- The exact required strict replay seek command passed after the adapter slice:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-092855-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_092916_pid70520.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- Post-online-session-lab hardening passed. `build_horse_mod.bat` passed,
  `build_and_deploy.bat` finished at `02-Jul-26 09:56:56.35`,
  `python -m py_compile tools\rollback_lab_test_run.py tools\replay_seek_test_run.py`
  passed, and `git diff --check` reported only CRLF normalization warnings.
  Direct EXE test launch now also has
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\steam_appid.txt`
  with AppID `544750`; without it SC6 reached UE4SS then exited before object
  construction, and Steam URI/applaunch did not start a process.
- Native MSVC self-tests were rebuilt from source and passed after the adapter
  slice: `rollback_snapshot_selftest` reported boundary refusals count `9`,
  `rollback_transport_selftest` passed ack/invalid-packet/handshake/hash/loopback
  gates, and `rollback_online_session_selftest` passed ack, prediction,
  correction, reorder, duplicate, conflict, over-window late, cache-write
  rejection, stock-drain requirement, drain-bypass, enforced hash rejection, and
  warn-only hash correction gates. The generated `.exe` artifacts were removed
  from `tools`.
- The in-game online-session lab gate passed after deployment:
  `python tools\rollback_lab_test_run.py --kill-game --launch-game --case online-session --trace --watch-seconds 20 --strict --require-online-session --kill-after`.
  UE4SS logged `online_session ok=1` with every policy bit `=1`.
- Post-hardening cache ownership plus strict replay seek passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-095923-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_095928_pid45912.jsonl`.
  The analyzer reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed
  frames, `0` state mismatches, and `rollback_cache_ownership ok=true` with the
  strong cache/drain/HgCpu policy fields.
- Post-hardening rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-100150-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_100152_pid2020.jsonl`.
  The trace has 10 `rollback_resim_window` events, all `ok=true`, windows
  `1,2,8,15,60`, all corrected states match baseline with zero unignored
  mismatches, and every delayed-input window has
  `predicted_differs_from_baseline=true`.
- The exact required strict replay seek command passed again after the
  online-session lab and request-file timing changes:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-100440-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_100443_pid80108.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- Ghidra MCP cleanup was rerun for the online/cache boundary. The stock cache
  reader `GetCachedInputForFrameInputLogSlot @ 0x1403F0720` now has EOL
  comments for the cache predicate offset, rolling mask, entry scale, cache base,
  and input-value field; `batch_analyze_completeness` reports effective score
  `100.0` with no fixable deductions. The Ghidra program was saved.
- Final-review remediation: the low-context reviewer found a blocking bug in
  `RollbackOnlineSessionModel` where accepting an older out-of-order packet
  after a newer packet could regress the held-last prediction seed. Remediation
  made remote prediction seed selection frame-aware by scanning confirmed
  history for the newest confirmed remote frame strictly before the prediction
  target. The self-test now has two explicit gates:
  `reorder_seed=1` proves frame `2` remains the prediction seed after frame `1`
  arrives late, and `no_future_seed=1` proves a future confirmed frame is not
  used when predicting an earlier missing frame.
- Post-remediation native MSVC self-tests were rebuilt from source and passed:
  `rollback snapshot self-test passed hash=0x132D79EBCC5C387A bytes=32`,
  `rollback snapshot boundary refusals passed count=9`,
  `rollback transport self-test passed ... loopback=1`, and
  `rollback online-session self-test passed` with `reorder_seed=1`,
  `no_future_seed=1`, `cache_write=1`, `stock_drain=1`, `bypass=1`,
  `hash_enforced=1`, and `hash_warn=1`.
- Post-remediation build and deploy passed. `build_horse_mod.bat` passed,
  `build_and_deploy.bat` finished at `02-Jul-26 10:16:51.83`,
  `python -m py_compile tools\rollback_lab_test_run.py tools\replay_seek_test_run.py`
  passed, and `git diff --check` reported only CRLF normalization warnings.
- The in-game online-session lab gate passed with the new seed-ordering gates:
  UE4SS logged `online_session ok=1 ... reorder_seed=1 no_future_seed=1 ...`
  at `2026-07-02 10:17:00`.
- Post-remediation cache ownership plus strict replay seek passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-101738-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_101738_pid52316.jsonl`.
  The analyzer reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed
  frames, `0` state mismatches, and `rollback_cache_ownership ok=true`.
- Post-remediation rollback matrix replay audit passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-102049-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_102052_pid41880.jsonl`.
  The trace has 10 `rollback_resim_window` events, all windows
  `1,2,8,15,60`, all delayed-input prediction-divergence rows present, and
  zero unignored mismatches.
- The exact required strict replay seek command passed again after remediation:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-102316-seek.json`,
  trace
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260702_102318_pid78540.jsonl`.
  It reports `strict: PASS`, 4/4 watch cases, `2400/2400` observed frames, and
  `0` state mismatches.
- One combined cache-ownership strict attempt
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-030156-seek.json`
  functionally passed 4/4 cases and 2400/2400 frame compares but missed the
  strict seek-land timing ceiling by 3 ms (`0.503s > 0.500s`). It was treated as
  a timing flake and rerun to the clean passes above. A later one-command
  strict attempt
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-031346-seek.json`
  similarly passed rollback/cache/state checks but missed by 2 ms
  (`0.502s > 0.500s`) before the clean `031631` rerun.

Pinned rerun command for the current cache-ownership plus strict replay
evidence:

```powershell
python E:\myMods\tools\replay_seek_test_run.py --kill-game --launch-game --allow-unknown-presence --start-replay E:\myMods\ReplayExample\REPLAY_12744704008398858106.bin --timeline-generation-mode lux-no-render --case-preset watch --watch-frames 600 --wait --analyze --strict --min-resume-tick-rate 58 --resume-tick-window 120 --max-seek-validation-seconds 0.5 --rollback-lab-case cache-ownership --require-rollback-cache-ownership --start-timeout 240 --state-timeout 120
```

- Ghidra MCP cleanup on 2026-07-02 added
  `LuxBattleCharaRollbackSlice` and applied `LuxBattleCharaRollbackSlice *` to
  `g_pLuxBattleCharaP1` and `g_pLuxBattleCharaP2`; program saved.
- Ghidra MCP cleanup on 2026-07-02 documented InputLog/cache ownership
  functions:
  `GetCachedInputForFrameInputLogSlot @ 0x1403F0720`,
  `UpdateFrameInputLogCacheLocalMode @ 0x1403F2AB0`, and
  `ProcessFrameInputLogCurrentInputRefresh @ 0x1403FDF30`. The latter two now
  score above the fix-now threshold (`90.07` and `92.0` respectively), and the
  program was saved.
- Ghidra MCP cleanup on 2026-07-02 added rollback evidence plate comments to
  the visual wind RNG functions observed in `rng_u32_callers`:
  `IwWind_UpdateParallelOscillation @ 0x140331980`,
  `IwWind_UpdateRingInOscillation @ 0x140333550`,
  `IwWind_InitPlaneObject @ 0x140332D60`, and
  `IwWind_ScheduleNewWindEffect @ 0x140334750`.
- Ghidra MCP cleanup on 2026-07-02 typed and renamed the
  `LuxMoveVM_InitMotionPlayback @ 0x140300400` parameters and updated its plate
  comment with the rollback motion-tail/provider-cache split. Program saved.
- Ghidra MCP cleanup on 2026-07-02 rechecked
  `LuxOnline_DrainRingBuffer_DecodeInputPackets_AndUpdateCache @ 0x1403F6770`,
  typed/renamed packet/deque/cache locals, documented the rollback adapter
  boundary, and saved the program. The plate note records that stock opcode
  `0`/`1` packets only carry low frame tags and are insufficient for rollback
  identity; a rollback adapter must use absolute frame ids, confirmations, and
  guarded game-thread cache ownership. Completeness reported effective
  `93.0657359638273` with fixable deductions below the 10-point threshold.
- Ghidra MCP cleanup on 2026-07-02 documented the stock online input senders:
  `LuxOnline_SendInputPacket_PerFrame_Opcode0 @ 0x1403F84E0` and
  `LuxOnline_SendInputPacket_BatchedRange_Opcode1 @ 0x1403F8710`. Both plate
  comments now state that opcode 0/1 are stock low-frame-tag input/cache
  packets, not the rollback protocol, and that Horse rollback must use the
  separate absolute-frame adapter path. Both functions verified at effective
  `93.0657359638273`; program saved.
- Ghidra MCP cleanup on 2026-07-02 documented
  `LuxOnlineBattleSync_RequestStage_SendOpcode6 @ 0x14051DBC0`. The plate and
  inline comments record that this path calls the stock transport vtable slot
  `+0x28` on channel `6` with message type `2`; Horse uses it as BattleSync
  liveness evidence for `RollbackLiveActivationGate`, not as an HRG1/Gekko send
  path. Effective completeness after comment cleanup is
  `90.74086836023766` with fixable deductions under the 10-point threshold; the
  program was saved.
- Ghidra MCP cleanup on 2026-07-02 added PRE comments after the Horse/Gekko
  bridge implementation to `LuxOnline_PushToRingBuffer_WithCriticalSection @
  0x1403F4BE0` and `LuxOnline_SendInputPacket_PerFrame_Opcode0 @ 0x1403F84E0`.
  The comments record that `HRG1` is currently an offline adapter envelope, that
  stock opcode 0 is not extended for rollback metadata, and that live receive
  remains enqueue/copy-only. A later receive-enqueue pass added
  `LuxDequeHeapBlock_PacketWrapperPartial` for the internal overflow path, so
  `LuxOnline_PushToRingBuffer_WithCriticalSection` now verifies at effective
  `100.0`; the opcode 0 sender remains at effective `93.0657359638273`.
  Program saved.
- Ghidra MCP cleanup on 2026-07-02 refreshed
  `LuxBattleChara_UpdatePlayerInputData_FromRoundCache @ 0x1403FCD10`, typed
  `pMoveProvider` as `void *`, replaced the plate comment with explicit
  Algorithm/Parameters/Returns/Special Cases/Structure Layout sections, and
  documented that this is the stock cache read boundary for future online
  rollback. Effective score improved to `91.13147192765459`; fixable deductions
  are below the 10-point threshold. Program saved.
- Live-boundary observability now installs inert x64 detours on the stock
  online drain boundary
  `LuxOnline_DrainRingBuffer_DecodeInputPackets_AndUpdateCache @ 0x1403F6770`
  and cache consumer
  `LuxBattleChara_UpdatePlayerInputData_FromRoundCache @ 0x1403FCD10`. The
  `online-boundary` lab case records drain enter/exit counts, consumer entry,
  thread ids, pointers, master clock, and consumed cache frame. It fails if the
  consumer runs while stock drain is active, if drain enter/exit are unbalanced,
  or if neither offline-boundary nor live-order proof is present. This is
  observability only: no prediction injection, live cache writes, or packet send
  replacement.
- Post-live-boundary validation passed: `build_and_deploy.bat` finished at
  `02-Jul-26 10:42:02.50`; Python harness compile passed; snapshot,
  transport, online-session, and live-boundary native self-tests passed; the
  boundary strict replay report
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-104211-seek.json`
  emitted `rollback_live_boundary ok=true`, `hooks_installed=true`,
  `consumer_count=6`, `offline_boundary_observed=true`, and
  `live_order_proven=false`; cache ownership report
  `replay_seek_e2e_20260702-104605-seek.json`, matrix report
  `replay_seek_e2e_20260702-104824-seek.json`, and the exact strict regression
  report `replay_seek_e2e_20260702-105056-seek.json` all passed. A Ghidra PRE
  comment was added to `0x1403F6770`, and the program was saved.
- Phase-8 cache-provenance model added after another Ghidra pass confirmed the
  receive/deque/drain/read split. `RollbackInputCacheShadow` models
  `FLuxReplayInputCacheEntry[2][512]` plus a per-cell source tag. It rejects
  network-thread cache writes, prediction before stock drain unless a guarded
  bypass is selected, stock drain after prediction for the same frame,
  prediction over confirmed cache, conflicting confirmed input, and stale
  ring-cell identity mismatches. The standalone self-test passed:
  `rollback input-cache adapter self-test passed layout=1 pred_after_drain=1
  net_reject=1 drain_required=1 stock_after_pred=1 confirmed_replace=1
  dup_confirmed=1 pred_over_confirmed=1 conflict=1 consume_source=1
  ring_mismatch=1`. The online-session self-test now requires
  `cache_provenance=1` and passed with that field. A Ghidra PRE comment was
  added to
  `LuxOnline_PushToRingBuffer_WithCriticalSection @ 0x1403F4BE0`, and the
  program was saved. This is still a provenance/ordering model only, not live
  cache injection.
- Post-cache-provenance validation passed: `build_horse_mod.bat` passed,
  `build_and_deploy.bat` finished at `02-Jul-26 11:07:46.77`, Python harness
  compile passed, and native snapshot/transport/input-cache-adapter/
  online-session/live-boundary self-tests all passed. In-game online-session
  trace `replay_trace_20260702_110754_pid12948.jsonl` emitted
  `cache_provenance_ok=true`. Boundary replay report
  `replay_seek_e2e_20260702-110820-seek.json`, cache ownership report
  `replay_seek_e2e_20260702-111030-seek.json`, matrix report
  `replay_seek_e2e_20260702-111256-seek.json`, and the exact strict regression
  report `replay_seek_e2e_20260702-111521-seek.json` all passed. Matrix trace
  parsing found 10 `rollback_resim_window` events, 0 failures, and no missing
  delayed-input prediction-divergence windows.
- Low-context review caught a missing cache-provenance negative test for
  prediction-over-confirmed cells. Remediation added
  `prediction_over_confirmed_rejected`, included it in the standalone
  `RunRollbackInputCacheAdapterSelfTest` gate/output and docs, and tightened the
  online-session evidence wording so it does not imply cache-provenance is wired
  into a live accept/write path. Targeted remediation validation passed:
  input-cache adapter self-test with `pred_over_confirmed=1`, online-session
  self-test with `cache_provenance=1`, `build_and_deploy.bat` finished at
  `02-Jul-26 11:25:32.61`, and in-game online-session trace
  `replay_trace_20260702_112538_pid54776.jsonl` emitted
  `cache_provenance_ok=true`.
- Runtime cache-injection validation passed. Ghidra MCP re-confirmed the
  consumer/cache boundary and the PRE comment at
  `LuxBattleChara_UpdatePlayerInputData_FromRoundCache @ 0x1403FCD10` now
  records both the idempotent cache-injection probe and the non-idempotent
  cache-prediction probe. The program was saved. `build_horse_mod.bat` passed,
  `build_and_deploy.bat` finished at `02-Jul-26 12:07:24.29`, Python harness
  compile passed, and native snapshot/transport/input-cache-adapter/
  online-session/live-boundary self-tests all passed. Strict replay
  cache-prediction report
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-120948-seek.json`
  emitted `rollback_cache_prediction ok=true` with
  `non_idempotent_write=true`, `injected_differs_from_original=true`,
  `original_input=0`, `injected_input=1`, `observed_current_input=1`,
  `restored_current_input_value=0`, `observed_output_input=1`,
  `observed_output_flags=1`, `restored_output_input=0`, and
  `restored_output_flags=0`. Follow-up strict cache-injection report
  `replay_seek_e2e_20260702-121347-seek.json`, boundary report
  `replay_seek_e2e_20260702-121600-seek.json`, cache ownership report
  `replay_seek_e2e_20260702-121833-seek.json`, matrix report
  `replay_seek_e2e_20260702-122054-seek.json`, exact strict regression report
  `replay_seek_e2e_20260702-122316-seek.json`, and in-game online-session trace
  `replay_trace_20260702_122528_pid45744.jsonl` all passed. Matrix trace
  parsing found 10 `rollback_resim_window` events, 0 failures, and no missing
  delayed-input prediction-divergence windows.
- GekkoNet session-core validation passed. A temp clone of
  [HeatXD/GekkoNet](https://github.com/HeatXD/GekkoNet) built no-ASIO static,
  `build_horse_mod.bat` passed after adding the FetchContent dependency,
  `build_and_deploy.bat` finished at `02-Jul-26 12:45:20.64`, standalone
  `RollbackGekkoSelfTest` passed with save/load/advance/rollback-advance gates
  and checksum `0x1004AFC6`. After low-context review, the CMake
  fully-disconnected gate now verifies the cached GekkoNet commit before
  bypassing FetchContent population, and the self-test requires
  `checksum_expected=1`. Targeted cache-state tests for `clean-cache`,
  `stale-all-sentinels`, and `stale-partial-cache` all produced
  `ready=OFF allow=ON updates=OFF fully=OFF`, covering the stale partial-cache
  edge case from review. The latest in-game trace
  `replay_trace_20260702_131033_pid10120.jsonl` emitted
  `rollback_gekko_session_selftest ok=true`, `create_ok=true`,
  `start_ok=true`, `actors_ok=true`, `saw_save=true`, `saw_load=true`,
  `saw_advance=true`, `saw_rollback_advance=true`, `no_desync=true`, and
  `final_checksum_expected=true`, and `destroy_ok=true`.
- GekkoNet adapter-loopback validation passed. Ghidra MCP cleaned
  `LuxOnline_PushToRingBuffer_WithCriticalSection @ 0x1403F4BE0`, typing
  `FReplayNetPacketWrapper_Partial *pPacketWrapper`, documenting the
  network-thread enqueue/cache-ownership boundary, then added
  `LuxDequeHeapBlock_PacketWrapperPartial` for the overflow path and improved
  the function to effective `100.0`; the program was saved. Standalone
  `RollbackGekkoSelfTest` passed with adapter gates
  `adapter_sent=1`, `adapter_recv=1`, `adapter_free=1`,
  `adapter_bidirectional=1`, `adapter_checksums=1`, `adapter_load=1`,
  `adapter_rollback=1`, `adapter_packets_sent=345`,
  `adapter_packets_recv=339`, `adapter_frees=1017`, and matching adapter
  checksums `0x68FC89EA`. `build_horse_mod.bat`, `build_and_deploy.bat`
  finished at `02-Jul-26 13:14:34.22`, Python harness compile passed, and
  in-game trace `replay_trace_20260702_131443_pid84448.jsonl` emitted
  `rollback_gekko_adapter_selftest ok=true` with create/adapter-set/start/
  actors/connected/session-started/save/load/advance/rollback-advance/
  no-desync/send/receive/free/bidirectional/checksum/destroy gates passing.
- Final strict replay regression after the delayed Gekko adapter changes passed:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260702-132124-seek.json`,
  trace `replay_trace_20260702_132125_pid79840.jsonl`, 4/4 watch cases,
  `2400/2400` observed frames, 0 state mismatches, and `strict: PASS`.

Live netcode candidate:

- Evaluate [HeatXD/GekkoNet](https://github.com/HeatXD/GekkoNet) before writing
  SC6 live peer-transport glue. Current upstream README describes it as a
  BSD-2-Clause C/C++ peer-to-peer rollback networking SDK with CMake/MSVC
  support, online/local/spectator/stress sessions, desync detection, network
  statistics, and a latest GitHub release dated 2026-06-29. Treat it as the
  first candidate for the live peer session layer, with a thin Horse adapter
  responsible for SC6-specific InputLog/cache ownership, Ghidra-verified
  boundaries, and replay/lab validation.
- Current integration result: GekkoNet is pinned and linked in no-ASIO mode, and
  the stress-session self-test passes both standalone and inside SC6/UE4SS. A
  Horse-owned `GekkoNetAdapter` bridge over deterministic queued packets now
  passes standalone and in-game lab validation, and a Horse-owned localhost UDP
  adapter now carries those HRG1-wrapped Gekko payloads through real WinSock
  sockets with endpoint/source/destination/session rejection gates. HRG1 v2
  bridge/session identity, the live-transport queue, stock transport surface
  guard, observe-only native send/receive-enqueue hooks, the combined
  live-online-capture readiness gate, the guarded live-activation gate, the
  paired live activation-candidate verdict, the post-capture JSONL analyzer for
  those gates, the integrated live-peer pipeline model, the local end-to-end
  rollback/Gekko pipeline, and the UDP adapter now pass standalone and in-game
  lab validation. The next slice must run
  `python tools\rollback_full_validation_run.py --live-online-only --require-live-activation-candidate --live-online-watch-seconds <seconds>`
  during a real SC6 online peer/match. That attach run is request-id isolated
  and reusable in the same SC6 process; it must prove
  `live_capture_complete=true`, then pass the explicit activation gate before
  any guarded live send/receive path is attempted.

Still intentionally deferred:

- Local input delay removal.
- Live online transport and peer protocol. Packet/model groundwork exists, but
  it is not yet a live Steam/SC6 integration and does not replace stock online
  packet handling. GekkoNet is integrated as a tested session core and
  deterministic queued adapter harness with a validated HRG1 bridge, a
  localhost UDP socket adapter, live-transport queue, live-peer pipeline model,
  local end-to-end composition harness, live-online-capture readiness gate, and
  live-activation policy gate, but it still needs real online send/receive
  capture that completes the live traffic/order gate, satisfies activation
  against real peer/session identity, proves live SC6/Gekko gameplay-input
  decode, and exercises guarded SC6 cache/drain integration in an online match.
- Hidden-resim side-effect gating beyond the rollback wind RNG gate and current
  restore-after-probe lab safety. This must be implemented before enabling
  rollback during visible live online play.
- Full stock InputLog cache/drain ordering for real online play. The ownership
  probe proves direct local resim leaves the stock InputLog/cache window
  unchanged, the live-boundary hook proves the detour boundary plus offline
  consumer ordering, the cache-provenance model proves the intended per-cell
  source policy offline, the cache-injection probe proves idempotent
  game-thread write/read/restore mechanics at the consumer boundary, and the
  cache-prediction probe proves non-idempotent write/read/output-restore
  mechanics during replay. Real online peer traffic still must prove
  `live_order_proven=true` and exercise the prediction writer under live drain
  ordering before prediction/cache injection can be enabled in online matches.
- Stage/barrier mutable-state restore proof beyond the fields covered by
  native HgCpuDirect and the current active replay matrix.
- Full live epoch binding for BattleManager, InputLog, round-start state, and
  stage context during online rollback. Snapshot-copy refusal tests now cover
  these epoch fields deterministically, but the same-process runtime restore path
  still only has active-presence and live-chara identity gates.

## Working Rules

1. Treat Ghidra MCP and runtime traces as authority. Local docs are guidance until
   verified against Ghidra, logs, or reproducible tests.
2. Keep rollback same-round only. Refuse correction across round/object lifecycle
   boundaries until a later design proves safe.
3. Prefer a native HorseMod implementation in new `horselib/Rollback*.hpp`
   modules. Reuse `ReplayScrub` primitives, but do not keep expanding
   `ReplayScrub.hpp` for live rollback.
4. Gate all lab/prototype behavior behind explicit developer toggles or command
   line flags. No hidden behavior in normal online matches.
5. After every implementation slice that touches reverse-engineered functions,
   use Ghidra MCP on the relevant functions before coding and after verifying.
   Fix clear names/types/comments/structs opportunistically.
6. Every field included in a gameplay hash must have one of:
   - a restore source in the snapshot manifest;
   - proof that HgCpuDirect restores it;
   - a documented exclusion with evidence and a test proving it does not affect
     gameplay determinism.
7. The no-delay baseline oracle must be proven stable before any rollback
   correction result is trusted.

## Phase 0 - Branch, Baseline, And Safety

Status: branch `rollback` created from `main`.

Implementation:

1. Confirm clean build on the branch with the existing build/deploy path.
2. Add rollback feature flags:
   - compile/runtime gate for rollback lab;
   - command line gate for automation;
   - ImGui developer controls only after the lab backend exists.
3. Keep online auto-disable behavior unchanged until a controlled rollback
   online prototype explicitly replaces the unsafe path.

Verification:

- `git status --short --branch` shows work on `rollback`.
- `build_and_deploy.bat` succeeds before deeper changes.
- Lab disabled regression proves existing input-delay behavior is unchanged:
  current stock behavior is measured before rollback changes and compared after
  every implementation phase that touches input timing, clocks, input caches, or
  online drain logic.

Ghidra work:

- Load MCP groups `function`, `analysis`, `comment`, `datatype`, `listing`,
  `xref`, `symbol`, and `program` as needed.
- Do not use Ghidra script endpoints for database edits.

## Phase 1 - Ghidra Grounding And Cleanup Backlog

Implementation:

1. Re-check the core anchors before code changes:
   - `LuxBattle_PerFrameTick @ 0x1402DBC60`
   - `LuxBattle_HgCpuDirect_ExecMoveChangeAndPost @ 0x1403841E0`
   - `LuxBattle_HgCpuDirect_ExecFinalizeAndPost @ 0x140384540`
   - `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520`
   - `LuxBattleChara_UpdatePlayerInputData_FromRoundCache @ 0x1403FCD10`
   - `GetCachedInputForFrameInputLogSlot @ 0x1403F0720`
   - `LuxOnline_DrainRingBuffer_DecodeInputPackets_AndUpdateCache @ 0x1403F6770`
2. Create or refine only structs that implementation immediately needs:
   - `FLuxBattlePerFrameTickArgs`
   - `FLuxReplayInputCacheEntry`
   - `FLuxHgCpuBuffer`
   - `FLuxHgCpuDirectSegment`
   - partial `ALuxBattleFrameInputLog`
   - partial `ALuxBattleManager`
3. Clean directly relevant Ghidra debt as encountered:
   - `LuxBattle_PerFrameTick` still has fixable globals and raw struct offsets.
   - HgCpuDirect writer/restore still need segment-table pointer typing and
     better restore plate coverage.
   - Online drain has a remaining raw `+0x10` packet-wrapper/deque access.
4. Use MCP workflow order:
   - rename/set prototype first;
   - type variables before Hungarian renames;
   - then batch comments;
   - finish with completeness analysis.
5. Maintain `docs/investigations/rollback-netcode-evidence-table.md` or an
   equivalent generated artifact with rows for:
   - copied memory range or scalar field;
   - owning function/global/address/offset;
   - type or struct field name;
   - MCP tool output used as evidence;
   - runtime trace/test that exercised it;
   - restore source or exclusion reason.

Verification:

- Each touched function has `batch_analyze_completeness` output recorded in the
  implementation notes or commit message.
- Fixable completeness deductions above 10 points are either addressed or
  explicitly deferred with evidence.

## Phase 2 - Rollback Lab Module Skeleton

Implementation:

1. Add dedicated modules and wire them into `HorseMod/CMakeLists.txt`:
   - `horselib/RollbackLab.hpp`
   - `horselib/RollbackSnapshot.hpp`
   - `horselib/RollbackStateHash.hpp`
   - `horselib/RollbackInputHistory.hpp`
   - `horselib/RollbackController.hpp`
   - `horselib/RollbackFaultInject.hpp`
   - `horselib/RollbackDiag.hpp`
2. Include the lab from `HorseMod/dllmain.cpp` with the same lifecycle style as
   existing gates and trace systems.
3. Add log output through existing UE4SS/HorseMod diagnostics and optional JSONL
   records through `ReplayDebugTrace`.
4. Add command line controls:
   - enable rollback lab;
   - choose test case;
   - choose rollback window;
   - choose deterministic seed;
   - choose output path.

Verification:

- Build succeeds with lab disabled.
- Build succeeds with lab enabled.
- Starting the game with no lab flags produces no rollback behavior.

Ghidra work:

- Document the exact runtime insertion points selected for the first lab hook.
- If new call targets are used, name/type/comment them before depending on them.

## Phase 3 - Snapshot Manifest

Implementation:

1. Implement a compact rollback snapshot:
   - HgCpuDirect blob using the same buffer contract as `ReplayScrub`;
   - required globals from the rollback boundary investigation, starting with:
     `g_LuxBattle_LfsrState`, `g_dwLuxBattleLfsrIndex`,
     `g_LuxBattle_LatestEngineInput_PerPlayer`,
     `g_LuxBattle_PerPlayerInputRing`,
     `g_LuxBattle_PerPlayerInputRingCursor`,
     `g_LuxBattle_InputRingBaseOffset_PerPlayer`,
     `g_LuxBattle_CCpuCommandArray`;
   - all battle RNG state reachable from the RNG functions until proven covered
     or irrelevant, including xorshift96 and LCG-style state used by
     `LuxMoveVM_GetRandXorshift96Gameplay`, `LuxMoveVM_GetRandLCG`,
     `LuxMoveVM_GetRandU32`, and `LuxMoveVM_GetRandFloat01`;
   - all mutable gameplay stage state reachable from stage/collision/contact
     paths until proven covered or irrelevant, including the fixed scbattle
     barrier block, terrain/contact flags, breakable wall/barrier state, and
     stage boundary context;
   - BM cursor/input fields around `BM+0x1488/+0x148C/+0x1490` and
     `BM+0x1498/+0x14A8/+0x14C8`;
   - `ALuxBattleFrameInputLog` cache/cursors only when a test uses the stock
     cache path.
2. Add pointer/lifetime validation:
   - BattleManager pointer stable;
   - two chara pointers stable;
   - active round sequence state is rollback-safe;
   - target snapshot belongs to the same round/lifecycle epoch.
3. Define lifecycle epoch strictly enough to reject:
   - BattleManager replacement or pointer reuse;
   - either chara pointer replacement or pointer reuse;
   - InputLog/cache replacement;
   - stage generation or barrier/boundary context changes;
   - round number/round-start blob changes;
   - rematch, loading, disconnect, replay seek, or object teardown;
   - any mode/presence transition that invalidates the live battle graph.
4. Store snapshots in a fixed rollback ring sized for 8, 12, 15, and 60-frame
   lab windows.

Verification:

- Immediate save/restore test on a stable active-round frame.
- Byte-for-byte validation of the manifest fields after restore.
- Restore refusal test during round transition or invalid actor state.
- Manifest coverage test fails if a hash field has no restore source, HgCpuDirect
  coverage proof, or documented exclusion.
- Stage coverage test fails if any mutable gameplay stage field reachable from
  stage/collision/contact paths lacks a restore source, HgCpuDirect coverage
  proof, or documented exclusion, even when that field is not yet part of the
  selected state hash.

Ghidra work:

- Any copied memory range gets a Ghidra-backed struct/field note or a named
  unresolved-offset comment.
- Do not promote speculative large structs unless field evidence is strong.

## Phase 4 - One-Frame Step Harness

Implementation:

1. Start with direct `LuxBattle_PerFrameTick` stepping:
   - prepare `FLuxBattlePerFrameTickArgs`;
   - inject P1/P2 input qwords;
   - call through the existing safe bypass/trampoline pattern when the public
     entry may be gated;
   - suppress duplicate normal frame advancement while manual catch-up runs.
2. If direct stepping fails determinism, upgrade to the full battle-frame step:
   - `LuxBattle_PerFrameTick`;
   - then BM main-state/simulation-loop work proven necessary by hashes.
3. If both direct/full battle-frame stepping fail, fall back to stock
   InputLog/BM SimulationLoop stepping with explicit cache writes.

Verification:

- One-frame manual step advances exactly one `g_LuxBattle_FrameCounter`.
- BM/InputLog frame counters remain in lockstep or mismatches are logged.
- Manual step does not double-run actor tick siblings.

Ghidra work:

- Re-check `LuxBattle_PerFrameTick` and the selected BM wrapper/callee.
- Add PRE/EOL comments for the exact call boundary and any side-effect gates.

## Phase 5 - Determinism And Hash Harness

Implementation:

1. Add a stable no-delay baseline oracle before rollback correction tests:
   - run the same deterministic input stream twice from the same setup;
   - no restore/resim or delayed-input correction is allowed in this baseline;
   - compare per-frame input, frame counters, RNG state, and state hashes;
   - reject the test scenario until the baseline is stable or every exclusion is
     documented with evidence.
2. Add `RollbackStateHash` with layered hashes:
   - full HgCpuDirect blob;
   - required rollback extras;
   - selected chara health/position/move-state fields;
   - BM counters and round state;
   - input history/cache state;
   - RNG state.
3. Add mismatch reporting:
   - first mismatching byte ranges;
   - field labels when a range maps to a known snapshot field;
   - frame number, seed, inputs, and rollback depth.
4. Add deterministic round-trip tests for `K = 1, 2, 8, 15, 60`:
   - capture snapshot `S0`;
   - record inputs for `K` frames;
   - run forward and hash;
   - restore `S0`;
   - run the same inputs again;
   - require matching hashes or an evidence-backed exclusion list.

Verification:

- New automation script, for example `tools/rollback_lab_test_run.py`, can
  launch SC6, enable lab mode, watch `UE4SS.log`/JSONL, and fail on mismatches.
- Every failure emits enough data to replay the same seed and frame window.
- Baseline oracle stability is a separate required pass condition before
  restore/resim or delayed-input correction results are considered valid.
- Existing strict replay seek test still passes after changes that touch
  ReplayScrub, clocks, tick gates, no-render gates, MoveVM/hit state, RNG, or
  timeline generation:

```text
E:\myMods\tools\replay_seek_test_run.py --kill-game --launch-game --allow-unknown-presence --start-replay E:\myMods\ReplayExample\REPLAY_12744704008398858106.bin --timeline-generation-mode lux-no-render --case-preset watch --watch-frames 600 --wait --analyze --strict --min-resume-tick-rate 58 --resume-tick-window 120 --max-seek-validation-seconds 0.5
```

Ghidra work:

- For every newly discovered hash field, either tie it to a named struct field,
  a labeled global, or an EOL comment at the access site.

## Phase 6 - Local Rollback Controller And Fault Injection

Implementation:

1. Build a local two-player rollback controller with no network dependency:
   - fixed absolute frame numbers;
   - local input history;
   - remote confirmed input history;
   - remote predictions using held-last-input;
   - snapshot per stable pre-frame;
   - correction queue.
2. Add fault injection:
   - delayed remote input;
   - dropped input;
   - reordered delivery;
   - duplicate delivery;
   - stale prediction;
   - wrong frame id;
   - corrupted input byte;
   - corrupted cache tag for cache-path tests;
   - master-clock jump/stall;
   - skipped/double manual tick;
   - RNG perturbation;
   - side-effect leak test;
   - round-boundary refusal.
3. On late confirmed input:
   - compare with prediction;
   - restore snapshot before the first wrong frame;
   - patch input history;
   - resimulate frame-by-frame to the present;
   - compare final hash with no-delay baseline.
4. Add a local input-delay scope guard:
   - record stock/lab-disabled input timing before rollback changes;
   - run a small matrix with rollback lab disabled, lab enabled without
     correction, and lab enabled with artificial remote delay;
   - fail if local input timing changes in lab-disabled mode or if correction
     tests depend on removing local input delay.

Verification:

- Local correction passes for delays within the configured rollback window.
- Inputs arriving beyond the window trigger a defined refusal/stall/desync policy.
- Fault cases are deterministic by seed and emit replayable traces.
- No test relies on local input delay removal.
- Lab-disabled delay regression matches the branch baseline for the same scenario
  after any input/cache/clock/online-drain change.

Ghidra work:

- If cache-path tests are used, re-check `GetCachedInputForFrameInputLogSlot`,
  `LuxBattleChara_UpdatePlayerInputData_FromRoundCache`, and online drain
  ordering before writing live cache cells.

## Phase 7 - Side-Effect And Presentation Gates

Implementation:

1. Add a hidden-resim side-effect policy:
   - suppress audio;
   - suppress VFX/particles;
   - suppress HUD/debug/UI event dispatch;
   - suppress animation notifies or dedupe them by frame/event id;
   - hold camera presentation during hidden resim and publish final state only;
   - prevent unrelated actor ticks from mutating battle state during catch-up.
2. Reuse existing gates where they are correct:
   - `WorldTickGate`;
   - `ReplayClockGate`;
   - `ActorTickGate`;
   - `TimeDilationGate`;
   - `VFXOff`;
   - no-render timeline lessons.
3. Add an event ledger for hidden-resim frames and final visible frames.

Verification:

- Gameplay hashes match with gates enabled.
- Hidden-resim event count is zero or explicitly deduped.
- Final-frame side effects are not lost.
- Visual/manual smoke test shows no duplicate hit sparks, audio spam, or camera
  discontinuity after a correction.

Ghidra work:

- Name/comment any newly hooked side-effect function before relying on it.
- If side-effect gating touches RNG users, re-check RNG function comments and
  hash inclusion.

## Phase 8 - Online Transport Adapter

Implementation:

1. Do not overload SC6 stock opcode 0/1 packets for rollback metadata.
2. Add a HorseMod rollback transport adapter after local rollback passes:
   - absolute frame id;
   - local input for frame;
   - last confirmed remote frame;
   - resend/input range metadata;
   - optional state hash;
   - prediction age and rollback stats for diagnostics.
3. Add a GekkoNet adapter layer before touching Steam transport:
   - use GekkoNet's custom adapter callbacks for send/receive/free;
   - bridge deterministic queued Horse packets first;
   - feed Gekko save/load/advance events from the existing rollback lab model;
   - require desync events to map to the current state-hash policy.
   Status: deterministic queued adapter harness, Horse/Gekko bridge envelope,
   and localhost UDP socket adapter are implemented and validated. The UDP
   adapter carries HRG1-wrapped GekkoNet payloads through real WinSock loopback
   sockets, rejects wrong UDP endpoint plus wrong source/destination/session
   identity, and still keeps gameplay input decode in the explicit `uint32_t`
   Gekko advance-event bridge.
   The live-transport queue model now validates HRG1 v2 source peer,
   destination peer, session id, and metadata on the network side, then drains
   into the online-session model only after the game-thread cache-order gate.
   The live-peer pipeline model now additionally
   proves that HRG1/Gekko metadata is not written as stock gameplay input, and
   that confirmed gameplay input must enter the cache shadow through a separate
   decoded-input step on the game thread after stock drain or through explicit
   drain-bypass. The Gekko gameplay-input bridge now decodes actual
   `GekkoAdvanceEvent` input buffers into that decoded-input step as
   `uint32_t` gameplay values, with HRG1 payload hashes kept separate from SC6
   cache input values. The stock transport surface guard now also
   proves HRG1 is rejected on known native stock send slots/channels even if a
   route is erroneously flagged Horse-owned, and remains confined to a distinct
   Horse-owned adapter path. The stock transport observe hook now proves the
   native acquisition/input/BattleSync send and receive-enqueue entry points can
   be installed and traced without replay regression. The combined
   live-online-capture gate now additionally requires the stock drain and
   cache-consumer boundary hooks to be active with no ordering violation. The
   guarded live-activation gate now refuses readiness-only capture, stock
   surfaces, missing strict identity, boundary violations, missing native
   session/InputLog pointers, self/zero peer identity, zero session id,
   operator-not-armed state, missing receive traffic, and non-HRG1 payloads. The
   activation-gated executor now refuses all HRG1 enqueue/drain/cache actions
   until that policy is ready, then proves queued-only network ingress, stock
   drain before metadata acceptance, metadata-vs-gameplay separation, decoded
   gameplay prediction/confirmation/consume, and wrong source/session rejection.
   The local end-to-end harness now proves the rollback/Gekko/pipeline layers compose
   across decoded gameplay input, HRG1 metadata, prediction divergence,
   confirmed correction, cache consumption, and deterministic convergence. The
   Python lab runner defaults to the request-file gate to avoid Steam
   launch-parameter prompts; `--live-online-only` is the stricter real-match
   attach path. Each run carries a unique `request_id` through the request file,
   trace events, live summary, and analyzer filter, and it must prove nonzero
   live send, BattleSync, receive-enqueue, drain/consumer, and
   `live_order_proven=true`, then pass the explicit activation policy and
   executor route checks before any HRG1 injection is attempted.
4. Keep stock online parser guarded or bypassed in rollback lab mode:
   - stock drain must run before prediction injection, or be bypassed;
   - never write the live cache from a network thread;
   - do not trust 4-bit frame-low tags for rollback identity.
5. Add a hard cache/drain ownership trace gate:
   - trace stock drain entry/exit, prediction writes, cache reads, and consumed
     frame ids on the game thread;
   - prove each consumed frame observes exactly one intended input source;
   - prove late packets cannot overwrite confirmed history incorrectly;
   - prove network-thread packet reception only touches the inbound deque, not
     live cache entries;
   - fail tests on out-of-order drain/prediction/read sequences.
6. Add peer compatibility handshake:
   - SC6 build id;
   - HorseMod version;
   - rollback protocol version;
   - gameplay-affecting mod/stage manifest hash;
   - rollback window and hash policy.

Verification:

- Two local instances or a loopback harness prove delayed/corrected input
  behavior before real matchmaking.
- State hash warnings appear on forced mismatch.
- Over-window late input has a deterministic policy.
- Connection diagnostics report rolling RTT, jitter, loss, reorder, duplicate,
  rollback depth, prediction age, and correction count.
- Cache ownership trace passes for stock-drain-before-prediction and drain-bypass
  lab modes before any real online prototype is trusted.

Ghidra work:

- Revisit online send/drain functions when integration starts, but keep native
  packet parser edits minimal and guarded.

## Phase 9 - Performance, Soak, And Acceptance

Implementation:

1. Measure snapshot save, restore, one-frame step, and worst-case catch-up time.
2. Tune default rollback window only after deterministic correctness passes.
3. Add developer metrics in UI/log:
   - current frame;
   - confirmed frame;
   - prediction age;
   - rollback depth;
   - correction count;
   - hash status;
   - average/max save/restore/resim time.
4. Document remaining exclusions and unsupported lifecycle boundaries.

Verification:

- Stress with seeded random inputs, hitstop-heavy sequences, stage boundary
  contact, VFX-heavy frames, and repeated round-start refusal tests.
- Run long soak sessions with max rollback window.
- Build and deploy final branch.
- Run strict replay seek regression whenever touched systems require it.

Acceptance criteria:

- Same initial snapshot plus same input stream produces identical final hash for
  `K = 1, 2, 8, 15, 60`.
- Delayed remote input correction within the rollback window matches the
  no-delay baseline hash.
- Hidden resim does not leak visible side effects.
- Rollback never crosses round/object lifecycle boundaries.
- Failure logs point to the first divergent frame/field well enough for another
  agent to reproduce.

## Final Low-Context Review Loop

1. After implementation and verification pass locally, ask a fresh low-context
   agent to review only a bounded packet:
   - this plan;
   - the final diff;
   - rollback evidence table with field/range/hook/exclusion mappings;
   - snapshot manifest;
   - hash manifest and exclusion list;
   - baseline no-delay JSONL and compare output;
   - rollback fault/correction JSONL and compare output;
   - side-effect event ledger;
   - cache/drain ownership trace;
   - strict replay seek logs when required;
   - known exclusions.
   - one pinned command that regenerates the full final-review evidence bundle,
     including manifests, baseline compare, correction compare, cache/drain
     ownership trace, side-effect ledger, strict replay logs when required, and
     exclusions; or per-artifact pinned rerun commands with build id, scenario,
     seed, rollback window, and output paths.
2. The reviewer should look for faulty assumptions, missing tests, unsafe
   thread/cache writes, unproven Ghidra claims, local input-delay scope creep,
   lifecycle-boundary mistakes, and weak acceptance criteria.
3. If the reviewer finds no blocking faults, record the review result and ship
   the branch for user validation.
4. If the reviewer finds any blocking fault:
   - create a new remediation plan from the fault list;
   - execute that remediation plan;
   - rerun build, rollback lab tests, required replay seek tests, and relevant
     Ghidra cleanup;
   - repeat this Final Low-Context Review Loop.
5. Every remediation plan must include this same final low-context review loop
   at its end.
