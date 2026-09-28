#include <cstdint>
#include <array>
#include <vector>
#include <sstream>
#include <string>
#define STR(x) x
enum class FailureCode { None, GenerationMismatch, UndoFailed };
struct Status { FailureCode code{};bool ok()const{return code==FailureCode::None;} static Status success(){return{};} static Status failure(FailureCode c){return{c};} };
static std::vector<std::string> logs;
namespace RC { enum class LogLevel { Warning };template<class T>auto to_generic_string(T value){return value;}
struct Output {template<LogLevel,class... T>static void send(const char* format,T... args){std::ostringstream s;s<<format;((s<<'|'<<args),...);logs.push_back(s.str());}}; }
static void* slots[2];static void* header_slots=slots;static int count=2,capacity=2,reads{},writes{};
struct Native {template<class T>static T Read(std::uintptr_t,std::uintptr_t offset){++reads;if constexpr(sizeof(T)==8)return reinterpret_cast<T>(header_slots);else return offset==0xa58?count:capacity;}
static bool SetPrivateEmitterHeader(std::uintptr_t,std::array<std::byte,16>){++writes;return true;}};
struct Owner {
    struct Lease {bool valid=true;Status ValidateObject(void*){return valid?Status::success():Status::failure(FailureCode::GenerationMismatch);}} lease;
    struct {std::uintptr_t copy=0x1234;}cold;
    struct Gpu {std::size_t ordinal;struct {void* root;}owner;};
    std::vector<Gpu> gpus{{1,{reinterpret_cast<void*>(0x4321)}}};
    void* slots=::slots;std::size_t count=2,slot_bytes=16;bool slots_installed=true,settled=true,native_dead=true,retired=false;
};
static Status Check(Owner& owner) {
#include "prepared_owner_slots.inl"
    return Status::success();
}
enum class RestoreOperationPhase { Recovered,Failed };enum class InteriorPhase { Holding,Failed };
struct Sc6ReplayParticleCopy { enum class Phase { Recovered,Released };struct {Phase phase=Phase::Recovered;}witness_;
    bool registry_prepared_{},registry_executing_{},execution_started_=true,execution_settled_{};
    bool execution_started(){return execution_started_;}
    void Finish(){
#include "prepared_owner_finish.inl"
    }
};
struct Host {
    Sc6ReplayParticleCopy copy_owner;Sc6ReplayParticleCopy* particle_copy_=&copy_owner;
    struct {bool execution=true;}transaction;
    struct {bool pending{};FailureCode failure{};const char* participant{};RestoreOperationPhase phase=RestoreOperationPhase::Recovered;}operation;
    InteriorPhase interior_phase_=InteriorPhase::Holding;int retirements{};bool pending_result{},failed{};
    Status RetirePreparedFreshParticles(bool& pending){++retirements;pending=pending_result;return failed?Status::failure(FailureCode::GenerationMismatch):Status::success();}
    void Poll(){auto copy=copy_owner.witness_;
#include "prepared_owner_gate.inl"
    }
};
int main(int argc,char** argv) {
    if(argc>1 && std::string(argv[1])=="gate") {
        Host executed;executed.Poll();if(executed.retirements)return 1;
        executed.copy_owner.Finish();executed.Poll();if(executed.retirements)return 81;
        Host cold;cold.copy_owner.execution_started_=false;cold.pending_result=true;cold.Poll();
        if(cold.retirements!=1 || !cold.operation.pending)return 2;
        cold.pending_result=false;cold.copy_owner.Finish();cold.Poll();if(cold.retirements!=1)return 3;
        Host invalid;invalid.copy_owner.execution_started_=false;invalid.failed=true;invalid.Poll();
        if(invalid.retirements!=1 || invalid.operation.phase!=RestoreOperationPhase::Failed || invalid.interior_phase_!=InteriorPhase::Failed)return 4;
        return 0;
    }
    for(int mode=0;mode<7;++mode) {
        logs.clear();reads=writes=0;header_slots=slots;count=capacity=2;slots[0]=nullptr;slots[1]=reinterpret_cast<void*>(0x4321);Owner owner;
        if(mode==1)owner.lease.valid=false;
        if(mode==2)header_slots=reinterpret_cast<void*>(0x9999);
        if(mode==3)count=1;
        if(mode==4)capacity=4;
        if(mode==5)slots[0]=reinterpret_cast<void*>(0x5555);
        if(mode==6)slots[1]=reinterpret_cast<void*>(0x6666);
        const auto result=Check(owner);
        if(!mode){if(!result.ok() || !logs.empty() || writes!=1 || owner.slots_installed)return 5;continue;}
        if(result.code!=FailureCode::GenerationMismatch || writes || !owner.slots_installed)return 6;
        if(mode==1 && reads)return 7;
        if(logs.size()!=1)return 82;
        const char* predicates[]={"","lease","header_slots","header_count","header_capacity","slot_root","slot_root"};
        if(logs[0].find(std::string("|")+predicates[mode]+"|")==std::string::npos)return 83;
        if(logs[0].find("observed={}")==std::string::npos || logs[0].find("expected={}")==std::string::npos)return 84;
    }
}
