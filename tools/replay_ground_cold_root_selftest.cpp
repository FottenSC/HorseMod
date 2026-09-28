#include <Windows.h>
#include <array>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <utility>
#include "../HorseMod/horselib/deterministic/ReplayGroundPose.hpp"
#include "../HorseMod/horselib/deterministic/ReplayGroundPrivateBody.hpp"
#include "../HorseMod/horselib/deterministic/ReplayGroundPrivateQueues.hpp"
#include "../HorseMod/horselib/deterministic/ReplayGroundInitialBodyState.hpp"
using Horse::Deterministic::ReplayGroundPose;
using Horse::Deterministic::ReplayGroundPrivateBody;
using Horse::Deterministic::ReplayGroundPrivateScene;
using Horse::Deterministic::ReplayGroundPrivateQueues;
using Horse::Deterministic::ReplayGroundInitialBodyState;
enum class FailureCode {None,IllegalTransition,GenerationMismatch};
struct Status {FailureCode code{};bool ok()const{return code==FailureCode::None;}static Status success(){return {};}static Status failure(FailureCode c){return {c};}};
struct Sc6ReplayObjectLease {unsigned releases{};bool fail{};std::size_t owned_bytes()const{return sizeof(*this);}
 Status Release(){++releases;return fail?Status::failure(FailureCode::GenerationMismatch):Status::success();}};
struct ReplayGpuCompletion {enum class Result {Complete,Cancelled,Timeout};bool done{};Result value{Result::Complete};std::uint64_t serial{};
 bool retired()const{return done;}Result result()const{return value;}std::uint64_t submitted_serial()const{return serial;}};
struct Sc6ReplayColdParticleOwner {template<class T>static T Read(std::uintptr_t p,std::size_t offset=0){T v{};std::memcpy(&v,reinterpret_cast<void*>(p+offset),sizeof(v));return v;}};
struct Sc6ReplayGroundDebrisState {
#include "ground_cold_root_fields.inl"
 static Status ReleaseColdRoot(ColdRoot&,const ReplayGpuCompletion&)noexcept;
};
#include "ground_cold_native.inl"
#include "ground_cold_destructor.inl"
#include "ground_cold_release.inl"
struct GroundGraphNative {
 using Owner=Sc6ReplayGroundDebrisState::ColdRoot;
 using Native=Sc6ReplayColdParticleOwner;
 struct Header {std::uintptr_t data{};int count{},capacity{};};
#include "ground_graph_private.inl"
};
template<class T>void put(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
int main(){
 {
  std::array<std::byte,0x540> engine{};std::array<std::byte,0xa0> substep{};
  const auto e=reinterpret_cast<std::uintptr_t>(engine.data());
  put(e+0x370,reinterpret_cast<std::uintptr_t>(substep.data()));
  const auto read=[](auto p,auto& v){std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));return true;};
  std::array<ReplayGroundPrivateQueues::Binding,1> bindings{{{0x1234,0x5678}}};
  assert(ReplayGroundPrivateQueues::Absent(read,e,bindings));
  std::uintptr_t row=0x9999;
  put(e+0x310,ReplayGroundPrivateQueues::Array{reinterpret_cast<std::uintptr_t>(&row),1,1});
  assert(ReplayGroundPrivateQueues::Absent(read,e,bindings));
  row=bindings[0].body;assert(!ReplayGroundPrivateQueues::Absent(read,e,bindings));
  put(e+0x310,ReplayGroundPrivateQueues::Array{});
  for(unsigned offset:{0x1a8u,0x1b8u,0x1f0u,0x200u,0x238u,0x248u}) {
   put(e+offset,1);assert(!ReplayGroundPrivateQueues::Absent(read,e,bindings));put(e+offset,0);
  }
  put(e+0x160,std::uintptr_t{1});assert(!ReplayGroundPrivateQueues::Absent(read,e,bindings));put(e+0x160,std::uintptr_t{});
  std::array<std::uintptr_t,3> map_row{bindings[0].actor,0,0};
  for(unsigned offset:{0x3f0u,0x490u}) {
   put(e+offset,reinterpret_cast<std::uintptr_t>(map_row.data()));put(e+offset+8,1);
   put(e+offset+0x10,1u);put(e+offset+0x28,1);put(e+offset+0x2c,32);
   assert(!ReplayGroundPrivateQueues::Absent(read,e,bindings));map_row[0]=0x9999;
   assert(ReplayGroundPrivateQueues::Absent(read,e,bindings));map_row[0]=bindings[0].actor;
   std::memset(engine.data()+offset,0,0x50);
  }
 }
 std::array<std::byte,0xa20> bytes{};
 Sc6ReplayGroundDebrisState::ColdRoot owner;owner.base=0x140000000;owner.object=reinterpret_cast<std::uintptr_t>(bytes.data());owner.outer=0x12345678;
 put(owner.object,owner.base+0x33566f8);put(owner.object+0x20,owner.outer);put(owner.object+0x190,owner.outer);
 assert(GroundColdNative::Cold(owner));
 for(auto flag:{1u,2u,4u,0x40000u,0x200000u,0x10000000u}) {
  put(owner.object+0x188,flag);assert(!GroundColdNative::Cold(owner));put(owner.object+0x188,0u);
 }
 for(unsigned offset:{0x11cu,0x7bcu}){put(owner.object+offset,static_cast<unsigned char>(0x40));assert(!GroundColdNative::Cold(owner));put(owner.object+offset,static_cast<unsigned char>(0));}
 for(unsigned offset:{0x1c8u,0x1d0u,0x8b0u,0x8d0u,0x970u,0x810u,0x820u,0x840u,0x990u,0x9e0u,0x9f0u}) {
  put(owner.object+offset,std::uintptr_t{1});assert(!GroundColdNative::Cold(owner));put(owner.object+offset,std::uintptr_t{});
 }
 owner.phase=Sc6ReplayGroundDebrisState::ColdRoot::Phase::Destroyed;owner.retirement_serial=4;owner.allowance=1024;owner.child_count=1;owner.children[0].object=0x42;
 ReplayGpuCompletion completion;completion.serial=5;
 assert(!Sc6ReplayGroundDebrisState::ReleaseColdRoot(owner,completion).ok()&&!owner.lease.releases);
 completion.done=true;completion.serial=4;
 assert(!Sc6ReplayGroundDebrisState::ReleaseColdRoot(owner,completion).ok()&&!owner.lease.releases);
 completion.serial=5;
 for(auto result:{ReplayGpuCompletion::Result::Cancelled,ReplayGpuCompletion::Result::Timeout}) {
  completion.value=result;assert(!Sc6ReplayGroundDebrisState::ReleaseColdRoot(owner,completion).ok()&&!owner.lease.releases&&owner.allowance==1024);
 }
 assert(!owner.children[0].acquisition_lease.releases);
 completion.value=ReplayGpuCompletion::Result::Complete;owner.graph_ready=true;owner.lease.fail=true;
 assert(!Sc6ReplayGroundDebrisState::ReleaseColdRoot(owner,completion).ok()&&owner.allowance==1024);
 owner.lease.fail=false;assert(Sc6ReplayGroundDebrisState::ReleaseColdRoot(owner,completion).ok()&&!owner.allowance);
 assert(owner.graph_lease.releases==1 && owner.graph_lease_released);
 assert(owner.children[0].acquisition_lease.releases==1 && owner.children[0].acquisition_released);
 assert(owner.lease.releases==2);assert(Sc6ReplayGroundDebrisState::ReleaseColdRoot(owner,completion).ok()&&owner.lease.releases==2);
 // The private graph validator is the production retirement boundary. A
 // registered child, body, streaming member or foreign ring array cannot pass.
 std::array<std::byte,0xa20> child{};
 std::array<std::byte,0x28> entry{};
 std::array<std::uintptr_t,6> controller{owner.base+0x3510a68,owner.object+0x820,owner.base+0x8a0230,0,7,0};
 owner.child_count=1;owner.graph_entries=reinterpret_cast<std::uintptr_t>(entry.data());owner.graph_callback=controller[2];
 auto& c=owner.children[0];c.object=reinterpret_cast<std::uintptr_t>(child.data());c.asset=0x987654;
 put(c.object,owner.base+0x36cefb0);put(c.object+0x20,owner.object);put(c.object+0x190,owner.outer);put(c.object+0x920,c.asset);
 put(owner.object+0x820,GroundGraphNative::Header{owner.graph_entries,1,1});put(owner.graph_entries,c.object);
 put(owner.object+0x8b0,reinterpret_cast<std::uintptr_t>(controller.data()));put(owner.object+0x8c0,3);
 assert(GroundGraphNative::Private(owner));
 // Repair the skipped delta through the production cold-state installer.
 // Relative values/cache/bounds travel with the world transform; no movement
 // setter or placement callback is used to manufacture the result.
 ReplayGroundPose::World wanted{};ReplayGroundPose::Auxiliary auxiliary{};
 const float tiny=0.00005f;std::memcpy(wanted.data()+16,&tiny,4);
 std::memcpy(auxiliary.data()+56,&tiny,4);
 const auto read=[](auto p,auto& v){v=Sc6ReplayColdParticleOwner::Read<std::remove_reference_t<decltype(v)>>(p);return true;};
 const auto write=[](auto p,const auto& v){put(p,v);return true;};
 put(c.object+0x240,0x810u);
 assert(ReplayGroundPose::InstallCold(read,write,c.object,wanted,auxiliary,9));
 assert(Sc6ReplayColdParticleOwner::Read<unsigned>(c.object,0x240)==0x819u);
 assert(GroundGraphNative::PoseMatches(c.object,wanted));
 assert(GroundGraphNative::PoseAuxMatches(c.object,auxiliary,9));
 put(c.object+0x2c0,0.0f);assert(!GroundGraphNative::PoseAuxMatches(c.object,auxiliary,9));
 for(unsigned offset:{0x1c8u,0x1d0u,0x128u,0x7c8u}) {
  put(c.object+offset,std::uintptr_t{1});const auto before=child;
  assert(!ReplayGroundPose::InstallCold(read,write,c.object,wanted,auxiliary,9)&&child==before);
  put(c.object+offset,std::uintptr_t{});
 }
 assert(ReplayGroundPose::InstallCold(read,write,c.object,c.captured_transform,c.captured_transform_auxiliary,0));
 put(c.object+0x240,0u);
 // Setter completion is not pose restoration: the native movement path may
 // skip X=0.00005 with unchanged rotation/scale. Admission must reject the
 // zero destination even though every ownership predicate still passes.
 const float skipped_x=0.00005f;
 std::memcpy(c.captured_transform.data()+0x10,&skipped_x,4);
 assert(!GroundGraphNative::Private(owner));
 assert(GroundGraphNative::Private(owner,false)); // Pose rejection must not block safe retirement.
 assert(!GroundGraphNative::PoseMatches(c.object,c.captured_transform,&owner));
 assert(owner.pose_component==c.object && owner.pose_offset==0x280 && !owner.pose_actual);
 put(c.object+0x280,skipped_x);assert(GroundGraphNative::Private(owner));
 std::memcpy(owner.captured_transform.data()+0x10,&skipped_x,4);
 assert(!GroundGraphNative::Private(owner));
 put(owner.object+0x280,skipped_x);assert(GroundGraphNative::Private(owner));
 put(c.object+0x280,0.0f);put(owner.object+0x280,0.0f);
 c.captured_transform={};owner.captured_transform={};
 // Exact quaternion/scale admission and ignored FTransform padding.
 for(unsigned offset:{0x270u,0x27cu,0x290u,0x298u}) {
  put(c.object+offset,1.0f);assert(!GroundGraphNative::Private(owner));put(c.object+offset,0.0f);
 }
 put(c.object+0x28c,0xdeadbeefu);put(c.object+0x29c,0xdeadbeefu);
 assert(GroundGraphNative::Private(owner));
 // Native UMeshComponent constructor141D6C770 sets navigation capability
 // even before registration. It is not world or octree membership.
 put(c.object+0x188,0x200000u);assert(GroundGraphNative::Private(owner));put(c.object+0x188,0u);
 const char* predicate{};std::uintptr_t observed{};
 put(c.object+0x520,std::uintptr_t{0x1234});put(c.object+0x528,std::uintptr_t{0x5678});
 assert(!GroundGraphNative::ChildPrivate(owner.base,c.object,owner.object,owner.outer,&predicate,&observed));
 assert(!std::strcmp(predicate,"ground_child_sync_body")&&observed==0x1234);
 put(c.object+0x520,std::uintptr_t{});
 assert(!GroundGraphNative::ChildPrivate(owner.base,c.object,owner.object,owner.outer,&predicate,&observed));
 assert(!std::strcmp(predicate,"ground_child_async_body")&&observed==0x5678);
 put(c.object+0x528,std::uintptr_t{});
 put(c.object+0x3f8,4u);assert(GroundGraphNative::Private(owner));put(c.object+0x3f8,0u);
 for(unsigned offset:{0x520u,0x528u,0x640u,0x790u,0x1c8u,0x1d0u,0x128u,0x7c8u}) {
  put(c.object+offset,std::uintptr_t{1});assert(!GroundGraphNative::Private(owner));put(c.object+offset,std::uintptr_t{});
 }
 for(auto flag:{1u,2u,4u,0x40000u,0x200001u,0x200004u,0x10000000u}) {
  put(c.object+0x188,flag);assert(!GroundGraphNative::Private(owner));put(c.object+0x188,0u);
 }
 for(auto flag:{1u,2u}){put(c.object+0x3f8,flag);assert(!GroundGraphNative::Private(owner));put(c.object+0x3f8,0u);}
 put(owner.graph_entries+8,GroundGraphNative::Header{1,1,1});assert(!GroundGraphNative::Private(owner));
 put(owner.graph_entries+8,GroundGraphNative::Header{});
 put(owner.object+0x82c,2);assert(!GroundGraphNative::Private(owner));put(owner.object+0x82c,1);
 put(c.object+0x490,GroundGraphNative::Header{1,1,1});assert(!GroundGraphNative::Private(owner));
 put(c.object+0x490,GroundGraphNative::Header{});
 controller[1]=owner.object;assert(!GroundGraphNative::Private(owner));controller[1]=owner.object+0x820;
 controller[2]=owner.base+0x89ff80;assert(!GroundGraphNative::Private(owner));controller[2]=owner.graph_callback;
 controller[4]=0;assert(!GroundGraphNative::Private(owner));controller[4]=7;
 assert(GroundGraphNative::Private(owner));
}
