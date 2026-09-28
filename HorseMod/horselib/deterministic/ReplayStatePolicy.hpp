#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace Horse::Deterministic::ReplayStatePolicy {
// Executable local state boundary. Unresolved entries retain existing exact
// captures/comparisons and admission; this manifest grants no new exclusion.
// Any semantic change must increment version. The content digest also changes
// automatically, so forgetting a version bump cannot admit an old checkpoint.
inline constexpr std::uint32_t version = 5;
enum class Class : std::uint8_t {Authoritative,Shared,Presentation,Ownership,Unresolved};
struct Entry {
    std::string_view domain,fields,producers,readers,side_effects,owner,representation,comparison,invalidation;
    Class classification;
};
inline constexpr std::array entries{
    Entry{"combat","inputs;MoveVM;fighters;hit/hurt;stage/ring-out;timers;combat animation",
        "recorded/revised input;native combat update","native fighting scripts;hit resolution;next traversal",
        "damage;actions;movement;round state","session/epoch and fighter logical identity",
        "existing gameplay snapshot and recorded input history","exact semantic state; canonical observer fields",
        "epoch/revision/binary or consumed field change",Class::Authoritative},
    Entry{"combat_crt","private MoveVM CRT cursor and draw count;seed/warmup epoch",
        "clone native PTD after 34F634/34F658 initialization at existing 34F915 hook;private LCG at 366FF4",
        "MoveVM opcode50006 only","combat random branch;does not advance native PTD","native owner thread and seed epoch",
        "version3 UcrtRandBrokerImage;both cursors retained in complete B",
        "exact state/count;independent modified control required;stock outcomes incompatible",
        "premature draw;thread/callsite/epoch/reseed/algorithm drift",Class::Authoritative},
    Entry{"native_crt","native CRT PTD+28;observed native and unresolved draw counts",
        "ground895D6E/896105;tile-pool1F9BD5C;audio54F91E;other original rand callers",
        "all native callers except owned MoveVM;unresolved consumers remain authoritative",
        "advances native stream;no permission to omit lifecycle or shared consumers","native owner thread and seed epoch",
        "actual PTD snapshot including bypass callers;IAT counts are not total native draws",
        "exact native state and observed counts retained;no native-lane hash exclusion",
        "thread/algorithm/callsite/order drift;unobserved reseed",Class::Shared},
    Entry{"shared_rng","LFSR;xorshift96;stage wind state",
        "native match/new-round initialization;all original native draws",
        "stage wind scheduling;hit camera;combat and unresolved consumers",
        "feeds active state;no split or draw suppression","native session/epoch",
        "existing exact native snapshots","exact state and actual corrected-history ordering",
        "consumer/algorithm/order/epoch drift",Class::Shared},
    Entry{"debris_lifecycle","recipe;birth/death;controller;fade clocks;manager slots/groups;callbacks;providers",
        "896410;8A4860;89F5F0;3BA410","manager+388 listeners;trace8D3F20;native/reflected GetGroundDebris",
        "shared IDs;group pruning;trace slot and tick/active/delegate changes","manager plus logical lifetime; native bindings separate",
        "existing ground/VFX/trace owned captures; stable logical event adapter pending","exact required lifecycle and ordering",
        "changed listener/provider/reader or motion-dependent lifetime",Class::Shared},
    Entry{"debris_motion","chunk pose;velocity;solver and query state",
        "ground placement/impulse;PhysX solver","physics pairs;transform publication;reflected/direct readers not fully closed",
        "possible feedback unresolved; placement RNG handled as shared","live ground child/native body generation",
        "existing exact physics/pose packets retained","existing exact comparisons; exclusion forbidden pending consumer proof",
        "free body;query/notification/constraint;reader/listener/filter change",Class::Unresolved},
    Entry{"particles","emitter state;event queues918/928/938/948/958;receiver payload;birth/death",
        "emitter tick and event generators","completion141F73990;receivers141FA0B80;world event manager;external callbacks",
        "receiver spawns;RNG;shared pools;callback writes","component/emitter logical lifetime plus native generation",
        "CPU/GPU owned snapshots; external-route admission incomplete","exact simulation/lifecycle; pointer array is not callback state",
        "unowned event route;owner/LOD/receiver/manager mutation",Class::Shared},
    Entry{"animation_hud_camera","combat pose/timing;IK;trails;HUD clocks/health;camera state",
        "native animation and presentation updates","combat consumers;HUD/pose/coherence observers;other readers unresolved",
        "simulation feedback retained until independently excluded","existing animation/trace/HUD participants",
        "existing owned participant snapshots","existing comparisons plus coherent normal rendering",
        "new reader/callback;invalid binding;epoch change",Class::Unresolved},
    Entry{"render_history","historical pixels;shading/noise history;held display image",
        "renderer/application tail","display; no permission to omit simulation-fed effect state",
        "display only within previously supported surface contract","render/application completion owner",
        "existing surface/presentation ownership","coherent actors/effects/poses/HUD; exact pixels optional",
        "simulation reader;incomplete render tail;stale epoch",Class::Presentation},
    Entry{"runtime_bindings","UObject generations;scene membership;queues;native/GPU allocations;retirement",
        "native creation/publication/destruction","all participants;native tasks;GPU work",
        "allocation lifetime and safe publication","checkpoint/B/operation/in-flight/deferred owners",
        "typed live binding and completion witnesses; linked GT prerequisite enforcement with current-application drain; PhysX start and known substep delegate callable admission before native wrapper; never canonical raw addresses","identity/membership/completion valid; peak simultaneous <=1GiB",
        "generation/scene/owner change;timeout not completion;unknown ownership",Class::Ownership}
};
constexpr std::uint64_t Digest() noexcept {
    std::uint64_t h=14695981039346656037ull;
    const auto add=[&](std::string_view s){for(const auto c:s)h=(h^static_cast<unsigned char>(c))*1099511628211ull;h=(h^0xffu)*1099511628211ull;};
    for(const auto& e:entries) {
        for(const auto s:{e.domain,e.fields,e.producers,e.readers,e.side_effects,e.owner,e.representation,e.comparison,e.invalidation})add(s);
        h=(h^static_cast<std::uint8_t>(e.classification))*1099511628211ull;
    }
    return h;
}
struct Stamp {std::uint32_t version{};std::uint64_t digest{};friend bool operator==(const Stamp&,const Stamp&)=default;};
inline constexpr Stamp current{version,Digest()};
constexpr bool Accepts(Stamp captured) noexcept {return captured==current;}
}
