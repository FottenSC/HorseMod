#include <array>
#include <memory>
#include <cstdint>
#include <cassert>
#include <chrono>
#include "HorseMod/horselib/deterministic/ReplaySourceState.hpp"
#include "HorseMod/horselib/deterministic/ReplayRollingTelemetry.hpp"
using Horse::Deterministic::ReplayRollingTelemetry;
#include "HorseMod/horselib/deterministic/ReplayStatePolicy.hpp"
using Horse::Deterministic::ReplaySourceState;
namespace ReplayStatePolicy=Horse::Deterministic::ReplayStatePolicy;
enum class FailureCode {GenerationMismatch,IllegalTransition};
struct Sc6ReplayHost {
 enum class RollingPhase {Advancing,Capturing};
 enum class CaptureAction {Begin};
 struct CaptureWitness {FailureCode failure=FailureCode::IllegalTransition;};
 enum class PauseBoundary {SimulationTick,CompletedApplication};
 struct Checkpoint {
  ReplayStatePolicy::Stamp state_policy=ReplayStatePolicy::current;
  bool valid=true,supported=true;std::uint64_t session=1;
  struct {std::uint64_t tick;} execution;
  PauseBoundary boundary=PauseBoundary::CompletedApplication;void* task{};void* event{};
  ReplaySourceState source;std::shared_ptr<int> input_revision;
  bool historical_target_shape_supported()const{return supported;}
 };
 using CheckpointHandle=std::shared_ptr<const Checkpoint>;
 std::size_t remaining{};
 std::size_t AdmissionRemaining()const{return remaining;}
 bool HasRollingReplacementCapacity(std::size_t)const noexcept;
 bool ValidateRollingReplacement()const noexcept;
 bool CommitRollingReplacement()noexcept;
 struct {struct {std::uint64_t current,checkpoint_generation=1,source_revision{},corrections_committed{};RollingPhase phase=RollingPhase::Advancing;} witness;std::array<CheckpointHandle,8> checkpoints,replacement;std::array<std::uint64_t,8> generations{},replacement_generations{};std::shared_ptr<int> proposed_revision;std::uint64_t proposed_revision_id{};std::size_t next_correction{};bool correcting{},correction_installed{},rebuild_complete{};int input_history{},replacement_history{};ReplaySourceState source;std::shared_ptr<int> revision;std::chrono::steady_clock::time_point forward_started{};std::uint64_t forward_us{};ReplayRollingTelemetry telemetry;} rolling_;
 enum class SeekReleaseResult {Rejected,Completed};struct {struct {SeekReleaseResult release_result=SeekReleaseResult::Rejected;} witness;} seek_;
 std::uint64_t checkpoint_session_=1;bool failed{};
 bool FailRolling(FailureCode,const char*) {failed=true;return false;}
 bool StoreRollingCheckpoint(CheckpointHandle) noexcept;
 bool historical_restore_{},checkpoint_restoring_{},seek_retirement_pending_{},seek_retirement_failed_{},idle=true;
 CheckpointHandle released_seek_checkpoint_;unsigned captures{};
 bool CanRetireSeekCheckpoint()const{return idle;}
 bool PrepareRollingCheckpointSlot()noexcept;
 bool CaptureOperation(CaptureAction,CaptureWitness*){++captures;return true;}
 bool StartCapture(std::uint64_t tick) {
  auto& r=rolling_;auto& w=r.witness;struct {std::uint64_t tick;} held{tick};
#include "rolling_capture_entry.inl"
 }

};
#include "rolling_window.inl"
int main(){
 Sc6ReplayHost host;
 host.remaining=8*56*1024*1024-1;assert(!host.HasRollingReplacementCapacity(56*1024*1024));
 ++host.remaining;assert(host.HasRollingReplacementCapacity(56*1024*1024));
 assert(!host.HasRollingReplacementCapacity(SIZE_MAX));
 assert(!host.HasRollingReplacementCapacity(0));
host.rolling_.source={1,2,3,1,1,0,0,1};
 const auto image=[&](std::uint64_t tick){auto p=std::make_shared<Sc6ReplayHost::Checkpoint>();p->execution.tick=tick;p->source=host.rolling_.source;return p;};
 // The ninth capture must wait until the obsolete ring reference has been
 // handed to the existing deferred-retirement owner. External pins survive.
 for(unsigned tick=210;tick<=217;++tick){host.rolling_.witness.current=tick;assert(host.StoreRollingCheckpoint(image(tick)));}
 auto pinned=host.rolling_.checkpoints[210%8];std::weak_ptr<const Sc6ReplayHost::Checkpoint> obsolete=pinned;
 host.StartCapture(218);
 assert(host.captures==0 && host.seek_retirement_pending_ && host.released_seek_checkpoint_==pinned);
 assert(!host.rolling_.checkpoints[210%8]);
 host.StartCapture(218);assert(host.captures==0); // Still awaiting completion.
 host.released_seek_checkpoint_.reset();host.seek_retirement_pending_=false;
 assert(!obsolete.expired()); // Native/GPU/external pins are not stolen.
 host.StartCapture(218);assert(host.captures==1);
 assert(host.StoreRollingCheckpoint(image(218)));
 for(unsigned i=211;i<=218;++i)assert(host.rolling_.checkpoints[i%8]->execution.tick==i);
 pinned.reset();assert(obsolete.expired());
 host.rolling_.checkpoints={};host.captures=0;
 host.rolling_.witness.current=218;host.rolling_.checkpoints[218%8]=image(210);
 const auto protected_slot=host.rolling_.checkpoints[218%8];
 host.historical_restore_=true;host.StartCapture(218);
 assert(host.captures==0 && host.rolling_.checkpoints[218%8]==protected_slot && !host.seek_retirement_pending_);
 host.historical_restore_=false;host.failed=false;host.seek_retirement_failed_=true;host.StartCapture(218);
 assert(host.captures==0 && host.rolling_.checkpoints[218%8]==protected_slot);
 host.seek_retirement_failed_=false;host.failed=false;host.rolling_.checkpoints={};
 std::shared_ptr<const Sc6ReplayHost::Checkpoint> operation_pin;std::weak_ptr<const Sc6ReplayHost::Checkpoint> first;
 for(unsigned tick=210;tick<242;++tick){
  host.rolling_.witness.current=tick;auto p=image(tick);
  if(tick==210){operation_pin=p;first=p;}
  assert(host.StoreRollingCheckpoint(p));
  if(tick>=217)for(unsigned offset=0;offset<8;++offset)assert(host.rolling_.checkpoints[(tick-offset)%8]->execution.tick==tick-offset);
 }
 assert(!first.expired());operation_pin.reset();assert(first.expired());
 host.rolling_.witness.current=242;
 auto stale=image(242);++stale->state_policy.version;
 assert(!host.StoreRollingCheckpoint(stale));
 stale=image(242);stale->state_policy.digest^=1;
 assert(!host.StoreRollingCheckpoint(stale));
 stale=image(242);stale->state_policy={};
 assert(!host.StoreRollingCheckpoint(stale));
 auto wrong=image(242);wrong->session=2;assert(!host.StoreRollingCheckpoint(wrong));
 wrong=image(242);wrong->source.round=1;assert(!host.StoreRollingCheckpoint(wrong));
 wrong=image(242);wrong->input_revision=std::make_shared<int>(1);assert(!host.StoreRollingCheckpoint(wrong));
 wrong=image(242);wrong->task=reinterpret_cast<void*>(1);assert(!host.StoreRollingCheckpoint(wrong));
 wrong=image(242);wrong->valid=false;assert(!host.StoreRollingCheckpoint(wrong));
 host.rolling_.witness.current=250;assert(!host.StoreRollingCheckpoint(image(250))); // A missed turn cannot silently replace a ring slot.
 assert(!host.ValidateRollingReplacement());
 assert(!host.CommitRollingReplacement());
 host.rolling_.witness.current=217;host.rolling_.checkpoints={};
 for(unsigned tick=210;tick<=217;++tick){auto p=image(tick);host.rolling_.checkpoints[tick%8]=p;}
 auto old=host.rolling_.checkpoints;
 auto revision=std::make_shared<int>(9);host.rolling_.proposed_revision=revision;host.rolling_.proposed_revision_id=9;
 host.rolling_.correcting=host.rolling_.correction_installed=host.rolling_.rebuild_complete=true;
 for(unsigned tick=210;tick<=217;++tick){auto p=image(tick);p->input_revision=revision;host.rolling_.replacement[tick%8]=p;host.rolling_.replacement_generations[tick%8]=2;}
 assert(host.ValidateRollingReplacement());
 auto stale_replacement=std::const_pointer_cast<Sc6ReplayHost::Checkpoint>(host.rolling_.replacement[0]);
 stale_replacement->state_policy.digest^=1;
 assert(!host.ValidateRollingReplacement());
 stale_replacement->state_policy=ReplayStatePolicy::current;
 assert(!host.CommitRollingReplacement() && host.rolling_.checkpoints==old);
 host.seek_.witness.release_result=Sc6ReplayHost::SeekReleaseResult::Completed;
 host.historical_restore_=true;assert(!host.CommitRollingReplacement() && host.rolling_.checkpoints==old);
 host.historical_restore_=false;
 host.rolling_.replacement_generations[0]=1;assert(!host.CommitRollingReplacement() && host.rolling_.checkpoints==old);
 host.rolling_.replacement_generations[0]=2;
 auto corrected=host.rolling_.replacement;
 assert(host.CommitRollingReplacement());
 assert(host.rolling_.checkpoints==corrected && host.rolling_.revision==revision && host.rolling_.witness.checkpoint_generation==2);
 assert(host.seek_retirement_pending_ && !host.rolling_.correcting);
 assert(!host.CommitRollingReplacement()); // No double publication/retirement.

}
