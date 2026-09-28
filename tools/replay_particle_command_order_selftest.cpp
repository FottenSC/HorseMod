// Actual host queue boundary with controlled native command-list dispatch.
#include <array>
#include <cstdint>
#include <cassert>
#include <type_traits>
#include <vector>
std::array<std::uintptr_t,32> list{};
std::array<std::uintptr_t,256> arena{};
std::vector<int> order;
void ResetList(){list[0]=0;list[1]=reinterpret_cast<std::uintptr_t>(&list[0]);list[6]=reinterpret_cast<std::uintptr_t>(arena.data());list[7]=reinterpret_cast<std::uintptr_t>(arena.data()+arena.size());}
void Flush(){
 auto* node=reinterpret_cast<std::uintptr_t*>(list[0]);
 while(node){auto* next=reinterpret_cast<std::uintptr_t*>(node[0]);reinterpret_cast<void(*)(void*,void*)>(node[1])(list.data(),node);node=next;}
 ResetList();
}
template<class T,class P>T& EngineField(P p,std::size_t offset){return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(p)+offset);}
template<class T=void,class...Args>T EngineNative(std::uintptr_t,std::uintptr_t offset,Args...){
 if constexpr(std::is_void_v<T>){assert(offset==0x15db1e0);Flush();}
 else {assert(offset==0x15edaf0);return reinterpret_cast<T>(list.data());}
}
struct Sc6ReplayHost {
 enum class ParticleCopyAction {PrepareExecutionCoordinates,RestoreUndo,CommitState,Poll};
 ParticleCopyAction particle_copy_action_=ParticleCopyAction::PrepareExecutionCoordinates;
 std::uintptr_t image_base_{};unsigned bodies{};bool nested{};
 void QueueParticleCopyCommand();
 static void ExecuteParticleCopyCommand(void*,void* command){
  auto* self=EngineField<Sc6ReplayHost*>(command,16);++self->bodies;order.push_back(2);
  if(!self->nested){self->nested=true;Flush();}
 }
};
#include "particle_command_order.inl"
int main(){
 bool threaded=false;Sc6ReplayHost h;h.image_base_=reinterpret_cast<std::uintptr_t>(&threaded)-0x434459e;
 ResetList();std::array<std::uintptr_t,3> preceding{0,reinterpret_cast<std::uintptr_t>(+[](void*,void*){order.push_back(1);}),0};
 list[0]=reinterpret_cast<std::uintptr_t>(preceding.data());list[1]=reinterpret_cast<std::uintptr_t>(&preceding[0]);
 h.QueueParticleCopyCommand();assert(h.bodies==1);assert((order==std::vector<int>{1,2}));assert(list[0]==0);
 // The command retains ordinary native queue ordering when no reentrant native API is used.
 h.particle_copy_action_=Sc6ReplayHost::ParticleCopyAction::Poll;h.nested=true;order.clear();h.QueueParticleCopyCommand();assert(h.bodies==2&&order==std::vector<int>{2});
}
