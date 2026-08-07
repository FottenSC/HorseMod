# HorseMod rollback beta: casual lobby over Steam P2P

## Supported beta path

The beta uses Soulcalibur VI’s ordinary two-player casual-lobby flow:

1. Both players install the same reviewed HorseMod DLL and the same Steam
   beta profile.
2. Both launch SC6 normally through Steam.
3. Either player creates a two-player casual lobby and invites the other.
4. Character and stage selection remain stock.
5. HorseMod observes the established Steam lobby and its exact opponent,
   performs an ephemeral ECDH/key-confirmation exchange on a dedicated Horse
   virtual channel, then authenticates every rollback datagram.
6. Rollback takes ownership only after SC6’s native fighter-slot capture,
   executable, schema, selection, and frozen battle identity all agree.

SC6 ships Steamworks v139’s legacy `ISteamNetworking` P2P API. It provides
Steam NAT traversal and relay fallback for the Horse gameplay channel. The
mod reuses SC6’s initialized Steam user and pipe; it does not initialize
Steam, create another user/pipe, dispatch callbacks, or close SC6’s shared P2P
session.

No public IP address, port forwarding, router configuration, or manually
shared gameplay secret is required for the Steam beta path. Windows Firewall
must still permit SC6/Steam network traffic in the same way as an ordinary
online match.

## Generate the profile

From `E:\myMods`:

```powershell
python tools\rollback_beta_config.py steam `
  --output E:\myMods\reports\rollback_beta\rollback_beta.ini
```

Install the resulting file on both PCs at:

```text
<SC6>\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\rollback_beta.ini
```

The minimal profile is:

```ini
config_version=2
enabled=true
transport=steam-p2p
rollback_window=12
input_delay=1
trace=true
```

Validate it with:

```powershell
python tools\rollback_beta_config.py validate <path-to-rollback_beta.ini>
```

Command-line and `rollback_lab_request.txt` configurations take precedence
when present at startup. Set `enabled=false` or remove `rollback_beta.ini` to
disable the persistent beta.

Explicit routed/port-forwarded UDP remains available as the compatibility
`direct-udp` profile produced by the tool’s `single` and `pair` commands. It
is not the default beta transport and is qualified separately.

## Fail-closed behavior

Rollback does not activate unless both peers agree on:

- the same two-member Steam lobby, owner, and opposite Steam peer;
- an ephemeral ECDH-derived session key and mutual key confirmation;
- opposite expected player sides, followed by authoritative native slot
  validation from SC6;
- executable and snapshot schema;
- observed character/stage selection;
- frozen stock battle baseline.

Packets from any Steam ID other than the bound opponent are discarded before
Protocol V2 authentication. Bootstrap packets are reliable; gameplay packets
are unreliable to avoid head-of-line blocking and are structurally capped at
Steam’s 1,200-byte complete-packet limit.

Ordinary shutdown closes only Horse’s virtual channel. Identity changes,
bootstrap timeouts, authentication failures, native slot mismatches, and
session-contract mismatches fail closed; they must never silently resume
delay-based simulation after rollback owns the native tick.

## Qualification before distribution

Freeze the candidate only after the runner and DLL have been built. Candidate
manifests are immutable and bind the Steam transport, protocol/snapshot
versions, source commit/diff identity, runner SHA-256, DLL SHA-256, replay
corpus and sidecars, complete trusted-golden manifest, fault-profile matrix,
runner helpers, rollback window, input delay, and rollback depth:

```powershell
python tools\rollback_two_client_acceptance_run.py `
  --write-candidate-manifest reports\rollback_beta\candidate.json `
  --built-dll build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll
```

The manual native-input lane uses two physical controllers. It pauses for an
explicit Enter acknowledgement before neutral, host-only, Sandboxie-only, and
completion boundaries. Follow each prompt; fixed elapsed time alone is not
accepted as controller-isolation evidence:

```powershell
python tools\rollback_two_client_acceptance_run.py `
  --mode stock-online-attach `
  --gameplay-transport steam-p2p `
  --input-source native `
  --native-controller-preflight `
  --profile wifi_50ms_jitter `
  --active-seconds 120 `
  --require-round-rearm `
  --built-dll build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll `
  --candidate-manifest reports\rollback_beta\candidate.json `
  --report reports\rollback_beta\native-attach.json
```

The full local gate consumes that report and executes the ON/OFF CMake matrix,
Steam/Sandboxie profile and seed matrix, all 14 replays, recovery cases,
both-role bounded worker stalls, a duplicate-only profile, a one-hour
same-process lobby/match lifecycle soak, a separate one-hour continuous
session soak with process resource telemetry, and strict replay:

```powershell
python tools\rollback_two_client_acceptance_run.py `
  --local-beta-gate `
  --gameplay-transport steam-p2p `
  --input-source replay `
  --replay-file ReplayExample\REPLAY_12744704008398858106.bin `
  --manual-attach-report reports\rollback_beta\native-attach.json `
  --built-dll build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll `
  --candidate-manifest reports\rollback_beta\candidate.json `
  --report reports\rollback_beta\local-qualified.json
```

`single-host-dual-client-qualified` means only that the exact artifact passed
the one-machine two-process gate with the exact, nonempty stage inventory.
The registered in-memory Steam pair and local Sandboxie runs remain
presubmit/local tiers. A beta artifact must also pass
two physical PCs on distinct consumer internet connections using the exact
DLL and source commit. The release gate combines, rather than regenerates,
those two immutable evidence sets:

```powershell
python tools\rollback_two_client_acceptance_run.py `
  --beta-release-gate `
  --gameplay-transport steam-p2p `
  --built-dll build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll `
  --candidate-manifest reports\rollback_beta\candidate.json `
  --local-qualification-report reports\rollback_beta\local-qualified.json `
  --release-qualification-manifest <physical-manifest.json> `
  --report reports\rollback_beta\release-qualified.json
```

Record:

- both DLL SHA-256 values and source commit;
- beta profile hashes and transport (`steam-p2p`);
- Steam interface version, lobby/owner/local/remote IDs, and whether Steam
  reports relay use;
- fighter slots, stage/selection/session contracts;
- protocol/snapshot versions, test seed, network profile, traces, and reports.

Run the clean casual-lobby path plus latency, jitter, loss, burst loss,
reorder, duplication, corruption, scheduler stall, disconnect/reconnect,
round and match transitions, the representative content matrix, and a long
soak. Localhost or Sandboxie alone cannot satisfy the beta release gate.
