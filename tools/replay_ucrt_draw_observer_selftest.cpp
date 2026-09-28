#include <Windows.h>
#include "UcrtRandBroker.hpp"
#include "Schema.hpp"
#include <atomic>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <tuple>
#include <thread>
using namespace Horse::Deterministic;
#include "independent_crt_observer.inl"
struct ObservedDraw {
 unsigned tick{},thread{};std::uintptr_t caller{};int result{};unsigned before{},after{};bool valid{},schema{};
 std::string_view lane;unsigned combat_before{},combat_after{};std::uint64_t combat_draws{},native_draws{},unknown_draws{},epoch{};
};
ObservedDraw observed;unsigned records{},native_calls{};
namespace RC {
const char* to_generic_string(const char* value) { return value; }
enum class LogLevel {Default};
namespace Output {
template<LogLevel,typename... Args> void send(const char* format,Args... args) {
 ++records;
 if constexpr(sizeof...(Args)==14) {
  const auto row=std::tuple(args...);
  observed={unsigned(std::get<0>(row)),unsigned(std::get<1>(row)),std::uintptr_t(std::get<2>(row)),
   int(std::get<3>(row)),unsigned(std::get<4>(row)),unsigned(std::get<5>(row)),bool(std::get<6>(row)),
   std::string_view(format).find("schema=3")!=std::string_view::npos,
   std::get<7>(row),unsigned(std::get<8>(row)),unsigned(std::get<9>(row)),
   std::uint64_t(std::get<10>(row)),std::uint64_t(std::get<11>(row)),
   std::uint64_t(std::get<12>(row)),std::uint64_t(std::get<13>(row))};
 }
}
}
}
#define STR(value) value
std::uintptr_t caller=0x10895d6e;
#define _ReturnAddress() reinterpret_cast<void*>(caller)
struct ReplayDiagnosticTrace {enum {Rng=2};};
bool diagnostics=true;unsigned admission_calls{};
bool ReplayDiagnosticAllowed(unsigned subsystem,unsigned tick) {assert(subsystem==2 && tick==617);++admission_calls;return diagnostics;}
bool SafeRead(std::uintptr_t address,std::uint32_t& tick) {assert(address==0x1470d0c4);tick=617;return true;}
struct DeterministicHookSet {
 inline static std::atomic<unsigned> callbacks_in_flight_{};
 inline static std::atomic<DeterministicHookSet*> active_{};
 UcrtRandFn original_rand_{};UcrtRandBroker* ucrt_broker_{};std::uintptr_t image_base_{0x10000000};
 static int __cdecl UcrtRandDetour() noexcept;
};
#include "ucrt_draw_detour.inl"
int NativeDraw(){++native_calls;return std::rand();}
unsigned next(unsigned state){return state*214013u+2531011u;}
template<class Broker> Status IndependentState(Broker& broker,unsigned thread,unsigned& state) {
 if constexpr(requires {broker.ObserveNative(thread,state);}) return broker.ObserveNative(thread,state);
 else {UcrtRandBrokerImage image;auto result=broker.Capture(thread,image);state=image.state;return result;}
}
int main() {
 const auto thread=GetCurrentThreadId();constexpr unsigned seed=0x01234500;
 UcrtRandBroker independent;assert(independent.Start().ok());
 assert(independent.BindNative(&std::rand,&std::srand).ok());
 std::srand(seed);unsigned state{};
 if(!IndependentState(independent,thread,state).ok() || state!=seed) {
  std::puts("independent observer cannot read real native CRT state without hook ownership");return 45;
 }
 assert(ReadReplayNativeCrt(state) && state==seed);
 assert(ReadReplayNativeCrt(state) && state==seed); // Observation cannot advance the stream.
 assert(independent.mode()==UcrtRandBrokerMode::Observing && independent.owner_thread_id()==0);
 assert(!IndependentState(independent,thread+1,state).ok());
 assert(std::rand()==int((next(seed)>>16)&0x7fff));
 assert(IndependentState(independent,thread,state).ok() && state==next(seed));
 assert(ReadReplayNativeCrt(state) && state==next(seed));
 std::rand();assert(IndependentState(independent,thread,state).ok() && state==next(next(seed)));
 std::thread other([&]{unsigned ignored{};assert(!IndependentState(independent,GetCurrentThreadId(),ignored).ok());assert(!ReadReplayNativeCrt(ignored));});other.join();
 assert(std::rand()==int((next(next(next(seed)))>>16)&0x7fff));
 independent.Stop();assert(!IndependentState(independent,thread,state).ok());
 UcrtRandBroker broker;assert(broker.Start().ok());assert(broker.BindNative(&std::rand,&std::srand).ok());
 broker.HandleSrand(thread,Schema::Sc6UcrtLayout::rng_init_srand_return_rva,seed,&std::srand);
 assert(broker.AcquireOwnership(thread).ok());
 DeterministicHookSet hooks;hooks.original_rand_=&NativeDraw;hooks.ucrt_broker_=&broker;
 DeterministicHookSet::active_=&hooks;
 const auto first=DeterministicHookSet::UcrtRandDetour();
 if(!observed.schema || observed.before!=seed || observed.thread!=thread) {
  std::puts("native RNG observation lacks actual thread and pre-draw state");return 42;
 }
 assert(records==1 && native_calls==1 && observed.tick==617 && observed.caller==0x895d6e
  && observed.result==first && first==int((next(seed)>>16)&0x7fff) && observed.after==next(seed) && observed.valid
  && observed.lane=="native_presentation" && observed.combat_before==seed && observed.combat_after==seed
  && observed.combat_draws==0 && observed.native_draws==1 && observed.unknown_draws==0 && observed.epoch!=0);
 caller=hooks.image_base_+Schema::Sc6UcrtLayout::movevm_rand_return_rva;
 const auto second=DeterministicHookSet::UcrtRandDetour();
 assert(records==2 && native_calls==1 && observed.before==next(seed) && observed.after==next(seed)
  && second==first && second==observed.result && observed.caller==Schema::Sc6UcrtLayout::movevm_rand_return_rva && observed.valid
  && observed.lane=="combat_private" && observed.combat_before==seed && observed.combat_after==next(seed)
  && observed.combat_draws==1 && observed.native_draws==1);
 // A direct shared CRT consumer between observed calls must be visible in
 // the next actual state; the observer must not manufacture continuity.
 std::rand();const auto shared=next(next(seed));
 caller=hooks.image_base_+0x896105;DeterministicHookSet::UcrtRandDetour();
 assert(records==3 && native_calls==2 && observed.before==shared && observed.after==next(shared));
 caller=hooks.image_base_+0x1111;DeterministicHookSet::UcrtRandDetour();
 assert(records==4 && native_calls==3 && observed.lane=="native_unresolved" && observed.unknown_draws==1
  && observed.combat_after==next(seed) && observed.native_draws==3);
 diagnostics=false;DeterministicHookSet::UcrtRandDetour();assert(records==4 && native_calls==4);
 diagnostics=true;
 std::thread foreign([&]{DeterministicHookSet::UcrtRandDetour();});foreign.join();
 assert(records==4 && native_calls==5); // Foreign TLS is forwarded, not mistaken for shared combat RNG.
 hooks.ucrt_broker_=nullptr;DeterministicHookSet::UcrtRandDetour();assert(records==4 && native_calls==6);
 assert(DeterministicHookSet::callbacks_in_flight_==0);
}
