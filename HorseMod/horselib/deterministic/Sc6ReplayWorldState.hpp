#pragma once

#include "Types.hpp"
#include "ReplayCreationRenderOwner.hpp"
#include "ReplayPhysicsSapImage.hpp"
#include "ReplayPhysicsNotifications.hpp"
#include "ReplayStageVisibility.hpp"
#include "Sc6ReplaySchedulerState.hpp"
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
class Sc6ReplayPhysicsMarkers;
// Owns timer callback copies independently of the live manager. This is a
// process-local checkpoint component, not a portable callback serialization.
// Historical world restoration is not admitted merely because Capture passes.
class Sc6ReplayWorldState final
{
public:
    static bool PhysicsActiveBodyIndexValid(unsigned rigid_flags,unsigned count,unsigned capacity,
        unsigned kinematic_count,unsigned index,std::uintptr_t storage) noexcept {
        // Scene's active array is partitioned: kinematics precede dynamics.
        // This is capture admission only, never writable body projection.
        return count<=64 && count<=capacity && kinematic_count<=count && index<count && storage
            && bool(rigid_flags&1)==(index<kinematic_count);
    }
    struct BindingDiagnostic { const char* check{}; std::uintptr_t actor{}; std::size_t component{}, offset{}; std::uint64_t expected{}, observed{}; std::uintptr_t registration_entry{}; unsigned primary_tick_flags{}; bool base_registration{}; unsigned secondary_tick_flags{}, expected_primary_flags{}, expected_secondary_flags{}; };
    friend struct ReplayStageStorageTestAccess;
    class PreparedRestore;
    Sc6ReplayWorldState() = default;
    ~Sc6ReplayWorldState();
    Sc6ReplayWorldState(const Sc6ReplayWorldState&) = delete;
    Sc6ReplayWorldState& operator=(const Sc6ReplayWorldState&) = delete;
    Status Capture(std::uintptr_t base, void* world, std::size_t budget,
        std::span<const std::uintptr_t> stage_actors = {}) noexcept;
    // Component restoration only. Caller owns simulation/task admission and
    // its enclosing undo; this does not admit historical world execution.
    Status Restore(std::uintptr_t base, void* world, std::size_t budget) const noexcept;
    Status PrepareRestore(std::uintptr_t base, void* world, std::size_t budget, PreparedRestore& output) const noexcept;
    Status ValidateHeld(std::uintptr_t base, void* world) const noexcept;
    std::size_t owned_bytes() const noexcept { return capacity_ * sizeof(Record) + callback_bytes_ + stage_.capacity()*sizeof(ReplayStageVisibility); }
    std::span<const ReplayStageVisibility> stage_visibility() const noexcept { return stage_; }
    std::size_t timer_count() const noexcept { return count_; }
    // Added to the checkpoint's existing GC lease; weak identities still gate writes.
    std::array<void*, 6> camera_owners() const noexcept { return camera_binding_.objects; }
    struct PhysicsBoundary
    {
        // Native ALuxBattleChara+3B0 owns creation meshes without PhysX bodies.
        // These are lifetime/membership witnesses and undo flags, not mesh state.
        using CreationOwner = ReplayCreationRenderOwner;
        std::array<CreationOwner, 128> creation_owners{};
        std::size_t creation_owner_count{};
        struct ActorObservation {
            std::uintptr_t actor{}, vtable{}, user_data{}, body{}, component{};
            std::uint16_t kind{};
            alignas(16) std::array<std::byte, 0xf0> properties{};
            // Read-only dependency witnesses, never memcpy restore payloads.
            // NpRigidDynamic includes Scb::Body and Sc::BodyCore. Its separate
            // BodySim and 64-byte kinematic state contain owning pointers.
            std::array<std::byte, 0x180> dynamic_storage{};
            std::array<std::byte, 0xc8> simulation_storage{};
            std::array<std::byte, 0x40> kinematic_storage{};
            std::uintptr_t simulation{}, kinematic{};
            // Logical scheduling and process-local storage witnesses. Only
            // checked arrays/counts/inverse indices and isolated node activity
            // are installed through current bindings, never historical pointers.
            // Native wake/sleep dispatch remains outside this projection.
            struct KinematicAdmission {
                std::uintptr_t scene{}, controller{}, controller_vtable{}, controller_owner{}, island_owner{};
                std::array<std::byte, 0x18> active_header{}; // scene+20, including kinematic prefix count+30
                std::array<std::uintptr_t, 64> active_bodies{};
                struct Island {
                    std::array<std::byte, 0x20> storage_header{}; // node/index storage+18..37
                    std::array<std::byte, 0x20> node{};
                    std::uint32_t index{};
                    std::array<std::array<std::byte, 0x10>, 2> list_headers{}; // +98, +190
                    std::array<std::array<std::uint32_t, 64>, 2> lists{};
                    friend bool operator==(const Island&, const Island&) = default;
                };
                std::array<Island, 2> islands{}; // island manager+B0, +310
                bool valid{};
                friend bool operator==(const KinematicAdmission&, const KinematicAdmission&) = default;
            } kinematic_admission;
            std::uintptr_t component_vtable{};
            std::array<std::byte, 0x30> component_transform{};
            std::array<std::byte, 120> component_transform_auxiliary{};
            std::array<std::int32_t, 2> component_weak{};
            std::array<std::uint32_t, 3> body_scale{};
            std::array<std::uintptr_t, 32> component_children{};
            std::uintptr_t component_parent{}, component_children_storage{};
            std::uint64_t component_socket{};
            std::uint32_t component_flags{}, component_transform_flags{};
            std::int32_t component_child_count{}, component_child_capacity{}, component_scopes{};
            std::array<std::uint64_t, 16> query_handles{};
            struct QueryShapeObservation {
                std::uintptr_t shape{}, body{};
                std::uint32_t control{}, geometry_kind{}, geometry_bytes{};
                std::uint8_t flags{};
                std::array<std::byte, 28> local_pose{};
                std::array<std::byte, 48> geometry{};
                std::array<std::byte, 24> bounds{};
                friend bool operator==(const QueryShapeObservation&, const QueryShapeObservation&) = default;
            };
            std::array<QueryShapeObservation, 16> query_shapes{};
            std::uint16_t query_handle_count{};
            struct InteractionObservation {
                std::uintptr_t address{};
                std::array<std::byte, 0x28> header{};
                // Type-2 marker ownership only; never installed into native storage.
                std::array<std::uintptr_t, 2> marker_elements{};
                std::uint32_t marker_filter_pair{};
                bool marker_registered{};
                // Prove complete graph coverage before any detach/clone. The
                // existing per-row registration witness alone allows unseen
                // scene or static-actor interactions.
                std::uint32_t marker_scene_count{}, marker_scene_active{}, marker_map_count{};
                std::array<std::uint32_t,2> marker_actor_counts{}, marker_attributes{};
                std::array<std::array<std::uint32_t,4>,2> marker_filter_data{};
                std::array<std::uintptr_t,2> marker_shape_cores{};
                bool marker_filter_valid{};
                friend bool operator==(const InteractionObservation&, const InteractionObservation&) = default;
            };
            std::array<InteractionObservation, 16> interactions{};
            unsigned interaction_count{};
            // Structural isolation is not a request to wake/sleep. Pending
            // notifications additionally require captured queue/owner proof.
            bool isolated_kinematic{};
            // Used only after valid_counts and full projection preflight.
            // Unchanged convex actors are retained witnesses, never writes.
            bool mutable_projection() const noexcept {
                if (kind != 2 || query_handle_count > query_shapes.size()) return false;
                for (unsigned i = 0; i < query_handle_count; ++i)
                    if (query_shapes[i].geometry_kind == 4 || query_shapes[i].geometry_kind == 5) return false;
                return true;
            }
            friend bool operator==(const ActorObservation&, const ActorObservation&) = default;
        };
        struct NodeDomain {
            std::uintptr_t owner{}, free_storage{};
            std::uint32_t free_count{}, free_capacity{}, next_id{}, pending_count{};
            std::array<std::uint32_t,64> free_ids{};
            std::array<std::uint32_t,2> counts{};
            std::array<std::array<std::array<std::byte,0x20>,64>,2> nodes{};
            std::array<std::array<std::uint32_t,64>,2> indices{};
            std::array<std::array<std::uint32_t,64>,2> island_ids{};
            std::array<std::uint32_t,2> island_id_counts{};
            std::array<bool,2> activation_domain_clear{};
            bool island_ids_valid{};
            bool valid{};
            bool notifications_quiescent{};
            std::uintptr_t filter_shader{}, filter_callback{}, filter_data{};
            std::uint32_t filter_data_size{};
            friend bool operator==(const NodeDomain&,const NodeDomain&)=default;
        };
        std::array<NodeDomain,2> node_domains{};
        std::array<ReplayPhysicsNotifications,2> notifications{};
        std::array<ReplayPhysicsSapImage,2> broadphases{};
        std::uintptr_t owner{}, module{};
        std::array<std::uintptr_t, 2> scenes{};
        std::array<std::array<std::uint32_t, 6>, 2> actor_counts{};
        std::array<std::uint32_t, 2> articulations{};
        std::array<std::uint32_t, 2> scene_flags{};
        std::array<std::int32_t, 2> render_work_counts{};
        std::array<std::array<std::byte, 0x60>, 2> query_pruners{};
        std::array<std::int16_t, 2> scene_ids{};
        std::array<std::array<ActorObservation, 64>, 2> actors{};
        std::array<std::uint32_t, 2> observed_actors{};
        std::uint32_t scene_count{};
        bool asynchronous{};
        bool valid_counts() const noexcept {
            if (creation_owner_count > creation_owners.size() || scene_count > scenes.size()) return false;
            for (std::size_t scene = 0; scene < actors.size(); ++scene) {
                if (observed_actors[scene] > actors[scene].size()) return false;
                for (std::size_t i = 0; i < observed_actors[scene]; ++i) {
                    const auto& actor = actors[scene][i];
                    if (actor.query_handle_count > actor.query_handles.size()
                        || actor.query_handle_count > actor.query_shapes.size()
                        || actor.interaction_count > actor.interactions.size()
                        || actor.component_child_count < 0
                        || static_cast<std::size_t>(actor.component_child_count) > actor.component_children.size()) return false;
                }
            }
            return true;
        }
        friend bool operator==(const PhysicsBoundary&, const PhysicsBoundary&) = default;
    };
    // Completed application only. Counts identify uncovered solver owners;
    // neither equal counts nor an empty task event admit historical physics.
    static Status ReadPhysicsBoundary(std::uintptr_t base, void* world, PhysicsBoundary& output) noexcept;
    static Status ReadPhysicsShapeGeometry(const void* storage, PhysicsBoundary::ActorObservation::QueryShapeObservation& output) noexcept;
    static Status ReadPhysicsNotifications(PhysicsBoundary& output,unsigned scene) noexcept;
    // Bounded kinematic projection only. Preparation rejects any uncovered
    // mutation/owner. Query/render reconstruction remains pending while held;
    // cancellation restores B values without invoking native transform fanout.
    struct PhysicsProjectionFailure { const char* check{}; unsigned scene{}, actor{}; };
    static Status PreparePhysicsProjection(const PhysicsBoundary& target, const PhysicsBoundary& original,
        PhysicsProjectionFailure* failure = nullptr, bool owned_render_work = false,
        const Sc6ReplayPhysicsMarkers* markers = nullptr, bool planned_creation_visibility = false) noexcept;
    static Status InstallPhysicsProjection(const PhysicsBoundary& source, const PhysicsBoundary& destination, bool owned_render_work = false) noexcept;
    static Status ValidatePhysicsProjection(const PhysicsBoundary& expected, const PhysicsBoundary& observed,
        bool queries_ready = false, bool owned_render_work = false) noexcept;
    static Status ReconstructPhysicsQueries(const PhysicsBoundary& expected) noexcept;

private:
    struct alignas(16) Record { std::array<std::byte, 0xc0> bytes{}; };
    struct Header { const Record* data{}; std::int32_t count{}, capacity{}; };
    static_assert(sizeof(Header) == 0x10 && sizeof(Record) == 0xc0);
    struct CameraBinding {
        std::array<void*, 6> objects{}; // BM, camera, lens, time groups, MPC asset, MPC instance
        std::array<std::array<std::int32_t, 2>, 6> weak{};
        std::uintptr_t component_vtable{}, provider{};
        // Native time-group and authored MPC storage witnesses. Existing
        // entries only: publication never grows a live map during restore.
        std::array<std::byte, 0x20> time_arrays{};
        std::array<std::byte, 0xa0> collection_maps{};
        std::array<std::uint64_t, 32> scalar_names{};
        std::array<std::uintptr_t, 32> scalar_values{};
        std::array<std::uint32_t, 32> scalar_defaults{};
        std::uintptr_t time_vtable{}, collection_resource{}, authored_scalars{};
        std::int32_t scalar_count{};

        std::array<std::byte, 0x60> authored_dof{}; // immutable rows +250..2AF, witness only
        friend bool operator==(const CameraBinding&, const CameraBinding&) = default;
    } camera_binding_;
    struct Values
    {
        // Native camera DOF history and published lens values, not shader observations.
        std::uint32_t camera_distance{}, camera_cursor{};
        std::array<std::uint32_t, 16> time_scales{};
        std::array<std::uint8_t, 16> time_inhibit{};
        std::array<std::uint32_t, 2> material_clocks{};
        std::array<std::uint32_t, 32> material_scalars{};

        std::array<std::uint32_t, 6> camera_envelope{};
        std::array<std::uint32_t, 5> camera_lens{};
        std::array<std::uint32_t, 2> camera_override_bits{};
        std::uint8_t camera_mode{}, camera_dof_method{};
        std::array<std::uint32_t, 5> world_clocks{};
        std::uint64_t timer_clock{};
        std::uint32_t ue_random_state{}; // Native SRand, separate from thread-local UCRT rand.
        // Native contact hysteresis/history consumed by 14038E2F0/14038E4C0/
        // 14038E6C0. Float lanes are retained as bits (including unused W).
        std::array<std::uint32_t, 32> foot_world_history{};
        std::array<std::uint32_t, 4> foot_height{};
        std::array<std::int32_t, 4> foot_phase{};
        std::array<std::uint32_t, 4> foot_material{}, foot_pose_scalars{};
        std::array<std::uint8_t, 2> foot_vfx_suppressed{};
        std::array<std::uint32_t, 2> foot_vfx_scale{};
        // Shared terrain runtime +F4/+FC: last native emission frame per player.
        // 14038EAE0 gates light-contact/ground-scrape callbacks on these values.
        std::array<std::uint32_t, 4> weapon_contact_frames{};
        std::array<std::int32_t, 3> counts{};
        bool timers_admitted{};
        friend bool operator==(const Values&, const Values&) = default;
    } values_;
    // Binding/lifetime data stays separate from the clock values.
    std::uintptr_t base_{};
    void* world_{};
    void* manager_{};
    void* owner_{};
    std::uint32_t thread_{};
    std::unique_ptr<Record[]> records_;
    std::size_t count_{}, capacity_{}, callback_bytes_{};
    std::uint64_t callback_binding_fingerprint_{};
    std::vector<ReplayStageVisibility> stage_;
    static Status ReadStage(std::uintptr_t base, std::uintptr_t actor, ReplayStageVisibility& output) noexcept;
    static Status ValidateStage(std::uintptr_t base, std::span<const ReplayStageVisibility> image, bool values, BindingDiagnostic* diagnostic=nullptr,
        const Sc6ReplaySchedulerState* scheduler=nullptr,
        const Sc6ReplaySchedulerState::PreparedRestore* scheduler_restore=nullptr) noexcept;
    static bool WriteStage(std::uintptr_t base, std::span<const ReplayStageVisibility> image,
        const Sc6ReplaySchedulerState* scheduler=nullptr,const Sc6ReplaySchedulerState::PreparedRestore* scheduler_restore=nullptr) noexcept;
    bool valid_{};
    void ClearRecords() noexcept;
    static Status ReadCamera(std::uintptr_t base, void* world, CameraBinding& binding, Values& values) noexcept;
    static bool ValidateCamera(std::uintptr_t base, void* world, const CameraBinding& binding, BindingDiagnostic* diagnostic=nullptr) noexcept;
    static bool WriteCamera(const CameraBinding& binding, const Values& values) noexcept;
    static Status Read(std::uintptr_t base, void* world, Values& values,
        void*& manager, void*& owner, std::array<Header, 3>& arrays, CameraBinding& camera) noexcept;
    static Status ValidateRecord(std::uintptr_t base, const Record& record,
        std::uint8_t state, std::size_t& callback_bytes) noexcept;
    static bool SameRecord(std::uintptr_t base, const Record& first, const Record& second) noexcept;
    static Status FingerprintCallbackBindings(std::uintptr_t base, const Record* records,
        std::size_t count, std::uint64_t& output) noexcept;
    static bool Write(std::uintptr_t base, void* world, void* manager, const Values& values,
        const std::array<Header, 3>& arrays, std::uint64_t timer_epoch, const CameraBinding& camera) noexcept;
};

// Fully allocated replacement storage. Prepare both target and undo before
// any enclosing gameplay write; Apply needs no replacement allocations.
class Sc6ReplayWorldState::PreparedRestore final
{
public:
    PreparedRestore() = default;
    ~PreparedRestore();
    PreparedRestore(const PreparedRestore&) = delete;
    PreparedRestore& operator=(const PreparedRestore&) = delete;
    Status Apply() noexcept;
    // Publication retains the displaced native arrays until the enclosing
    // gameplay/render transaction has verified all of its participants.
    Status Publish() noexcept;
    Status ValidatePublished() const noexcept;
    Status Undo() noexcept;
    Status Commit() noexcept;
    // Timer-array handoff only. The enclosing host must separately admit
    // render/scheduler/callback owners and complete their work before settle.
    Status BeginExecution(std::size_t retirement_budget,const Sc6ReplaySchedulerState* scheduler=nullptr,
        const Sc6ReplaySchedulerState::PreparedRestore* scheduler_restore=nullptr) noexcept;
    Status SettleExecution() noexcept;
    Status ReopenExecutionForUndo() noexcept;
    Status ReopenRenderWorkForUndo() noexcept;
    // Historical host only: displace the two native weak-object end-frame
    // sets before gameplay restoration can enqueue render updates. Empty A/B
    // boundaries only; private fixed capacity admits the retained components.
    Status PrepareRenderWork(const PhysicsBoundary& target, const PhysicsBoundary& original, std::size_t budget) noexcept;
    Status PublishRenderWork() noexcept;
    Status PublishCreationVisibility(const PhysicsBoundary& image, bool recovery) noexcept;
    Status ValidateRenderWork() const noexcept;
    struct RenderWorkDiagnostic { const char* check{}; std::uintptr_t component{}; std::uint64_t observed{}, expected{}; };
    BindingDiagnostic binding_diagnostic() const noexcept { return binding_diagnostic_; }
    RenderWorkDiagnostic render_work_diagnostic() const noexcept { return render_diagnostic_; }
    Status UndoRenderWork() noexcept;
    Status CommitRenderWork() noexcept;
    Status BeginRenderWorkExecution(std::size_t retirement_budget) noexcept;
    Status SettleRenderWorkExecution(const PhysicsBoundary& observed) noexcept;
    Status CompleteUnexecutedPublicationRenderWork() noexcept;
    bool published() const noexcept { return published_; }
    bool render_published() const noexcept { return render_published_; }
    bool render_repair_pending() const noexcept { return render_published_ && (!render_write_complete_ || render_undo_started_); }
    std::size_t owned_bytes() const noexcept { return owned_bytes_; }
private:
    friend class Sc6ReplayWorldState;
    std::uintptr_t base_{};
    void* world_{};
    void* manager_{};
    void* owner_{};
    std::uint32_t thread_{};
    Values values_{};
    Values previous_values_{};
    CameraBinding camera_binding_{};
    std::vector<ReplayStageVisibility> stage_, previous_stage_;
    const Sc6ReplaySchedulerState* stage_scheduler_{};
    const Sc6ReplaySchedulerState::PreparedRestore* stage_scheduler_restore_{};
    std::array<Header, 3> arrays_{};
    std::array<Header, 3> previous_arrays_{};
    std::uint64_t epoch_{}, previous_timer_epoch_{};
    std::uint64_t target_fingerprint_{}, previous_fingerprint_{};
    std::size_t owned_bytes_{};
    bool ready_{}, published_{}, write_complete_{}, undo_started_{};
    bool executing_{}, execution_settled_{};
    std::size_t execution_retirement_budget_{};
    struct RenderBinding { std::uintptr_t component{}; std::array<std::int32_t, 2> weak{}; std::uint32_t flags{}; };
    using RenderSet = std::array<std::byte, 0x50>;
    std::array<RenderSet, 2> render_previous_{}, render_owned_{};
    std::array<std::uint64_t, 2> render_previous_fingerprint_{};
    static constexpr int RenderCapacity=512;
    std::array<RenderBinding, RenderCapacity> render_bindings_{};
    std::array<ReplayStageParticleAdmission, RenderCapacity> render_particle_admission_{};
    Status PublishStageVisibility(bool recovery) noexcept;
    std::array<PhysicsBoundary::CreationOwner, 128> render_creation_owners_{};
    std::size_t render_creation_owner_count_{};
    std::size_t render_binding_count_{};
    bool render_ready_{}, render_published_{}, render_write_complete_{}, render_undo_started_{};
    bool render_executing_{},render_execution_settled_{};
    std::size_t render_execution_budget_{};
    // Borrowed handoff identity only: never frees or revives native backing.
    enum class PublicationWork { Unavailable, HandedOff, Completing, Completed };
    PublicationWork render_publication_work_{};
    std::array<RenderSet,2> render_publication_backing_{};
    std::array<std::uint64_t,2> render_publication_fingerprint_{};
    mutable RenderWorkDiagnostic render_diagnostic_{};
    mutable BindingDiagnostic binding_diagnostic_{};
    std::uint32_t StageTickFlagMask(std::uintptr_t component) const noexcept;
    static bool FingerprintRenderSet(const RenderSet& set, std::uint64_t& hash) noexcept;
    void ClearRenderWork() noexcept;
    void Clear() noexcept;
    void InvokeNativePublicationRenderTail() const noexcept;
    Status ValidateRenderWorkStorage(std::uint64_t epoch,const std::array<RenderSet,2>& owned,bool native_capacity) const noexcept;
    Status ValidateBinding() const noexcept;
    Status ValidateBinding(std::uint64_t epoch) const noexcept;
    static Status ValidateExecutionStorage(std::uintptr_t base, const std::array<Header,3>& current,
        const std::array<Header,3>& previous, std::size_t budget) noexcept;
    Status ValidateImage(bool target) const noexcept;
    static Status Fingerprint(std::uintptr_t base, const std::array<Header, 3>& arrays,
        std::uint64_t& output) noexcept;
};
}
