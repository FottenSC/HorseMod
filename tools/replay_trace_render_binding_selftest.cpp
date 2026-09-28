// Production retained-trace render admission; weak identity is a controlled dependency.
#include <Windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
struct Object {std::array<std::byte,0x1000> bytes{};int index{},serial{1};bool alive{true};};
struct Id {Object* object{};int index{},serial{};};
struct Ref {std::byte* state{};std::byte* controller{};};
struct State {Ref ref;Id actor,attachment,mesh,animation;};
struct Root {Id component,chara,manager,scene;};
struct Image {std::byte* address{};std::vector<std::byte> bytes;std::uint32_t mutable_bits{};};
template<class T>T& At(void* p,std::size_t offset){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
struct Fixture {
 bool captured_=true;std::uintptr_t base_=0x140000000;
 std::vector<State> states_;std::vector<Root> roots_;std::vector<Image> bindings_;
 static bool Live(const Id& id){return !id.object || (id.object->alive && id.object->index==id.index && id.object->serial==id.serial);}
#include "trace_render_binding_methods.inl"
};
int main(){
 std::array<Object,6> objects{};std::array<std::byte,0x108> control{};
 auto identify=[&](unsigned i){objects[i].index=int(i);return Id{&objects[i],int(i),1};};
 Fixture f;State state;state.actor=identify(0);state.mesh=identify(1);state.animation=identify(2);
 state.ref={control.data()+16,control.data()};f.states_.push_back(state);f.roots_.push_back({identify(3),identify(4),identify(5)});
 auto* mesh=state.mesh.object;At<std::uintptr_t>(mesh,0)=f.base_+0x38829c0;At<unsigned>(mesh,0x420)=860;
 At<std::uintptr_t>(mesh,0x910)=0x1234;At<std::uintptr_t>(control.data(),0)=f.base_+0x3362590;At<int>(control.data(),8)=2;
 At<Object*>(mesh,0x190)=state.actor.object;At<Object*>(state.actor.object,0x398)=mesh;
 At<Ref>(&objects[3],0x500)=state.ref;
 for(const auto pair:{std::pair{reinterpret_cast<std::byte*>(mesh)+0x190,8u},std::pair{reinterpret_cast<std::byte*>(state.actor.object)+0x398,8u},std::pair{objects[3].bytes.data()+0x500,16u}})
  f.bindings_.push_back({pair.first,std::vector<std::byte>(pair.first,pair.first+pair.second),0});
 for(const auto pair:{std::pair{reinterpret_cast<std::byte*>(mesh)+0x420,4u},std::pair{reinterpret_cast<std::byte*>(mesh)+0x910,8u}})
  f.bindings_.push_back({pair.first,std::vector<std::byte>(pair.first,pair.first+pair.second),0});
 const auto original=objects;const auto original_control=control;
 const auto check=[&](bool dormant=true){return f.RetainedRenderBinding(reinterpret_cast<std::uintptr_t>(mesh),{1,1},860,0x1234,dormant);};
 assert(check());assert(f.RetainedDormantRenderBinding(reinterpret_cast<std::uintptr_t>(mesh)));
 for(unsigned fault=0;fault<13;++fault){objects=original;control=original_control;
  switch(fault){
   case 0:objects[1].alive=false;break;
   case 1:objects[1].serial=2;break;
   case 2:objects[0].alive=false;break;
   case 3:objects[3].alive=false;break;
   case 4:At<int>(control.data(),8)=0;break;
   case 5:At<std::uintptr_t>(control.data(),0)=0;break;
   case 6:At<unsigned>(mesh,0x420)=861;break;
   case 7:At<std::uintptr_t>(mesh,0x910)=0;break;
   case 8:At<void*>(mesh,0x790)=mesh;break;
   case 9:At<void*>(mesh,0xa18)=mesh;break;
   case 10:At<unsigned>(mesh,0x188)=0x20;break;
   case 11:At<unsigned>(mesh,0x240)=0x10;break;
   case 12:At<Ref>(&objects[3],0x500)={};break;
  }
  if(f.RetainedDormantRenderBinding(reinterpret_cast<std::uintptr_t>(mesh)) && fault!=11)return 2;
  if(check()){std::printf("unexpected trace admission fault=%u\n",fault);return 1;}
 }
 objects=original;control=original_control;At<unsigned>(mesh,0x240)=0x10;
 assert(check(false) && !check());assert(f.RetainedDormantRenderBinding(reinterpret_cast<std::uintptr_t>(mesh)));
 auto missing=f.bindings_.back();f.bindings_.pop_back();assert(!f.RetainedDormantRenderBinding(reinterpret_cast<std::uintptr_t>(mesh)));f.bindings_.push_back(missing);
 f.bindings_.push_back(missing);assert(!f.RetainedDormantRenderBinding(reinterpret_cast<std::uintptr_t>(mesh)));f.bindings_.pop_back();
 f.captured_=false;assert(!f.RetainedDormantRenderBinding(reinterpret_cast<std::uintptr_t>(mesh)));assert(!check(false));
 std::puts("retained trace binding rejects dead generations, changed membership/assets and non-dormant work");
}
