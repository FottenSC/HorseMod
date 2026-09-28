#include <Windows.h>
#include <array>
#include <vector>
#include <span>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <chrono>
enum class FailureCode{GenerationMismatch,CapacityExceeded,UnsupportedContent,ContextUnavailable};
struct Status{bool valid;bool ok()const{return valid;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
struct Lease{bool live=true;Status Validate()const{return {live};}};
bool CopyBytes(void* out,const void* in,std::size_t bytes){if(!in)return false;std::memcpy(out,in,bytes);return true;}
template<class T>T At(const void* p,std::size_t offset){T v;std::memcpy(&v,static_cast<const std::byte*>(p)+offset,sizeof(v));return v;}
struct Sc6ReplayVfxState {
 struct Identity{std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};};
 struct ReconstructionBinding{Identity identity;const Lease* lease;};
 struct Image{std::uintptr_t field{};std::uintptr_t vector_field_asset()const{return field;}};
 struct GpuOwner{std::uintptr_t component{},emitter{},render_storage{},system{},pool{},coordinate_resource{};std::size_t ordinal{};std::vector<std::uint32_t> tiles;Image image;};
 struct ComponentBinding{std::uintptr_t address{};std::vector<std::uintptr_t> emitters;};
 struct CoordinateFailure{std::uintptr_t component{},a{},b{};std::size_t ordinal{},an{},bn{},first{};std::uint32_t av{},bv{};bool has_b{},system{},pool{};};
 struct CoordinateRebuild{std::uintptr_t component{},emitter{},render{},system{},pool{},descriptor{},resource{},vector_field_asset{};std::span<const std::uint32_t> tiles;std::uint32_t previous_count{};std::span<const std::uint32_t> previous_tiles;bool rebuild_tiles=true,fresh=false;std::array<int,2> component_weak{};bool native_retired{};std::uintptr_t source_render{};};
 bool valid_=true,mapping_valid=true;std::uintptr_t base_=0x140000000;Lease lease;Lease* object_lease_=&lease;
 std::vector<GpuOwner> gpu_owners_;std::vector<ComponentBinding> components_;
 Status ValidateReconstructionBindings(std::span<const ReconstructionBinding>)const{return {mapping_valid};}
 Status PrepareCoordinateReconstruction(const Sc6ReplayVfxState&,std::vector<CoordinateRebuild>&,std::size_t,CoordinateFailure*,std::span<const ReconstructionBinding>)const noexcept;
};
#include "fresh_coordinates.inl"
template<class T>T& Field(std::uintptr_t p,std::size_t offset=0){return *reinterpret_cast<T*>(p+offset);}
struct ReplayGpuCompletion {using Clock=std::chrono::steady_clock;HRESULT Submit(void*,Clock::time_point){return S_OK;}};
bool ReadBytes(void* out,const void* in,std::size_t size){std::memcpy(out,in,size);return true;}
struct Sc6ReplayParticleCopy {
 std::uintptr_t base_=0x140000000,system_=400,pool_=500;
 enum class Phase {ReadExecutionCoordinates,ReadUndoCoordinates,ReadCoordinates};
 struct Witness {std::uintptr_t reconstruction_owner{};unsigned reconstruction_issue{};Phase phase{};} witness_;
 std::vector<Sc6ReplayVfxState::CoordinateRebuild> coordinates_;std::size_t coordinate_refs_{};
 bool coordinate_execution_started_{},coordinate_commit_started_{},coordinate_undo_complete_{};
 ReplayGpuCompletion completion_;ReplayGpuCompletion::Clock::time_point request_deadline_;
 struct Context {void* Get(){return nullptr;}} context_;
 bool PrepareTransfer(){return true;}void Fail(HRESULT){}
 bool RebuildCoordinates(bool,bool) noexcept;
 bool CoordinatesBound(const Sc6ReplayVfxState::CoordinateRebuild&,bool) noexcept;
};
#include "fresh_coordinates_bound.inl"
template<class T>void Put(auto& data,std::size_t offset,T value){std::memcpy(data.data()+offset,&value,sizeof(value));}
struct Packet {std::uintptr_t render;const std::uint32_t* tiles;int count,capacity;std::uintptr_t resource;};
static unsigned rebuild_calls;
static void Rebuild(const Packet* p) {
 ++rebuild_calls;
 // Controlled native141F95B10 writes buffers/counts/resource, not request flags.
 Field<std::uintptr_t>(p->render,0x48)=p->resource;
 Field<unsigned>(p->render,0x40)=p->count;Field<unsigned>(p->render,0x44)=(p->count+7)&~7;
 Field<unsigned>(p->render,0x230)=p->count*16;
 Field<unsigned char>(p->render,0x28)=Field<unsigned char>(p->render,0x218)=1;
 Field<std::uintptr_t>(p->render,0x30)=Field<std::uintptr_t>(p->render,0x38)=Field<std::uintptr_t>(p->render,0x220)=0x1234;
}
int main(){
 std::array<std::byte,0xb00> component{};std::array<std::byte,0x2a0> emitter{};std::array<std::byte,0x260> render{};std::array<std::byte,0x300> descriptor{};std::array<std::byte,0x240> resource{};
 const auto ptr=[](auto& a){return reinterpret_cast<std::uintptr_t>(a.data());};
 const auto c=ptr(component),e=ptr(emitter),r=ptr(render),d=ptr(descriptor),q=ptr(resource);
 std::array<std::uintptr_t,3> slots{0,e,0};
 auto* native=VirtualAlloc(nullptr,0x4400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(native);
 const auto base=reinterpret_cast<std::uintptr_t>(native);
 auto* thunk=reinterpret_cast<unsigned char*>(base+0x1f95b10);thunk[0]=0x48;thunk[1]=0xb8;
 const auto destination=reinterpret_cast<std::uintptr_t>(&Rebuild);std::memcpy(thunk+2,&destination,8);thunk[10]=0xff;thunk[11]=0xe0;
 FlushInstructionCache(GetCurrentProcess(),thunk,12);
 Sc6ReplayVfxState a,b;a.base_=b.base_=base;a.lease.live=false;
 a.gpu_owners_.push_back({1,7,8,400,500,q,1,{3,4},{}});a.components_.push_back({1,{0,7,0}});
 Sc6ReplayVfxState::ReconstructionBinding mapping{{1,c,{1,1},{2,1}},&b.lease};
 Put(component,0xa50,ptr(slots));Put(component,0xa58,3);Put(component,0xa5c,3);
 Put(emitter,0,a.base_+0x394c100);Put(emitter,0x1d0,std::uintptr_t{400});Put(emitter,0x1d8,d);Put(emitter,0x1e0,r);
 Put(render,0,a.base_+0x394bfc0);Put(render,0x1f0,a.base_+0x394c000);Put(render,0x240,-1);Put(render,0x250,std::uint32_t{0x01000101});Put(descriptor,0x2b0,q);
 std::vector<Sc6ReplayVfxState::CoordinateRebuild> out;
 if(!a.PrepareCoordinateReconstruction(b,out,100000,nullptr,{&mapping,1}).ok()){std::puts("fresh constructor coordinates rejected because original A lease expired");return 1;}
 assert(out.size()==1 && out[0].fresh && out[0].component==c && out[0].emitter==e && out[0].render==r);
 assert(out[0].tiles.data()==a.gpu_owners_[0].tiles.data() && out[0].previous_tiles.empty() && !out[0].previous_count && out[0].resource==q);
 assert(out[0].component_weak==mapping.identity.target_weak && a.components_[0].address==1 && b.components_.empty());
 Put(resource,0x230,LONG{1});
 Sc6ReplayParticleCopy copy;copy.base_=base;
 if(!copy.CoordinatesBound(out[0],false)){std::printf("fresh empty render owner rejected: %u\n",copy.witness_.reconstruction_issue);return 2;}
 assert(!copy.CoordinatesBound(out[0],true));
 auto row=out[0];row.fresh=false;assert(!copy.CoordinatesBound(row,false));
 row=out[0];row.native_retired=true;row.emitter=1;row.render=1;assert(copy.CoordinatesBound(row,false));assert(!copy.CoordinatesBound(row,true));row.fresh=false;assert(!copy.CoordinatesBound(row,false));
 row=out[0];row.previous_count=1;assert(!copy.CoordinatesBound(row,false));
 Put(emitter,0x18,c);assert(copy.CoordinatesBound(out[0],false)); // A CPU storage is published before the RT rebuild.
 Put(emitter,0x18,std::uintptr_t{0});
 for(unsigned fault=0;fault<8;++fault){auto saved=render;
  switch(fault){case 0:Put(render,0x240,0);break;case 1:Put(render,0x218,std::uint8_t{1});break;
   case 2:Put(render,0x220,q);break;case 3:Put(render,0x48,q);break;case 4:Put(render,0x252,std::uint8_t{1});break;case 5:Put(render,0x250,std::uint8_t{0});break;
   case 6:Put(render,0x251,std::uint8_t{0});break;case 7:Put(render,0x253,std::uint8_t{0});break;}
  assert(!copy.CoordinatesBound(out[0],false));render=saved;
 }
 {
  const auto original=render;Put(emitter,0x18,c);copy.coordinates_=out;copy.coordinate_refs_=out.size();
  if(!copy.RebuildCoordinates(true,true)){std::printf("fresh rebuild failed to consume native request flags: %u\n",copy.witness_.reconstruction_issue);return 3;}
  assert(rebuild_calls==1 && Field<unsigned short>(r,0x250)==0 && Field<unsigned char>(r,0x253)==1);
  assert(copy.CoordinatesBound(out[0],true));render=original;Put(emitter,0x18,std::uintptr_t{0});
 }
 const auto clean_component=component;const auto clean_emitter=emitter;const auto clean_render=render;const auto clean_descriptor=descriptor;
 for(unsigned fault=0;fault<10;++fault){component=clean_component;emitter=clean_emitter;render=clean_render;descriptor=clean_descriptor;a.mapping_valid=true;b.lease.live=true;b.components_.clear();out.clear();
  switch(fault){case 0:a.mapping_valid=false;break;case 1:b.lease.live=false;break;case 2:b.components_.push_back({c,{}});break;
   case 3:Put(component,0xa58,2);break;case 4:Put(emitter,0,std::uintptr_t{0});break;case 5:Put(emitter,0x18,c);break;
   case 6:Put(render,0x240,0);break;case 7:Put(render,0x28,std::uint8_t{1});break;case 8:Put(descriptor,0x2b0,std::uintptr_t{601});break;
   case 9:Put(render,0x48,q);break;}
  assert(!a.PrepareCoordinateReconstruction(b,out,100000,nullptr,{&mapping,1}).ok());
 }
 // Unchanged tile arrays still require typed ownership for render parameters.
 component=clean_component;emitter=clean_emitter;render=clean_render;descriptor=clean_descriptor;
 a.lease.live=b.lease.live=true;a.mapping_valid=true;b.components_.clear();
 a.gpu_owners_[0].component=c;a.gpu_owners_[0].emitter=e;a.gpu_owners_[0].render_storage=r;
 b.gpu_owners_=a.gpu_owners_;out.clear();
 assert(a.PrepareCoordinateReconstruction(b,out,100000,nullptr,{}).ok());
 if(out.size()!=1){std::puts("unchanged no-field GPU owner omitted from parameter bindings");return 4;}
 assert(!out[0].fresh&&!out[0].rebuild_tiles&&out[0].render==r&&out[0].previous_count==2);
 const auto stable=out[0];b.gpu_owners_[0].coordinate_resource=q+8;out.clear();
 assert(!a.PrepareCoordinateReconstruction(b,out,100000,nullptr,{}).ok());
 assert(stable.tiles.data()==a.gpu_owners_[0].tiles.data());
 VirtualFree(native,0,MEM_RELEASE);
 std::puts("Mapped coordinate preparation uses fresh constructor resources and no fictional B coordinates");
}
