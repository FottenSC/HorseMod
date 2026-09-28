# G1 first trace writer: manager continuation follow-up

**Decision: same-manager-task correlation established; continuous generation/lease and complete trace/material recovery remain NO-GO.** Pair2 remains the first failed prefix. No new live run, deployment, 408 retry or 600-cycle run. GPT-6 Astra performed all coding in the existing checkout; no delegation or public API change.

## Retained evidence

- [Final index: source archive, build/native checks, cleanup, raw logs](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/8f6f7f5c94c9af2840dd8f7c908698c050ea081cbef649625b0db59a6fabff05.json).
- [Audit: exact first writer, earlier producer/hub, source hashes, mapped identities and runner receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/0caedbd598ccef265096a40b128c12fe6254333f3cd6d5181c110bb79978257c.json).
- [Original immutable live sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7840e51724420a6ce1584d28b8e56f2c74057f7e7b7f16c1d5cdda6e07776910.json), [candidate raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7ef0fe47a04d3fe895876a06cd82840e8793adc79b21510382bd37ebf5718e56.log), [original failed paired manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f0a0bc72716807e87246a43a8a59d44e8ea82bfbc45847d4fbcb24673659b831.json).
- [Native MCP transcript and verified saved plate comment](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/80996a88572c92f3d56a864c1bfe6d986c72f3bcbb48f217d164009ebbe58479.json), [independent semantic route](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/399a836c6ad26100911b7041cca4d385867c0c72d060c9dfbd7d93d287b3a6c8.json).

The fourteen audited runtime/observer source files match the original live build archive byte-for-byte. The original required build, run manifest and mapped runtime/framework/observer identities agree. The new required build retains those same DLL hashes. All compared files and the final runner/test changes match the new reconstructible [source archive](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-73ece0b49df4f6de81cac04456c29827a7820413a0f05addbbcbb7a99875ad7d.zip), SHA-256 `690e2f5c720b726fa92b82e23f937f08bd1611d5446afa15da4bb898b8a643fe`. Dirty/untracked inputs remain preserved.

## First prefix, not later repair

Pair2 is registration caller RVA **8CDC8C**, sequence **114830/114831**, inside kind3 scope **57428**, sequence **114829/114832**, OS thread **94556**, native thread **2**, application epoch **1758**. The indexed execution owner is **LuxBattleManager**, not the registered trace receiver.

Earlier **producer67** at **2629651062 ms** was inside kind2 scope **57422**, nested under selected disable-hub scope **57427**. Pair2 enters at **2629651078 ms**. The hub returned at that same millisecond; timestamps alone do not order everything within it. The producer snapshot and pair2 scope agree on all of these fields:

| Binding | Both observations |
|---|---|
| Task / scheduled task | `0x278209c0510` |
| Tick function | `0x2791997ba68`, table RVA `381C720` |
| Completion-reference argument / event | `0x278209c0550` / `0x2787e077900` |
| World | `0x27829b3cae0` |
| Manager | `0x2791997ba40`, index311213, serial8343, table RVA `327AA20` |
| Application epoch / native thread | 1758 / 2 |

The actual registration manager is a different object (index310119/serial8362); its listener is index311114/serial8104. A valid indexed **execution manager** does not lease either receiver or their material/resources.

## Host and native ownership

`Sc6ReplayTaskGroup::DispatchTask` selects the bound manager by tick virtual **141C15C90** and actor identity. It sets `pending_task_`, increments `manager_tasks_`, and constructs kind2 before prerequisite processing, timing updates or the manager virtual call. `ExecuteReplayInterval` yields its logical continuation only when `OwnsManagerEntry` holds. This is a task that has already executed work, not `ConsumerAdmission::Hold` before native entry.

`PumpOne` services `pending_task_` before dequeuing later work. `ResumeManagerTask` validates a pending phase, function+18 scheduled binding, task+38 constructed byte and incomplete event, then constructs kind3 and advances the existing executor. `FinishManagerTask` alone clears the binding, completes/releases the event and recycles this custom manager task. Native **14215D326** clears the binding after its virtual call; **14215ED4D..14215EE12** performs construction clear, completion, reference release and TLS pool return. Manager vtable +3C0/+418 resolve to **141C2D330/1403FBF30**; the former has only an epilogue after +418 returns.

This source path strongly supports **57428 resuming the task entered under57422**. There is no observed contradictory binding. However, the retained kind2 metadata was sampled **at producer67 entry**, not at the original task entry. Registration scratch is discarded at kind2 return when no registration selected it. The sidecar contains no original kind2 entry/return ordinals, pending-task handoff, original allocation-generation token, or retirement transition. Scope IDs are global invocation serials; application epochs and UObject serials are different identities. Adjacent hub57427/resume57428 IDs strengthen ordering but are not a generation receipt.

`ValidatePendingTask` reads the current function/binding/event, without comparing a retained original tuple. Host `ValidManager` checks indexed address/serial/validity, and `AdvanceInteriorHold` checks binding and epoch, but pair2 is not tied to an explicit `BeginHold`/`AdvanceInteriorHold` receipt. Ordinary logical yielding must not be relabeled a proven user-visible hold. The known source protocol is not evidence of a separately enforced trace-receiver lifetime or complete-B lease.

## Before-effects boundary and next independent route

The captured game PCs include **8CDC8C <- 8CD97A <- 3C53F5 <- 3EF099 <- 1D38382 <- 400A9A <- 3786C6**, followed by MoveVM/main-simulation frames. This positively identifies **ActivateLuxTraceManagerRequest**, rather than relying on a hypothetical BeginTrace caller. Start tail-jumps into the binder; its missing frame is expected. Registration remains after Start's component/material effects. The 32-PC stack is saturated and non-game frames are not fully symbolized.

The kind2 source boundary precedes payload effects reached through that manager invocation, including the earlier required producer67 callback. The retained producer snapshot itself cannot establish before-every-effect entry state, whole writer coverage, or exclusion across resumption. It cannot admit undo at kind3 or registration entry.

**Next independent route:** inspect the already identified collection18 semantic dispatch **140400A00**, whose native body constructs a stack descriptor then synchronously broadcasts through **141D38300**. Its exact listener reaches **1403C5360 -> 1408CD940 -> Start**. Extend the existing `DeterministicHookSet::CallbackExecutorDetour` owner only if a bounded pre-broadcast receiver/state witness is justified; do not install another hook at141D38300. Establish collection/listener generations, recursive writer exclusion, the complete affected trace/component/material B participants and required completion before any admission. The descriptor supplies start configuration only, not history or undo. This independent boundary avoids treating saturated registration ancestry as a lease; it does not remove the manager handoff obligation. No new live recipe is justified by this evidence alone.

## Narrow runner change and verification

The existing retention boundary now emits `vfx_first_trace_writer`: pair2, scope57428, earlier producer67/scope57422 at `producer_entry`, and explicit false generation-transition/ownership/before-effects claims. It selects the first writer in native event order and never promotes a later row. Missing first-pair ancestry is already rejected by the existing validator; corrupt subsequent publication clears an old receipt.

[Production retention RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/5e397b1f6a3f4a3dcb0c2be0fb9e6dcc6edf62292f84e02994a1baa6d69c0e49.log) used the immutable live sidecar and failed specifically because the correlation receipt was absent. [Final local GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/4739131caafc2fa2b2ef8ec5ae07807cea39f50a19bad5cfd2c4242edee0f467.log): **83 passed**, including changed function/binding/event/world/serial/thread/epoch, later/reused scope, unreadable/truncated input and missing first witness. These thirteen evidence regressions explicitly skip when their immutable live artifact is unavailable; all ran here. No fixture grants task ownership. An intermediate test incorrectly expected missing ancestry to pass validation; its retained failure confirmed the existing rejection, and the assertion was corrected. [Exact two-file runner/test diff](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6f71884fdd9d56ce9892d3dedfbc25bb78f89e2e7f5608e6bc14c574bfaf259d.diff).

[Required build](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/a1e9f6e5512ecf838be82849f6d02cdeae43cc63127d075587424a3ebfad5bec.json) passed both CTests and the shipped native checks. No C++ or runtime behavior changed; observer reservation remains **3 MiB**, production ceiling **1 GiB**. One directly relevant plate comment at14215D250 was updated, read back and saved through native MCP in the existing SoulcaliburVI.exe; no type/prototype changes, imports, scripts or competing hooks. Existing conservative/partial type limitations remain.

| Dimension | Result |
|---|---|
| Observer | Local83 GREEN; first-writer correlation now retained; original bounded live witness preserved; lease/coverage unproved |
| Simulation | No new execution; prior120 compared continuation ticks remain inside the failed paired stage |
| Recovery | No changed VFX B/application/render/GPU settlement; site11 containment is not recovery |
| Coherence | Unqualified; no new normal-rendering evidence |
| Performance | Prior55.202 TPS failure unchanged; cause unassigned; no new runtime overhead added |
| Cleanup | [Fresh receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/991ad17b2f5f73c5f20474021e9dfeefbe9a52e476fce75b573ead5bc587a055.log): journal ready, all six prior deployed states verified, no game; Steam98544 retained |
| Gates | G1/G2 OPEN; debris exact/Unresolved; no408/600 advancement |
