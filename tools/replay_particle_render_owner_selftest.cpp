#include <Windows.h>
#include <array>
#include <vector>
#include <span>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <stdexcept>
#define STR(x) x
namespace RC {enum class LogLevel{Warning};struct Output{template<LogLevel,class... T>static void send(const char*,T...) {}};}
#include "ReplayLightingBinding.hpp"
using Horse::Deterministic::ReplayLightingBinding;
struct Status {bool value;bool ok()const{return value;}};
struct Sc6ReplayVfxState {
 struct CoordinateRebuild {bool fresh=true;std::uintptr_t component{};std::array<int,2> component_weak{};std::uintptr_t render{};};
 struct Identity {std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};};
 struct ReconstructionBinding {Identity identity;const void* lease{};};
 std::array<std::size_t,2> emitter_counts{0,1};
 std::array<std::size_t,2> ParticleEmitterCounts(std::uintptr_t)const{return emitter_counts;}
 bool valid=true;
 Status ValidateReconstructionBindings(std::span<const ReconstructionBinding>)const{return {valid};}
};
template<class T>T& Field(std::uintptr_t p,std::size_t o=0){return *reinterpret_cast<T*>(p+o);}
struct Sc6ReplayTraceState {
 struct FreshChild {Sc6ReplayVfxState::ReconstructionBinding binding;bool valid=true,throws=false;};
 Status FreshChildMeshBinding(const FreshChild& child,Sc6ReplayVfxState::ReconstructionBinding& output)const {
  if(child.throws)throw std::runtime_error("trace owner admission unavailable");
  if(!child.valid || !child.binding.lease)return{false};output=child.binding;return{true};
 }
};
void** Assign(void** slot,void* value){*slot=value;return slot;}
struct Sc6ReplayParticleCopy {
 enum class Phase{UndoReady,Installed};
 struct Witness {Phase phase{Phase::UndoReady};bool undo_ready=true,target_prepared=false,native_write_uncommitted=false;std::size_t bytes=0;}witness_;
 struct Completion {bool done=true;bool retired()const{return done;}}completion_;
 struct LightingPrimitive {
  std::uintptr_t address{},proxy{},component{},allocation{},proxy_type{};
  std::array<int,2> weak{};std::array<std::uintptr_t,17> slots{};
  std::array<std::uintptr_t,7> proxy_inputs{};
  unsigned id{},slot_count{};std::uint8_t dirty{};
  enum class Binding {ScenePrimitive,PendingTraceTarget,PendingParticleTarget}binding{};
 };
 struct Uniform {std::uintptr_t slot{};void* value{};std::uintptr_t component{};unsigned ordinal{};};
 struct Map {std::array<std::byte,0x50> header{};std::vector<std::byte> entries,flags,hashes;};
 struct Allocation {std::uintptr_t address{};void* installed{};};
 struct Image {std::array<Map,2> maps;std::vector<LightingPrimitive> primitives;std::vector<Uniform> uniforms;std::vector<void*> owners;std::vector<Allocation> allocations;bool captured=true;}lighting_a_,lighting_b_;
 struct ParticleRenderOwner {Sc6ReplayVfxState::ReconstructionBinding binding;std::uintptr_t particle_template{},proxy_type{};unsigned source_id{},target_id{};std::array<std::size_t,2> emitter_counts{0,1};};
 std::array<ParticleRenderOwner,32> particle_render_owners_{};std::size_t particle_render_owner_count_{};bool particle_render_bound_{};
 bool lighting_dirty_=false,lighting_transferred_=false,lighting_executing_=false,owner_live=true;
 std::uintptr_t base_=reinterpret_cast<std::uintptr_t>(&Assign)-0x15b83f0,scene_{};
 bool ReadParticleRenderOwner(ParticleRenderOwner& p)const noexcept {
  if(!owner_live || !p.binding.lease)return false;
  p.target_id=Field<unsigned>(p.binding.identity.target,0x420);p.particle_template=Field<std::uintptr_t>(p.binding.identity.target,0x808);return true;
 }
 bool LightingPrimitiveSlots(LightingPrimitive& p)const {p.slot_count=1;p.slots[0]=p.address+0x58;return true;}
 bool VisibilityWritable(std::uintptr_t p,std::size_t n)const {MEMORY_BASIC_INFORMATION b{};return VirtualQuery(reinterpret_cast<void*>(p),&b,sizeof(b)) && b.State==MEM_COMMIT && n<=reinterpret_cast<std::uintptr_t>(b.BaseAddress)+b.RegionSize-p;}
 bool LightingBindings(const Image& image)const {return owner_live;}
 bool PrepareParticleRenderOwners(const Sc6ReplayVfxState&,std::span<const Sc6ReplayVfxState::ReconstructionBinding>,std::size_t)noexcept;
 bool PendingParticleTargetBinding(const LightingPrimitive&)const noexcept;
 bool BindParticleRenderOwners()noexcept;
 bool execution_started_=false,execution_settled_=false,registry_executing_=false,coordinate_valid=true,drain_ok=true;
 std::uintptr_t system_{};std::vector<Sc6ReplayVfxState::CoordinateRebuild> coordinates_;unsigned drains{};
 bool CoordinatesBound(const Sc6ReplayVfxState::CoordinateRebuild&,bool target){assert(target);return coordinate_valid;}
 bool DrainExecutionWork(){++drains;return drain_ok;}
 bool trace_render_pending()const {return std::any_of(lighting_a_.primitives.begin(),lighting_a_.primitives.end(),[](const auto& r){return r.binding==LightingPrimitive::Binding::PendingTraceTarget;});}
 bool TraceRenderBinding(const LightingPrimitive&,bool)const {return owner_live;}
 bool CompleteParticleRenderOwners()noexcept;
 struct FreshTraceRenderOwner {Sc6ReplayVfxState::ReconstructionBinding binding;unsigned source_id{},target_id{};std::uintptr_t mesh{};};
 struct TraceRenderOwnerProof {const void* context{};std::size_t count{};Status(*binding)(const void*,std::size_t,Sc6ReplayVfxState::ReconstructionBinding&,bool){};bool(*dormant)(const void*,std::size_t,unsigned&,std::uintptr_t&){};};
 const Sc6ReplayTraceState* trace_render_owner_{};TraceRenderOwnerProof fresh_trace_render_proof_{};
 std::array<FreshTraceRenderOwner,16> fresh_trace_render_owners_{};std::size_t fresh_trace_render_count_{};
 bool PrepareFreshTraceRenderOwners(const TraceRenderOwnerProof&,std::size_t)noexcept;
 bool FreshTraceRenderBinding(const LightingPrimitive&)const noexcept;
};
#include "particle_render_owner.inl"
int main(){
 std::array<std::byte,0xb00> component{};std::array<std::byte,0x1000> scene{};
 std::array<std::byte,0x400> proxy{};std::array<std::byte,0x100> info{};
 const auto c=reinterpret_cast<std::uintptr_t>(component.data()),p=reinterpret_cast<std::uintptr_t>(proxy.data()),s=reinterpret_cast<std::uintptr_t>(scene.data()),n=reinterpret_cast<std::uintptr_t>(info.data());
 Field<unsigned>(c,0x420)=8;Field<std::uintptr_t>(c,0x808)=900;
 Sc6ReplayVfxState target;Sc6ReplayVfxState::ReconstructionBinding binding{{1,c,{1,1},{2,1}},&target};
 Sc6ReplayParticleCopy original;original.scene_=s;
 auto& a=original.lighting_a_;auto& b=original.lighting_b_;
 Sc6ReplayParticleCopy::LightingPrimitive row;row.component=1;row.weak={1,1};row.id=1;row.slot_count=1;row.proxy_type=original.base_+0x3956e80;row.allocation=500;row.dirty=1;
 a.primitives.push_back(row);a.uniforms.push_back({777,reinterpret_cast<void*>(600),1,0});a.owners.push_back(reinterpret_cast<void*>(1));a.allocations.push_back({500,reinterpret_cast<void*>(501)});
 b.primitives.push_back({.address=100,.proxy=200,.component=300,.id=2,.slot_count=1});
 b.uniforms.push_back({888,reinterpret_cast<void*>(999),300,0});
 auto& map=a.maps[1];map.entries.resize(24);map.flags.resize(4);map.hashes.resize(4);
 Field<unsigned>(reinterpret_cast<std::uintptr_t>(map.header.data()),8)=1;
 Field<unsigned>(reinterpret_cast<std::uintptr_t>(map.entries.data()))=1;
 Field<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(map.entries.data()),8)=500;
 Field<unsigned>(reinterpret_cast<std::uintptr_t>(map.entries.data()),16)=UINT32_MAX;
 Field<unsigned>(reinterpret_cast<std::uintptr_t>(map.flags.data()))=1;
 const auto prepare=[&](auto& f,std::size_t budget=100000){return f.PrepareParticleRenderOwners(target,{&binding,1},budget);};
 for(unsigned fault=0;fault<13;++fault){auto f=original;target.valid=true;auto oldbinding=binding;
  switch(fault){
   case 0:f.witness_.undo_ready=false;break;case 1:f.completion_.done=false;break;case 2:f.lighting_dirty_=true;break;
   case 3:target.valid=false;break;case 4:f.owner_live=false;break;case 5:f.lighting_a_.primitives[0].weak[1]=3;break;
   case 6:f.lighting_a_.primitives[0].slot_count=2;break;case 7:f.lighting_b_.primitives[0].id=8;break;
   case 8:f.lighting_a_.uniforms[0].ordinal=1;break;case 9:binding.lease=nullptr;break;
   case 10:f.lighting_a_.maps[1].hashes[0]=std::byte{4};break;
   case 11:f.lighting_b_.primitives[0].component=1;break;case 12:f.witness_.bytes=100000;break;
  }
  assert(!prepare(f));assert(f.lighting_a_.primitives[0].component==1 && !f.particle_render_owner_count_);
  binding=oldbinding;
 }
 target.valid=true;auto f=original;assert(prepare(f));
 assert(f.lighting_a_.primitives[0].component==c && f.lighting_a_.primitives[0].id==8);
 assert(Field<unsigned>(reinterpret_cast<std::uintptr_t>(f.lighting_a_.maps[1].entries.data()))==8);
 assert(f.lighting_b_.primitives[0].component==300 && f.lighting_b_.uniforms[0].slot==888);
 assert(original.lighting_a_.primitives[0].component==1 && !prepare(f));
 assert(f.PendingParticleTargetBinding(f.lighting_a_.primitives[0]));
 assert(!f.BindParticleRenderOwners());f.witness_.phase=Sc6ReplayParticleCopy::Phase::Installed;f.witness_.native_write_uncommitted=true;f.lighting_dirty_=true;
 assert(!f.BindParticleRenderOwners()); // A pending mapping is not a native proxy.
 Field<std::uintptr_t>(c,0x790)=p;Field<std::uintptr_t>(p)=row.proxy_type;Field<std::uintptr_t>(p,0x118)=n;
 Field<std::uintptr_t>(n,0xe8)=c;Field<std::uintptr_t>(n,0xd8)=s;Field<unsigned>(n,0x10)=8;Field<int>(n,0xe4)=0;
 std::uintptr_t native_member=n;Field<int>(s,0xad0)=1;Field<std::uintptr_t>(s,0xac8)=reinterpret_cast<std::uintptr_t>(&native_member);
 for(unsigned fault=0;fault<7;++fault){auto copy=f;
  switch(fault){case 0:Field<unsigned>(n,0x10)=7;break;case 1:Field<std::uintptr_t>(n,0xe8)=1;break;
   case 2:native_member=0;break;case 3:Field<std::uintptr_t>(p)=0;break;case 4:copy.owner_live=false;break;
   case 5:copy.lighting_a_.allocations.clear();break;case 6:copy.lighting_b_.primitives[0].address=n;break;}
  assert(!copy.BindParticleRenderOwners());assert(!Field<std::uintptr_t>(n,0x50) && !Field<std::uintptr_t>(n,0x58));
  Field<unsigned>(n,0x10)=8;Field<std::uintptr_t>(n,0xe8)=c;native_member=n;Field<std::uintptr_t>(p)=row.proxy_type;
 }
 assert(f.BindParticleRenderOwners() && f.particle_render_bound_);
 assert(Field<std::uintptr_t>(n,0x50)==501 && Field<std::uintptr_t>(n,0x58)==600 && Field<std::uint8_t>(n,0xf2)==1);
 assert(f.lighting_a_.primitives[0].address==n && f.lighting_a_.uniforms[0].slot==n+0x58);
 assert(f.lighting_b_.primitives[0].component==300 && f.lighting_b_.uniforms[0].slot==888);
 // Registration completion observes native-owned C; it never republishes A's lighting bytes.
 auto native=f;native.particle_render_bound_=false;native.execution_started_=native.registry_executing_=native.lighting_executing_=true;
 std::array<std::byte,0x100> system{};std::array<std::byte,0x260> render{};
 const auto sys=reinterpret_cast<std::uintptr_t>(system.data()),ren=reinterpret_cast<std::uintptr_t>(render.data());
 native.system_=sys;native.coordinates_.push_back({true,c,{2,1},ren});
 std::uintptr_t registry_member=ren;Field<std::uintptr_t>(sys,0x40)=reinterpret_cast<std::uintptr_t>(&registry_member);
 Field<int>(sys,0x48)=Field<int>(sys,0x68)=1;Field<unsigned>(sys,0x50)=1;Field<int>(ren,0x240)=0;
 Field<std::uintptr_t>(n,0x50)=777;Field<std::uintptr_t>(n,0x58)=888;Field<std::uint8_t>(n,0xf2)=1;
 for(unsigned fault=0;fault<10;++fault){auto subject=native;
  switch(fault){case 0:subject.execution_started_=false;break;case 1:subject.registry_executing_=false;break;
   case 2:subject.lighting_executing_=false;break;case 3:subject.completion_.done=false;break;
   case 4:subject.coordinate_valid=false;break;case 5:registry_member=0;break;case 6:Field<unsigned>(sys,0x50)=0;break;
   case 7:Field<int>(ren,0x240)=-1;break;case 8:subject.owner_live=false;break;case 9:native_member=0;break;}
  assert(!subject.CompleteParticleRenderOwners() && !subject.drains && !subject.particle_render_bound_);
  registry_member=ren;Field<unsigned>(sys,0x50)=1;Field<int>(ren,0x240)=0;native_member=n;
 }
 assert(native.CompleteParticleRenderOwners() && native.drains==1 && native.particle_render_bound_);
 assert(Field<std::uintptr_t>(n,0x50)==777 && Field<std::uintptr_t>(n,0x58)==888 && Field<std::uint8_t>(n,0xf2)==1);
 assert(native.lighting_b_.uniforms[0].slot==888 && !native.CompleteParticleRenderOwners());
 std::array<std::byte,0x200> cpu_root{};auto cr=reinterpret_cast<std::uintptr_t>(cpu_root.data());std::array<std::uintptr_t,2> cpu_slots{0,cr};
 Field<std::uintptr_t>(c,0xa50)=reinterpret_cast<std::uintptr_t>(cpu_slots.data());Field<int>(c,0xa58)=Field<int>(c,0xa5c)=2;
 Field<std::uintptr_t>(cr)=native.base_+0x3949b60;Field<std::uintptr_t>(cr,0x18)=c;
 auto cpu=native;cpu.particle_render_bound_=false;cpu.coordinates_.clear();cpu.drains=0;
 auto cpu_plan=original;target.emitter_counts={1,0};assert(prepare(cpu_plan));cpu.particle_render_owners_=cpu_plan.particle_render_owners_;
 target.emitter_counts={0,0};cpu_plan=original;assert(!prepare(cpu_plan));target.emitter_counts={0,129};cpu_plan=original;assert(!prepare(cpu_plan));target.emitter_counts={0,1};
 assert(cpu.CompleteParticleRenderOwners() && cpu.drains==1 && cpu.particle_render_bound_);
 for(unsigned fault=0;fault<5;++fault){auto subject=cpu;subject.particle_render_bound_=false;subject.drains=0;
  if(fault==0)Field<std::uintptr_t>(cr)=native.base_+0x394c100;
  if(fault==1)cpu_slots[1]=0;if(fault==2)Field<std::uintptr_t>(cr,0x18)=1;if(fault==3)subject.coordinates_=native.coordinates_;if(fault==4)subject.completion_.done=false;
  assert(!subject.CompleteParticleRenderOwners()&&!subject.drains);
  Field<std::uintptr_t>(cr)=native.base_+0x3949b60;Field<std::uintptr_t>(cr,0x18)=c;cpu_slots[1]=cr;
 }
 std::array<std::byte,0x260> second_render{};auto ren2=reinterpret_cast<std::uintptr_t>(second_render.data());
 std::array<std::uintptr_t,2> registry_pair{ren,ren2};Field<int>(ren2,0x240)=1;
 Field<std::uintptr_t>(sys,0x40)=reinterpret_cast<std::uintptr_t>(registry_pair.data());Field<int>(sys,0x48)=Field<int>(sys,0x68)=2;Field<unsigned>(sys,0x50)=3;
 auto multi=native;multi.particle_render_bound_=false;multi.drains=0;multi.coordinates_.push_back({true,c,{2,1},ren2});
 target.emitter_counts={0,2};auto multi_plan=original;assert(prepare(multi_plan));multi.particle_render_owners_=multi_plan.particle_render_owners_;
 assert(multi.CompleteParticleRenderOwners()&&multi.drains==1);
 for(unsigned fault=0;fault<4;++fault){auto subject=multi;subject.particle_render_bound_=false;subject.drains=0;
  if(fault==0)registry_pair[1]=0;if(fault==1)Field<unsigned>(sys,0x50)=1;
  if(fault==2)subject.coordinates_[1].render=ren;if(fault==3)subject.coordinates_.pop_back();
  assert(!subject.CompleteParticleRenderOwners()&&!subject.drains);registry_pair[1]=ren2;Field<unsigned>(sys,0x50)=3;
 }
 Field<std::uintptr_t>(sys,0x40)=reinterpret_cast<std::uintptr_t>(&registry_member);Field<int>(sys,0x48)=Field<int>(sys,0x68)=1;Field<unsigned>(sys,0x50)=1;
 auto trace=native;trace.particle_render_owner_count_=0;trace.particle_render_bound_=false;trace.drains=0;
 trace.lighting_a_.primitives[0].binding=Sc6ReplayParticleCopy::LightingPrimitive::Binding::PendingTraceTarget;
 Field<std::uintptr_t>(c,0xa18)=p;
 for(unsigned fault=0;fault<5;++fault){auto subject=trace;
  if(fault==0)subject.owner_live=false;if(fault==1)Field<std::uintptr_t>(c,0xa18)=0;
  if(fault==2)Field<std::uintptr_t>(c,0x790)=0;if(fault==3)native_member=0;if(fault==4)subject.completion_.done=false;
  assert(!subject.CompleteParticleRenderOwners() && !subject.drains);
  Field<std::uintptr_t>(c,0xa18)=p;Field<std::uintptr_t>(c,0x790)=p;native_member=n;
 }
 assert(trace.CompleteParticleRenderOwners() && trace.drains==1 && trace.particle_render_bound_);
 assert(!trace.CompleteParticleRenderOwners() && trace.drains==1);
 assert(Field<std::uintptr_t>(n,0x50)==777 && Field<std::uintptr_t>(n,0x58)==888);
 {
  // One fresh allocation recycles another historical child's address. Each
  // source row must be mapped once, including uniforms and owner inventory.
  auto* old=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);assert(old);
  std::array<std::byte,0xb00> second{};const auto c2=reinterpret_cast<std::uintptr_t>(second.data());
  const auto old1=reinterpret_cast<std::uintptr_t>(old);
  Sc6ReplayTraceState source;Sc6ReplayParticleCopy f;f.trace_render_owner_=&source;
  std::array<Sc6ReplayTraceState::FreshChild,2> children;
  children[0].binding={{old1,c,{1,1},{2,2}},&children[0]};
  children[1].binding={{c,c2,{3,3},{4,4}},&children[1]};
  const Sc6ReplayParticleCopy::TraceRenderOwnerProof owners{&children,children.size(),
   [](const void* context,std::size_t i,Sc6ReplayVfxState::ReconstructionBinding& output,bool) {
    const auto& rows=*static_cast<const std::array<Sc6ReplayTraceState::FreshChild,2>*>(context);
    return Sc6ReplayTraceState{}.FreshChildMeshBinding(rows.at(i),output);
   }};
  Field<unsigned>(c,0x420)=20;Field<unsigned>(c2,0x420)=21;
  Field<std::uintptr_t>(c,0x910)=Field<std::uintptr_t>(c2,0x910)=42;
  for(unsigned i=0;i<2;++i) {
   Sc6ReplayParticleCopy::LightingPrimitive row;row.component=children[i].binding.identity.source;row.weak=children[i].binding.identity.source_weak;
   row.id=10+i;row.slot_count=1;row.proxy_type=f.base_+0x39af350;row.proxy_inputs[0]=42;
   f.lighting_a_.primitives.push_back(row);f.lighting_a_.uniforms.push_back({123,reinterpret_cast<void*>(456+i),row.component,0});
   f.lighting_a_.owners.push_back(reinterpret_cast<void*>(row.component));
  }
  f.lighting_a_.maps[1].hashes.resize(4,std::byte{0xff});
  {
   auto denied=f;children[0].throws=true;
   assert(!denied.PrepareFreshTraceRenderOwners(owners,100000)&&denied.fresh_trace_render_count_==0);
   children[0].throws=false;
  }
  {
   auto hidden=f;hidden.lighting_a_.primitives.clear();hidden.lighting_a_.uniforms.clear();hidden.lighting_a_.owners.clear();
   auto proof=owners;
   proof.dormant=[](const void* context,std::size_t i,unsigned& id,std::uintptr_t& mesh) {
    const auto& rows=*static_cast<const std::array<Sc6ReplayTraceState::FreshChild,2>*>(context);
    if(!rows[i].valid)return false;id=10+unsigned(i);mesh=42;return true;
   };
   assert(hidden.PrepareFreshTraceRenderOwners(proof,100000));
   assert(hidden.lighting_a_.primitives.empty()&&hidden.lighting_a_.uniforms.empty()&&!hidden.trace_render_pending());
   assert(hidden.fresh_trace_render_count_==2&&!hidden.PrepareFreshTraceRenderOwners(proof,100000));
   for(unsigned fault=0;fault<6;++fault) {
    auto bad=f;bad.lighting_a_.primitives.clear();bad.lighting_a_.uniforms.clear();auto q=proof;
    if(fault==0)q.dormant=nullptr;
    if(fault==1)children[0].valid=false;
    if(fault==2)bad.lighting_a_.primitives.push_back(f.lighting_a_.primitives[0]),bad.lighting_a_.primitives[0].weak[1]++;
    if(fault==3)bad.lighting_a_.uniforms.push_back(f.lighting_a_.uniforms[0]);
    if(fault==4)q.dormant=[](const void*,std::size_t,unsigned& id,std::uintptr_t& mesh){id=0;mesh=42;return true;};
    if(fault==5)q.dormant=[](const void*,std::size_t,unsigned& id,std::uintptr_t& mesh){id=10;mesh=43;return true;};
    assert(!bad.PrepareFreshTraceRenderOwners(q,100000)&&!bad.fresh_trace_render_count_);children[0].valid=true;
   }
  }
  assert(f.PrepareFreshTraceRenderOwners(owners,100000));
  assert(f.lighting_a_.primitives[0].component==c&&f.lighting_a_.primitives[1].component==c2);
  assert(f.lighting_a_.uniforms[0].component==c&&f.lighting_a_.uniforms[1].component==c2);
  assert(f.lighting_a_.owners[0]==reinterpret_cast<void*>(c)&&f.lighting_a_.owners[1]==reinterpret_cast<void*>(c2));
  assert(f.FreshTraceRenderBinding(f.lighting_a_.primitives[0])&&f.FreshTraceRenderBinding(f.lighting_a_.primitives[1]));
  children[0].valid=false;assert(!f.FreshTraceRenderBinding(f.lighting_a_.primitives[0]));
  VirtualFree(old,0,MEM_RELEASE);
 }
 std::puts("Particle render projection preserves B and requires actual fresh proxy membership before binding");
}
