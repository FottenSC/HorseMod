> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Marker graph and broadphase ownership

The assembled full-index seek remains rejected before A publication. Schema72
integrates consumed CPU SAP state with reversible marker ownership. Candidate
replay-0d3901b6740047cd84fc6a94c0f890e4 indexed through B11506, then rejected
`sap_pending_scratch_or_aggregate`; complete B was retained, cleanup completed
with zero games, and no independent control was launched. The retained
`index-sap-admission-failure.json` links its actual retained log and sources.
The grouped diagnostic does not identify a specific native field. Current
instrumentation splits these unchanged guards; no rejection is bypassed.

## Proven from the shipped PhysX binary

* ScScene+728 owns the AABB manager; manager+100 is its broadphase.
* SAP constructor155FA0 and destructor156700 use vtable1ACE30.
  Slot+90 is15A5E0 (`48 8b 81 80 01 00 00 c3`): it returns SAP+180,
  the persistent active-pair array, not a GPU-only/null owner.
* E3F40 passes this table to11A800 and parallel tasks.11ACF0 and100DE0
  publish an interaction pointer into a 16-byte pair's last eight bytes.
* EFEE0 passes destroyed-overlap cached interaction pointers to11B030.
  When nonnull,11B030 releases the supplied interaction; when null it looks
  up the current element pair. Retaining B's pool slot without covering this
  external pointer would not protect B from native destruction.
* SAP+160 is its pair manager: hash+160, next+168, hash extent+170,
  hash allocation capacity+174, minimum capacity+178, pairs+180, states+188,
  live count+190, pair allocation capacity+194, hash mask+198.
 156BB0 frees per-update scratch and can shrink/rebuild pair backing.
 1570C0 removes deleted pairs via161AA0; it is not a complete reset.
* SAP constructor allocates three box endpoint arrays at+D0/D8/E0;
  three encoded endpoint-value arrays at+E8/F0/F8 and corresponding owner
  arrays at+100/108/110. Box capacity+C8, live/previous box count+140/+144,
  endpoint capacity+148. These arrays drive incremental overlap changes.
* Native150880 constructs the next broadphase input from manager+C0/C8,
  +D8/E0, +F0/F8 (added/updated/removed); groups+90; bounds owner+108
  (data+8, logical capacity+10, changed flag+18); contact-distance owner+A0
  (data+8). Native150290 constructs handle lists from bitmap owners
  +48/+58/+68 and consults volume records+ A8 (16-byte records).

Upstream NVIDIA PhysX3.4 `BpBroadPhaseSap.h/.cpp` and
`BpSimpleAABBManager.h` support interpretation; shipped native code above is
the authority. No runtime change is justified solely by upstream layout.

## Immediate design constraints and unresolved evidence

1. **Pair consistency and B lifetime require one coherent physics transaction.**
   A marker graph cannot be rewound independently of the SAP pairs which hold
   its pointers. Restoring only pointer values leaves wrong incremental pair
   history. A bounded historical image of consumed endpoints/pairs and pending
   bounds inputs is justified by simulation consumers and reversible ownership,
   not object-count or byte-equality qualification.
2. **Concrete live implementation and full handle-domain identity:** validate
   SAP vtable/signatures, volume-to-shape ownership and absence of unsupported
   aggregate/particle/cloth/contact owners before writes. The existing fixture
   does not prove the live replay satisfies those conditions.
3. **Scratch versus continuation:** trace the next-update reset of overlap and
   task buffers; prove the admitted application boundary has no outstanding
   consumers. Never copy stale task pointers or call a buffer-free routine while
   native tasks can consume it. Pending admission bitmaps/bounds are inputs,
   not scratch merely because their storage is reusable.
4. **Undo after native allocation changes:** restore logical data through
   validated current backing, or retain native-owned B allocations explicitly.
  156BB0 can resize pair arrays; old pointers must not be republished. Prewrite
   validation, partial publication recovery and native C pair creation/removal
   need actual-memory coverage including external pair pointers.
5. **Reconstruction comparison:** shiftOrigin156CC0 preserves endpoint order
   and is not a pair reset. Refiltering11CF70/11D770 can wake actors or allocate
   contacts/triggers and does not by itself preserve B. Neither is currently a
   proven cheaper replacement for the bounded consumed-state image.

Before adding capture, finish the reset/consumer trace in item3 and define the
minimum image from it. Bound its allocations under the existing512MiB budget;
measure added bytes and capture/restore CPU time. No GPU wait/readback is needed.
Test with native pair creation/removal and deliberately interrupted publication,
then run the assembled endpoint-to170-to208 transaction with complete B retained
and independently compared continuation. No separate storage-only live campaign.

Completed local proof: `index-marker-lease-local-build.json` in the retained
replay-tests directory links the exact build/source archive and local logs.
No schema71 live attempt has been made.

## Integrated design and current evidence

The CPU image is 81200 bytes, 162400 bytes for two scene slots per checkpoint.
It covers consumed endpoints/inverses, persistent pairs and their marker links,
handle admission bitmaps, bounds and verified immutable filter/shape owners.
Current native backing is validated and retained; historical allocation addresses
are not installed. Pair hash chains are rebuilt using the shipped signed hash.
Native150CF0 resets overlap outputs before producing the next ones;158990 restores
temporary link permutations. Both remain validated completed-boundary constraints.

The actual-native fixture now includes SAP AddPair/RemovePair and marker lifecycle.
It covers pre/post publication cancellation, native C cached-pointer consumption,
C marker reuse, B undo through relocated pair backing, commit, foreign-pointer
rejection and a fault after complete SAP publication. These are local proofs;
the live replay has not yet passed SAP admission or published A on schema72.
The broadphase-free fixture limitation above describes the preceding schema71.

Next unknown: which scratch/aggregate/output guard rejects the real boundary.
Observation: exact named admission in the assembled endpoint restore, with all
existing guards unchanged. It determines whether a boundary is premature, a
layout interpretation is wrong, or a currently unsupported owner needs analysis.
Do not assume zero scratch or add owner capture without that evidence. Remaining
extent/filter/shape compatibility, publication, complete B undo and independent
120-tick continuation are still unproven live. No GPU readback or renderer-history
participant was added. Full arbitrary-seek coverage and practical rollback cost
remain separate unfinished requirements.

## Active-aggregate finding

Diagnostic-only candidate replay-0515298c581343f8a46d8e6de3674cc1 identifies
sap_active_aggregates before A publication. Complete B11504 remained retained;
cleanup completed with zero games. index-sap-active-aggregate-failure.json links
the retained raw log, with exact source/binary identities in that report and
index-sap-active-aggregate-build.json. No native control launched.

Native14FAE0/14FCE0 increment/decrement manager+1C8 as aggregates are born/retired.
Retired table slots contain 32-bit free indices, not live pointers. Native151EF0
constructs aggregate+50 with handle+0, member array+8/count+10, optional self-pair
owner+18, dirty-index+20, inflated-bound owner+28/size+30 and sort-dirty+4C.
Native151690 uses manager timestamp+1F0 and the persistent-pair owner to emit
creation/deletion events. Native152BB0 consumes member ordering, inflated bounds,
and groups for self pairs. These are simulation inputs and event continuation.

Reconstruction is not automatically cheaper/correct: native152120 reconstructs
inflated bounds from member handles, current bounds and contact distances, but
1523E0 also reorders members and uses temporary allocations. Rebuilding pairs
without A history may manufacture events. No such operation has been added.

The immediate bounded diagnostic captures at most eight aggregate rows on the
already-rejected image, reporting total/observed counts explicitly. It inventories
A and B memberships, optional self-pair counts and the two aggregate pair maps.
These fields never pass admission or drive simulation. Unknown live nonempty
membership/pair history determines whether native reconstruction suffices or an
additional reversible logical image is necessary. Publication remains rejected.

## Empty live aggregates: bounded implementation

Candidate replay-964decd8bbe842dca98a3c7e7ec7f4c2 proves the two A/B aggregate
owners are identical, with zero members/self pairs, no dirty membership and empty
aggregate/actor-aggregate maps. Handles48/55 retain the same aggregate and optional
self-cache owners. index-sap-empty-aggregate-inventory.json links retained raw log
and source; cleanup complete, still no A publication or native control.

Schema74 validates the entire bounded aggregate slot/free-list membership, empty
member counts, no dirty work, self-cache binding/count/deletion flag, and exact
root-volume linkage. Empty roots must have no SAP endpoints or pending admission
bits. No aggregate allocation, timestamp or cached-sort image is republished.
Bindings remain exact across A/B/current checks. Nonempty aggregates still reject.
The completed failure-only inventory probe has been removed; minimal empty-owner
binding witnesses replace it. Native150290/150CF0 cannot visit these roots without
pending/dirty work;151690/152BB0 cannot consume their empty member histories.

The local fixture extends publication, native C marker/pair recreation, partial
undo, late undo, relocated backing and commit with an empty aggregate root plus
free slot and optional self-cache. It uses the shipped151EF0 no-self constructor
and verifies all aggregate/cache/slot bytes remain untouched. Negative cases
cover nonempty membership, pending root admission and free-list cycles. Build and
live outcomes still need recording. This is not admission for nonempty aggregates
or proof of general lifecycle recovery when membership changes during execution.

## Empty admission passed; first consumed input difference unresolved

Schema74 candidate replay-faab1aea55134702ab000b9e809cc88f passes the empty
aggregate checks and completes both SAP images. It rejects SameOwners against
live B before A publication. Complete B11506 retained; cleanup complete/zero games.
index-sap-input-owner-failure.json links retained log and full source identities.

Current diagnostic reports the first unequal binding or consumed filter/contact
input with handle index, captured side and values. It uses existing complete
images; no capture participant is added and no mismatch is normalized. Local
native-memory regression deliberately changes contact distance and verifies
precise rejection before marker detachment. Exact-build bootstrap and the next
assembled diagnostic run must complete before interpreting a concrete field.

## Handle high-water mark versus allocation continuation

Candidate replay-416eaffab51649c59a7e470db21e497f reports A extent62/B extent91
before publication. Retained index-sap-handle-extent-failure.json links raw log,
exact binaries and reconstructible sources. Complete B retained, cleanup zero games.

Native133D00 pops the scene+1130 element ID free stack or increments its counter;
1142C0 does the same for shape IDs at scene+1120.133E10/114350 queue releases via
FF080 (deleted bitmap+18, pending vector+28/count+30). NativeFDB00 processes these:
a highest-ID release decrements the counter, otherwise it appends the free stack.
F1ED0 and E75D0 call FDB00 and clear the deleted bitmap. Thus native retirement can
return allocation continuation while manager+1C0 still holds its largest bound ID.

Schema75 captures bounded ID continuation witnesses for scene1118/1120/1128/1130,
requiring exact owner/counter/free-stack equality and no pending deletion/bitmap
work. It does not republish ID state or allocation headers. A larger SAP extent
is admitted only for inactive tail volumes without endpoints, pairs or pending
bits. Current allocation/extent stays native-owned; A publication validates all
used continuation values rather than requiring identical retired-tail extent.
Desired writes remain within the validated current extent. Empty-aggregate free
head/order is also exact; it is not assumed allocation-order-independent.

The native-memory fixture calls shippedFDB00 on reverse-order transient releases,
then proves A publication/C pair recreation/B undo through larger retained bounds
storage. Changed next-ID counters still reject before detaching B. Both C++ suites
and native fixture pass. Two scene images cost183424 bytes/checkpoint. No GPU wait,
readback, additional native allocation or resource retirement participant is added.
The real replay's ID continuation and tail compatibility remain live unknowns;
changed continuation requires further ownership work, not a normalization exception.

## Logical ID continuation transaction

Schema75 candidate replay-aeb3e7cf9d094be6b90582040929a326 reports shape-ID
counter60/78 at A/B. index-sap-shape-id-continuation-failure.json links retained
raw log/exact sources; complete B retained and cleanup completed. The unchanged
allocator alternative is insufficient for this observed history.

Native1118F0 assigns the scene1128 rigid-ID to ActorSim+50. Native1142C0 assigns
the scene1120 shape-ID to ShapeSim+48;133D00 assigns the scene1130 element-ID to
ElementSim+18 (low29 bits), and EC3D0 uses that tracker for aggregate roots.
The scene1118 tracker is used by constraint construction115FC0 and remains strict.

Schema76 inventories shape/rigid IDs with existing volumes, requires complete
active-ID coverage and identical ID-to-native-owner bindings, and restores only
next/free logical continuation for trackers1120/1128/1130. Pending releases and
deleted bitmaps must be empty. Current owner/free-buffer ranges must be distinct
and fit both A and complete B; no historical pointer/capacity is installed. B's
immutable logical image survives C execution and current buffer relocation.
Unknown active IDs or changed active bindings reject rather than being overwritten.

The fixture uses nativeFDB00 non-LIFO retirement, native133D00 allocation from
published A,133E10 retirement, then full B restoration through relocated free
backing. It covers publication faults, post-execution recovery, commit, swapped
shape-ID bindings and aliased allocator buffers. Both C++ suites/native fixture
pass; index-sap-id-transaction-local-build.json retains exact local evidence.
The two-scene SAP image is191616 bytes/checkpoint, no GPU readback/wait or new
native resource-retirement owner. Live eligibility and continuation remain unproven.

Intrinsic invalid images now reject index retention immediately with their exact
admission stage. This avoids indexing the entire replay with an already-known
unusable checkpoint; it does not replace validation against current B.

## Static anchor accounted before the live attempt

NativeE8D30 constructs a scene-owned static anchor viaF2810/1118F0 and publishes
it at scene1138. It consumes rigid ID zero and has no broadphase volume. The
native113E90 base constructor sets scene+40/core+48 and core.sim back-reference,
with empty interaction and element lists. Scene construction fixes the core pose
to identity. This was resolved statically before launching a guaranteed missing-ID
coverage experiment.

Schema77 validates that root's vtable1AAE70, scene/core/back-reference, zero ID,
empty memberships and constructor-defined core flags/identity pose. Only its
binding is witnessed; no anchor history or allocation is restored. The fixture
uses shipped106DA0/1118F0 constructors, rejects a changed anchor pose, and checks
anchor bytes through publication/C execution/B undo/commit. An alias fixture was
updated for the extra rigid ID so the alias check, rather than earlier coverage,
is exercised. All local suites pass;191648 bytes for two SAP images/checkpoint.
index-sap-anchor-id-transaction-local-build.json retains exact local evidence.
No schema77 live qualification yet. The original runtime/schema75 input mismatch
retains its own identity and has not been relabeled a successful restoration.


## Non-broadphase shape ownership (schema78)

Schema77 replay-8dfa03ad7edc4a0d90e98fb98fb13cb2 rejected A170 retention with
sap_shape_id_coverage, before B capture or A publication. Retained
index-sap-non-broadphase-shape-failure.json links its actual raw log; cleanup
completed with zero games. This is not historical restoration proof.

Native115940 keeps a ShapeSim and both IDs alive when ShapeCore+40 flags&5 is
zero, while150050 clears its AABB volume. Native113F30/113F50 maintain the
ActorSim+38 -> ElementSim+8 owner list. Schema78 enumerates those lists from
already validated broadphase actors, requires unique element IDs and matching
ShapeSim/actor ownership, and records excluded shapes as immutable witnesses.
Exact active shape/rigid/element ID coverage still rejects any owner outside that
verified seed set. No ShapeSim, list, flag or filter history is written.

Invariant: allocator restoration may only alter free/next continuation when all
live IDs have the same owners. Reconstruction cannot substitute for identity of
an authoritative live ShapeSim. Focused native fixtures include a query-only
sibling, native C allocation/retirement and complete B undo with relocated free
backing, plus cyclic membership, missing owner and simulation-enabled-without-
volume rejection. This adds32768 bytes per scene image (257184 bytes for two
SAP images per checkpoint), no GPU work, no native allocation or new retirement
participant. Read cost is bounded by512 linked shapes. The first local build
passed both C++ suites and the shipped-native transaction; added negative cases
are being rebuilt. Live seed completeness and full-index restore remain unproven.


## Full native actor inventory replaces incomplete broadphase seeds (schema79)

Schema78 replay-b41edf548a8346ddad355b4f7e561fd1 still rejected A170 shape-ID
coverage before B capture/A publication. Cleanup completed; retained
index-sap-actor-seed-coverage-failure.json points to its raw log and sources.
The seed assumption is disproven. No repeated run used that same assumption.

Native41140/41290 consume NpScene+2590 pointer/+2598 count for static/dynamic
actors (concrete type7/6). Actual scene vtable is19D008. Static3ECE0->3FAF0
consumes NpRigidStatic+80 Core.sim and traverses ActorSim+38; dynamic39780 uses
the same core offset. Vtables19BE60/19B7C0, core.sim, ActorSim.core/scene and
rigid ID are checked before following lists. Schema79 uses this complete native
inventory, including actors with no broadphase shapes. It retains ID-indexed
binding witnesses only; no native actor array, core, membership or backing is
rewritten. All active-ID masks still require exact coverage and same owners.

Native-memory fixtures now include a separate query-only static actor, not a
sibling on a broadphase actor. Both C++ suites and shipped-native publication,
C execution, complete B recovery/commit and negative membership tests pass.
index-sap-scene-membership-local-build.json retains exact local sources/binaries.
Two scene images consume273584 bytes/checkpoint; no new GPU wait/readback,
native allocation or retirement owner. Source compile typo was fixed before
local testing; no live evidence exists on schema79 yet. Full endpoint restoration,
independent continuation and arbitrary seeking remain unproven.


## Full index admission passed; transform-current ownership (schema80)

Schema79 replay-8ad3ae4e6bba4ac792ecb1fb2bf12a07 retained A170 at234413564 owned
bytes, indexed11502 entries/11497 intervals/3 multi-tick intervals through native
finish, captured B and passed marker/SAP preparation. It then rejected physics
actor42 component_transform_flags_changed before A publication. Cleanup completed;
index-component-transform-flags-failure.json links retained raw log/exact sources.
This proves live ID-owner admission, not endpoint restoration or continuation.

Classification: component+240 bit0 is simulation-consumed cached-transform
validity (native141DB0600), not pixel history. Cached world/relative transforms
are already restored but their validity bit was erroneously immutable. Schema80
restores only bit0 with those values, checks write storage before publication,
keeps all other bits exact and validates the full installed flag word. Complete
B restoration uses the same existing transaction. Zero added checkpoint bytes,
allocations, GPU work or retirement owners; one masked CPU write per projected
component. A fixture reads native memory after A install and B undo, and rejects
visibility-bit changes. Both C++ suites/native marker tests pass; exact local
sources in index-component-transform-current-local-build.json.

The prior log does not include the actual differing component flag words, so
bit0 is a justified completeness correction, not yet a demonstrated cause of
this rejection. Bounded preflight logging now reports actual A/B words. The next
assembled attempt supports the hypothesis only if bit0 differs and preparation
passes; another bit remains rejected and its consumer must be audited. No
additional renderer participant or historical lighting capture is justified.


## Visibility difference classified; existing publication ordering (schema81)

Schema80 replay-02bb010d054b41198a22419175d1a4a3 again rejected pre-publication.
Exact A411/B401 flags on two components shared by actors42..47 disprove bit0 as
the cause: visibility bit10 differs. index-component-visibility-failure.json
retains raw log and sources; cleanup complete. The independent bit0 ownership
correction retains local proof only and is not called the fix for this failure.

Existing PreparedRestore::PublishCreationVisibility uses native1DAD440, retained
weak/component/owner membership, owned world end-frame sets, mesh recreation when
needed and ordered render/GPU completion. It runs before physics installation
and on B recovery. Initial physics preflight nevertheless rejected visibility
before that existing publication could run. Schema81 adds a preparation-only
admission for bit10 when both physics rows resolve to exactly the same retained
creation/weapon owner, with matching captured visibility and SameBinding. Every
other flag stays exact. InstallPhysicsProjection and ValidatePhysicsProjection
do not use planned admission: native visibility must already match before CPU
installation. No native visibility pointer/history write or new participant.

Invariant: admit an upcoming owned publication without pretending it already
completed. Existing reconstruction handles it; extra historical state is not
required. Tests cover owned planned admission, missing owner, changed mesh,
and rejected early Install with B untouched. Both C++/native suites pass;
index-planned-visibility-local-build.json retains exact local evidence. Zero
checkpoint growth, new GPU waits, allocations or retirement machinery; bounded
CPU owner lookup only. Live membership and assembled A publication remain
unproven. Next attempt must resolve through retained owners or reject unchanged.


## Visibility owner coverage passed; native-region publication and undo failed

Schema81 replay-35af19e67da8478db420be9a30a835c2 passes retained visibility-owner
preparation, proving both differing components were already covered. Complete
B11506 was retained (309732590 conservative prepared bytes). Gameplay publication
then fails code18/adapter operations2052 = native-region failure plus internal
undo failure. Enclosing B gameplay recovery also fails. The host correctly keeps
native visibility/end-frame reconstruction blocked while gameplay is mixed, so
subsequent B physics validation sees desired401/observed411. It did not commit or
resume; process cleanup complete is not in-session undo proof. Retained
index-gameplay-publication-undo-failure.json links raw log/exact source build.

No visibility guard was relaxed after that failure. Current source preserves
NativeCandidateRegions' first read/write diagnostic or semantic verification
field mask across internal undo, and CandidateGameStateAdapter preserves it
across its enclosing undo. The log reuses existing target/undo/verification
scratch; no new capture, resource owner, expected-state input or GPU work. Mask
bits0..16 map to frame, round sequence, input log, dispatch masks, VFX edges,
MoveVM shorts, pump, schedulers, sub-VMs, move commands, slot params, pending hit,
RNG, freeze record, wind emitters, camera components, camera distance history.
A local test proves camera mismatch bit15 survives successful internal undo.
Both C++ suites/native fixture pass; index-native-first-failure-local-build.json
retains exact sources. This is diagnostic implementation only, not a runtime fix.
Next bounded attempt identifies the native failure before any recovery changes.


## Local camera codec omission (2026-09-11, schema 82)

Schema81 candidate replay-e2073692c7024fb08b3a1701b87e67e3 rejected A and B native verification with mask32768: camera_components only. The retained index-native-camera-verification-failure.json points to its retained raw log and index-native-camera-verification-build.json preserves the executed source. Cleanup completed; in-session B recovery did not.

The first failure is causal: NativeCandidateRegions::CanonicalBytes intentionally excludes component images, but CandidateCheckpointCodec used that stream as the complete local image. Decode therefore dropped already captured cameras. CandidateGameStateAdapter additionally erased cameras from the enclosing undo and masked absent expected slots in final verification. Strict replay publication verification exposed this omission.

Schema82/codec20 retains those existing typed fields in a separate checksummed local payload. It does not add native captures, write ownership pointers or alter the existing peer fingerprint. The adapter preserves complete cameras for undo and verifies them without the old omission mask. HgCpu still reconstructs native director publication before typed field installation; immutable backing and publication checks remain. This is a local transport fix, not a finding that cameras cannot affect gameplay (shared RNG/orientation consumers still matter).

Both C++ tests and native physics fixture pass; focused codec coverage preserves every populated field and absent slots and rejects corruption. 86 runner tests pass. Live A publication, B recovery and independent continuation on schema82 remain unproven pending the assembled index-to-seek attempt. Fixed fields use the existing reserved checkpoint envelope and add no GPU readbacks; actual aggregate ownership and timings must come from the live report.


## Camera publication passes; monitored release dead end (2026-09-11)

Schema82 candidate replay-09be2afafbac4ca7b53810166e0f6309 completed indexing, captured B11506, published A170 and entered ExecutionActive with B retained. No subsequent simulation observation arrived; timeout cleanup completed with zero games. index-camera-installed-stall.json links its retained raw log; index-camera-installed-stall-build.json retains exact source/binaries. A small index-camera-installed-stall.dmp was captured before timeout. This proves camera publication, not target208, B recovery or continuation.

The active application thread was in logging/held application work, not a consumed gameplay loop. Static control flow identifies a monitor dead end: AdvanceInteriorHold returned whenever a pause monitor was installed and interior_phase was not Holding. After QueueSurface(ReleaseForAdvance) changed it to Releasing, that unconditional monitor guard prevented the existing Releasing+surface_complete branch forever. The index-seek observer keeps its monitor installed, unlike earlier bounded observers that unsubscribed before release.

The replacement predicate allows unchanged Holding/Releasing only, returns on monitor phase mutation or stepping/settlement, and retains all native release/admission checks. Focused production-predicate coverage includes unchanged Releasing, changed phases, failed state, step and settlement. Live dispatch/continuation proof remains pending. No capture, render ownership, memory-budget or B retirement behavior is changed.


## First committed index seek: source publication diverges (2026-09-11)

Candidate replay-11e655a8fa5b4b14b004687f77a57118 on runtime f02dae45 / observer e94274a4 completes B11506 -> A170 -> exact208, holds512568us/30 application updates with pending task and unchanged observations, completes target tails, commits/retires and observes209..328. Monitor release correction therefore has live progress proof. This is NOT a correct seeker: independently compared suffix fails.

The parser initially rejected ticks329/330 recorded after the explicit120-tick completion marker. Offline revalidation now retains all46668 callbacks for physical accounting and bounds the requested comparison at actor_tail328; negative coverage rejects late ticks before that marker. The candidate was not rerun for this parser fix. index-monitored-release-candidate.json links retained raw log, original failed parser report, original build sources and actual parser overlay contents. Native control is index-monitored-release-native.json; index-monitored-release-comparison.json contains prefix pass/suffix fail. Both cleanup complete/zero games. Capture101084us, retained checkpoint234437652bytes. Approximately357ms request-to-held target is not practical rollback performance. No pixel/image coherence proof in this capture.

Same-process first restored publication at tick170: expected source cursor127 and authored words00000402/00000800; actual cursor126 and zero words. Tick171 gameplay immediately differs. Native input game-time still advances; tracker active flag is restored. This localizes the defect to replay-source dispatch, not physics/rendering and not independent startup variation.

Native1403E1FC0 calls current-input dispatch before cache publication/GameTime increment. 1403F6070 dispatches InputLog+43D0 through1403D73F0, which gates entry+30, wrapper+38 and target+20, then calls target+18. Wrapper143298810 has always-true admission1402D72F0 and resolver14041D7F0 returning owner+390. Tracker admission140427930 is MOVZX [RCX+8]/RET; reader140428D70 increments cursor once. Native140428040 appends43D0 entry using1403E1E30, constructs wrapper140401CC0 and stores its sequence handle at player+3C8. Removal140427DA0 calls1403F71A0 and clears+3C8; removal compacts/destroys through142DCC200. Restoring tracker scalars alone does not recreate this subscription.

Current read-only source-route witness inspects existing producer registry at checkpoint capture; preflight rejects target-active restoration without a valid unique current subscription, before A publication. No storage participant/native mutation/reconstruction was added. The next assembled bounded attempt distinguishes absent entry/handle from changed admission. Unknowns: actual A/B registry topology; whether other subscribers exist/order matters; native wrapper destruction/copy semantics and global sequence-token consumers required for complete B undo. These must close before reconstruction is admitted.


### Bounded source-registration lifecycle design, pending B topology witness

Correctness invariant: native current-input dispatch must reach the retained replay tracker exactly once per source sample, and source+3C8 must name the corresponding wrapper token. The first differing consumed input proves this is authoritative replay-source continuation. Scalar tracker restoration cannot reconstruct removed array membership. B's native array/backing/handle must remain intact until seek-wide irreversible commit; no source publication may precede preparation of that undo.

Native140418DA0 (wrapper+50) calls140399420 to construct a32-byte inline instance and copies only class143298810, owner+8 and sequence handle+18. No new sequence is allocated. Native1404F81F0 (wrapper+48 with flag0) destroys the wrapper without destroying its owner. Array removal142DCC200 destroys elements, frees external wrapper storage if present, compacts64-byte rows and may shrink outer backing. Global sequence helper140D24030 only allocates nonzero identity cookies; its counter must never be rewound.

A170 witness in current candidate: one inline wrapper, count1/capacity4, unique owner match, source handle equals wrapper token. B is pending. If B is empty, the smallest supported transaction can retain B's original header/backing and source handle, create a private A array through verified native allocation/wrapper copy, publish membership with tracker scalars under the existing host owner, transfer the private A array to native execution, adopt/validate C at settlement, restore B header/handle before retiring C on undo, and retire detached B only at irreversible commit. Reject unknown/external/additional recipients or aliases until their ownership is separately justified; do not generalize one-owner evidence into arbitrary registry reconstruction.

Focused proof required: local actual-memory publication, interrupted publication, complete B undo, native copy/destructor, C array removal/reallocation, and commit cleanup; then assembled endpoint->170->208 with first source publication and120 independent gameplay/pose ticks. Test cancellation before publication and after execution using the same seek-wide owner. Additional storage should be bounded to the admitted one-entry outer arrays plus metadata (no new GPU resources or waits); report actual memory/latency and cleanup rather than an estimated performance pass. Source-routing probe is read-only and adds no checkpoint payload.


### Source registration topology and integrated transaction (2026-09-11)

Candidate replay-fe2af6158d4449bfb9503401025d53d7 confirms A170 has one inline receiver (capacity4, handle3a5a), while B11506 has no array/backing and handle0. The same InputLog owner survives. The prepublication guard rejected safely with B retained; cleanup completed. index-source-registration-absent.json points to retained raw log and reconstructible build sources. This is topology proof, not restoration.

Schema83 integrates ReplaySourceRegistration into the existing seek ownership chain as participant11: prepare private native A wrapper/array with retained owner leases, publish header/handle with source state, hand execution ownership to native, adopt current C at settlement, restore complete original B before retiring C on undo, and retire detached B only after target tails and irreversible commit. The known inline wrapper is reconstructed by verified native copy, without allocating or rewinding the global sequence counter. Unknown/additional/external receivers reject. Native function signatures are checked before capture. Historical array backing is never revived. Conservative transaction storage reservation is12416 bytes plus metadata; no GPU resources or readbacks are introduced.

Initial production build and both C++ tests passed. Local coverage includes prepublication cancellation, partial root publication, invalid binding retention, settled undo, reopen, and idempotent commit. Additional native-removal/no-double-free and registered-B identity recovery fixtures are pending the current build. Native copy execution, final independent continuation and live cancellation remain unproven. Next attempt is the assembled full-index endpoint->170->208 plus120 continuation with a specific first-publication cursor/input expectation; expected observations remain comparison-only.


### First independently matching full-index historical seek (2026-09-11)

Schema83 runtime4fb9a99b036cc31bd16dff09be62325c89785ffb14a8df596a30da33ea3e7186, observer7908452de2b10ddc3d774427f1bcc56ccc6c86ff738729cb6024e1893ca08a52: candidate16c1f54d4cc7442abf6f1ebe9a004c39 passes full11502-entry index, B11506->A170->exact208,504001us validation hold across30application updates with pending task and unchanged state, commit/retirement and120 independent ticks/480callbacks/Lux pose outputs. All632 callback records from restored170..328 also match same-process original playback. The first restored source publication now advances cursor127 and consumes authored inputs. Capture97752us; retained ownership234437892bytes; no diagnostic GPU readbacks. Both processes cleaned up. No new held-image or practical rollback-performance proof.

index-source-registration-audit.json links retained candidate/control reports and raw logs, exact reconstructible source archive, and actual parser overlay. Initial accounting-stage failure is retained separately. Two native ticks329/330 occur after the compared suffix completes while the observer detaches; all eight callbacks must remain in physical execution accounting. Executor logical counters rewind by11336ticks/intervals at A, while physical observations retain that execution. Revalidated totals:11666ticks,11662intervals,11664input publications. The parser now distinguishes logical/physical counters and tests rejection when a physical tick, interval or publication is omitted. No live rerun was used to repair parser accounting.

Next ownership proof: updated observer injects after all11 settled CPU participants, including source registration, instead of the former10. Require complete registered-B recovery and independent120-tick continuation. Existing source undo tests alone do not supply native execution proof. Full checkpoint coverage, arbitrary targets, UI/lifecycle, strict sentinel/corpus and rollback cost remain outstanding.


### Late recovery first failure: weapon membership in render undo

Candidate52130203ed5a4fea8acc99ea7d6bbf50 deliberately fails at C214 after11CPU owners settle, reopens settlement, retires8C-only particles and publishes B source revision. It then rejects physics undo with GenerationMismatch; no native control launched, cleanup complete. source-registration-late-recovery-failure.json retains exact raw log/build. This is not complete B recovery. The observer's final B_retained=false field is not evidence of a successful commit; native ownership remains Recovering and no commit was issued.

Static first-failure investigation finds a concrete mismatch in UndoRenderWork: ValidateRenderWork already supports inline weapon membership via owner+390, but UndoRenderWork hardcodes owner+3B0 creation-array backing/count/capacity for every owner. A weapon admitted and reconstructed during publication therefore rejects on undo. The fix shares a live membership reader between validation and undo, preserving exact membership kind/backing/slot/component and all surrounding weak-generation/parent/owner checks. It rejects changed backing before dereferencing it. Local fixture requires that a weapon read only its inline390slot and cannot borrow creation-array membership; unreadable storage rejects. No historical capture, GPU readback, new participant, allocation or retirement path is added. Build/live retry pending.


## Midpoint mesh-emitter settlement blocker (2026-09-11)

Candidate replay-57d19edd945c40c2a0d86675bbf38179 completed indexing and held exact5750 for30 application updates after B11506->A170 resimulation. It rejected C settlement before commit at STG009 LuxParticleSystemComponent_2420, emitter vtable143949D88. B remained retained; cleanup complete. Request-to-ready15089061us, owned362345306 bytes, no diagnostic GPU readbacks: this FAILS the0.5s ceiling, independently of settlement failure. No native control was launched. Retained index-midpoint-settlement-capture-failure.json/log/build.json identify exact binaries and reconstructible sources. This is neither a committed seek nor continuation proof.

Native allocation/consumer audit: factory141F942F0 allocates0x200, base ctor141F8DC30, vtable143949D88. Initializer141F9C890 takes template LOD0 (141F77990: template+38/count40) TypeData+48 into emitter+1D0. TypeData+30 is mesh asset. Payload-size141FA13E0 sets rotation offset+1DC and optional previous-particle offset+1E0. Tick141FAA9B0 and grow141FA18D0 read/write those payloads inside the existing particle buffer: preserve them, not presentation-only. Destructor141F90870 frees+1E8 then base destructor141F8F9E0. Virtual+218141FAB350 passes address+1E8 to1419F2610 (TArray resize,8-byte elements), fills material overrides from component maps. Render material selection141F9A0F0 reads count+1F0/data+1E8. This closes the earlier unknown allocation as a material-pointer TArray; referenced material lifetime is still unqualified for nonempty arrays.

Classification/design: particle payload and emitter continuation remain in the existing allocation graph; TypeData/mesh are validated process-local UObject bindings. The first supported domain requires empty override storage (+1E8/+1F0/+1F4 allzero); it does not omit allocated materials or clear them. Dynamic mesh render data is reconstructed by native141F99A20->virtual220141F96F20->141FD05B0 from retained particle and authored mesh inputs. No historical dynamic-render object, lighting participant, GPU image, or new subsystem is added. Existing scheduler/component/render membership, C-only lifecycle and complete B undo remain mandatory.

Implementation candidate schema84 uses an explicit Sprite/Mesh/Gpu kind, exact0x200 mesh root extent for allocation/alias checks, payload bounds, LOD0 TypeData/mesh binding checks and the existing CPU replacement/settlement transaction. Local negative tests cover overflow, nonempty or malformed override backing, wrong mesh/LOD, root-tail alias and CPU/GPU separation. Build/live results pending; native construction/destruction does not itself prove historical mesh playback. Focused next attempt is the same5750+600 transaction, with no expanded campaign. It must settle/commit and independently reproduce gameplay/poses; any first rejection is investigated before retry. New fixed binding metadata adds32 bytes per emitter image/prepared object, and mesh native root adds48 bytes versus sprite; particle buffers remain charged by capacity. No new GPU waits/readbacks.


### Mesh integration result and bounded render-ownership follow-up

Runtimea256f5e4d35a99746dd43de5ea88be6c2414a0409b82d65b685c8273ca7ceb34 / observer303407779af82cc1237f280b578d10e233f51e333ce473d983b9293ebcff0cad, candidate8b0186e3cd414a16a75f8edc5422ee0e: mesh C capture now passes. VFX22883us, traces35563us, HUD18970us, scheduler299us, physics268us. Before CPU settlement, render completion rejects RebindLightingImage(B,C) because B static fence BaseMesh(id341) has no C proxy.17 absent B primitives:7 supported creation variants,9 static stage meshes (fences,icicles,wallhide,fallen tree),1 stage Dropwater particle. Owners remain alive; C includes native alternate/translucent meshes and stage particle births. All B allocations/references remain retained, commit_decided=false. Cleanupcomplete0games; no native control. Ready15040494us,362346762 conservative bytes, no diagnostic GPU readbacks. Retained index-midpoint-mesh-render-settlement-failure reports/log/build are directly linked; log is complete under the new128MiB indexed-seek bound. Prior64MiB failure trace was truncated and cannot establish full comparison.

Diagnostic-only same-process original/restored stream has22324 matching callbacks170..5750 (index-midpoint-prefix-diagnostic.json). The failed run lacks successful observer completion; this diagnostic cannot be promoted to an independently completed gate. Mesh historical A/B replacement and cancellation remain unproven separately from successful current-C capture.

Ownership invariant: complete private B maps, point allocations and uniform references must survive C without requiring B publication proxies to remain visible in C. Current settlement unnecessarily resolves all B destinations before commit; simply removing that check is unsafe because cancellation still needs valid B destinations and native reconstruction. No runtime bypass is implemented. Reconstruction/deferral must validate the actual retained components and their native consumers; it must not resurrect old proxies or invoke stage lifecycle initialization.

Native140535350 constructs LuxStageMeshActor, vtable1432D2368, BaseMesh+3A8,SoftOpacityMesh+3B0,TranslucentMesh+3B8.14054F320 changes state+389 and switches visibility or initiates fade using+3C8/+3CC;14054DC80 advances fade and publishes native visibility. Thus replaying a full tick merely to rebuild a proxy changes continuation; the constructor/BeginPlay/refresh are also not pure reconstruction. Stage actor fade/visibility inputs and the stage particle render owner require consumer/lifetime classification before changing settlement. Generic barrier initializer14054A960 is destructive and writes authoritative hit count+468: do not reuse it as a visual reset. Existing stale comments in14053CA70/140BDAD60 generic typing are not proof of actual stage-mesh identity.

Smallest next design to verify: retain B private resources without live publication-slot assumptions while C is committed; for recovery, reconstruct missing destinations through native render-state lifecycle at an owned game/render boundary before B installation. Required proof is complete B recovery after changed native proxy membership, then committed5750 plus600 independent callbacks/poses and coherent visuals. Unknowns: exact actual stage component/actor immutable memberships and consumed fade inputs; safe recreation ordering relative to B CPU restoration and C cache retirement; Dropwater renderer recreation without particle simulation advancement. Resolve via native callers/consumers and a bounded ownership transaction, not additional historical lighting capture. Expected checkpoint-memory addition is only bounded owner/reconstruction metadata if needed; no GPU-history images or readbacks. Measure actual native reconstruction wait/cleanup and all seek costs after implementation.


## Midpoint stage reconstruction dependency map (2026-09-11)

Retained `index-midpoint-proxy-owner-inventory.json` and its raw log/build archive identify actual absent B owners at C5750: LuxStageBreakableWallActor vtable1432CFE40, LuxStageMeshActor1432D2368, and LuxEmitter14335D518, plus existing retained fighter variants. B remains retained; no commit or independent control. This is an ownership inventory, not restoration proof.

Native factory140BDB690/constructor140535350 establishes StageMesh actor size3D0 and members3A8/3B0/3B8. Requested visibility389 and fade3C8/3CC are consumed by14054DC80; invoking that callback advances the fade and is not reconstruction.14054F320 admits requested transitions and changes fade rate;14054AD90 resets, and BeginPlay14053CE30 can destroy/filter actors. None is a historical restoration shortcut.14055CF00 publishes Alpha to existing MIDList9A8/9B0; it does not create materials.

Wall native140564260 selects among six mesh slots420..448 from break state468 and fade46C/470.140562090 also runs the base actor tick and consumes breaking skeletal animation141DA31E0 before transitioning state1 to2. Its old unrelated callback name/comment is misleading. Active breaking animation remains an ownership requirement. Wall particle460 and event emitter3B0 cannot be silently omitted.

Emitter visibility thunk1408D3D10 jumps14054F4A0: updates389 and calls141DAD440 on root168 with propagation derived from388. Native particle Create141F711A0 and Destroy141F71FD0 can complete pending async work; Destroy finalizes emitter graphs when830 bit80 is set. Create can change template/tick state. Therefore generic destroy/create is not assumed presentation-only.

141DAD440 mode0 changes self only; mode1 marks descendants dirty without changing their visibility; mode2 propagates visibility.141D4E910 queues owned end-frame weak sets.141EFEBA0 drains and joins these jobs; host RT/GPU completion must precede rebinding B publication destinations. No old proxy address is a retained allocation after execution.

Implementation invariant: retain private B allocation bytes and RHI references; retain/validate native actors, member components and assets; restore authoritative stage fade/request state; reconstruct outputs through native setters and owned queues; only then rebind B destinations. Active break animation or unsafe particle destruction stays rejected. Focused test must exercise the actual midpoint missing-owner set and cancellation/complete B recovery, then independent continuation. New CPU source records are budgeted; no new GPU images or historical lighting capture is justified. Unknowns: actual material membership and particle recreation admission in the replay; stage-source changes across A/B; safe B destination reconstruction under cancellation. Existing callback matching alone does not close these.


### Stage reconstruction first assembled recovery failure

Candidate replay-f6b588c62c0e46b7a57ba0055fd6d46c, retained stage-reconstruction-created-bit-rejection.json and its directly linked raw log/source archive: C5750 settles, injected failure retains B2504, CPU B installation reaches stage visibility. First guard is stage_particle_finalize_or_create, observed40c versus required200; cleanup complete/zero games. Earlier uninstrumented c46a12a6 failure is separately retained. Neither proves B continuation.

Native141F711A0 sets component830 bit200 after PrimitiveCreate. Actual PE scan of all direct830/831 accesses locates mesh consumer141FAAFB2 (function141FAAF90): when200 is set, it selects current component translation into emitter134/13C rather than emitter50/58 previous position.141F73990 clears200 after results. Therefore this is authoritative simulation admission, not optional shading history.141F71FD0 finalizes allocations only with80; that guard remains. Template clearing/tick disabling and unfinished async completion remain rejected.

Bounded fix: retain actual pre-native-call flags in the existing prepared world transaction, call verified native visibility/dirty/end-frame paths, join end-frame jobs, reject changes other than setting200, then restore the retained word before further simulation. Retry never adopts the mutated word as B. Fixed witness array costs4096 bytes in sizeof(HistoricalRestore), already charged by host admission; checkpoint images unchanged, no additional allocation/readback/GPU wait. Existing ordered render publication and GPU completion still retain B references. Native finalization remains unsupported; no hidden recreation to another interval. Focused local tests check40c->60c, unchanged, retry identity,80 and unrelated changes. Live proof remains same deliberate late cancellation and120 independent B ticks; matrix waits for that proof.


### Assembled stage recovery succeeds; omitted HUD pool blocks qualification

Runtime1788b954/observer3c9cd489 candidate5b5df13d: C5750 completes first settlement; cancellation retires13 C-only particles,9 static+1 particle deferred B destinations become0, GPU publication completes, and B2504 resumes. Owned350914978 bytes/no diagnostic maps; ready15221198us includes engine9285414us and application tails5692809us; cancellation-to-Recovered159992us. No completed-seek latency claim. Parser originally rejected the second backward traversal5750->2504. Explicit caller recovery plan plus Recovered ownership/observer markers now validates33309 boundaries/8327 physical ticks; negative tests reject missing markers, wrong B, early/duplicate receipts and commit. No candidate rerun. Independent native control reveals a C-only DmgTypeEff active player surviving into B2505/2506; terminal time0.35. Existing bar pool remains empty in both. Retained stage-reconstruction-B-HUD-failure-audit.json links all source/raw evidence; not complete B qualification.

Correctness invariant: restoring B must remove discarded C animation admission and its visible properties without late evaluation altering B. This is a known omitted pool from the static fork, now causally observed. Cooked DmgTypeEff DamgeEffect has six color/six transform/one MAIN visibility tracks; no event tracks or authored Construct actions. CreateDamageTypeEffect reparents to the selected vitality canvas and overwrites all root transform fields before Play. Thus inactive historical canvas slots may safely be deferred (validate current p1/p2 canvas membership); unused roots have zero area until that native producer, as in existing damage-bar reconstruction. Slot identity/layout history is not gameplay state.

Native1418010D0 was rechecked: with evaluation761 clear it sets status6D8 stopped, calls evaluation-root Finish141674020, then drains pending actions1417D60C0. Unlike14181F0B0 it does not evaluate time0 or broadcast player6E0/animation340 finished delegates. Pending/evaluating players reject. Finish is restricted to discarded C, before B visual publication; C owners remain leased through cleanup, retry journals retain incomplete retirement. Original B/A type active arrays must be empty; active targets/B are explicitly unsupported until an evaluation-state design exists. No raw owning player/header image is restored. Current code captures bounded pool membership, six image color/transforms, MAIN visibility and root transform; native setters1418180B0/14181C380/14181E1B0 reconstruct current Slate output. All captured objects/players are GC leased. New Finish and visibility signatures verified against the actual PE. No GPU readback/wait or archive participant added; fixed HUD source storage and leases remain in existing memory admission. Focused live test remains5750 late cancellation+B120, with independent type-pool active counts; no matrix expansion before it passes.


### Complete B and bounded target matrix

Schema86 stage-particle-complete-B-audit.json proves late C5750 cancellation, native stage/particle reconstruction, GPU publication and B2504+120 independent gameplay/callback/Lux-pose/HUD continuation.

seek-target-matrix-2026-09-11.json retains early208, boundary2384, midpoint5750, late11000 and repeated208/214 results. Early/boundary/midpoint continuation passes; completed work excluding deliberate holds is0.513468s/5.511941s/16.126897s, all above0.5s. Midpoint includes600 ticks and a round transition. Late11000 holds exactly then rejects module payload SpawnPerUnit during C settlement; B retained, no commit or continuation claim. Observer30s native-end watchdog previously remained active during seek; phase routing corrected and original failure retained. Repeated holds and resumed/native230/300 images inspected coherent, exact particles/shading optional. Every process cleaned up. No extra renderer history capture. Next: bounded native payload consumer audit, checkpoint coverage/reserved B memory and fixed/per-application latency costs.


### SpawnPerUnit bounded admission after matrix

Invariant: C settlement must preserve the exact allocation graph and all emitter-local state until commit or complete B undo. The late matrix failure identifies sole payload module ParticleModuleSpawnPerUnit, four bytes, ME_Pt_01 asset; it is emission history, not optional render history. Native class registration1426ABAE0 records size88 and constructor trampoline1426A7DD0 to141FB53D0, whose vtable is14395ED20. Vslot268 is140301490 (returns4),270 is141E030C0 (returns-1 without touching memory). Native141F9BFE0 allocates emitter+100 with extent+108 and zeroes that complete range before initializer dispatch. Read-only141F9A380 resolves module identity through authored+C0 offset map. Sole module+offsetzero and exact4-byte allocation remain required.

Consumer141FCEE40 reads float payload[0], computes movement from emitter+50/+54/+58 minus+134/+138/+13C, applies authored axis masks/maxdistance, computes births/rate and writes carried distance back. Actual movss/subss assembly confirms inline emitter vectors; the old partial Ghidra type renders them misleadingly as pointers. The helper calls native distribution141E0DA20; this admission never invokes it or reseeds RNG. Existing RNG/transform/emitter owners remain required. There are no payload pointers or output callbacks in this consumer; count/rate outputs are consumed by native spawning. Native reconstruction from zero would lose this history and change births, so preserve it rather than invalidate it.

Implementation schema87: existing module buffer copy, lease, mapping validation, private allocation, C settlement and B retirement are unchanged. Added exact class/size/initializer/consumer predicate and local negative tests. Incremental checkpoint cost is4 payload bytes plus an existing Buffer record/lease when present; prepared allocations are charged by existing accounting. No new GPU resource, wait, diagnostic readback or native lifecycle call. Capture/restore latency is unmeasured until the bounded retry. The focused live check is the same full-index->11000 commit and independent120 ticks; future historical snapshots containing this module also require valid whole-checkpoint shape. No active-child or active-HUD guards are relaxed. Read-only Ghidra investigation; no database types changed.


## Retained source-stop pair lifetime (2026-09-13)

Candidate a463980ee1b0455b90a641879ed0c1d9, runtime dff6f6b2 / observer
0c65bff4 / framework 4e41f527, holds B11148 before native finish. Both A170
and B11148 decode as complete one-marker graphs; rejection is specifically
`marker_target_inputs_retained`, A row0. No A publication, complete B retained,
cleanup complete/zero games. `retained-source-stop-target-pair-failure-stage.json`
links exact sources, retained report and raw log. This closes the ambiguous
captured-graph hypothesis; it is not a malformed B graph.

The invariant is live shape/core/actor/filter ownership plus coherent SAP and
registration publication, not preservation of the old overlap pair. Shipped
PhysX 180120030 constructs a new marker from each shape+10 actor and registers
it in both actor lists and scene; 18011C410 inserts its sorted shape pair into
the NPhase map. These functions were re-read in Ghidra for this change. Neither
requires the historical marker allocation to survive. The transaction already
allocates a fresh A slot and retains detached B slots until commit/recovery.

The bounded replacement validates every A endpoint against the existing,
live-validated SAP volume inventory: exactly one shape, identical core, actor,
attributes/filter words and native actor scene; native getters independently
recheck consumed inputs. The complete A/B SAP owner/ID and graph guards remain.
Publication repeats the check before any pool or registration write. The
non-SAP fixture retains its old pair-presence guard. No new image, participant,
checkpoint bytes, GPU readback or GPU wait is added. Reconstruction alone cannot
replace this simulation state: SAP caches interaction pointers and incremental
overlap history. Native marker reconstruction stays coupled to SAP publication.

Focused proof adds a native fixture topology that really destroys A's overlap
and removes its SAP pair before B capture, preserving both shape owners. It
requires A publication into a new slot, native C removal/recreation, interrupted
publication including the SAP tail, complete B graph/SAP undo, commit and a
changed-core rejection. Build/local results and assembled live proof remain
pending at this source checkpoint. No assumption that this is the last
retained-endpoint dependency; coherent target visuals and independent suffix
remain required.


The absent-pair fixture passed on runtime eace859f / observer b2ec2ed7.
A second topology leaves one A fighter with an empty B interaction list.
Before the caller fix, all seven native lifecycle variants failed only the
actual `NormalizePreflight` contract, while publication/C execution/undo
checks passed. Retained `absent-marker-actor-preflight-local-failure.json`
links the exact local failure log and reconstructible sources. No live retry
was used to discover it. Normalization now recognizes the prepared A/B actor
union for registration slots/backing/count only; bytes from shape+38 onward
remain independently compared. Rebuild and live proof are pending.


Candidate 2d9756011a784768861ba45f884ceb3c on c4ee091b / 2d441591 passes
marker/SAP preparation, then projection rejects actor47 with
`component_or_interaction_binding`, before A publication. B11148 remains
retained; cleanup complete/zero games. Retained
`retained-source-physics-projection-failure-stage.json` links its sources/log.
The captured actor has A_interactions=1/B_interactions=0 and A_isolated=false /
B_isolated=true; native core, node and lifetime admission remain valid.

`isolated_kinematic` is a C++ derived admission witness, not native memory.
Capture only evaluates its no-SQ-bounds shape walk when IsolatedPhysicsCore
passes, which requires zero interactions. All consumers were checked in
PhysicsNodes, PhysicsOrder and projection/installation: the isolated-state
write permissions require BOTH original A/B witnesses. The narrow projection
comparison now recognizes only a count-explained derived-flag transition after
successful marker registration normalization, and checks the empty-side core.
It leaves original A/B witnesses and the earlier `isolated` write decision
unchanged. Installed A and recovered B must still reproduce their own flags.
No new capture/native write/GPU work or additional ownership participant.

The native marker fixture now also calls the actual enclosing
PreparePhysicsProjection using controlled projection prerequisites around
shipped native registration observations. It requires the empty-list witness
case to pass and an impossible true isolated witness on a registered list to
reject. Build and assembled live proof are pending for this correction.
