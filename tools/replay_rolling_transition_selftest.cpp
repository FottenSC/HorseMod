#include <cassert>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <algorithm>
#include <memory>
#include "../HorseMod/horselib/deterministic/ReplayRollingTelemetry.hpp"
#define STR(x) x
namespace RC { enum class LogLevel{Warning};const char* to_generic_string(const char* p){return p;} struct Output {template<LogLevel,typename... T> static void send(T...) {}}; }
enum class FailureCode{None,IllegalTransition,AdvanceFailed,PresentationFailed};
enum class SeekPhase{Restored,Advancing,CompletingApplication};
enum class RollingPhase{Seeking,Failed};
enum class InteriorPhase{Holding,Releasing,Arming,Armed,Resumed,Failed};
enum class TickAdvancePhase{Idle,Releasing,Arming,Advancing,Held,Failed,Stepping,Settling,Settled};
// Modeled pending-owner inputs read by the production observation prefix.
struct Sc6ReplayParticleCopy { static bool capture_retirement_pending(){return false;} };
struct Host {
 struct {bool correcting=true,rebuild_complete=false;
  std::unique_ptr<Horse::Deterministic::ReplayRollingTelemetry> telemetry_storage=std::make_unique<Horse::Deterministic::ReplayRollingTelemetry>();
  Horse::Deterministic::ReplayRollingTelemetry& telemetry=*telemetry_storage;
  struct {std::uint64_t current=217,peak_bytes{};RollingPhase phase=RollingPhase::Seeking;} witness;} rolling_;
 struct {bool surface_pending=false;} held;
 struct {bool cancel=false;struct {SeekPhase phase=SeekPhase::Advancing;FailureCode failure=FailureCode::None;std::uint64_t owned_bytes{};} witness;} seek_;
 struct {TickAdvancePhase phase=TickAdvancePhase::Releasing;FailureCode failure=FailureCode::AdvanceFailed;} tick_advance_;
 bool surface_event_=false,admitted=true,terminal=false;
 bool seek_retirement_pending_=false,seek_retirement_failed_=false;
 unsigned reads{};
 bool AdvanceSeekRetirement(){return true;}
 std::size_t AdmissionBytes(){assert(!particle_command_pending_.load());++reads;return 123;}
 std::atomic<bool> particle_command_pending_=false;
 InteriorPhase interior_phase_=InteriorPhase::Releasing;
 bool HistoricalExecutionAdmitted(){return admitted;}
 bool FailRolling(FailureCode,const char*){terminal=true;return false;}
 bool Drive(){
#include "rolling_transition.inl"
 return false;
 }
 void ReadSeek(){auto& witness=seek_.witness;const auto fail=[](FailureCode){};
#include "seek_accounting_entry.inl"
 }
 void ReadRolling(){
#include "rolling_accounting_entry.inl"
 }
};
int main(){
 Host queued;queued.particle_command_pending_=true;queued.ReadSeek();queued.ReadRolling();assert(queued.reads==0);
 queued.particle_command_pending_=false;queued.ReadSeek();queued.ReadRolling();assert(queued.reads==2);
 for(auto phase:{TickAdvancePhase::Releasing,TickAdvancePhase::Arming,TickAdvancePhase::Advancing,TickAdvancePhase::Stepping,TickAdvancePhase::Settling}) {
  Host h;h.tick_advance_.phase=phase;assert(h.Drive());assert(!h.seek_.cancel && !h.terminal);
  h.interior_phase_=InteriorPhase::Holding;assert(h.Drive());assert(!h.seek_.cancel && !h.terminal);
 }
 Host ready;ready.tick_advance_.phase=TickAdvancePhase::Held;ready.interior_phase_=InteriorPhase::Holding;
 assert(!ready.Drive() && !ready.seek_.cancel && !ready.terminal);
 Host failed;failed.tick_advance_.phase=TickAdvancePhase::Failed;assert(failed.Drive());
 assert(failed.seek_.cancel && failed.seek_.witness.failure==FailureCode::AdvanceFailed && !failed.terminal);
 Host invalid;invalid.tick_advance_.phase=TickAdvancePhase::Held;invalid.admitted=false;
 assert(invalid.Drive() && invalid.seek_.cancel && !invalid.terminal);
}
