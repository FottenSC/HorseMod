#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <memory>
namespace Horse::Deterministic {
enum class FailureCode {IllegalTransition,CapacityExceeded,CapturePreflightFailed,RestorePreflightFailed,GenerationMismatch};
struct Status {bool good;bool ok()const{return good;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
struct Sc6ReplayObjectLease {
 bool alive{true};static inline bool reject_release{};
 Status Release(){if(reject_release)return {false};alive=false;return {true};}
 Status ValidateObject(void* p)const{return {p && alive};}
 bool registered()const{return false;} Status Validate()const{return {alive};}
 std::size_t owned_bytes()const{return 128;}
};
// Native material capture/creation is a controlled dependency of this owner-
// lifetime fixture. Its actual graph and failure paths have separate production
// tests; this fixture exercises recipe-independent handle/lease retirement.
struct ReplayParticleMaterialGraph {
 unsigned materials{};bool ValuesEqual(const ReplayParticleMaterialGraph&)const{return true;}
 std::size_t owned_bytes()const{return sizeof(*this);}
};
struct Sc6ReplayColdParticleOwner {
 struct Witness {const char* check{};
std::uintptr_t source{},copy{};bool created{},destroyed{},native_construction_entered{};unsigned payload{},serial{};};
 static Status CaptureMaterials(std::uintptr_t,void*,std::size_t,Sc6ReplayObjectLease&,ReplayParticleMaterialGraph&,Witness&){return Status::success();}
 static bool ConstructConfiguration(std::uintptr_t base,void* source,Sc6ReplayObjectLease& lease,Witness& w,const ReplayParticleMaterialGraph&){return ConstructDetached(base,source,lease,w);}
 static bool RealizeMaterials(std::uintptr_t,const ReplayParticleMaterialGraph&,Sc6ReplayObjectLease&,Witness&){return true;}
 static inline unsigned payload{},serial=1;
 static bool EquivalentDetached(std::uintptr_t,const Witness& a,const Sc6ReplayObjectLease& al,const Witness& b,const Sc6ReplayObjectLease& bl) {
  return al.alive && bl.alive && a.source==b.source && a.payload==b.payload && a.serial==b.serial;
 }
 static inline unsigned constructions{},destructions{},clones{};static inline bool preflight_failure{},uncertain_failure{};
 static bool ConstructDetached(std::uintptr_t,void* p,Sc6ReplayObjectLease&,Witness& w){
  ++constructions;w.source=reinterpret_cast<std::uintptr_t>(p);w.payload=payload;w.serial=serial;
  if(preflight_failure)return false;
  w.native_construction_entered=true;if(uncertain_failure)return false;
  w.created=true;w.copy=100;return true;
 }
 static bool CloneDetached(std::uintptr_t,const Witness& source,const Sc6ReplayObjectLease& retained,Sc6ReplayObjectLease&,Witness& w){
  ++clones;if(!retained.ValidateObject(reinterpret_cast<void*>(source.copy)).ok())return false;
  w.created=true;w.copy=200;return true;
 }
 static bool Retire(std::uintptr_t,Sc6ReplayObjectLease& lease,Witness& w){
  if(!w.destroyed){++destructions;w.destroyed=true;}
  return lease.Release().ok();
 }
};
}
#include "particle_configuration.inl"
using namespace Horse::Deterministic;
struct CaptureHost {
 struct Checkpoint {bool valid=true;unsigned session=1;std::vector<Sc6ReplayParticleConfiguration::Handle> particle_configurations;};
 std::uintptr_t image_base_=1;unsigned checkpoint_session_=1;
 bool historical_restore_{};
 std::vector<std::weak_ptr<Checkpoint>> retained_checkpoints_;
 std::size_t AdmissionRemaining(Checkpoint*)const{return 16*1024*1024;}
 Sc6ReplayParticleConfiguration::Handle CaptureOne(std::uintptr_t source) {
  Checkpoint output;output.particle_configurations.emplace_back();auto& host=*this;
#include "particle_configuration_capture_route.inl"
  if(!status.ok())return {};
  return output.particle_configurations.back();
 }
};
int ReuseCheck() {
 using Owner=Sc6ReplayParticleConfiguration;using Native=Sc6ReplayColdParticleOwner;
 CaptureHost host;auto saved=std::make_shared<CaptureHost::Checkpoint>();
 auto a=host.CaptureOne(2);saved->particle_configurations.push_back(a);host.retained_checkpoints_.push_back(saved);
 auto b=host.CaptureOne(2);
 if(b!=a || Native::constructions!=2 || !Owner::retirement_pending())return 1;
 if(!Owner::RetirePending(1).ok() || !a->Validate().ok() || Native::destructions!=1)return 2;
 host.historical_restore_=true;auto private_b=host.CaptureOne(2);if(private_b==a)return 9;
 host.historical_restore_=false;private_b.reset();if(!Owner::RetirePending(1).ok())return 10;
 ++Native::payload;auto changed=host.CaptureOne(2);if(changed==a)return 3;
 Native::payload=0;++Native::serial;auto replaced=host.CaptureOne(2);if(replaced==a)return 4;
 Native::serial=1;auto foreign=host.CaptureOne(3);if(foreign==a)return 5;
 for(unsigned i=0;i<3;++i){auto again=host.CaptureOne(2);if(again!=a || !Owner::RetirePending(1).ok())return 6;}
 saved->valid=false;auto invalid=host.CaptureOne(2);if(invalid==a)return 7;
 invalid.reset();foreign.reset();replaced.reset();changed.reset();b.reset();a.reset();saved.reset();
 if(!Owner::RetirePending(1).ok() || Owner::pending_bytes())return 8;
 Native::constructions=Native::destructions=Native::clones=0;
 return 0;
}
int main(){
 if(const auto failed=ReuseCheck()){std::printf("configuration reuse failure %d\n",failed);return 100+failed;}
 using namespace Horse::Deterministic;using Owner=Sc6ReplayParticleConfiguration;using Native=Sc6ReplayColdParticleOwner;
 Owner::Handle a;const std::size_t budget=2*1024*1024;
 if(Owner::Capture(1,reinterpret_cast<void*>(2),1,a).ok() || a || Native::constructions)return 1;
 if(!Owner::Capture(1,reinterpret_cast<void*>(2),budget,a).ok() || !a || !a->Validate().ok())return 2;
 auto b=a;auto charged=a->owned_bytes();Sc6ReplayObjectLease clone_lease;Native::Witness clone;
 if(!a->Clone(clone_lease,clone).ok() || Native::clones!=1 || Native::destructions)return 3;
 a.reset();if(Owner::retirement_pending())return 4;b.reset();
 if(!Owner::retirement_pending() || Owner::pending_bytes()!=charged || Native::destructions)return 5;
 if(Owner::RetirePending(3).ok() || Native::destructions || Owner::pending_bytes()!=charged)return 6;
 Sc6ReplayObjectLease::reject_release=true;
 if(Owner::RetirePending(1).ok() || Native::destructions!=1 || Owner::pending_bytes()!=charged)return 7;
 Sc6ReplayObjectLease::reject_release=false;
 if(!Owner::RetirePending(1).ok() || Native::destructions!=1 || Owner::pending_bytes() || Owner::retirement_pending())return 8;
 if(!Owner::RetirePending(1).ok() || Native::destructions!=1)return 9;
 Native::preflight_failure=true;
 if(Owner::Capture(1,reinterpret_cast<void*>(2),budget,a).ok() || !a)return 10;
 a.reset();if(!Owner::RetirePending(1).ok() || Native::destructions!=1)return 11;
 Native::preflight_failure=false;Native::uncertain_failure=true;
 if(Owner::Capture(1,reinterpret_cast<void*>(2),budget,a).ok() || !a)return 12;
 a.reset();if(Owner::RetirePending(1).ok() || !Owner::retirement_pending() || Native::destructions!=1)return 13;
 std::puts("configuration handles retain partial native ownership, clone independently and defer retirement until an admitted owner drain");
}
