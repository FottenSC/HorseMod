> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Active damage-type B ownership

The bounded A170/B300 ->300 guard-input attempt rejects before A publication because B owns one active DmgTypeEff player in slot2. The diagnostic candidate is replay-7cda34fac9b14ef8a0e3448aa4ecf0ae. `same-origin-guard-B300-active-type-diagnostic-audit.json` in the build replay-tests directory links its retained raw log, candidate, exact binaries and reconstructible sources. Cleanup completed with zero games; this is not B recovery proof.

## Native findings and required invariant

* Native1417ED0A0 searches widget ActiveSequencePlayers at190, excluding stopped entries, for animation370. If absent it creates a new player owned by that widget. It does not search UObject outer children. Retained decompilation: `artifacts/replay-live-recovery-20260911/active-hud-native-start-20260912.json`.
* Rechecked native1418272A0 ticks only the active array, removes stopped entries, then processes widget latent actions. Its call to141826F90 is the only code reference. The other three references are native exception/unwind metadata (.pdata and .rdata), not a second scheduling owner.
* Cooked DmgTypeEff has only Construct and an empty returning event graph; no Blueprint Tick or latent operation. Existing callback/latent admission remains mandatory. Existing TypePlayerBinding and TypeAssetReconstructible validate the actual player callback, empty asset delegates, cooked image color/transform/visibility KeepState tracks and source owners.
* Native141764AC0 quantizes an eight-byte-element allocation, updates capacity and calls FMemory_Realloc. A request of zero frees backing without dispatching animation callbacks. Its actual installed24-byte prefix is `48895c2408574883ec204863da488bf985d2741e488bcb33` and production checks it before private preparation.
* Native Finish1418010D0 closes evaluation without ordinary animation-finished delegates. UObject/root destruction and GC reachability remain separate lifetimes.

The invariant is that B's active player, evaluation root, callback and exact list allocation survive A publication and native C execution untouched. A must never restart a B-owned player. C must not be able to reuse B's player or free B's original list. Undo must retire discarded evaluation before reattaching B, without copying a saved player image or manufacturing animation events.

## Integrated implementation

The existing HUD participant now journals private-player preparation, detachment, exact recovery and irreversible retirement. The0x780-byte player witness is read-only. Native C receives separate list backing because native removal can shrink/reallocate an active list to zero; count-only exclusion does not guarantee B undo. The original allocation is transferred back on recovery, not restored from a stale address. Commit finishes B evaluation and releases its detached list through the verified native allocator. Player/asset leases survive the host's ordered retirement. ReleaseCapture rejects still-private or retiring ownership.

Unknown callbacks, multiple active players per type slot, changed playback/bindings, changed B witnesses, aliased players, oversized arrays and incomplete retirement reject. An active historical A still requires the independently implemented retained-player reconstruction and cannot borrow B. No additional render-history participant, GPU copy or readback is added.

Witness/journal storage is included in sizeof(HUD image), approximately31KiB per image. Detached list backing is charged separately while privately owned; validated capacity is at most16 pointers per active slot. Native C may allocate a new sequence player using its ordinary producer. Capture, publication, retirement and full seek latency still require measurement; the existing broad memory accounting must not be interpreted as a newly measured deep allocation census of these native evaluation graphs.

## Focused proof order

1. Local journal rejection/retry tests, production build and native fixtures. Local tests are not actual native HUD integration proof.
2. A170/B300 before-publication cancellation: no private-player detachment/retirement, unchanged B and120 independently matching gameplay/pose/HUD ticks.
3. Same after-publication cancellation: actual slot2/player/list detachment and original allocation recovery, no B retirement, then120 independent ticks. HUD comparison requires every301..420 observation, logical type-player clocks and active type coverage, not just an empty damage-bar pool.
4. Retry A170/B300 ->300 corrected guard201..230 commit, compare behaviorally changed continuation against an independently edited native control. Any first failure is retained and investigated before retry.
5. Exercise C execution and cancellation after C/GPU drain, with complete B recovery, before claiming general active-B support. The initial cancellation checks do not prove that boundary or C-created-player cleanup.

Pixel equality is optional. These changes do not complete checkpoint coverage, the unsupported initial/tail ranges, full seeker qualification or rollback performance.

## Current live evidence

Runtime4a84d504 / observerce64a46c passes the before/after publication B300 cancellation checks and the same-origin guard201..230 correction through C300 commit. Each includes120 independent gameplay/pose/HUD continuation ticks. The corrected run changes gameplay before C and matches its independently edited native control. Retained audit prefixes are `private-HUD-B300-before-qualified`, `private-HUD-B300-after-qualified`, and `private-HUD-B300-same-origin-guard-qualified` under the build replay-tests directory. Completed corrected seek917725us fails500ms; no new held-image review. C/GPU-drain cancellation remains pending on observerde8c2e09; publication checks alone do not prove it.

The C300 Render::Drained experiment now passes on runtime4a84d504/observerde8c2e09: eight C-only owners retire, a fresh GPU drain completes, original B player/list recover without commit, and120 independent gameplay/poses/HUD ticks match (53 active type ticks). See `private-HUD-B300-drained-qualified-audit.json`. This closes the bounded C-execution recovery case; it does not establish arbitrary HUD membership or general visual coherence.


## B5695 observer completion contract (2026-09-13)

Read-only Ghidra recheck of native141826F90: after adding float delta multiplied by speed+758 to double time+6A0, completed forward playback clamps time to authored end+6D0; reverse playback clamps to start+6C0. Loop/reverse modes have separate branches. Once finished, status+6D8 becomes0 and the native completion delegates dispatch. Therefore after-before==delta is not a universal invariant.

The explicit B5695 HUD window exposes identical native/candidate DmgTypeEff completion at5695,5747,5752: before0.350000018, after0.350000024, delta0.016666668. The old parser rejected both. The regression executes the actual production rejection condition and reproduces that false rejection. The replacement still requires fixed60 delta and requires every nonlinear candidate tick/widget/player/delta/before/after tuple to match an independent native observation; wall time is excluded. Missing or differing native transitions reject. Full120-tick HUD, gameplay and pose continuation comparisons remain mandatory and pass on retained909881bb/e0125c73/93cc6306 evidence. No runtime behavior changed. See private-B5695-drained-qualified-audit.json for raw logs, executed sources and parser overlay; image coherence remains separately unreviewed.
