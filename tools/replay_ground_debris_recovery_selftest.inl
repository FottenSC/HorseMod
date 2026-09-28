// Included by the existing preparation fixture. Compile the actual ground
// recovery method against controlled native dependencies; this proves host
// ordering only. The shipped-PhysX fixture separately tests body operations.
namespace ground_recovery_contract {
struct ReplayPhysicsPublicationLists {
    enum class Phase {Empty,Poisoned};Phase current{Phase::Empty};
    Phase phase()const{return current;}
    const char* failed_check()const{return "controlled_publication";}
};
struct ReplayPhysicsRemovalOperation {
    enum class Phase {Empty,Retained,Removing,Detached,Recovered,Released,Poisoned};
    enum class Result {Rejected,Pending,Complete,Poisoned};
    Phase current{Phase::Retained};unsigned calls{},releases{};bool release_pending{};
    Result next{Result::Complete};
    Result Recover(){++calls;if(next==Result::Complete)current=Phase::Recovered;return next;}
    bool ReleaseRecoveredStorage(){++releases;if(release_pending)return false;current=Phase::Released;return true;}
    Phase phase()const{return current;}
    const char* failed_check()const{return "controlled_native";}
};
static void* GetModuleHandleW(const wchar_t*){return reinterpret_cast<void*>(1);}
class Sc6ReplayGroundDebrisState {
public:
    using Progress=ReplayPhysicsRemovalOperation::Result;
    struct State {
        enum class Physics {Unprepared,Prepared,Removing,Detached,Recovering,Recovered,Poisoned};
        enum class Retirement {Retained,Ready,Retiring,Retired,Poisoned};
        Retirement retirement{Retirement::Retained};
        Physics physics_phase{Physics::Unprepared};unsigned count{1},queue_reads{};
        bool quiescent{},lock_allowed{true};
        std::array<std::unique_ptr<ReplayPhysicsRemovalOperation>,2> physics;
        std::array<std::uintptr_t,2> scenes{};
        ReplayPhysicsPublicationLists publication;bool publication_ok{true};unsigned publication_recoveries{};
        bool restore_publication(){++publication_recoveries;return publication_ok;}
        bool engine_physics_quiescent(){++queue_reads;return quiescent;}
        struct SceneLock {std::uintptr_t scene{},module{};bool Enter(){return true;}};
    };
    std::unique_ptr<State> state_{std::make_unique<State>()};
    const char* failed_check_{};bool frozen{true};Progress removal{Progress::Complete};unsigned removal_calls{};
    Status ValidateFrozen()const{return {frozen?FailureCode::None:FailureCode::GenerationMismatch};}
    Progress AdvanceRemoval(){++removal_calls;if(removal==Progress::Complete)state_->physics_phase=State::Physics::Detached;return removal;}
    Progress RecoverBodies() noexcept;
};
#include "replay_ground_recovery_method.inl"
static void Test() {
    using Owner=Sc6ReplayGroundDebrisState;using Phase=Owner::State::Physics;using Result=Owner::Progress;
    {
        Owner o;auto& s=*o.state_;s.physics[0]=std::make_unique<ReplayPhysicsRemovalOperation>();
        // Failed preparation has retained a body but performed no native write.
        // A nonquiescent input that caused admission failure must not trap B.
        REQUIRE(o.RecoverBodies()==Result::Complete);
        REQUIRE(!s.queue_reads && s.physics[0]->calls==1 && s.physics[0]->releases==1);
        REQUIRE(o.RecoverBodies()==Result::Complete && s.physics[0]->calls==1);
    }
    {
        Owner o;auto& s=*o.state_;s.physics_phase=Phase::Detached;
        s.physics[0]=std::make_unique<ReplayPhysicsRemovalOperation>();
        s.physics[0]->current=ReplayPhysicsRemovalOperation::Phase::Detached;
        REQUIRE(o.RecoverBodies()==Result::Rejected && !s.physics[0]->calls);
        s.quiescent=true;s.physics[0]->next=Result::Pending;
        REQUIRE(o.RecoverBodies()==Result::Pending && s.physics[0]->calls==1 && !s.physics[0]->releases);
        s.physics[0]->next=Result::Complete;s.physics[0]->release_pending=true;
        REQUIRE(o.RecoverBodies()==Result::Pending && s.physics[0]->releases==1);
        s.physics[0]->release_pending=false;
        REQUIRE(o.RecoverBodies()==Result::Complete && s.physics[0]->releases==2);
    }
    {
        Owner o;auto& s=*o.state_;s.physics_phase=Phase::Removing;s.quiescent=true;
        s.physics[0]=std::make_unique<ReplayPhysicsRemovalOperation>();o.removal=Result::Pending;
        REQUIRE(o.RecoverBodies()==Result::Pending && !s.physics[0]->calls);
        o.removal=Result::Complete;REQUIRE(o.RecoverBodies()==Result::Complete && o.removal_calls==2);
    }
    {
        Owner o;o.state_->physics_phase=Phase::Poisoned;
        REQUIRE(o.RecoverBodies()==Result::Poisoned && !o.state_->queue_reads);
        o.state_->physics_phase=Phase::Unprepared;o.frozen=false;
        REQUIRE(o.RecoverBodies()==Result::Rejected);
    }
    {
        Owner o;o.state_->publication_ok=false;
        REQUIRE(o.RecoverBodies()==Result::Rejected && o.state_->physics_phase!=Phase::Recovered);
        o.state_->publication_ok=true;
        REQUIRE(o.RecoverBodies()==Result::Complete && o.state_->publication_recoveries==2);
    }
    for(auto phase:{Owner::State::Retirement::Retiring,Owner::State::Retirement::Retired,Owner::State::Retirement::Poisoned}) {
        Owner o;o.state_->retirement=phase;
        REQUIRE(o.RecoverBodies()==Result::Rejected && !o.state_->queue_reads && !o.state_->publication_recoveries);
    }
    {Owner o;o.state_->retirement=Owner::State::Retirement::Ready;REQUIRE(o.RecoverBodies()==Result::Complete);}
}
}
