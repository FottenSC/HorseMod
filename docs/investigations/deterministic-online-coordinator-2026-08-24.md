# Deterministic online coordinator milestone

## Status

`Horse::Deterministic::OnlineCoordinator` is a compiled, structurally tested
component of `HorseDeterministicCore`. It is not connected to `dllmain.cpp`,
does not start Steam networking, and cannot activate rollback. The production
native-region manifest and runtime content allowlist remain empty.

## Enforced contract

- Activation is limited to an injected, qualified content contract in a
  two-member casual Player Match lobby.
- Both peers must agree on the protocol and snapshot schema versions,
  executable and HorseMod build identities, ordered Steam identities, opposing
  player slots, lobby identity, content identity, input delay, and rollback
  window.
- A generation and canonical baseline hash must be acknowledged bilaterally
  before the coordinator enters `Active`.
- The first successful `NotifyOwnedTick` is the ownership boundary. Failures
  before it produce `LeaveStockUntouched`; failures after it produce
  `TerminateMatchToLobby`.
- Input packets are typed, value-only, generation-tagged, and unreliable.
  Confirmed state hashes are typed, generation-tagged, reliable, and accepted
  only on the 30-frame cadence. Round-boundary hashes use reliable control
  messages.
- Session IDs are checked before payload parsing. Old-generation gameplay and
  completed round-barrier duplicates are ignored; future or conflicting
  generations fail closed. Queued gameplay is discarded atomically when a
  round generation completes.
- Pre-ownership exit stops only the Horse transport and immediately resumes
  stock lobby observation. Post-ownership exit waits for an explicit lobby
  return notification before re-entry.

## Structural evidence

`OnlineCoordinatorSelfTest` exercises bilateral handshake and baseline
activation, executable/build/content agreement, qualification rejection,
two-member casual-lobby restrictions, typed input and signed-axis encoding,
reliable state hashes, hash cadence rejection, duplicate control messages,
round barriers and generation re-entry, stale-session rejection, pre-ownership
exit/failure, post-ownership transport failure, lobby return, and later match
re-entry.

The test runs under the `deterministic;online;fast` CTest labels. On 2026-08-24,
all three registered deterministic tests passed against the same build.

## Deliberately open gates

This milestone does not claim networking qualification. The following remain
required before production wiring:

1. A reviewed Steam P2P `IRollbackTransport` with an authenticated dedicated
   Horse channel, ephemeral peer key agreement, confirmed session keys,
   bounded preallocated worker/game-thread queues, and teardown that does not
   close SC6's shared Steam session.
2. The pinned GekkoNet integration and confirmed-frame/save/load APIs.
3. Admission of a complete native adapter and at least one content case after
   normal-render offline correction proof.
4. In-memory plus normal-host/Sandboxie-client impairment, re-entry, soak, and
   release qualification using distinct Steam identities, isolated writable
   state/logs, immutable identical artifacts, and evidence-bound reports.
   Sandboxie remains external runner infrastructure and cannot enter production
   HorseMod code.

Until those gates close, the coordinator remains production-inactive and the
game continues to use stock simulation.
