#pragma once

#include "Types.hpp"
#include "Sc6ReplayCpuEmitterState.hpp"
#include "Sc6ReplayObjectLease.hpp"
#include "ReplayVfxFinishBinding.hpp"
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
// Owns the two native VFX slot tables' request/provider copies. Component,
// emitter and callback-target state remain separate lifecycle dependencies;
// capturing these tables alone never admits historical restoration.
class Sc6ReplayVfxState final
{
    struct ComponentBinding;
public:
    Sc6ReplayVfxState() = default;
    ~Sc6ReplayVfxState();
    Sc6ReplayVfxState(const Sc6ReplayVfxState&) = delete;
    Sc6ReplayVfxState& operator=(const Sc6ReplayVfxState&) = delete;
    Status Capture(std::uintptr_t base, void* battle_manager, std::size_t budget,
        std::span<void* const> particle_components = {}, std::span<void* const> companion_owners = {}) noexcept;
    // First rejected capture participant; diagnostic only, never restored state.
    struct CaptureRejection { std::uintptr_t component{}, emitter{}; };
    CaptureRejection capture_rejection() const noexcept { return capture_rejection_; }
    Status ValidateHeld(std::uintptr_t base, void* battle_manager) const noexcept;
    // Validate the captured completion graph against owners captured by the
    // enclosing host. Retaining a delegate receiver is not mutable-state ownership.
    using CompletionOwnerCheck = bool(*)(const void*, bool manager,
        const std::array<std::int32_t,2>& weak, std::uint64_t name);
    Status ValidateCompletionOwnership(const void* context, CompletionOwnerCheck check,
        const char** failed_check=nullptr) const noexcept;

    // Explicit retirement at an admitted game-thread boundary. Failure keeps
    // the native registry owner alive; caller must not report cleanup success.
    Status ReleaseOwners() noexcept;
    bool owners_retained() const noexcept { return object_lease_ && object_lease_->registered(); }
    // Selection-only negative filter. GC retention cannot prevent explicit
    // native component destruction. Success does not replace topology, emitter
    // storage or render-thread preflight against complete B.
    // Preserve unavailable GC admission separately from confirmed ownership
    // loss. Index observation must defer rather than retire on a busy collector.
    Status ValidateRetainedOwners() const noexcept;
    // Asset/manager/receiver reachability only. Captured component identities
    // are deliberately excluded from this independent lease. Success cannot
    // admit publication without explicit replacement bindings for components.
    Status ValidateReconstructionDependencies() const noexcept;
    struct ReconstructionBinding {
        Sc6ReplayCpuEmitterState::ComponentReplacement identity;
        const Sc6ReplayObjectLease* lease{};
    };
    // Complete component inventory: unchanged identities use a live B lease;
    // replacements use their private lease. This validates owners only, not
    // manager/provider/scheduler translation or publication readiness.
    Status ValidateReconstructionBindings(std::span<const ReconstructionBinding> bindings) const noexcept;
    struct ReconstructionRequest {
        std::uintptr_t source{};
        std::array<std::int32_t,2> weak{};
        std::size_t emitter_count{},gpu_ordinal{}; //ordinal==count denotes a CPU-only component.
        std::array<std::size_t,128> gpu_ordinals{};
        std::size_t gpu_count{};
    };
    struct ReconstructionFailure {
        std::uintptr_t source{};
        std::size_t roots{},gpu{},managed{},slots{};
        bool lux{},source_live{};
    };
    struct ReconstructionFactory {
        void* context{};
        // The factory publishes its operation journal before native entry and
        // retains partial ownership on failure. Returned leases are borrowed.
        Status(*construct)(void*,const ReconstructionRequest&,ReconstructionBinding&){};
    };
    Status PrepareReconstructionBindings(const Sc6ReplayVfxState& current, std::size_t budget,
        std::vector<ReconstructionBinding>& output, ReconstructionFactory factory, ReconstructionFailure* failure=nullptr) const noexcept;
    // Retain configuration for primary-manager Lux owners with typed emitters
    // and captured typed CPU emitters. Other owner
    // shapes still require their original live identity; this is not general
    // reconstruction admission. Call only at a completed application hold.
    Status VisitSingleGpuComponents(void* context,Status(*visit)(void*,std::uintptr_t)) const noexcept;
    bool retained_owners_live() const noexcept;
    bool RetainsComponent(const void* component) const noexcept;
    std::array<std::size_t,2> ParticleEmitterCounts(std::uintptr_t component) const noexcept {
        std::array<std::size_t,2> counts{};
        for(const auto& emitter:cpu_emitters_)counts[0]+=emitter.component==component;
        for(const auto& emitter:gpu_owners_)counts[1]+=emitter.component==component;
        return counts;
    }
    // Private fresh-owner staging only. The caller owns the registered empty
    // destination and its lease/hold. Translates one captured component identity
    // and writes only the audited value projection; no old UObject is resolved.
    // dirty is set before the first write and remains set on success/failure.
    Status ApplyFreshComponentValues(const Sc6ReplayCpuEmitterState::ComponentReplacement& binding,
        bool& dirty) const noexcept;
    Status ConstructFreshParticleOwner(std::uintptr_t source,std::uintptr_t target,std::size_t budget,
        Sc6ReplayCpuEmitterState::ComponentReplacement& binding,Sc6ReplayCpuEmitterState::FreshGpuOwner& owner,
        std::size_t ordinal=SIZE_MAX) const noexcept;
    Status QueueFreshGpuRetirement(const Sc6ReplayCpuEmitterState::ComponentReplacement& binding,
        Sc6ReplayCpuEmitterState::FreshGpuOwner& owner,std::size_t ordinal=SIZE_MAX) const noexcept;
    void DiagnoseRetainedOwnerFailure(const void* context,Sc6ReplayObjectLease::FailureSink sink) const noexcept;
    void DiagnoseReconstructionOwnerFailure(const void* context,Sc6ReplayObjectLease::FailureSink sink) const noexcept;
    // Borrowed admission proof for the enclosing lifecycle transaction. Both
    // VFX images/GC leases must outlive every participant using this object.
    // This identifies a birth; it does not unregister, hide or destroy it.
    class ParticleBirthSet;
    class ParticleBirth final
    {
    public:
        struct RenderBinding
        {
            std::uintptr_t base{}, component{}, emitter{}, render{}, system{}, pool{};
            std::uint64_t epoch{};
        };
        std::uintptr_t component() const noexcept { return component_; }
        Status ValidateOwners() const noexcept;
        // Lifetime proof only. Execution/epoch admission remains the enclosing
        // owner's responsibility; this does not establish a held B image.
        Status ValidateLifetime() const noexcept;
        Status ValidateQuarantined(const char** failed_check=nullptr) const noexcept;
        Status ValidateCurrentB() const noexcept;
        Status ReadRenderBinding(RenderBinding& output) const noexcept;
        Status ValidateRetirement(const char** failed_check=nullptr) const noexcept;
        Status ValidateQuarantinedRetirement() const noexcept;
        // Irreversible commit only. Native release must not return B's tiles
        // after A's free prefix has been installed. RHI retirement is external.
        Status RetireAfterCommit() const noexcept;
    private:
        friend class Sc6ReplayVfxState;
        friend class ParticleBirthSet;
        friend struct ReplayVfxMaterialTestAccess;
        Status RetireValidated() const noexcept;
        Status ValidateRetirementBody(const char** failed_check=nullptr) const noexcept;
        const Sc6ReplayVfxState* target_{};
        const Sc6ReplayVfxState* current_{};
        void* battle_{};
        std::uintptr_t component_{};
        std::array<std::int32_t, 2> weak_{};
        std::uint64_t epoch_{};
        // A's independent dependency lease protects this B-only admission.
        // No fresh replacement or expired A component enters complete B.
        bool reconstructed_{};
    };
    Status PrepareParticleBirth(const Sc6ReplayVfxState& current, void* battle,
        ParticleBirth& output) const noexcept;
    // Bounded, allocation-free admission tokens. The limit rejects capacity;
    // it never truncates an ownership delta. Images retain all referenced owners.
    class ParticleBirthSet final
    {
    public:
        static constexpr std::size_t capacity = 32;
        std::span<const ParticleBirth> members() const noexcept { return {members_.data(), count_}; }
        bool empty() const noexcept { return count_ == 0; }
        Status ValidateOwners() const noexcept;
        Status ValidateLifetimes() const noexcept;
        Status ValidateCurrentB() const noexcept;
        Status ValidateRetirement() const noexcept;
        Status RetireAfterCommit() const noexcept;
        Status ValidateQuarantinedRetirement() const noexcept;
        Status RetireQuarantinedAfterCommit() const noexcept;
        Status ReadRenderBindings(std::span<ParticleBirth::RenderBinding> output, std::size_t& count) const noexcept;
        struct RecoveryRetirement { std::size_t completed{};bool admitted{},slot_removed{},failed{}; };
        // C-only cleanup, while C still owns its actual tile pool. Protected B
        // identities cannot be retired; the host subsequently drains render
        // teardown before recapturing C and restoring the private B graphs.
        Status RetireCurrentForUndo(const Sc6ReplayVfxState& protected_b, RecoveryRetirement& progress) const noexcept;
        // Every live provider retaining this C-only attachment must be in the
        // retirement set; a surviving historical provider would dangle.
        Status ValidateTraceAttachment(int slot_id, std::uintptr_t attachment) const noexcept;
        static ParticleBirthSet Single(const ParticleBirth& birth) noexcept;
    private:
        friend class Sc6ReplayVfxState;
        std::array<ParticleBirth, capacity> members_{};
        std::size_t count_{};
    };
    enum class Topology : std::uint8_t { Unchanged, SingleBirth, Births };
    // Exact B-only secondary owners retained by the enclosing lifecycle
    // transaction. The visitor must validate its leases and frozen values on
    // every call. This is not permission to ignore arbitrary suffix slots.
    struct RetainedSecondaryOwners {
        const void* context{};
        Status(*visit)(const void*,void*,bool(*)(void*,std::uintptr_t)){};
    };
    Status PrepareTopology(const Sc6ReplayVfxState& current, void* battle,
        Topology& topology, ParticleBirthSet& births, RetainedSecondaryOwners private_owners = {},
        std::span<const ReconstructionBinding> bindings = {}) const noexcept;
    // Recovery delta is actual C minus protected B; expired historical A is irrelevant.
    Status PrepareRecoveryRetirement(const Sc6ReplayVfxState& current, void* battle,
        ParticleBirthSet& births) const noexcept;
    // Read-only primary particle inventory for trace attachment settlement.
    // Does not admit retirement of the enclosing manager or secondary actors.
    Status PrepareExecutionParticles(const Sc6ReplayVfxState& current, void* battle,
        ParticleBirthSet& births) const noexcept;
    struct CoordinateFailure {
        std::uintptr_t component{}, render_a{}, render_b{};
        std::size_t ordinal{}, target_count{}, original_count{}, first{};
        std::uint32_t target_tile{}, original_tile{};
        bool found{}, system_equal{}, pool_equal{};
    };
    struct CoordinateRebuild {
        std::uintptr_t component{}, emitter{}, render{}, system{}, pool{}, descriptor{}, resource{}, vector_field_asset{};
        std::span<const std::uint32_t> tiles;
        std::uint32_t previous_count{};
        // Borrowed from the retained complete B image, just like tiles from
        // A. The enclosing transaction must outlive ordered GPU retirement.
        std::span<const std::uint32_t> previous_tiles;
        bool rebuild_tiles{true};
        // No B coordinate owner exists for a reconstructed component. Its
        // generated buffers retire with that native owner, never through B undo.
        bool fresh{};
        std::array<std::int32_t,2> component_weak{};
        bool native_retired{}; // Sealed before CPU undo, consumed only on the ordered render route.
        std::uintptr_t source_render{}; // Captured identity only; never dereference an expired A render owner.
    };
    Status PrepareCoordinateReconstruction(const Sc6ReplayVfxState& current, std::vector<CoordinateRebuild>& output,
        std::size_t budget, CoordinateFailure* failure = nullptr,
        std::span<const ReconstructionBinding> bindings = {}) const noexcept;
    class PreparedEmitterSet;
    class PreparedManager final
    {
    public:
        PreparedManager() = default;
        ~PreparedManager();
        PreparedManager(const PreparedManager&) = delete;
        PreparedManager& operator=(const PreparedManager&) = delete;
        Status Publish() noexcept;
        Status ValidatePublished() const noexcept;
        // The caller retains the observed image and all component leases until
        // this transaction retires. Settlement requires a quiescent native C.
        Status BeginExecution(std::size_t retirement_budget) noexcept;
        Status SettleExecution(const Sc6ReplayVfxState& observed, RestoreSettlement purpose = RestoreSettlement::RecoverOriginal,
            const PreparedEmitterSet* reconstructed = nullptr,const PreparedEmitterSet* reconstructed_cpu = nullptr) noexcept;
        Status ReopenExecutionForUndo(const Sc6ReplayVfxState& target) noexcept;
        Status Undo() noexcept;
        Status Commit() noexcept;
        bool published() const noexcept { return published_; }
        // Comparison contract from the owned published allocation and captured
        // rows. Does not sample/promote live listeners or establish a lease.
        Status ReadFinishDispatchBinding(ReplayVfxFinishBinding&) const noexcept;
        std::size_t owned_bytes() const noexcept { return bytes_; }
    private:
        friend class Sc6ReplayVfxState;
        friend struct ReplayVfxMaterialTestAccess;
        struct Array { void* data{}; std::int32_t count{}, capacity{}; };
        struct Allocation { void* target{}; void* previous{}; std::size_t target_bytes{}, previous_bytes{}; };
        const Sc6ReplayVfxState* target_{};
        const Sc6ReplayVfxState* current_{};
        // Borrowed operation leases, separate from immutable complete B.
        std::vector<ReconstructionBinding> reconstruction_bindings_;
        std::vector<ComponentBinding> projected_components_;
        // Private prepublication values are separate from immutable complete B.
        // Only unexecuted publication undo may revisit these fresh identities.
        std::vector<ComponentBinding> private_original_components_;
        bool reconstructed_{};
        void* battle_{};
        std::array<Array, 5> arrays_{}, previous_{};
        std::array<std::byte, 0x50> map_{}, previous_map_{};
        std::vector<Allocation> allocations_;
        std::array<std::vector<const std::byte*>, 2> provider_bindings_;
        std::uint64_t epoch_{};
        std::size_t bytes_{};
        std::size_t execution_reservation_{};
        std::uint64_t previous_fingerprint_{};
        RestoreSettlement execution_settlement_{RestoreSettlement::RecoverOriginal};
        bool executing_{}, execution_settled_{};
        bool PreviousFingerprint(std::uint64_t& result) const noexcept;
        bool ready_{}, published_{}, write_complete_{}, undo_started_{};
        Status ValidateBinding() const noexcept;
        Status ValidateImage(bool target) const noexcept;
        Status ValidatePayload(bool target) const noexcept;
        Status ValidateComponents(bool target, bool compare_values) const noexcept;
        bool Write(bool target) noexcept;
        void Clear(bool retire_previous = false) noexcept;
    };
    struct MaterialAdmissionWitness {
        std::uintptr_t component{},first_material{},first_parent{};
        unsigned array{};int count{},non_null{};
    };
    // Failure-only view of already captured membership; no live reads or leases.
    MaterialAdmissionWitness HistoricalMaterialAdmission() const noexcept;
    Status ValidateHistoricalMaterials() const noexcept;
    Status PrepareManager(const Sc6ReplayVfxState& current, void* battle,
        std::size_t budget, PreparedManager& output,
        std::span<const ReconstructionBinding> bindings = {}) const noexcept;
    // Owner bits must come from quiescent, independently validated emitter
    // bindings. This checks allocation partition only, not owner lifetimes.
    static bool IsCompleteTilePartition(std::array<std::uint64_t, 1024> owned,
        std::span<const std::uint32_t> free_indices) noexcept
    {
        if (free_indices.size() > 65536) return false;
        for (const auto tile : free_indices)
        {
            if (tile >= 65536) return false;
            const auto mask = std::uint64_t{1} << (tile % 64);
            if (owned[tile / 64] & mask) return false;
            owned[tile / 64] |= mask;
        }
        for (const auto word : owned) if (word != ~std::uint64_t{0}) return false;
        return true;
    }
    static Status ValidateNativeTilePartition(std::uintptr_t pool,
        const std::array<std::uint64_t, 1024>& owned, std::size_t& free_count) noexcept;
    // Low-level prefix writer shared by the transaction and local fault tests.
    // Caller supplies retained native bindings and validated owner partition.
    // dirty stays set after any attempted write until enclosing verification.
    // compare_expected=false is reserved for undo of an already dirty write.
    static Status ReplaceNativeTilePrefix(std::uintptr_t pool, std::span<const std::uint32_t> expected,
        std::span<const std::uint32_t> replacement, bool compare_expected, bool& dirty) noexcept;
    std::size_t owned_bytes() const noexcept;
    std::size_t slot_count() const noexcept { return constructed_; }
    std::uint64_t storage_fingerprint() const noexcept;
    std::size_t cpu_emitter_count() const noexcept { return cpu_emitters_.size(); }
    // Prepares owned backing only. The enclosing lifecycle transaction must
    // retain component/asset owners and coordinate GPU/scheduler publication.
    Status PrepareCpuEmitter(std::size_t index, std::size_t budget, void*& component,
        std::size_t& ordinal, Sc6ReplayCpuEmitterState::Prepared& output) const noexcept;
    class PreparedEmitterSet final
    {
    public:
        PreparedEmitterSet() = default;
        PreparedEmitterSet(const PreparedEmitterSet&) = delete;
        PreparedEmitterSet& operator=(const PreparedEmitterSet&) = delete;
        std::size_t size() const noexcept { return entries_.size(); }
        std::size_t owned_bytes() const noexcept;
        bool published() const noexcept;
        Status Publish() noexcept;
        // Reversible allocation-graph installation only. The enclosing owner
        // must hold game/render work and publish pool/render/world state before
        // any playback. Commit requires the enclosing transaction's decision.
        Status PublishGpuStorage() noexcept;
        // Storage handoff only; the enclosing owner must separately exclude
        // retained B producers and own component/render lifecycle. A partial
        // admission remains recoverable by settling the entries already begun.
        // Both images must outlive this set. Settlement observes C, never A.
        Status BeginExecution(const Sc6ReplayVfxState& original, std::size_t retirement_budget,
            const ReplayGpuCompletion* completion = nullptr) noexcept;
        bool FreshComponentDeath(std::uintptr_t component, const std::array<std::int32_t,2>& weak) const noexcept;
        bool FreshComponentRetired(std::uintptr_t component, const std::array<std::int32_t,2>& weak) const noexcept;
        std::size_t FreshComponentRoots(std::uintptr_t component) const noexcept {
            std::size_t count{};for(const auto& entry:entries_)count+=entry.reconstructed && entry.identity.target==component;return count;
        }
        struct SettlementFailure {
            Sc6ReplayCpuEmitterState::ValidationFailure detail{};
            void* component{};
            std::size_t ordinal{};
            // Known=16, native emitter dead=1, reconstructed=2, component dead=4, observed component live=8.
            unsigned observed_member_flags{};
        };
        Status SettleExecution(const Sc6ReplayVfxState& observed, SettlementFailure* failure = nullptr) noexcept;
        Status ReopenExecutionForUndo() noexcept;
        bool execution_started() const noexcept { return execution_original_ != nullptr; }
        Status ValidatePublished(SettlementFailure* failure = nullptr) const noexcept;
        Status Undo() noexcept;
        // Enclosing commit only, after game/render/pool publication and the
        // cancellation boundary. Retains every B native graph until then.
        Status ValidateCommit(const Sc6ReplayVfxState& current, SettlementFailure* failure = nullptr) const noexcept;
        Status Commit(const Sc6ReplayVfxState& current) noexcept;
        void* component(std::size_t i) const noexcept { return i < size() ? entries_[i].component : nullptr; }
        void* expected(std::size_t i) const noexcept { return i < size() ? entries_[i].expected : nullptr; }
        std::size_t ordinal(std::size_t i) const noexcept { return i < size() ? entries_[i].ordinal : 0; }
        const Sc6ReplayCpuEmitterState::Prepared* replacement(std::size_t i) const noexcept
        { return i < size() ? entries_[i].replacement.get() : nullptr; }
    private:
        friend class Sc6ReplayVfxState;
        struct Entry
        {
            void* component{};
            void* expected{};
            std::size_t ordinal{};
            std::unique_ptr<Sc6ReplayCpuEmitterState::Prepared> replacement;
            bool reconstructed{}, fresh_write_started{};
            Sc6ReplayCpuEmitterState::ComponentReplacement identity{};
        };
        std::vector<Entry> entries_;
        const Sc6ReplayVfxState* reconstruction_source_{};
        std::vector<ReconstructionBinding> reconstruction_bindings_;
        bool gpu_storage_{};
        const Sc6ReplayVfxState* execution_original_{};
        bool execution_settled_{}, execution_undo_started_{};
        const Sc6ReplayCpuEmitterState* FindImage(const Sc6ReplayVfxState& image,
            const Entry& entry, bool original) const noexcept;
    };
    // All replacements remain owned until the enclosing CPU/GPU/world
    // transaction commits or recovers them; preparation never permits playback.
    Status PrepareCpuEmitters(std::size_t budget, PreparedEmitterSet& output,
        std::span<const ReconstructionBinding> bindings = {}) const noexcept;
    Status PrepareGpuEmitters(std::size_t budget, PreparedEmitterSet& output,
        std::span<const ReconstructionBinding> bindings = {}) const noexcept;

    class PreparedTilePools final
    {
    public:
        PreparedTilePools() = default;
        PreparedTilePools(const PreparedTilePools&) = delete;
        PreparedTilePools& operator=(const PreparedTilePools&) = delete;
        std::size_t owned_bytes() const noexcept;
        bool published() const noexcept;
    private:
        friend class Sc6ReplayVfxState;
        struct Patch
        {
            std::uintptr_t system{}, pool{};
            std::vector<std::uint32_t> target, previous;
            std::array<std::uint64_t, 1024> target_owned{};
            bool dirty{};
        };
        std::vector<Patch> patches_;
        std::vector<ReconstructionBinding> reconstruction_bindings_;
        std::uintptr_t base_{}, manager_{};
        std::uint64_t epoch_{};
        std::uint32_t thread_{};
        bool ready_{}, in_flight_{};
        bool executing_{}, execution_settled_{};
        Status CheckPool(const Patch& patch, bool target) const noexcept;
        Status WritePool(Patch& patch, bool undo) noexcept;
    };
    // The allocation-order participant never copies native locks/resources or
    // changes the RHI-owned image selector. Complete emitter/render/scheduler
    // membership must be coherent before the enclosing transaction can resume.
    Status PrepareTilePools(const Sc6ReplayVfxState& current, std::size_t budget, PreparedTilePools& output,
        std::span<const ReconstructionBinding> bindings = {}) const noexcept;
    Status PublishTilePools(const Sc6ReplayVfxState& current, PreparedTilePools& prepared) const noexcept;
    Status ValidatePublishedTilePools(const Sc6ReplayVfxState& current, const PreparedTilePools& prepared) const noexcept;
    Status UndoTilePools(const Sc6ReplayVfxState& current, PreparedTilePools& prepared) const noexcept;
    Status CommitTilePools(const Sc6ReplayVfxState& current, PreparedTilePools& prepared) const noexcept;
    Status BeginTilePoolExecution(const Sc6ReplayVfxState& current, PreparedTilePools& prepared,
        std::size_t budget) const noexcept;
    Status SettleTilePoolExecution(const Sc6ReplayVfxState& current, const Sc6ReplayVfxState& observed,
        PreparedTilePools& prepared) const noexcept;
    Status ReopenTilePoolsForUndo(const Sc6ReplayVfxState& original, PreparedTilePools& prepared) const noexcept;

private:
    friend struct ReplayVfxMaterialTestAccess;
    struct alignas(16) Slot
    {
        std::array<std::byte, 0xc0> bytes{};
        std::array<std::int32_t, 2> actor_weak{};
    };
    struct Header { const std::byte* data{}; std::int32_t count{}, capacity{}; };
    // Slot listeners (weak object + FName), visibility keys, time-scale bits.
    // These own array storage, not the state of any listener's UObject.
    struct Table { std::unique_ptr<std::byte[]> bytes; std::size_t count{}; std::int32_t native_capacity{}; };
    static constexpr std::array<std::size_t, 3> table_offsets{0x388, 0x458, 0x468};
    static constexpr std::array<std::size_t, 3> table_strides{16, 12, 4};
    std::array<Table, 3> tables_;
    // Native sparse-map layout and backing, including chain order and holes.
    // Bucket asset pointers are bindings; the assets themselves are not copied.
    std::array<std::byte, 0x50> definition_layout_{};
    std::array<Table, 3> definition_storage_;
    static constexpr std::array<std::size_t, 3> definition_strides{32, 4, 4};
    struct TilePool
    {
        std::uintptr_t system{}, address{};
        std::unique_ptr<std::uint32_t[]> free_indices;
        std::size_t count{};
    };
    // Only free-index order is owned here. Native locks, render resources and
    // emitter state are separate dependencies, never copied as pool payload.
    std::vector<TilePool> tile_pools_;
    struct CpuEmitter
    {
        std::uintptr_t component{};
        std::size_t ordinal{};
        Sc6ReplayCpuEmitterState image;
    };
    std::vector<CpuEmitter> cpu_emitters_;
    // Native1408D64E0/6620 create unique MIDs under this component. Retain
    // identity/membership, not mutable shader parameters or historical root bits.
    struct MaterialRoots {
        std::uintptr_t data{};int count{},capacity{};
        struct Entry {std::uintptr_t object{},parent{};friend bool operator==(const Entry&,const Entry&)=default;};
        std::array<Entry,32> entries{};
        friend bool operator==(const MaterialRoots&,const MaterialRoots&)=default;
    };
    static bool ReadMaterialRoots(std::uintptr_t base,std::uintptr_t component,std::array<MaterialRoots,2>& output) noexcept;
    struct CompletionBindings
    {
        // Native multicast: weak UObject + FName per 16-byte entry. This is
        // immutable binding evidence, never a copied native array header.
        std::array<std::array<std::byte,16>,16> entries{};
        std::uint32_t count{};
        bool operator==(const CompletionBindings&) const = default;
    };
    static Status ReadCompletionBindings(std::uintptr_t component,CompletionBindings& output) noexcept;
    struct EventBindings {
        std::uintptr_t world{},owner{},manager{},owner_class{};
        std::array<std::int32_t,2> world_weak{},owner_weak{},manager_weak{};
        bool emitter_owner{};
        friend bool operator==(const EventBindings&,const EventBindings&)=default;
    };
    static Status ReadEventBindings(std::uintptr_t base,std::uintptr_t component,EventBindings& output) noexcept;
    void DescribeCapturedReferences(Sc6ReplayObjectLease::FailureWitness& output) const noexcept;
    void DiagnoseOwnerFailure(const Sc6ReplayObjectLease* lease,const void* context,Sc6ReplayObjectLease::FailureSink sink) const noexcept;
    struct ComponentBinding
    {
        std::uintptr_t address{};
        std::array<std::int32_t, 2> weak{};
        std::vector<std::uintptr_t> emitters;
        // Value fields read before the next native particle tick overwrites
        // its scratch. Task references, registration, owning arrays and render
        // resources are deliberately not bytes in this projection.
        std::array<std::byte, 384> values{};
        std::uintptr_t tick_entry{}, attach_parent{}, particle_template{};
        std::uint64_t attach_socket{};
        std::uint32_t active_flags{};
        bool lux{};
        std::array<MaterialRoots,2> material_roots{};
        CompletionBindings completions{};
        EventBindings events{};
    };
    struct ComponentSpan { std::size_t offset{}, bytes{}; bool lux{}; };
    // Tick 141F853C0, completion 141F73990, Lux wrapper 1408D8A10 and the
    // native scene-transform overlay establish these pointer-free fields.
    static constexpr std::array component_spans{
        ComponentSpan{0x24c, 28}, // cached bounds origin/extent/radius
        ComponentSpan{0x270, 48}, // ComponentToWorld
        ComponentSpan{0x2a0, 28}, // cached world quaternion/degrees
        ComponentSpan{0x2c0, 24}, // relative location/rotation
        ComponentSpan{0x2e0, 28}, // cached relative quaternion/degrees
        ComponentSpan{0x300, 12}, // relative scale
        ComponentSpan{0x674, 4}, // logical last-render time consumed by 141F6FCF0 with world+930
        ComponentSpan{0x830, 4}, ComponentSpan{0x834, 1},
        ComponentSpan{0x838, 8}, // particle world clock and render-state bits
        ComponentSpan{0x890, 24}, // previous translation and velocity
        ComponentSpan{0x8b0, 1},
        // Native proxy constructor141FB3AE0 selects material relevance and
        // initializes its LOD from this signed index; a fresh owner defaults
        // it independently. Preserve A and complete B through the same path.
        ComponentSpan{0x8b4, 4}, ComponentSpan{0x8b8, 4},
        ComponentSpan{0x8c0, 4}, ComponentSpan{0x8c4, 1},
        ComponentSpan{0x8c8, 8}, ComponentSpan{0x8d0, 1},
        ComponentSpan{0x8d4, 1}, ComponentSpan{0x8d8, 4},
        ComponentSpan{0x914, 4}, ComponentSpan{0x968, 8},
        // +96C is the component delay selected by141F7A6D0 (possibly UCRT
        // rand), then consumed again by duration setup141FA5A20 on loop reset.
        // It is simulation continuation, not reconstructible render history.
        ComponentSpan{0xa4c, 4}, ComponentSpan{0xa78, 8},
        ComponentSpan{0xa84, 8},
        ComponentSpan{0xa90, 1, true}, ComponentSpan{0xa94, 4, true},
        ComponentSpan{0xab8, 8, true}, ComponentSpan{0xac0, 1, true},
        ComponentSpan{0xac4, 4, true}
    };
    struct ManagerSlotProjection {
        std::array<std::byte, 0xc0> bytes{};
        std::array<std::byte, 48> provider{};
    };
    Status ProjectManagerSlot(std::size_t row, std::span<const ReconstructionBinding> bindings,
        ManagerSlotProjection& output) const noexcept;
    Status ProjectManagerComponent(const ReconstructionBinding& binding, ComponentBinding& output) const noexcept;
    static Status ReadComponentValues(std::uintptr_t base, ComponentBinding& component) noexcept;
    static Status CheckComponentBoundary(std::uintptr_t base, const ComponentBinding& component) noexcept;
    // Includes all registered replay-world particle components. Weak identity is not a
    // GC lease or permission to restore component/render payload.
    std::vector<ComponentBinding> components_;
    struct GpuOwner
    {
        std::uintptr_t component{}, emitter{}, system{}, pool{}, render_storage{}, coordinate_resource{};
        std::size_t ordinal{};
        std::vector<std::uint32_t> tiles;
        Sc6ReplayCpuEmitterState image;
    };
    // Ordered tile ownership, not a complete GPU emitter/render snapshot.
    std::vector<GpuOwner> gpu_owners_;
    std::unique_ptr<Sc6ReplayObjectLease> object_lease_;
    std::unique_ptr<Sc6ReplayObjectLease> reconstruction_lease_;
    std::uintptr_t base_{}, manager_{};
    std::array<std::int32_t, 2> manager_weak_{};
    std::uint32_t thread_{};
    std::int32_t sequence_{};
    std::array<std::int32_t, 2> slot_capacities_{};
    std::array<std::int32_t, 2> counts_{};
    std::unique_ptr<Slot[]> slots_;
    std::size_t capacity_{}, constructed_{}, provider_bytes_{};
    bool valid_{};
    void Swap(Sc6ReplayVfxState&) noexcept;
    Status CaptureUnchecked(std::uintptr_t, void*, std::size_t, std::span<void* const>, std::span<void* const>);
    static Status ReadManager(void*, std::uintptr_t&, std::int32_t&, std::array<Header, 2>&) noexcept;
    static Status ValidateProvider(std::uintptr_t, const std::byte*, std::size_t&) noexcept;
    static bool SameRequest(std::uintptr_t, const std::byte*, const std::byte*) noexcept;
    Status CaptureTables(std::size_t budget);
    bool SameTables() const noexcept;
    Status CaptureDefinitionMap(std::size_t budget);
    bool SameDefinitionMap() const noexcept;
    Status ValidateEmptyNormalizer() const noexcept;
    Status CaptureTilePools(std::size_t budget, std::span<void* const>);
    Status CaptureComponent(std::uintptr_t component, std::size_t budget);
    Status ValidateOwnerPartition() const noexcept;
    Status RetainOwners(std::size_t budget, std::span<void* const> companion_owners);
    Status PrepareEmitters(std::size_t budget, PreparedEmitterSet& output, bool gpu,
        std::span<const ReconstructionBinding> bindings = {}) const noexcept;
    bool SameTilePools() const noexcept;
    Status ValidatePoolTransaction(const Sc6ReplayVfxState& current, const PreparedTilePools& prepared) const noexcept;
    static Status ReadTilePool(std::uintptr_t address, std::uint32_t* output,
        std::size_t capacity, std::size_t& count, bool compare) noexcept;
    CaptureRejection capture_rejection_{};
};
}
