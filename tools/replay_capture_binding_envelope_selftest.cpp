#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <array>
#include <vector>
#define CHECK(x) do {if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct FrameCoordinate {std::uint64_t generation{},tick{};};
enum class FailureCode {ContextUnavailable,GenerationMismatch,CapacityExceeded};
struct Status {bool valid=true;bool ok()const{return valid;}static Status success(){return{};}static Status failure(FailureCode){return{false};}};
struct MotionBankSnapshot {static std::size_t BindAllocationEnvelopeBytes(){return 60*1024*1024;}};
struct Snapshot {FrameCoordinate coordinate;};
struct Scratch {int wind{};};
struct CandidateCheckpointCodec {static inline unsigned decodes{};static Status Decode(const Snapshot&,Scratch&){++decodes;return{};}};
struct WindTransaction {
 struct Address {std::uintptr_t base,size,root;std::uint64_t generation;};
 unsigned prepares{};std::size_t available{};
 std::size_t AllocationEnvelopeBytes()const{return 1024;}
 Status Prepare(Address,int,bool,std::size_t budget){++prepares;available=budget;return{};}
};
struct Sc6CandidateCheckpointCapture {
 struct Regions {bool bound=true;bool IsBound()const{return bound;}} regions;
 Regions* regions_=&regions;
 std::uintptr_t image_base_=1,bound_manager_=2;
 std::uint64_t bound_session_generation_=3,bound_round_generation_=4;
 unsigned binds{};
 WindTransaction wind;WindTransaction* wind_transaction_=&wind;Scratch scratch;Scratch* auxiliary_decode_scratch_=&scratch;
 std::uintptr_t image_size_=4096;static constexpr std::uintptr_t wind_root_pointer_rva=32;
 Status PrepareEnclosingWind(const Snapshot&,std::size_t)noexcept;
 static std::size_t transient_allocation_envelope_bytes(){return 89821158;}
 static std::size_t BoundAllocationEnvelopeBytes()noexcept;
 bool RequiresCaptureBinding(std::uintptr_t,FrameCoordinate,std::uint64_t)const noexcept;
 std::size_t CaptureAllocationEnvelopeBytes(std::uintptr_t,FrameCoordinate,std::uint64_t)const noexcept;
 Status BindForCanonicalCapture(std::uintptr_t,FrameCoordinate,std::uint64_t,std::uint32_t)noexcept;
 Status bind(std::uintptr_t,FrameCoordinate,std::uint64_t,std::uint32_t){++binds;return Status::success();}
};
#include "capture_binding_methods.inl"
struct Host {
 Sc6CandidateCheckpointCapture* checkpoint_capture_;
 void* manager_=reinterpret_cast<void*>(2);std::uint64_t checkpoint_session_=3;
 struct Output {struct {std::uint64_t tick=217;} execution; };
 std::size_t envelope() {
 Output output;
 // Host uses its checkpoint session as the capture generation.
 checkpoint_capture_->bound_round_generation_=checkpoint_session_;
#include "capture_binding_admission.inl"
 }
};
namespace CompleteBAccounting {
struct Bytes {std::size_t count{};std::size_t owned_bytes()const{return count;}};
using Sc6ReplayParticleConfiguration=Bytes;
using Sc6ReplayGroundDebrisState=Bytes;
struct LocalReconstructionImage {std::vector<unsigned char> bytes;};
struct Sc6ReplayParticleCopy {static std::size_t captured_bytes(std::size_t n){return n;}};
struct Sc6ReplayHost {
 struct SurfaceSnapshot {struct {std::size_t bytes;} image;};
 struct Checkpoint {
  struct {std::vector<unsigned char> bytes;std::vector<LocalReconstructionImage> local_images;} gameplay;
  Bytes world,scheduler,vfx;
  std::shared_ptr<Bytes> input_revision,hud,traces,vfx_handler,source_registration,ground;
  std::vector<std::shared_ptr<Bytes>> particle_configurations;
  std::shared_ptr<SurfaceSnapshot> surface;std::size_t gpu{};
  std::size_t owned_bytes()const noexcept;
 };
 struct Operation {Checkpoint undo;std::shared_ptr<Checkpoint> target;unsigned other[7];};
 Operation* historical_restore_{};
 std::vector<std::shared_ptr<Checkpoint>> extra;
 std::size_t Charge(bool undo_counted,bool target_counted,const Checkpoint* pending) {
  std::size_t total{};bool pending_counted=false;
  const auto add=[&](std::size_t n){total+=n;};
#include "complete_b_checkpoint_charge.inl"
  for(const auto& c:extra)checkpoint_bytes(*c);
  if(undo_counted)checkpoint_bytes(historical_restore_->undo);
  if(target_counted)checkpoint_bytes(*historical_restore_->target);
#include "complete_b_admission.inl"
  return total;
 }
};
#include "complete_b_owned.inl"
void Run() {
 Sc6ReplayHost::Operation op;Sc6ReplayHost host{&op};
 op.undo.gameplay.bytes.reserve(4096);op.undo.hud=std::make_shared<Bytes>(Bytes{8192});
 op.undo.ground=std::make_shared<Bytes>(Bytes{4096});
 op.target=std::make_shared<Sc6ReplayHost::Checkpoint>();op.target->gameplay.bytes.reserve(2048);
 const auto expected=sizeof(op)+(op.undo.owned_bytes()-sizeof(op.undo))+op.target->owned_bytes()
  +sizeof(std::array<const Sc6ReplayHost::SurfaceSnapshot*,32>)+sizeof(std::size_t)
  +sizeof(std::array<const Sc6ReplayParticleConfiguration*,128>)+sizeof(std::size_t);
 for(bool registered_b:{false,true})for(bool registered_a:{false,true})
  for(const auto* pending:{&op.undo,op.target.get()})
   CHECK(host.Charge(registered_b,registered_a,pending)==expected);
 // The full B dynamic state remains charged; no lifecycle or pin is changed.
 op.undo.hud->count+=16384;
 CHECK(host.Charge(false,false,nullptr)==expected+16384);
 op.undo.ground->count+=32768;
 CHECK(host.Charge(false,false,nullptr)==expected+16384+32768);
 op.undo.surface=std::make_shared<Sc6ReplayHost::SurfaceSnapshot>();op.undo.surface->image.bytes=65536;
 op.target->surface=std::make_shared<Sc6ReplayHost::SurfaceSnapshot>();op.target->surface->image.bytes=65536;
 const auto distinct=host.Charge(false,false,nullptr);
 op.target->surface=op.undo.surface;
 for(bool registered_b:{false,true})for(bool registered_a:{false,true})
  CHECK(host.Charge(registered_b,registered_a,nullptr)==distinct-sizeof(Sc6ReplayHost::SurfaceSnapshot)-65536);
 CHECK(op.undo.surface.use_count()==2); // Accounting never removes either owner.
 const auto shared=host.Charge(false,false,nullptr);
 std::size_t added{};
 for(unsigned i=0;i<33;++i) {
  auto c=std::make_shared<Sc6ReplayHost::Checkpoint>();
  c->surface=std::make_shared<Sc6ReplayHost::SurfaceSnapshot>();c->surface->image.bytes=256;
  added+=c->owned_bytes();host.extra.push_back(std::move(c));
 }
 // Table exhaustion may overcount, but must never omit owned images. Repeated
 // calls rebuild the ledger and retain exactly the same ownership.
 CHECK(host.Charge(false,false,nullptr)>=shared+added);
 host.extra.clear();CHECK(host.Charge(false,false,nullptr)==shared);
 auto first=std::make_shared<Bytes>(Bytes{1048576}),second=std::make_shared<Bytes>(*first);
 op.undo.particle_configurations.push_back(first);op.target->particle_configurations.push_back(second);
 const auto separate_configs=host.Charge(false,false,nullptr);
 op.target->particle_configurations[0]=first;
 CHECK(host.Charge(false,false,nullptr)==separate_configs-first->owned_bytes());
 CHECK(first.use_count()==3); // Both checkpoint leases and the test owner survive.
 const auto shared_configs=host.Charge(false,false,nullptr);added=0;
 for(unsigned i=0;i<129;++i) {
  auto c=std::make_shared<Sc6ReplayHost::Checkpoint>();
  c->particle_configurations.push_back(std::make_shared<Bytes>(Bytes{1024}));
  added+=c->owned_bytes();host.extra.push_back(std::move(c));
 }
 CHECK(host.Charge(false,false,nullptr)>=shared_configs+added);
 host.extra.clear();CHECK(host.Charge(false,false,nullptr)==shared_configs);
}
}
int main(){
 CompleteBAccounting::Run();
 Sc6CandidateCheckpointCapture c;Host h{&c};
 const auto full=c.transient_allocation_envelope_bytes();
 const auto steady=h.envelope();
 CHECK(steady==full-MotionBankSnapshot::BindAllocationEnvelopeBytes());
 CHECK(steady<=69233874); // Retained native failure's available bytes.
 CHECK(c.BindForCanonicalCapture(2,{3,217},3,1).ok() && c.binds==0);
 c.regions.bound=false;CHECK(h.envelope()==full);
 CHECK(c.BindForCanonicalCapture(2,{3,217},3,1).ok() && c.binds==1);
 c.regions.bound=true;c.bound_manager_=99;CHECK(h.envelope()==full);
 CHECK(c.BindForCanonicalCapture(2,{3,217},3,1).ok() && c.binds==2);
 c.bound_manager_=2;c.bound_session_generation_=99;CHECK(h.envelope()==full);
 CHECK(c.BindForCanonicalCapture(2,{3,217},3,1).ok() && c.binds==3);
 c.bound_session_generation_=3;
 // Direct method checks generation changes independently of the host setup.
 CHECK(c.CaptureAllocationEnvelopeBytes(2,{99,217},3)==full);
 CHECK(c.BindForCanonicalCapture(2,{99,217},3,1).ok() && c.binds==4);
 CHECK(!c.BindForCanonicalCapture(0,{3,217},3,1).ok() && c.binds==4);
 CHECK(!c.BindForCanonicalCapture(2,{3,217},3,0).ok() && c.binds==4);
 const Snapshot snapshot{{c.bound_round_generation_,210}};
 const auto decode=full-MotionBankSnapshot::BindAllocationEnvelopeBytes();
 CHECK(c.PrepareEnclosingWind(snapshot,decode+1024).ok());
 CHECK(c.wind.prepares==1 && c.wind.available==1024 && CandidateCheckpointCodec::decodes==1);
 CHECK(!c.PrepareEnclosingWind(snapshot,decode+1023).ok());
 CHECK(c.wind.prepares==1 && CandidateCheckpointCodec::decodes==1);
 CHECK(!c.PrepareEnclosingWind({{99,210}},full+1024).ok());
 CHECK(c.wind.prepares==1 && CandidateCheckpointCodec::decodes==1);
 std::puts("Production capture admission, binding and wind decode envelope passed");
}
