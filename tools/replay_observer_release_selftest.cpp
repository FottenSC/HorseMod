#include <memory>
#include <chrono>
#include <atomic>
#include <string>
#include <cstring>
#include <cstdint>
#include <optional>
#define STR(x) L##x
enum class LogLevel {Default};
struct Output {template<LogLevel,typename... T>static void send(T&&...) {}};
namespace RC {template<class T>const T& to_generic_string(const T& v){return v;}}
unsigned destroyed{},seek_calls{},resume_calls{};
struct ReplayHost {
 struct Checkpoint {struct {std::uint64_t tick{205};}execution;~Checkpoint(){++destroyed;}};
 enum class SeekAction {Release};
 enum class SeekReleaseResult {Rejected,AcceptedPending,Completed};
 enum class PauseBoundary {CompletedApplication};
 struct SeekWitness {SeekReleaseResult release_result{};unsigned phase{},undo_tick{},checkpoint{};bool pending{};};
 struct InteriorWitness {unsigned tick{210},epoch{},completed_applications{};bool surface_pending{},pending_task{};PauseBoundary boundary{};};
 using SeekObserver=void(*)(void*,const SeekWitness&,const InteriorWitness&);
};
std::shared_ptr<const ReplayHost::Checkpoint> host_pin;
unsigned references_at_release{};
bool seek_export(ReplayHost::SeekAction,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness* out,void*,ReplayHost::SeekObserver) {
 ++seek_calls;
 if(seek_calls==1){references_at_release=host_pin.use_count();host_pin.reset();out->release_result=ReplayHost::SeekReleaseResult::AcceptedPending;out->pending=true;}
 else out->release_result=ReplayHost::SeekReleaseResult::Completed;
 return true;
}
std::uint16_t resume_export(){++resume_calls;return destroyed==1?0:1;}
template<class T>T ResolveHorseModExport(const char* name){return reinterpret_cast<T>(std::strcmp(name,"horsemod_resume_replay_execution")==0?reinterpret_cast<void*>(&resume_export):reinterpret_cast<void*>(&seek_export));}
struct ReplayTrajectorySample {};
bool ReadReplayTrajectory(void*,ReplayTrajectorySample&,bool){return true;}
struct Observer {
 struct Request {bool host_seek{true},historical_single_step{};std::string historical_cancel{"before"},run_id{"fixture"};}request_;
 std::shared_ptr<const ReplayHost::Checkpoint> combat_checkpoint_;
 std::shared_ptr<const ReplayHost::Checkpoint> pair_checkpoint_;
 std::optional<std::uint64_t> host_seek_release_checkpoint_;
 bool host_seek_release_waiting_{},historical_operation_started_{true},interior_started_{true},world_resume_measured_{},host_seek_repeated_{},failed{};
 void* battle_manager_{};
 unsigned host_seek_rewind_ticks_{},historical_rewind_ticks_{},host_seek_rewind_intervals_{},historical_rewind_intervals_{};
 unsigned interior_epoch_{},application_hold_completed_{},world_resume_frame_{},world_resume_viewport_{},world_resume_max_gap_us_{},world_resume_late_gaps_{};
 std::chrono::steady_clock::time_point world_resume_started_,world_resume_previous_;
 std::atomic<unsigned> viewport_frame_count_{};
 void Fail(const char*){failed=true;}
 void LogTrajectorySample(const ReplayTrajectorySample&,int,const wchar_t*){}
 const auto& HostSeekCheckpoint()const{return combat_checkpoint_;}
#include "observer_release.inl"
};
int main(){
 Observer observer;host_pin=std::make_shared<ReplayHost::Checkpoint>();observer.combat_checkpoint_=host_pin;
 ReplayHost::InteriorWitness held;
 observer.ReleaseHostSeekAndResume(held);
 if(references_at_release!=1 || destroyed!=1 || observer.combat_checkpoint_ || resume_calls || observer.failed)return 1;
 if(!observer.host_seek_release_waiting_)return 2;
 if(observer.HostSeekExpectedCheckpoint()!=205)return 4;
 observer.ReleaseHostSeekAndResume(held);
 if(resume_calls!=1 || observer.failed || destroyed!=1 || observer.historical_operation_started_)return 3;
}
