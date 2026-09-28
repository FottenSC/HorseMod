#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <cstring>
#include "deterministic/Sc6ReplayHost.hpp"
#include "replay_qualification_mod/ReplayIndexCompletion.hpp"
using ReplayHost=Horse::Deterministic::Sc6ReplayHost;
using IndexSeek=ReplayQualification::IndexSeek;
#define STR(x) x
namespace RC {enum class LogLevel{Default};inline const std::string& to_generic_string(const std::string& s){return s;}}
using LogLevel=RC::LogLevel;
struct Output{template<RC::LogLevel L,typename... Args>static void send(Args&&...) {}};
static ReplayHost::SeekWitness receipt;
static std::array<std::uint64_t,12> counters;
static unsigned requests,monitors;static bool read_ok=true;
static bool ReadCounters(std::uint64_t* p,std::size_t n){std::copy_n(counters.begin(),n,p);return true;}
static bool Operation(ReplayHost::SeekAction action,const ReplayHost::Checkpoint*,std::uint64_t,ReplayHost::SeekWitness* out,void*,ReplayHost::SeekObserver){if(action!=ReplayHost::SeekAction::Read)std::abort();*out=receipt;return read_ok;}
static bool Monitor(void*,ReplayHost::PauseMonitor){++monitors;return true;}
static std::uint16_t Request(std::uint64_t){++requests;return 0;}
template<typename F>F ResolveHorseModExport(const char* name){
 if(!std::strcmp(name,"horsemod_get_replay_executor_status"))return reinterpret_cast<F>(&ReadCounters);
 if(!std::strcmp(name,"horsemod_replay_seek_operation"))return reinterpret_cast<F>(&Operation);
 if(!std::strcmp(name,"horsemod_set_replay_pause_monitor"))return reinterpret_cast<F>(&Monitor);
 if(!std::strcmp(name,"horsemod_request_indexed_replay_seek"))return reinterpret_cast<F>(&Request);
 return nullptr;
}
struct ReplayQualificationMod {
 enum class State{Active,Failed};State state_=State::Active;
 struct {unsigned index_sequence=1,index_seek_continuation=120;std::string run_id="test";}request_;
 IndexSeek index_seek_phase_=IndexSeek::ArmingNext;
 std::size_t index_sequence_position_{};
 std::uint64_t index_seek_samples_=120,index_origin_interval_{},boundary_samples_=500;
 struct {unsigned frame=11147;}trajectory_last_;
 std::chrono::steady_clock::time_point index_seek_requested_;
 unsigned failures{},case_logs{};
 std::uint32_t IndexedTarget()const{return ReplayQualification::IndexSequence(request_.index_sequence)[index_sequence_position_];}
 void Fail(const char*){++failures;state_=State::Failed;}
 void ObserveIndexedSeek(const ReplayHost::InteriorWitness&){}
 void LogIndexedSequenceCase(const ReplayHost::InteriorWitness&,std::uint64_t){++case_logs;}
 bool ObserveIndexedSequenceBoundary(const ReplayHost::InteriorWitness&);
};
#include "sequence_boundary_method.inl"
int main(){
 for(unsigned mode=0;mode<10;++mode){
  ReplayQualificationMod driver;receipt={};receipt.phase=ReplayHost::SeekPhase::Idle;
  receipt.release_result=ReplayHost::SeekReleaseResult::Completed;
  counters={};counters[2]=329;counters[3]=329;requests=monitors=0;read_ok=true;
  ReplayHost::InteriorWitness held{};held.tick=329;held.phase=ReplayHost::InteriorPhase::Holding;
  held.application_idle=held.engine_idle=held.world_idle=held.arena_empty=true;
  held.boundary=ReplayHost::PauseBoundary::CompletedApplication;
  if(mode==1)receipt.pending=true;
  if(mode==2)receipt.release_result=ReplayHost::SeekReleaseResult::AcceptedPending;
  if(mode==3)receipt.phase=ReplayHost::SeekPhase::Failed;
  if(mode==4)held.tick=330;
  if(mode==5)held.surface_pending=true;
  if(mode==6)driver.index_seek_phase_=IndexSeek::Seeking;
  if(mode==7)read_ok=false;
  if(mode==8)held.phase=ReplayHost::InteriorPhase::Failed;
  if(mode==9)driver.index_sequence_position_=4;
  if(driver.ObserveIndexedSequenceBoundary(held))return 1;
  if(!mode){
   if(requests!=2 || monitors!=2 || driver.index_sequence_position_!=1 || driver.index_seek_samples_ || driver.failures)return 2;
   driver.ObserveIndexedSequenceBoundary(held);
   if(requests!=2 || monitors!=2 || driver.case_logs!=1)return 3;
  }else if(requests || monitors || driver.index_sequence_position_!=(mode==9?4u:0u) || driver.case_logs)return 10+mode;
 }
 std::puts("Actual sequence boundary rejects pending/failed release; accepted next request exactly once");
}
