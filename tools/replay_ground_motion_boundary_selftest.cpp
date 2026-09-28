#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <cassert>
enum class FailureCode {None, RestoreVerificationFailed, UnsupportedContent};
struct Status {FailureCode code{};bool ok()const{return code==FailureCode::None;}};
struct UcrtRandBrokerImage {unsigned state{};bool operator==(const UcrtRandBrokerImage&)const=default;};
struct Broker {
    unsigned calls{},state{};bool fail_first{},fail_second{};
    Status Capture(DWORD,UcrtRandBrokerImage& out) {
        ++calls;out.state=state;
        return {(calls==1?fail_first:fail_second)?FailureCode::UnsupportedContent:FailureCode::None};
    }
};
struct Sc6ReplayGroundDebrisState {
    struct MotionProbeWitness {const char* check{"not_started"};unsigned roots{},meshes{},changed{};std::size_t owned_bytes{};};
    inline static unsigned calls{};inline static bool partial{},change_rng{};
    inline static Broker* broker{};
    static Status ProbeMotion(std::uintptr_t,void*,void*,std::size_t,MotionProbeWitness& out) {
        ++calls;out.roots=1;out.meshes=2;out.changed=partial?1:2;
        if(change_rng)++broker->state;
        return {partial?FailureCode::UnsupportedContent:FailureCode::None};
    }
};
struct Sc6ReplayHost {
    enum class InteriorPhase {Holding,Failed,Resumed};
    enum class PauseBoundary {CompletedApplication,SimulationTick};
    enum class RollingPhase {Complete,Correcting};
    enum class TickAdvancePhase {Idle,Failed};
    inline static Sc6ReplayHost* active_{};
    bool failed_{},motion_probe_used_{},engine_ready{true},world_ready{true},arena_ready{true};
    DWORD thread_{GetCurrentThreadId()};
    struct Simulation {struct Continuation {std::uint64_t tick{623};};Continuation continuation(){return {};}} sim,*simulation_{&sim};
    InteriorPhase interior_phase_{InteriorPhase::Holding};PauseBoundary pause_boundary_{PauseBoundary::CompletedApplication};
    void* surface_event_{};void* historical_restore_{};void* checkpoint_restoring_{};bool seek_retirement_pending_{};
    struct Rolling {struct Witness {RollingPhase phase{RollingPhase::Complete};} witness;}rolling_;
    struct TickAdvance {TickAdvancePhase phase{};FailureCode failure{};}tick_advance_;
    Broker broker;Broker* checkpoint_broker_{&broker};
    std::uintptr_t image_base_{};void* manager_{};void* world_{};
    bool engine_idle(){return engine_ready;}bool world_idle(){return world_ready;}bool arena_empty(){return arena_ready;}
    std::size_t AdmissionRemaining(){return 4096;}
    static bool ProbeGroundMotion(std::uint32_t,std::uint64_t,Sc6ReplayGroundDebrisState::MotionProbeWitness*) noexcept;
};
#include "ground_motion_boundary.inl"
int main() {
    using H=Sc6ReplayHost;using G=Sc6ReplayGroundDebrisState;G::MotionProbeWitness witness;
    // Each unsafe boundary must reject before calling the native mover or RNG getter.
    for(unsigned variant=0;variant<16;++variant) {
        H host;H::active_=&host;G::calls=0;unsigned protocol=1;std::uint64_t tick=623;
        switch(variant) {
        case 0:protocol=2;break;case 1:tick=624;break;
        case 2:host.failed_=true;break;case 3:host.motion_probe_used_=true;break;
        case 4:host.thread_=0;break;case 5:host.simulation_=nullptr;break;
        case 6:host.interior_phase_=H::InteriorPhase::Resumed;break;
        case 7:host.pause_boundary_=H::PauseBoundary::SimulationTick;break;
        case 8:host.engine_ready=false;break;case 9:host.world_ready=false;break;
        case 10:host.arena_ready=false;break;case 11:host.surface_event_=&host;break;
        case 12:host.historical_restore_=&host;break;case 13:host.checkpoint_restoring_=&host;break;
        case 14:host.seek_retirement_pending_=true;break;
        case 15:host.rolling_.witness.phase=H::RollingPhase::Correcting;break;
        }
        assert(!H::ProbeGroundMotion(protocol,tick,&witness));assert(!G::calls && !host.broker.calls);
    }
    for(unsigned variant=0;variant<5;++variant) {
        H host;H::active_=&host;G::broker=&host.broker;G::calls=0;
        G::partial=variant==1;G::change_rng=variant==2;
        host.broker.fail_first=variant==3;host.broker.fail_second=variant==4;
        const bool result=H::ProbeGroundMotion(1,623,&witness);
        assert(result==(variant==0));assert(host.motion_probe_used_);
        if(variant==3)assert(!G::calls && host.interior_phase_==H::InteriorPhase::Holding);
        else {
            assert(G::calls==1 && host.broker.calls==2);
            assert(witness.changed==(variant==1?1:2));
            if(variant)assert(host.failed_ && host.interior_phase_==H::InteriorPhase::Failed && host.tick_advance_.phase==H::TickAdvancePhase::Failed);
        }
        assert(!H::ProbeGroundMotion(1,623,&witness));assert(G::calls==(variant==3?0:1));
    }
    H::active_=nullptr;assert(!H::ProbeGroundMotion(1,623,&witness));assert(!H::ProbeGroundMotion(1,623,nullptr));
}
