#pragma once

#include "Types.hpp"
#include "Sc6ReplayVfxState.hpp"
#include "ReplayStageVisibility.hpp"
#include "ReplayPrerequisiteConsumer.hpp"
#include <array>
#include <bit>
#include <vector>

namespace Horse::Deterministic
{
// Process-local scheduler image. Native binding/layout bytes are separate from
// owned backing copies and are never a portable canonical state representation.
// Capture and backing replacement require completed application work. Historical
// simulation restoration is not supported by this scheduler component alone.
// Capture, validation, move/replacement and destruction belong to the replay
// owner thread, matching the native non-thread-safe scene reference controller.
class Sc6ReplaySchedulerState final
{
public:
    class PreparedRestore;
    struct RetainedPrimaryTicks {
        const void* context{};
        Status(*visit)(const void*,void*,bool(*)(void*,std::uintptr_t)){};
    };
    Sc6ReplaySchedulerState() = default;
    Sc6ReplaySchedulerState(const Sc6ReplaySchedulerState&) = delete;
    Sc6ReplaySchedulerState& operator=(const Sc6ReplaySchedulerState&) = delete;
    Sc6ReplaySchedulerState(Sc6ReplaySchedulerState&&) = default;
    Sc6ReplaySchedulerState& operator=(Sc6ReplaySchedulerState&&) = default;
    Status Capture(std::uintptr_t base, void* world, std::size_t budget,
        RetainedPrimaryTicks retained={}, const Sc6ReplaySchedulerState* continuation_owner=nullptr) noexcept;
    bool OwnsStageComponent(const ReplayStageVisibility::Component& component) const noexcept;
    Status RestoreStageRegistration(const PreparedRestore& prepared,
        const ReplayStageVisibility::Component& component, bool publish) const noexcept;
    // Owner-thread, read-only lifetime/task preflight. Does not certify that
    // uncaptured physics or presentation state can be historically restored.
    struct BindingFailure {
        const char* check{};std::uintptr_t tick{},owner{},expected{},actual{};
        std::array<std::int32_t,2> weak{};std::size_t offset{};
    };
    Status ValidateBindings(std::uintptr_t base, void* world,BindingFailure* failure=nullptr) const noexcept;
    Status ReadPrerequisiteConsumer(std::uintptr_t component,
        std::span<const ReplayPrerequisiteConsumer::Identity> sources,
        ReplayPrerequisiteConsumer::Contract& output) const noexcept;
    // Private image projection only. Complete B and all native registration
    // changes remain owned by the enclosing component transaction.
    Status ReconstructComponentBindings(const Sc6ReplayVfxState& owners,
        std::span<const Sc6ReplayVfxState::ReconstructionBinding> bindings,
        std::size_t budget,Sc6ReplaySchedulerState& output,BindingFailure* failure=nullptr) const noexcept;
    // Shared identity/lease layout only. The owner participant supplies its
    // own complete inventory proof; trace meshes never use VFX admission.
    using ComponentBinding=Sc6ReplayVfxState::ReconstructionBinding;
    struct ComponentOwnerProof {
        const void* context{};
        Status(*validate)(const void*,std::span<const ComponentBinding>){};
    };
    Status ReconstructOwnedComponentBindings(ComponentOwnerProof owners,
        std::span<const ComponentBinding> bindings,std::size_t budget,
        Sc6ReplaySchedulerState& output,BindingFailure* failure=nullptr) const noexcept;
    // Compare retained values/membership while allowing separately owned native
    // backing addresses. Used only to adopt equivalent B after private registration.
    bool SameLogicalImage(const Sc6ReplaySchedulerState& other) const noexcept;
    static Status RehashTickSetStorage(std::span<std::byte> slots,
        std::span<const std::uint32_t> occupied,std::span<std::int32_t> hashes) noexcept;
    struct PreflightFailure {
        const char* check{}; std::uintptr_t address{};
        std::size_t target_ticks{}, current_ticks{}, births{};
        struct Difference { std::uintptr_t tick{}, owner{}; bool current{}; };
        std::array<Difference, 8> differences{};
        std::size_t difference_count{};
    };
    Status PrepareRestore(const Sc6ReplaySchedulerState& current, std::uintptr_t base,
        void* world, std::size_t budget, PreparedRestore& output, PreflightFailure* failure = nullptr,
        const Sc6ReplayVfxState::ParticleBirthSet* births = nullptr, RetainedPrimaryTicks private_owners = {},
        std::span<const Sc6ReplayVfxState::ReconstructionBinding> fresh_components = {}) const noexcept;
    // Completed, externally held application only. Target and current images
    // have distinct roles; original live storage stays owned through commit.
    // Historical scheduler installation alone is not a simulation restore.
    // The optional owner-thread commit query is read-only, may request undo,
    // and must not invoke nested core operations or native gameplay callbacks.
    using CommitQuery = bool (*)(void*) noexcept;
    Status RestoreBacking(const Sc6ReplaySchedulerState& current, std::uintptr_t base, void* world, std::size_t budget,
        PreparedRestore& prepared, CommitQuery cancel_before_commit = nullptr, void* context = nullptr) const noexcept;
    // Enclosing restore transactions must retain B until every participant and
    // presentation publication succeeds. These operations split installation
    // from irreversible old-backing retirement; membership preflight is intact.
    Status PublishBacking(const Sc6ReplaySchedulerState& current, std::uintptr_t base, void* world,
        std::size_t budget, PreparedRestore& prepared) const noexcept;
    struct PublishedFailure { const char* check{}; std::uintptr_t address{}, owner{}; std::size_t offset{}; unsigned expected{}, observed{}; };
    Status ValidatePublishedBacking(std::uintptr_t base, void* world, const PreparedRestore& prepared,
        PublishedFailure* failure = nullptr) const noexcept;
    Status FinalizeContainerOrder(std::uintptr_t base, void* world, PreparedRestore& prepared,
        PublishedFailure* failure = nullptr) const noexcept;
    Status UndoBacking(const Sc6ReplaySchedulerState& current, std::uintptr_t base, void* world,
        PreparedRestore& prepared) const noexcept;
    Status CommitBacking(std::uintptr_t base, void* world, PreparedRestore& prepared) const noexcept;
    // The enclosing owner must quarantine B births and retire C-only owners
    // before settlement. All tasks/world tails must be complete. These APIs
    // retain B backing; they do not independently admit native execution.
    Status BeginExecution(std::uintptr_t base, void* world, PreparedRestore& prepared,
        std::size_t retirement_budget) const noexcept;
    Status SettleExecution(const Sc6ReplaySchedulerState& observed, std::uintptr_t base,
        void* world, PreparedRestore& prepared, RestoreSettlement purpose = RestoreSettlement::RecoverOriginal,
        PublishedFailure* failure = nullptr) const noexcept;
    // Native 142163BA0 sign-extends the 32-bit stamp before comparing it with
    // the full engine epoch. Preserve that predicate without changing the epoch.
    static constexpr bool RebaseEpochStamp(std::int32_t stamp, std::uint64_t captured_epoch,
        std::uint64_t live_epoch, std::int32_t& output) noexcept
    {
        const bool admitted = static_cast<std::uint64_t>(static_cast<std::int64_t>(stamp)) == captured_epoch;
        output = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(admitted ? live_epoch : live_epoch - 1));
        return (static_cast<std::uint64_t>(static_cast<std::int64_t>(output)) == live_epoch) == admitted;
    }
    std::size_t owned_bytes() const noexcept;
    std::size_t level_count() const noexcept { return levels_.size(); }
    std::size_t tick_count() const noexcept { return ticks_.size(); }
    std::size_t prerequisite_count() const noexcept;
    std::uint64_t storage_fingerprint() const noexcept;

private:
    struct ObjectBinding
    {
        std::uintptr_t object{};
        std::array<std::int32_t, 2> weak{};
    } world_;
    struct SceneBinding
    {
        std::uintptr_t scene{}, control{};
        SceneBinding() = default;
        ~SceneBinding();
        SceneBinding(SceneBinding&& other) noexcept;
        SceneBinding& operator=(SceneBinding&& other) noexcept;
        void Release() noexcept;
    } scene_;
    struct SetImage
    {
        std::array<std::byte, 0x50> binding_layout{};
        std::vector<std::byte> slots;
        std::vector<std::uint32_t> allocation_flags;
        std::vector<std::int32_t> hash;
    };
    struct Level
    {
        ObjectBinding owner;
        std::uintptr_t address{};
        std::array<SetImage, 3> sets;
        std::vector<std::uintptr_t> cooldown_order;
    };
    struct Tick
    {
        std::uintptr_t address{};
        ObjectBinding owner;
        // Known 0x50 common prefix plus the verified +50 target binding.
        // Task +18 must be null. Pointer-bearing fields remain binding data.
        std::array<std::byte, 0x58> binding_layout{};
        std::vector<std::array<std::byte, 16>> prerequisites;
    };
    std::uintptr_t base_{};
    std::uint64_t epoch_{};
    std::uint32_t thread_{};
    std::vector<Level> levels_;
    std::vector<Tick> ticks_;
    Status CaptureUnchecked(std::uintptr_t base, void* world, std::size_t budget);
    Status BindObject(std::uintptr_t object, ObjectBinding& binding) const noexcept;
    Status BindScene(std::uintptr_t scene) noexcept;
    bool IsLiveObject(const ObjectBinding& binding) const noexcept;
    Status CaptureSet(std::uintptr_t set, SetImage& output, std::size_t budget);
    Status CaptureTick(std::uintptr_t tick, std::uintptr_t level, std::size_t budget);
    Status ValidateCurrentImage(std::uintptr_t base, void* world) const noexcept;
    Status ValidateExcludedTick(const PreparedRestore& prepared, bool registered) const noexcept;
    Status ValidatePrivateBacking() const noexcept;
};

// Native backing storage prepared without publishing any live header or tick
// state. Installation retains current headers and rebases native tick admission;
// historical reconstruction of the enclosing world remains separate work.
class Sc6ReplaySchedulerState::PreparedRestore final
{
public:
    PreparedRestore() = default;
    ~PreparedRestore();
    PreparedRestore(const PreparedRestore&) = delete;
    PreparedRestore& operator=(const PreparedRestore&) = delete;
    std::size_t owned_bytes() const noexcept;
    std::size_t allocation_count() const noexcept;
    std::size_t patch_count() const noexcept { return patches_.size(); }
    bool ready() const noexcept { return ready_; }
    bool published() const noexcept { return published_; }
    Status ReopenExecutionForUndo(const Sc6ReplaySchedulerState& target) noexcept;
    std::uint64_t storage_fingerprint() const noexcept;
private:
    friend class Sc6ReplaySchedulerState;
    struct Allocation
    {
        void* data{};
        void* previous{};
        std::size_t requested{}, copied{}, charged{}, previous_requested{}, previous_charged{};
        bool previous_is_added{}; // Detached constructor backing, not complete B.
    };
    struct Patch
    {
        std::uintptr_t destination{};
        std::array<std::byte, 0x58> bytes{};
        std::array<std::byte, 0x58> previous{};
        std::size_t size{};
    };
    std::vector<Allocation> allocations_;
    std::vector<Patch> patches_;
    std::uintptr_t base_{};
    void* world_{};
    std::uint64_t epoch_{}, target_epoch_{};
    std::uint32_t thread_{};
    bool ready_{}, published_{}, allocation_ownership_lost_{};
    Sc6ReplayVfxState::ParticleBirthSet birth_;
    static constexpr std::size_t excluded_capacity = Sc6ReplayVfxState::ParticleBirthSet::capacity + 16;
    std::array<std::uintptr_t, excluded_capacity> excluded_ticks_{};
    std::array<std::uintptr_t, excluded_capacity> excluded_owners_{};
    RetainedPrimaryTicks private_owners_{};
    Status ValidatePrivateOwners() const noexcept;
    std::size_t excluded_count_{};
    struct AddedTick {
        std::uintptr_t address{}, owner{};
        const Sc6ReplayObjectLease* lease{};
        std::array<std::byte,0x58> previous{};
        // Native trace construction owns one prerequisite, with spare array
        // capacity. Preserve its entire backing until publication completes.
        std::array<std::byte,16*16> constructor_prerequisites{};
    };
    std::array<AddedTick,excluded_capacity> added_ticks_{};
    std::size_t added_count_{};
    Status ValidateAddedTicks() const noexcept;
    const Sc6ReplaySchedulerState* previous_image_{};
    std::uint64_t published_fingerprint_{};
    const Sc6ReplaySchedulerState* execution_image_{};
    std::uint64_t original_epoch_{}, previous_fingerprint_{};
    std::size_t execution_reservation_{};
    bool executing_{}, execution_settled_{}, execution_undo_started_{};
    bool PreviousFingerprint(std::uint64_t& output) const noexcept;
    static bool FingerprintImages(std::span<const Patch> patches, std::span<const Allocation> allocations,
        bool previous, std::uint64_t& output) noexcept;
    void Clear() noexcept;
    Status Allocate(std::size_t requested, const void* source, std::size_t copied,
        std::uintptr_t previous, std::size_t previous_requested, std::size_t budget, std::uintptr_t& address);
};
}
