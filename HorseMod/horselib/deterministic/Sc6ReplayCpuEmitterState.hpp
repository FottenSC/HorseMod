#pragma once

#include "Types.hpp"

namespace Horse::Deterministic
{
class ReplayGpuCompletion;
// Allocation-graph image of audited sprite (0x1D0), mesh (0x200), and GPU (0x2A0) variants. A native
// emitter may be destroyed and its address reused between A and B: never
// restore through its former address. Prepare independently owned backing.
// The enclosing component transaction owns publication, B undo and rendering.
class Sc6ReplayCpuEmitterState final
{
    enum class Kind { Sprite, Mesh, Gpu };
    static constexpr std::size_t ObjectBytes(Kind kind) noexcept { return kind == Kind::Gpu ? 0x2a0 : kind == Kind::Mesh ? 0x200 : 0x1d0; }
    static constexpr std::uintptr_t Vtable(Kind kind) noexcept { return kind == Kind::Gpu ? 0x394c100 : kind == Kind::Mesh ? 0x3949d88 : 0x3949b60; }
    static bool MeshPayloadSupported(const void* image) noexcept;
    static bool MeshBindingsMatch(const void* image, std::uintptr_t type_data, std::uintptr_t mesh) noexcept;
    struct Binding { std::uintptr_t address{}; std::array<std::int32_t, 2> weak{}; };
public:
    struct ValidationFailure {
        const char* phase{};
        const char* check{};
        std::size_t offset{};
        std::uintptr_t expected{},actual{};
    };
    static constexpr bool IsCpuVtable(std::uintptr_t base, std::uintptr_t table) noexcept {
        return table == base + Vtable(Kind::Sprite) || table == base + Vtable(Kind::Mesh);
    }
    Sc6ReplayCpuEmitterState() = default;
    Sc6ReplayCpuEmitterState(const Sc6ReplayCpuEmitterState&) = delete;
    Sc6ReplayCpuEmitterState& operator=(const Sc6ReplayCpuEmitterState&) = delete;
    Sc6ReplayCpuEmitterState(Sc6ReplayCpuEmitterState&&) noexcept = default;
    Sc6ReplayCpuEmitterState& operator=(Sc6ReplayCpuEmitterState&&) noexcept = default;
    class Prepared final
    {
    public:
        Prepared() = default;
        ~Prepared();
        Prepared(const Prepared&) = delete;
        Prepared& operator=(const Prepared&) = delete;
        void* get() const noexcept { return object_; }
        std::size_t owned_bytes() const noexcept;
        // Caller owns the application hold and retains the displaced native
        // emitter. No native tick/reallocation may run before undo or commit.
        // These operations do not advance, destroy or render either emitter.
        Status ValidateDestination(std::size_t ordinal, void* expected) noexcept;
        Status Publish(std::size_t ordinal, void* expected) noexcept;
        // Install the GPU allocation graph into the CURRENT live B slot, never
        // A's captured address. Keep B's render owner as a process-local binding
        // and preserve its complete object/backing for undo. Caller must own
        // the game/render hold and coordinate render/pool/world publication.
        // This storage operation alone never admits playback or GPU commit.
        Status ValidateGpuDestination(std::size_t ordinal, void* expected) noexcept;
        Status PublishGpuStorage(std::size_t ordinal, void* expected) noexcept;
        // The enclosing fresh-component owner retains a newly constructed GPU
        // root/render pair already placed in its private emitter slot. Install
        // the explicit component edge without native parameter initialization
        // (which selects duration/RNG), then publish the captured graph. This
        // neither registers GPU work nor authorizes execution/retirement.
        Status ValidateFreshGpuDestination(std::size_t ordinal, void* constructed) noexcept;
        Status PublishFreshGpuStorage(std::size_t ordinal, void* constructed, bool& dirty) noexcept;
        Status ValidatePublished() const noexcept;
        Status ReopenExecutionForUndo(const Sc6ReplayCpuEmitterState* original) noexcept;
        Status UndoPublication() noexcept;
        // Full transaction commit transfers the replacement to native code;
        // the caller receives B for explicit retirement, never implicit deletion.
        Status CommitPublication(void*& displaced) noexcept;
        // Null original admits a CPU null-B slot or an explicitly replaced fresh GPU owner.
        Status BeginExecution(const Sc6ReplayCpuEmitterState* original, std::size_t retirement_budget,
            const ReplayGpuCompletion* completion = nullptr) noexcept;
        Status SettleExecution(const Sc6ReplayCpuEmitterState& observed,
            const Sc6ReplayCpuEmitterState* original, ValidationFailure* failure = nullptr) noexcept;
        Status SettleNativeDestruction(const Sc6ReplayCpuEmitterState* original) noexcept;
        bool native_destroyed() const noexcept;
        // Bounded witness journal; never reads retired native storage.
        std::uint32_t destruction_diagnostic() const noexcept;
        // Native deleting-destructor witnesses. Never suppress native work or
        // dereference a freed emitter on return. Only registered C roots match.
        static void NativeDestruction(void* root, bool mesh, unsigned flags, bool completed) noexcept;
        static void NativeGpuDestruction(void* root, unsigned flags, bool completed) noexcept;
        static void NativeComponentDestruction(void* component, bool completed) noexcept;
        // Native-call receipts only: this never proves render/GPU completion.
        bool FreshComponentDestructionWitness(std::uintptr_t component,
            const std::array<std::int32_t,2>& weak) const noexcept;
        // Exact GPU-root death, with either witnessed component death or its
        // still-live immutable slot array containing a null retired slot.
        bool FreshGpuDestructionWitness() const noexcept;
        // Game-thread export only, after native death and ordered GPU settlement.
        bool FreshComponentRetirementWitness(std::uintptr_t component,
            const std::array<std::int32_t,2>& weak) const noexcept;
        static bool HasExecutionOwners() noexcept;
        bool published() const noexcept { return published_; }
        bool execution_started() const noexcept { return executing_; }
        bool execution_settled() const noexcept { return execution_settled_; }
    private:
        friend class Sc6ReplayCpuEmitterState;
        friend struct ReplayEmitterStorageTestAccess;
        struct Allocation
        {
            void* address{};
            std::size_t charged{}, requested{};
            // The parent pointer published by PrepareReplacement. Native
            // reallocation invalidates manual reclamation of this graph.
            void* pointer_slot{};
        };
        std::vector<Allocation> allocations_;
        std::uintptr_t base_{};
        void* object_{};
        std::array<Binding, 9> bindings_{};
        std::uint32_t thread_{};
        std::int32_t lod_peak_{};
        std::size_t ordinal_{};
        void* displaced_{};
        bool published_{};
        bool component_replaced_{};
        Binding replaced_source_{}; // historical identity, never a dereference target
        Kind kind_{Kind::Sprite};
        bool gpu_storage() const noexcept { return kind_ == Kind::Gpu; }
        void* gpu_target_{};
        bool gpu_write_complete_{}, gpu_undo_started_{};
        std::array<std::byte, 0x2a0> gpu_undo_{};
        void RebindRootSlots(void* from, void* to) noexcept;
        std::uint64_t BackingFingerprint() const noexcept;
        std::uint64_t published_fingerprint_{};
        bool executing_{},execution_settled_{};
        std::size_t execution_budget_{};
        enum class NativeLifetime { Live, Destroying, Destroyed, Invalid };
        NativeLifetime native_lifetime_{NativeLifetime::Live};
        NativeLifetime component_lifetime_{NativeLifetime::Live};
        // Borrowed from the enclosing ordered render owner through retirement.
        const ReplayGpuCompletion* fresh_gpu_completion_{};
        std::uint64_t native_death_submission_{};
        bool fresh_gpu_retired_{};
        Status SettleFreshGpuDestruction() noexcept;
        Status SettleFreshCpuComponentDestruction() noexcept;
        Prepared* lifetime_next_{};
        bool lifetime_registered_{};
        std::uintptr_t execution_slots_{};
        int execution_count_{}, execution_capacity_{};
        static Prepared* lifetime_head_;
        void ForgetExecutionOwner() noexcept;
        unsigned DestructionEntryFailure(bool mesh, unsigned flags) const noexcept;
        unsigned destruction_entry_failure_{}, destruction_entries_{}, destruction_returns_{};
        bool ValidateGpuDestructionEntry(unsigned flags) const noexcept;
        bool ValidateComponentDestructionEntry() const noexcept;
        void* volatile* ResolveDestroyedSlot() const noexcept;
        void* volatile* ResolveSlot(bool settling = false) const noexcept;
        void Clear() noexcept;
        void* Allocate(std::size_t bytes, std::size_t budget);
    };

    Status Capture(std::uintptr_t base, void* component, void* emitter, std::size_t budget) noexcept;
    // GPU storage includes pending work, but render resources/registration are
    // separate owners. The CPU publication API rejects a GPU storage image.
    Status CaptureGpu(std::uintptr_t base, void* component, void* emitter, std::size_t budget) noexcept;
    std::uintptr_t vector_field_asset() const noexcept { return bindings_[8].address; }
    std::uintptr_t event_module() const noexcept { return bindings_[4].address; }
    std::uintptr_t second_instance_module() const noexcept { return bindings_[7].address; }
    std::uintptr_t emitter_type_data() const noexcept { return bindings_[5].address; }
    std::uintptr_t mesh_asset() const noexcept { return bindings_[6].address; }
    Status ValidateBindings(ValidationFailure* failure = nullptr) const noexcept;
    Status ValidateNativeGraph(const void* emitter, ValidationFailure* failure = nullptr) const noexcept;
    // Compare captured allocation ownership, without reading or installing
    // native values. Only an explicitly shared GPU root may overlap; its
    // separately owned backing must still be disjoint.
    bool StorageDisjoint(const void* root, const Sc6ReplayCpuEmitterState& other,
        const void* other_root, bool shared_gpu_root = false) const noexcept;
    Status PrepareReplacement(std::size_t budget, Prepared& output) const noexcept;
    // Explicit physical owner translation for an immutable emitter image.
    // This prepares private storage only. The enclosing transaction must own
    // the fresh component's lease, scheduler and render registration separately.
    struct ComponentReplacement {
        std::uintptr_t source{}, target{};
        std::array<std::int32_t,2> source_weak{}, target_weak{};
    };
    // Native allocations never retire implicitly. The enclosing component
    // transaction owns the lease, hold and render/GPU completion route, also
    // when construction fails after native entry. A constructor return is not
    // registration or permission to execute.
    struct FreshGpuOwner {
        enum class Phase { Empty, Constructing, Constructed, Destroying, RetirementQueued, Failed } phase{Phase::Empty};
        void* root{};
        std::uintptr_t component{},render{},system{},descriptor{};
        std::size_t charged_bytes{};
        std::uintptr_t captured_render{};
        unsigned verification_failure{};
    };
    Status ConstructFreshGpuOwner(const ComponentReplacement& component,
        std::size_t budget,FreshGpuOwner& output) const noexcept;
    // Only unused/undone constructor storage, removed from the private slot.
    // Success means native retirement was queued. Keep the journal and asset
    // leases until the enclosing ordered render/RHI/GPU drain completes.
    Status QueueFreshGpuRetirement(const ComponentReplacement& component,FreshGpuOwner& owner) const noexcept;
    Status PrepareReplacement(std::size_t budget, Prepared& output,
        const ComponentReplacement& component) const noexcept;
    // B's retained image verifies its original native allocation graph after
    // A publication. Only the enclosing, fully verified transaction may call
    // CommitDisplacedGpu; it does not run a native emitter destructor or return
    // tiles. The current emitter/render owner remains the one installed at B.
    Status ValidateDisplaced(const Prepared& replacement) const noexcept;
    Status CommitDisplacedGpu(Prepared& replacement) const noexcept;
    std::size_t owned_bytes() const noexcept;
    std::uint64_t storage_fingerprint() const noexcept;
private:
    friend struct ReplayEmitterStorageTestAccess;
    static bool SpawnPerUnitPayloadMatches(std::uintptr_t base, const void* module, int bytes) noexcept;
    static bool GpuInstancePayloadMatches(std::uintptr_t base, const void* image) noexcept;
    static bool GpuDescriptorBindingMatches(std::uintptr_t base, const void* image, std::uintptr_t type) noexcept;
    static bool GpuFieldBindingMatches(const void* image, std::uintptr_t asset) noexcept;
    static bool ModuleBindingsMatch(const void* image,std::uintptr_t first,std::uintptr_t second) noexcept;
    static bool InstancePayloadMatches(std::uintptr_t base, const void* image, std::uintptr_t module) noexcept;
    struct Buffer
    {
        // -1 means a pointer slot in the main object, otherwise in an earlier buffer.
        int parent{-1};
        std::size_t offset{};
        std::vector<std::byte> bytes;
    };
    std::array<std::byte, 0x2a0> object_{};
    Kind kind_{Kind::Sprite};
    bool gpu_storage() const noexcept { return kind_ == Kind::Gpu; }
    std::size_t object_bytes() const noexcept { return ObjectBytes(kind_); }
    // +1A0 is the material override consumed by native vslot +170, not a
    // required particle module. Weak identity does not retain its mutable state.
    std::array<Binding, 9> bindings_{}; // asset, component, LOD, material override, first instance module, mesh/GPU TypeData, mesh asset, second instance module
    // Read while A is live. A later replacement must not inspect a retired
    // original UObject to establish its class or particle template.
    std::uintptr_t component_type_{}, component_template_{};
    std::vector<Buffer> buffers_;
    std::uintptr_t base_{};
    std::uint32_t thread_{};
    std::int32_t lod_peak_{};
    bool valid_{};
    Status CaptureUnchecked(std::uintptr_t base, void* component, void* emitter, std::size_t budget);
    Status CaptureImpl(std::uintptr_t base, void* component, void* emitter, std::size_t budget, bool gpu) noexcept;
    Status CaptureGpuBuffers(std::size_t budget);
    Status ValidateBindingsForComponent(const ComponentReplacement* component, ValidationFailure* failure = nullptr) const noexcept;
    Status PrepareReplacementImpl(std::size_t budget, Prepared& output, const ComponentReplacement* component) const noexcept;
    Status AddBuffer(int parent, std::size_t offset, std::uintptr_t source, std::size_t bytes, std::size_t budget);
};
}
