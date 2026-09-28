#pragma once
#include "Types.hpp"
#include "Sc6ReplayObjectLease.hpp"
#include "ReplayPhysicsRemovalOperation.hpp"
#include "ReplayPhysicsPublicationLists.hpp"
#include "ReplayGroundBodyAdmission.hpp"
#include "ReplayPhysicsBodyInventory.hpp"
#include "ReplayGroundDebrisConfiguration.hpp"
#include "ReplayGroundBodyConfiguration.hpp"
#include "ReplayGroundPose.hpp"
#include "ReplayGroundPrivateBody.hpp"
#include "ReplayGroundPrivateQueues.hpp"
#include "ReplayGroundInitialBodyState.hpp"
#include "ReplayParticleMaterialGraph.hpp"
#include "ReplayGpuCompletion.hpp"
#include <memory>
#include <vector>

namespace Horse::Deterministic {
// Captured owner inventory and owned configuration arrays, plus private B
// lifecycle. Configuration capture alone does not grant A reconstruction.
// Only the private B instance removes bodies or retires native owners.
// Manager/scheduler/render publication must exclude exactly its retained roots.
class Sc6ReplayGroundDebrisState final {
public:
    Sc6ReplayGroundDebrisState();
    ~Sc6ReplayGroundDebrisState();
    Sc6ReplayGroundDebrisState(const Sc6ReplayGroundDebrisState&)=delete;
    Sc6ReplayGroundDebrisState& operator=(const Sc6ReplayGroundDebrisState&)=delete;
    Status Prepare(std::uintptr_t base,void* battle,void* world,std::uint64_t deactivation_name,std::size_t budget) noexcept;
    using Progress=ReplayPhysicsRemovalOperation::Result;
    Status PreparePhysics(std::size_t budget) noexcept;
    Progress AdvanceRemoval() noexcept;
    Progress RecoverBodies() noexcept;
    Status ReleaseRecoveredOwners() noexcept;
    Status PrepareCommit() noexcept;
    Status CommitNativeOwners() noexcept;
    // The host calls this only after its final ordered render/GPU retirement.
    Status ReleaseCommittedOwners() noexcept;
    Status ValidateFrozen() const noexcept;
    struct UpdateDiagnostic {
        struct Child {
            std::uintptr_t component{},root{},root_actor{};
            int component_index{-1},component_serial{};
            unsigned root_slot{},ring_slot{};
        };
        struct PhysicsCallback {
            std::uintptr_t entry{},callable{},vtable{},invoke{};
            std::array<std::uintptr_t,4> words{};
            int storage_count{};bool readable{};
        };
        const char* check{"none"};std::uintptr_t owner{};unsigned roots{},meshes{};
        std::uintptr_t physics_scene{},physics_collection{};
        int physics_count{},physics_recursion{};
        unsigned physics_observed{};bool physics_complete{};
        std::array<PhysicsCallback,8> physics_callbacks{};
        std::array<Child,64> children{};
        ReplayPhysicsBodyInventory body_inventory{};
    };
    // Current native callback/transform consumers only, with bounded stack
    // scratch. This neither captures history nor proves collision isolation.
    static Status ValidateUpdate(std::uintptr_t base,void* battle,void* world,UpdateDiagnostic& diagnostic) noexcept;
    static std::size_t update_scratch_bytes() noexcept;
    struct MotionProbeWitness {
        const char* check{"not_started"};
        unsigned roots{},meshes{},changed{};
        std::size_t owned_bytes{};
    };
    // Bounded diagnostic after a completed transaction. This grants no motion
    // exclusion and never runs inside restoration or resimulation.
    static Status ProbeMotion(std::uintptr_t base,void* battle,void* world,
        std::size_t budget,MotionProbeWitness& witness) noexcept;
    Status VisitRoots(void* context,bool(*visitor)(void*,std::uintptr_t)) const noexcept;
    struct Mesh {
        std::uintptr_t component{},root{},actor{},asset{},class_object{};
        std::array<std::int32_t,2> weak{};
        unsigned primitive_id{},visibility{},flags{},ordinal{};
        unsigned char mobility{};
        // Native impulse callback 14089FF80 consumes translation at +280;
        // preserve the complete world FTransform at +270 without rerunning RNG.
        std::array<std::byte,0x30> transform{};
        std::array<std::byte,120> transform_auxiliary{};
        ReplayGroundBodyConfiguration body_configuration;
        std::array<std::uintptr_t,2> body_actors{};
        std::array<ReplayGroundInitialBodyState,2> initial_body_states{};
        std::array<std::byte,12> initial_body_velocity{};
        unsigned initial_body_velocity_flag{};
        std::array<std::uintptr_t,32> material_overrides{};
        unsigned material_count{};
    };
    std::span<const Mesh> meshes() const noexcept;
    bool RetainedRenderBinding(std::uintptr_t component,const std::array<int,2>& weak,
        unsigned primitive_id,std::uintptr_t asset,bool dormant) const noexcept;
    std::size_t owned_bytes() const noexcept;
    const char* failed_check() const noexcept {return failed_check_;}
    bool empty() const noexcept;
    bool prepared() const noexcept {return state_!=nullptr;}
    // First owned native acquisition for a replacement graph. A cold root is
    // not a reconstructed ground effect and never grants publication.
    struct ColdRoot {
        enum class Phase {Empty,Entered,Cold,Destroyed,Released};
        Phase phase{Phase::Empty};
        Sc6ReplayObjectLease lease;
        std::uintptr_t source{},object{},outer{},base{};
        std::array<std::int32_t,2> weak{};
        std::size_t allowance{};
        std::uint64_t retirement_serial{};
        const char* check{"none"};
        unsigned flags{},primary_tick{},secondary_tick{};
        bool detached{};
        std::array<std::byte,0x30> captured_transform{};
        std::array<std::byte,120> captured_transform_auxiliary{};
        unsigned captured_transform_flags{};
        struct Child {
            Sc6ReplayObjectLease acquisition_lease;
            bool acquisition_released{};
            std::uintptr_t source{},object{},asset{};std::array<std::int32_t,2> weak{};
            std::uintptr_t body_responses{};unsigned body_response_count{};
            std::uintptr_t displaced_body_responses{};
            std::array<std::byte,0x30> captured_transform{};
            std::array<std::byte,120> captured_transform_auxiliary{};
            unsigned captured_transform_flags{};
            struct NativeArray {std::uintptr_t data{};int count{},capacity{};};
            alignas(16) std::array<std::byte,0x98> physics_helper{};
            std::array<unsigned char,4> physics_options{0,0,1,0};
            std::array<NativeArray,5> physics_arrays{};
            std::array<ReplayGroundPrivateBody,2> private_bodies{};
            ReplayGroundPrivateScene private_scene;
            std::array<std::uintptr_t,2> source_actors{};
            std::array<ReplayGroundInitialBodyState,2> initial_body_states{};
            std::array<std::byte,12> initial_body_velocity{};
            unsigned initial_body_velocity_flag{};
            std::uintptr_t body_setup{};
            std::array<std::int32_t,2> body_setup_weak{};
            bool physics_entered{},physics_ready{},physics_retired{};
            std::array<std::uintptr_t,2> material_arrays{};
            std::array<unsigned,2> material_counts{};
            std::array<std::array<std::uintptr_t,32>,2> material_objects{};
        };
        struct Material {std::uintptr_t source{},object{};};
        std::array<Child,16> children{};
        std::array<Material,256> materials{};
        unsigned child_count{},material_count{};
        std::uintptr_t graph_entries{},graph_callback{},child_observed{};
        std::uintptr_t pose_component{};unsigned pose_offset{},pose_expected{},pose_actual{};
        unsigned body_configuration_offset{};
        unsigned physics_state_offset{};
        Sc6ReplayObjectLease graph_lease;
        bool graph_entered{},graph_ready{},graph_lease_released{};
        std::uintptr_t physics_world{},physics_owner{},physics_module{};
        bool physics_entered{},physics_ready{};
        ~ColdRoot();
        std::size_t owned_bytes() const noexcept {
            auto bytes=sizeof(*this)+lease.owned_bytes()+graph_lease.owned_bytes()+allowance;
            for(const auto& child:children)bytes+=child.acquisition_lease.owned_bytes();
            return bytes;
        }
    };
    std::size_t root_count() const noexcept;
    Status ValidateCapturedOwners() const noexcept;
    Status ConstructColdRoot(std::size_t ordinal,std::size_t budget,ColdRoot& output) const noexcept;
    Status ConstructColdGraph(std::size_t ordinal,std::size_t budget,ColdRoot& output) const noexcept;
    Status ConstructColdPhysics(std::size_t budget,ColdRoot& output) const noexcept;
    static Status DestroyColdRoot(ColdRoot& owner,std::uint64_t preceding_completion) noexcept;
    static Status ReleaseColdRoot(ColdRoot& owner,const ReplayGpuCompletion& completion) noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
    const char* failed_check_{"none"};
};
}
