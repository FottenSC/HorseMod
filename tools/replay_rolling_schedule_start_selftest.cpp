#include "HorseMod/horselib/deterministic/ReplayRollingCorrections.hpp"
#include <Windows.h>
#include <cassert>
#include <cstdint>
#include <memory>

#define STR(s) s
namespace RC {
enum class LogLevel {Default};
struct Output {template<LogLevel, class... Args> static void send(const char*, Args...) {}};
template<class T> const T& to_generic_string(const T& value) {return value;}
}

using namespace Horse::Deterministic;

struct Sc6ReplayHost {
    enum class RollingPhase {Idle,Advancing,Failed};
    enum class RollingAction {Begin};
    struct RollingWitness {RollingPhase phase=RollingPhase::Idle;FailureCode failure=FailureCode::None;};
    struct Checkpoint {
        std::uint64_t session=1;
        struct {std::int32_t round=0,cursor=0;} source;
        struct {std::uint64_t tick=210;} execution;
        struct Revision {std::uint64_t id{};};
        std::shared_ptr<Revision> input_revision;
    };
    struct Simulation {
        bool SetBoundaryObserver(void*,void(*)()) {return true;}
    } simulation_storage_;
    struct Rolling {
        RollingWitness witness;
        ReplayCorrectionSchedule::Handle schedule;
        ReplayRollingInputHistory input_history;
    } rolling_;
    static Sc6ReplayHost* active_;
    static constexpr std::uint32_t rolling_schedule_protocol=1;
    static void ObserveIndexBoundary() {}
    Simulation* simulation_=&simulation_storage_;
    std::shared_ptr<Checkpoint> owned_=std::make_shared<Checkpoint>();
    DWORD thread_=GetCurrentThreadId();
    unsigned depth_{};
    int operation_mode{}; // 0 success, 1 rejected before start, 2 failed after starting.
    bool schedule_at_start{},history_at_start{},recorded_at_start{};
    std::size_t remaining_at_start{},full_budget=1024*1024;

    Checkpoint* ResolveOwnedCheckpoint(const Checkpoint* image) const {
        return image==owned_.get()?owned_.get():nullptr;
    }
    std::size_t AdmissionRemaining() const {
        const auto charged=rolling_.schedule?rolling_.schedule->owned_bytes():0;
        return full_budget-charged;
    }
    bool RollingOperation(RollingAction,const Checkpoint*,std::uint64_t,RollingWitness*) {
        schedule_at_start=bool(rolling_.schedule);
        history_at_start=rolling_.input_history.Matches(owned_->session,1,owned_->execution.tick);
        remaining_at_start=AdmissionRemaining();
        if(operation_mode==1)return false;
        recorded_at_start=rolling_.input_history.Record(owned_->execution.tick+1,1,0,0).ok();
        if(operation_mode==2) {rolling_.witness.phase=RollingPhase::Failed;return false;}
        rolling_.witness.phase=RollingPhase::Advancing;return true;
    }
    bool BeginRollingSchedule(std::uint32_t,const Checkpoint*,std::uint64_t,
        std::span<const ReplayCorrectionRequest>,RollingWitness*) noexcept;
};
Sc6ReplayHost* Sc6ReplayHost::active_=nullptr;

#include "rolling_schedule_start.inl"

int main() {
    ReplayInputOverride edit{0,0,{1,0},1};
    ReplayCorrectionRequest request{217,1,1,0,{&edit,1}};
    for(int mode=0;mode<3;++mode) {
        Sc6ReplayHost host;
        Sc6ReplayHost::active_=&host;host.operation_mode=mode;
        Sc6ReplayHost::RollingWitness witness;
        const bool began=host.BeginRollingSchedule(1,host.owned_.get(),1,{&request,1},&witness);
        assert(began==(mode==0));
        assert(host.schedule_at_start && host.history_at_start);
        assert(host.remaining_at_start<host.full_budget);
        if(mode==1) {
            assert(!host.rolling_.schedule);
            assert(!host.rolling_.input_history.Matches(1,1,210));
        } else {
            assert(host.recorded_at_start && host.rolling_.schedule);
            assert(host.rolling_.input_history.FirstConsumption(0,0)==211);
        }
    }
    Sc6ReplayHost host;
    Sc6ReplayHost::active_=&host;
    host.owned_->input_revision=std::make_shared<Sc6ReplayHost::Checkpoint::Revision>();
    host.owned_->input_revision->id=4;
    Sc6ReplayHost::RollingWitness witness;
    assert(!host.BeginRollingSchedule(1,host.owned_.get(),1,{&request,1},&witness));
    assert(witness.failure==FailureCode::GenerationMismatch);
    assert(!host.rolling_.schedule);
}
