#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
struct Status{bool good;bool ok()const{return good;}};
struct Sc6ReplayObjectLease {bool valid=true;Status ValidateObject(const void*)const{return {valid};}};
template<class T>T& At(std::uintptr_t p,std::size_t o=0){return *reinterpret_cast<T*>(p+o);}
struct Subject {
 static inline std::uintptr_t base=0x140000000,proxy_address{},relevance_address{};
 static inline bool enabled=true,inactive=true,remove_ok=true,relevance_ok=true,create_ok=true,restore_ok=true;
 static inline unsigned removes{},rebuilds{},creates{},hidden_calls{},restores{};
 template<class T>static T Read(std::uintptr_t p,std::size_t o=0){
  if(p==base+0x40956b8)return T(enabled);
  if((p==base+0x394c100 || p==base+0x3949b60) && o==0x170)return T(base+0x1f981c0);
  if(p==base+0x3949d88 && o==0x170)return T(base+0x1f982a0);
  return At<T>(p,o);
 }
 template<class T>static bool InactiveRegistered(std::uintptr_t,const Sc6ReplayObjectLease& l,const T&){return inactive && l.valid;}
 static inline unsigned gpu_queues{};static inline bool gpu_queue_ok=true;
 static bool NativeRegisterGpuRender(std::uintptr_t,std::uintptr_t system,std::uintptr_t render){assert(system && render);++gpu_queues;return gpu_queue_ok;}
 static bool NativeRemoveRenderState(std::uintptr_t,std::uintptr_t c){++removes;At<unsigned>(c,0x188)&=~2u;return remove_ok;}
 static bool NativeRebuildRelevance(std::uintptr_t,std::uintptr_t c,std::uintptr_t){++rebuilds;assert(At<int>(c,0xa58)>0);At<std::uintptr_t>(c,0x8e0)=relevance_address;At<int>(c,0x8e8)=At<int>(c,0x8ec)=1;return relevance_ok;}
 static bool NativeCreateRenderState(std::uintptr_t,std::uintptr_t c){++creates;assert(!At<std::uintptr_t>(c,0xa50) && !At<int>(c,0xa58) && !At<int>(c,0xa5c));At<std::uintptr_t>(c,0x790)=proxy_address;At<unsigned>(c,0x188)|=2;return create_ok;}
 static bool SetPrivateEmitterHeader(std::uintptr_t c,const std::array<std::byte,16>& h){
  std::uintptr_t slots{};std::memcpy(&slots,h.data(),8);
  if(slots){++restores;if(!restore_ok)return false;}else ++hidden_calls;
  std::memcpy(reinterpret_cast<void*>(c+0xa50),h.data(),16);return true;
 }
#include "particle_proxy_handoff.inl"
 static void Reset(){enabled=inactive=remove_ok=relevance_ok=create_ok=restore_ok=true;removes=rebuilds=creates=hidden_calls=restores=0;}
};
int main(){
 std::array<std::byte,0xb00> component{};std::array<std::byte,0x300> emitter{};
 std::array<std::byte,0x200> particle_template{};std::array<std::byte,0x400> proxy{};std::uint64_t relevance{};
 const auto c=reinterpret_cast<std::uintptr_t>(component.data()),t=reinterpret_cast<std::uintptr_t>(particle_template.data()),e=reinterpret_cast<std::uintptr_t>(emitter.data());
 Subject::proxy_address=reinterpret_cast<std::uintptr_t>(proxy.data());Subject::relevance_address=reinterpret_cast<std::uintptr_t>(&relevance);
 std::array<std::uintptr_t,3> slots{0,e,0};
 Subject::Registration registration;registration.phase=Subject::Phase::Registered;registration.component=c;registration.world=100;registration.outer=200;registration.particle_template=t;
 Sc6ReplayObjectLease lease;
 const auto initial=[&]{Subject::Reset();component={};particle_template={};emitter={};proxy={};lease.valid=true;
  At<std::uintptr_t>(c)=Subject::base+0x335db28;At<unsigned>(c,0x188)=3;At<unsigned>(c,0x420)=8;At<std::uintptr_t>(c,0x808)=t;At<std::uintptr_t>(c,0x1c8)=100;At<std::uintptr_t>(c,0x190)=200;
  At<std::uintptr_t>(e)=Subject::base+0x394c100;At<std::uintptr_t>(e,0x18)=c;At<std::uintptr_t>(Subject::proxy_address)=Subject::base+0x3956e80;
 };
 const auto activate=[&]{At<unsigned>(c,0x188)|=0x40000;At<std::uintptr_t>(c,0xa50)=reinterpret_cast<std::uintptr_t>(slots.data());At<int>(c,0xa58)=At<int>(c,0xa5c)=3;};
 const auto create=[&](auto& r,std::size_t budget=2*1024*1024){return Subject::CreateActiveRenderState(Subject::base,lease,registration,r,budget);};
 using Phase=Subject::RenderPhase;
 for(unsigned fault=0;fault<4;++fault){initial();Subject::RenderPreparation r;
  if(fault==0)lease.valid=false;if(fault==1)At<unsigned>(c,0x830)=0x80;if(fault==2)Subject::inactive=false;if(fault==3)At<unsigned>(c,0x420)=0;
  assert(!Subject::RemoveInactiveRenderState(Subject::base,lease,registration,r) && !Subject::removes);
 }
 for(unsigned fault=0;fault<11;++fault){initial();Subject::RenderPreparation r;assert(Subject::RemoveInactiveRenderState(Subject::base,lease,registration,r));activate();
  switch(fault){case 0:lease.valid=false;break;case 1:Subject::enabled=false;break;case 2:At<unsigned char>(t,0xbc)=2;break;
   case 3:At<int>(c,0x908)=1;break;case 4:At<std::uintptr_t>(c,0xa70)=4;break;case 5:At<unsigned>(c,0x188)|=2;break;
   case 6:At<unsigned>(c,0x420)=9;break;case 7:At<int>(c,0xa5c)=129;break;case 8:At<std::uintptr_t>(e,0x18)=4;break;
   case 9:At<unsigned char>(t,0xf0)=2;break;
   case 10:At<unsigned char>(t,0xf0)=1;At<unsigned char>(t,0xb4)=1;break;
  }
  if(create(r) || Subject::rebuilds || Subject::creates){std::printf("unexpected native proxy admission fault=%u\n",fault);return 1;}
 }
 initial();Subject::RenderPreparation r;assert(Subject::RemoveInactiveRenderState(Subject::base,lease,registration,r));activate();
 assert(!create(r,0) && !Subject::rebuilds);assert(create(r));
 assert(r.phase==Phase::Created && !r.array_hidden && Subject::creates==1 && Subject::rebuilds==1 && Subject::restores==1);
 assert(At<std::uintptr_t>(c,0xa50)==reinterpret_cast<std::uintptr_t>(slots.data()) && At<int>(c,0xa58)==3 && slots[1]==e);
 assert(!create(r) && Subject::creates==1);
 for(unsigned fault=0;fault<3;++fault){initial();Subject::RenderPreparation f;assert(Subject::RemoveInactiveRenderState(Subject::base,lease,registration,f));activate();
  if(fault==0)Subject::relevance_ok=false;if(fault==1)Subject::create_ok=false;if(fault==2)Subject::restore_ok=false;
  assert(!create(f) && f.charged_bytes && f.phase!=Phase::Created);
  if(fault==1)assert(!f.array_hidden && At<int>(c,0xa58)==3 && f.phase==Phase::Failed);
  if(fault==2)assert(f.array_hidden && !At<int>(c,0xa58) && f.phase==Phase::Failed);
  const auto calls=Subject::creates;assert(!create(f) && Subject::creates==calls);
 }
 // Mixed sprite/mesh roots contribute native material relevance, while the
 // initial empty proxy packet must not consume their pending simulation work.
 std::array<std::byte,0x300> sprite{},mesh{};
 auto sp=reinterpret_cast<std::uintptr_t>(sprite.data()),mp=reinterpret_cast<std::uintptr_t>(mesh.data());
 initial();r={};assert(Subject::RemoveInactiveRenderState(Subject::base,lease,registration,r));activate();
 At<std::uintptr_t>(sp)=Subject::base+0x3949b60;At<std::uintptr_t>(sp,0x18)=c;
 At<std::uintptr_t>(mp)=Subject::base+0x3949d88;At<std::uintptr_t>(mp,0x18)=c;
 slots={sp,e,mp};assert(create(r));assert(slots[0]==sp&&slots[1]==e&&slots[2]==mp&&Subject::restores==1);
 initial();r={};assert(Subject::RemoveInactiveRenderState(Subject::base,lease,registration,r));activate();
 slots={sp,0,mp};assert(create(r));assert(slots[0]==sp&&slots[1]==0&&slots[2]==mp&&Subject::restores==1);
 initial();r={};assert(Subject::RemoveInactiveRenderState(Subject::base,lease,registration,r));activate();slots={};assert(!create(r));
 initial();r={};assert(Subject::RemoveInactiveRenderState(Subject::base,lease,registration,r));activate();
 At<std::uintptr_t>(sp)=Subject::base+0x394c100;slots={sp,e,mp};
 if(!create(r)){std::puts("two GPU roots rejected before native material/proxy handoff");return 2;}
 assert(slots[0]==sp&&slots[1]==e&&slots[2]==mp&&Subject::restores==1);
 slots={0,e,0};
 // Registration submits only initialized fresh storage and never retries native entry.
 initial();r={};assert(Subject::RemoveInactiveRenderState(Subject::base,lease,registration,r));activate();assert(create(r));
 std::array<std::byte,0x260> gpu_render{};auto gr=reinterpret_cast<std::uintptr_t>(gpu_render.data());
 At<std::uintptr_t>(e,0x1d0)=400;At<std::uintptr_t>(e,0x1e0)=gr;
 At<std::uintptr_t>(gr)=Subject::base+0x394bfc0;At<int>(gr,0x240)=-1;
 At<unsigned char>(gr,0x28)=At<unsigned char>(gr,0x218)=1;At<std::uintptr_t>(gr,0x48)=600;
 for(unsigned fault=0;fault<5;++fault){Subject::GpuRegistration j;auto saved=gpu_render;Subject::gpu_queues=0;
  switch(fault){case 0:At<int>(gr,0x240)=0;break;case 1:At<unsigned char>(gr,0x28)=0;break;
   case 2:At<unsigned char>(gr,0x218)=0;break;case 3:At<unsigned char>(gr,0x252)=1;break;case 4:At<std::uintptr_t>(gr,0x48)=0;break;}
  assert(!Subject::QueueGpuRenderRegistration(Subject::base,lease,registration,r,e,gr,j) && !Subject::gpu_queues);gpu_render=saved;
 }
 Subject::GpuRegistration j;assert(Subject::QueueGpuRenderRegistration(Subject::base,lease,registration,r,e,gr,j));
 assert(j.phase==Subject::GpuRegistrationPhase::Queued && Subject::gpu_queues==1);
 assert(!Subject::QueueGpuRenderRegistration(Subject::base,lease,registration,r,e,gr,j) && Subject::gpu_queues==1);
 j={};Subject::gpu_queue_ok=false;assert(!Subject::QueueGpuRenderRegistration(Subject::base,lease,registration,r,e,gr,j));
 assert(j.phase==Subject::GpuRegistrationPhase::Failed && !Subject::QueueGpuRenderRegistration(Subject::base,lease,registration,r,e,gr,j));
 std::puts("Private proxy handoff preserves emitter work and retains ownership on partial native failure");
}
