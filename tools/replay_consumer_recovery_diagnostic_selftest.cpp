#include <string>
#include <string_view>
#include <vector>
#include <sstream>
#include <optional>
#include <cstdint>
#define STR(x) x
namespace Horse::Deterministic {
enum class FailureCode { None, UnsupportedContent, UndoFailed };
struct ReplaySeekOwnership { enum class Phase { FailedRecoverable, Recovering }; };
}
namespace ReplayHost {
using Horse::Deterministic::FailureCode;
enum class RestoreOperationPhase { Failed, Recovered };
enum class RestoreOperationAction { Read };
enum class PauseBoundary { CompletedApplication };
struct Checkpoint {};
struct RestoreOperationWitness {
    RestoreOperationPhase phase{RestoreOperationPhase::Failed};
    FailureCode failure{FailureCode::UnsupportedContent};
    const char* participant="execution_consumer_prerequisite_changed";
    bool pending{},original_recovered{},commit_decided{};
    std::uint64_t original_tick{217},target_tick{210},executed_ticks{1},executed_intervals{1};
    std::optional<Horse::Deterministic::ReplaySeekOwnership::Phase> ownership_phase{Horse::Deterministic::ReplaySeekOwnership::Phase::FailedRecoverable};
};
}
enum class LogLevel { Default, Warning };
static std::vector<std::string> logs;
struct Output { template<LogLevel,class... T> static void send(const char* format,T... values) {
    std::ostringstream text;text<<format;((text<<'|'<<values),...);logs.push_back(text.str());
}};
namespace RC { template<class T> auto to_generic_string(T value) { return value; } }
static unsigned reads;
static bool available=true,read_ok=true;
static ReplayHost::RestoreOperationWitness witness;
static bool Read(ReplayHost::RestoreOperationAction,const ReplayHost::Checkpoint*,ReplayHost::RestoreOperationWitness* out) {
    ++reads;*out=witness;return read_ok;
}
template<class T> T ResolveHorseModExport(const char*) { return available?&Read:nullptr; }
enum class Phase { Failed, Recovering };
struct Observer {
    struct { bool consumer_mutation=true;std::string run_id="fixture"; } request_;
    struct { bool injected=true;bool ConsumerMutationInjected(){return injected;} } boundary_observer_;
    bool host_seek_failure_observed_{};Phase host_seek_phase_{};
    unsigned cancellations{};std::string failure;
    void Fail(const char* value){failure=value;}
    bool CancelHostSeek(){++cancellations;return true;}
    struct Seek { Phase phase=Phase::Failed;bool pending=true,commit_decided=false;Horse::Deterministic::FailureCode failure=Horse::Deterministic::FailureCode::UnsupportedContent;std::uint64_t undo_tick=217; };
    struct Held { std::uint64_t tick=211;bool pending_task=false,application_idle=true;ReplayHost::PauseBoundary boundary=ReplayHost::PauseBoundary::CompletedApplication; };
    void Observe(Seek state={},Held held={}) {
#include "consumer_recovery_observer.inl"
    }
};
int main() {
    Observer observer;observer.Observe();
    if(reads!=1 || observer.cancellations!=1 || !observer.failure.empty())return 1;
    witness.phase=ReplayHost::RestoreOperationPhase::Recovered;
    witness.failure=Horse::Deterministic::FailureCode::UndoFailed;
    witness.participant="undo_fresh_owner_retirement";
    witness.ownership_phase=Horse::Deterministic::ReplaySeekOwnership::Phase::Recovering;
    observer.Observe({Phase::Failed,true,false,Horse::Deterministic::FailureCode::UndoFailed});
    if(reads!=2 || observer.cancellations!=1 || observer.failure!="consumer_mutation_recovery_failed")return 81;
    if(logs.back().find("restore_available={}")==std::string::npos || logs.back().find("undo_fresh_owner_retirement")==std::string::npos)return 82;
    if(logs.back().find("|fixture|2|1|0|217|211|0|1|1|1|2|0|0|0|217|210|1|1|1|undo_fresh_owner_retirement")==std::string::npos)return 83;
    for(bool missing:{false,true}) {
        reads=0;available=!missing;read_ok=false;logs.clear();Observer next;next.host_seek_failure_observed_=true;next.Observe();
        if(reads!=(missing?0:1) || next.cancellations || next.failure!="consumer_mutation_recovery_failed" || logs.size()!=1)return 84;
    }
    available=true;read_ok=true;reads=0;logs.clear();Observer pending;pending.host_seek_failure_observed_=true;
    pending.Observe({Phase::Recovering});
    if(reads || pending.cancellations || !pending.failure.empty() || !logs.empty())return 85;
    reads=0;Observer invalid;invalid.boundary_observer_.injected=false;invalid.Observe();
    if(reads || invalid.cancellations || invalid.failure!="consumer_mutation_complete_application_or_B_unproven")return 86;
}
