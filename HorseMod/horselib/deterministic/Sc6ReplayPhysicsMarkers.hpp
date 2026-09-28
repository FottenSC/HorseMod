#pragma once
#include "Sc6ReplayWorldState.hpp"
#include <memory>

namespace Horse::Deterministic {
// A seek-local lease, not a checkpoint archive of PhysX pool/map storage.
// Inactive suppressed markers still own registrations and native pool slots.
class Sc6ReplayPhysicsMarkers final {
public:
    Sc6ReplayPhysicsMarkers();
    ~Sc6ReplayPhysicsMarkers();
    Sc6ReplayPhysicsMarkers(const Sc6ReplayPhysicsMarkers&)=delete;
    Sc6ReplayPhysicsMarkers& operator=(const Sc6ReplayPhysicsMarkers&)=delete;
    using Boundary=Sc6ReplayWorldState::PhysicsBoundary;
    Status Prepare(std::uintptr_t game_base,const Boundary& a,const Boundary& b,std::size_t budget) noexcept;
    Status Publish() noexcept;
    Status BeginExecution() noexcept;
    Status Undo() noexcept;
    Status ValidateCommit() noexcept;
    Status Commit() noexcept;
    // Bounded qualification fault between completed native operations.
    bool ArmPublicationFailure(unsigned completed_operations) noexcept;
    bool NormalizePreflight(const Boundary& a,const Boundary& b,unsigned scene,unsigned actor,
        Boundary::ActorObservation& normalized) const noexcept;
    std::size_t owned_bytes() const noexcept;
    bool released() const noexcept;
    struct Diagnostic { const char* check{"none"}; std::uintptr_t owner{}; unsigned row{0xffffffffu},source{}; std::uint64_t expected{},observed{}; };
    Diagnostic diagnostic() const noexcept {return diagnostic_;}
private:
    struct State;
    std::unique_ptr<State> state_;
    Diagnostic diagnostic_;
};
}
