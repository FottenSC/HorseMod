// Compile actual production admission/publication methods with controlled UE
// metadata and native constructors. Native Blueprint/GPU behavior is live-only.
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <stdexcept>
#define STR(x) L##x
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
namespace RC {
inline const wchar_t* to_generic_string(const char*){return L"check";}
enum class LogLevel{Default};struct Output{template<LogLevel,class...T>static void send(const wchar_t*,T...) {}};
namespace Unreal {
struct UObject;struct UFunction;
struct FString {const wchar_t* p;std::uintptr_t pad{};static inline unsigned live{};FString(const wchar_t* x):p(x){++live;}~FString(){--live;}};
enum {FNAME_Find};struct FName{const wchar_t* p;FName(const wchar_t* p,int):p(p){}};
struct FProperty {std::size_t size=24,offset=16;std::size_t GetSize(){return size;}template<class T>T* ContainerPtrToValuePtr(void* p){return reinterpret_cast<T*>(static_cast<char*>(p)+offset);}};
struct FStrProperty:FProperty{FStrProperty(){size=16;offset=0;}};
template<class T>T* CastField(FProperty* p){return static_cast<T*>(p);}
enum class EFunctionFlags{FUNC_Native};
struct UObject {
 static inline struct Entry {void* get_function_address(){return reinterpret_cast<void*>(1);}} ProcessInternalInternal;
 UObject* outer{};UObject* type{};UObject* current{};UFunction* event{};
 bool valid=true;bool throwing{};bool accepted=true;unsigned publications{};
 UObject* GetClassPrivate(){return type;}UObject* GetOuterPrivate(){return outer;}
 template<class T>T* GetValuePtrByPropertyNameInChain(const wchar_t*){return reinterpret_cast<T*>(&current);}
 static inline const wchar_t* requested_event{};
 UFunction* GetFunctionByNameInChain(const wchar_t* name){requested_event=name;return event;}
 static bool IsReal(UObject* o){return o && o->valid;}
 void ProcessEvent(UFunction*,void*);
};
struct UFunction:UObject {
 FStrProperty name,command;FProperty data,menu_widget,target_widget,controller;unsigned extent=80;
 UFunction(){command.offset=32;data.offset=48;menu_widget={8,16};target_widget={8,24};controller={4,72};}
bool native=false;void* func=reinterpret_cast<void*>(1);
 void* GetFunc(){return func;}bool HasAnyFunctionFlags(EFunctionFlags){return native;}
 unsigned GetParmsSize(){return extent;}unsigned GetPropertiesSize(){return extent;}
 FProperty* FindProperty(FName n){
  if(!std::wcscmp(n.p,L"MenuName"))return &name;
  if(!std::wcscmp(n.p,L"CommandName"))return &command;
  if(!std::wcscmp(n.p,L"MenuWidget"))return &menu_widget;
  if(!std::wcscmp(n.p,L"TargetWidget"))return &target_widget;
  if(!std::wcscmp(n.p,L"ControllerId"))return &controller;
  return &data;
 }
};
struct Item{UObject* object{};bool valid=true;int serial=7;UObject* GetUObject()const{return object;}bool IsValid(bool)const{return valid;}int GetSerialNumber()const{return serial;}};
struct FUObjectArray{static inline Item item;static const Item* IndexToObject(int){return &item;}};
struct UObjectGlobals{static inline std::vector<UObject*> managers;static void FindAllOf(const wchar_t*,std::vector<UObject*>& out){out=managers;}};
}}
using namespace RC::Unreal;
namespace Horse::Deterministic {
struct ReplayTickIndex{enum class Phase{Complete,Recording};struct Witness{Phase phase=Phase::Complete;}state;Witness witness()const{return state;}};
enum class FailureCode{ContextUnavailable};
struct Sc6ReplayHost {
 enum class SessionExitPhase{Idle,Failed};struct Exit{SessionExitPhase phase{};FailureCode failure{};}session_exit_;
 enum class InteriorPhase{Holding,Releasing};enum class PauseBoundary{CompletedApplication,Simulation};enum class ApplicationPhase{Idle,Engine};enum class CapturePhase{Idle,Ready};
 DWORD thread_=GetCurrentThreadId();unsigned session_exit_hook_=1;bool exit=false,binding=true,seek=false,retire=true;
 ReplayTickIndex index_;InteriorPhase interior_phase_=InteriorPhase::Holding;PauseBoundary pause_boundary_=PauseBoundary::CompletedApplication;ApplicationPhase application_phase_=ApplicationPhase::Idle;
 struct Simulation{bool complete=true;struct State{unsigned tick=230;}state;bool interval_complete(){return complete;}State continuation(){return state;}} sim;Simulation* simulation_=&sim;
 bool historical_restore_=false,checkpoint_restoring_=false,surface_event_=false,seek_retirement_pending_=false,seek_retirement_failed_=false;
 struct Capture{CapturePhase phase=CapturePhase::Idle;}capture_operation_;struct Pending{bool active{};bool pending()const{return active;}}index_checkpoint_;
 std::atomic<bool> particle_command_pending_{};void* session_exit_function_{};int session_exit_function_index_=1,session_exit_function_serial_=7;std::uintptr_t image_base_{};
 bool SessionExitRequested()const{return exit || session_exit_.phase==SessionExitPhase::Failed;}bool CheckBinding(){return binding;}bool engine_idle()const{return application_phase_==ApplicationPhase::Idle;}
 bool SeekOwnsExecution()const{return seek;}bool CanRetireSeekCheckpoint()const{return retire;}
 bool CanRequestMenuExit()const noexcept;bool RequestMenuExit()noexcept;
};
static unsigned acquired{},retired{};static bool live{};static Sc6ReplayHost* active{};
static void EngineNative(std::uintptr_t,std::uintptr_t address,void*){if(address==0x2ed1370){CHECK(!live);live=true;++acquired;}else{CHECK(address==0x2ed6a80 && live);live=false;++retired;}}
#include "replay_menu_exit_methods.inl"
}
void UObject::ProcessEvent(UFunction*,void* p){
 ++publications;CHECK(p && Horse::Deterministic::live);
 CHECK(std::wcscmp(static_cast<FString*>(p)->p,L"BattleMenu")==0);
 CHECK(std::wcscmp(reinterpret_cast<FString*>(static_cast<char*>(p)+32)->p,L"LuxPauseMenu::GoBackToReplaySelect")==0);
 CHECK(*reinterpret_cast<void**>(static_cast<char*>(p)+16)==nullptr && *reinterpret_cast<void**>(static_cast<char*>(p)+24)==nullptr);
 if(throwing)throw std::runtime_error("controlled event rejection");
 if(accepted && std::wcscmp(requested_event,L"OnRequestInputCommand")==0)Horse::Deterministic::active->exit=true;
}
int main(){
 using namespace Horse::Deterministic;
 std::vector<unsigned char> binary(0x2ed6a90);const unsigned char init[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74},destroy[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83};
 std::memcpy(binary.data()+0x2ed1370,init,8);std::memcpy(binary.data()+0x2ed6a80,destroy,8);
 for(unsigned fault=0;fault<18;++fault){
  Sc6ReplayHost h;active=&h;h.image_base_=reinterpret_cast<std::uintptr_t>(binary.data());
  UObject type,scene,manager;UFunction stop,event;stop.outer=&type;event.outer=&type;scene.type=&type;scene.event=&event;manager.current=&scene;
  h.session_exit_function_=&stop;FUObjectArray::item={&stop};UObjectGlobals::managers={&manager};acquired=retired=0;live=false;
  switch(fault){case 1:h.seek=true;break;case 2:h.historical_restore_=true;break;case 3:h.surface_event_=true;break;
   case 4:h.particle_command_pending_=true;break;case 5:h.seek_retirement_pending_=true;break;case 6:h.binding=false;break;
   case 7:event.extent=48;break;case 8:event.data.offset=0;break;case 9:event.native=true;break;
   case 10:FUObjectArray::item.serial=8;break;case 11:UObjectGlobals::managers.push_back(&manager);break;
   case 12:scene.throwing=true;break;case 13:scene.accepted=false;break;case 14:h.pause_boundary_=Sc6ReplayHost::PauseBoundary::Simulation;break;
   case 15:h.thread_=0;break;case 16:h.capture_operation_.phase=Sc6ReplayHost::CapturePhase::Ready;break;case 17:binary[0x2ed1370]=0;break;}
  CHECK(h.RequestMenuExit()==(fault==0));CHECK(!live && acquired==retired && FString::live==0);
  CHECK(acquired==((fault==0 || fault==12 || fault==13)?1u:0u));
  if(fault==0){CHECK(!h.RequestMenuExit() && acquired==1 && scene.publications==1);}
  if(fault==12 || fault==13) CHECK(!h.RequestMenuExit() && acquired==1 && scene.publications==1);
  binary[0x2ed1370]=init[0];
 }
 std::puts("production menu exit admission/acquisition/retirement passed");
}
