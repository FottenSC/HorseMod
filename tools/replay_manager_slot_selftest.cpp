#include <Windows.h>
#include <array>
#include <span>
#include <cstdint>
#include <cstring>
#include <cassert>
enum class FailureCode {RestorePreflightFailed,UnsupportedContent,ContextUnavailable};
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
template<class T>T At(const void* p,std::size_t n){T v;std::memcpy(&v,static_cast<const std::byte*>(p)+n,sizeof(v));return v;}
template<class T>void Put(void* p,std::size_t n,const T& v){std::memcpy(static_cast<std::byte*>(p)+n,&v,sizeof(v));}
const std::byte* Provider(const std::byte* p){return At<const std::byte*>(p,0x90);}
bool CopyBytes(void* a,const void* b,std::size_t n){__try{std::memcpy(a,b,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
struct Sc6ReplayVfxState {
 struct Identity {std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};};
 struct ReconstructionBinding {Identity identity;};
 struct Slot {std::array<std::byte,0xc0> bytes{};};
 struct ManagerSlotProjection {std::array<std::byte,0xc0> bytes{};std::array<std::byte,48> provider{};};
 std::array<Slot,1> slots_;std::size_t constructed_=1;
 Status ProjectManagerSlot(std::size_t,std::span<const ReconstructionBinding>,ManagerSlotProjection&)const noexcept;
};
#include "manager_slot.inl"
int main(){
 Sc6ReplayVfxState a;auto& source=a.slots_[0].bytes;
 for(std::size_t i=0;i<source.size();++i)source[i]=std::byte(i);
 std::array<std::byte,48> provider;for(std::size_t i=0;i<provider.size();++i)provider[i]=std::byte(i+20);
 std::array<Sc6ReplayVfxState::ReconstructionBinding,2> bindings{{{{1,3,{7,11},{9,13}}},{{2,4,{8,12},{10,14}}}}};
 Put(source.data(),0,std::uintptr_t{1});Put(source.data(),0x68,std::uintptr_t{2});
 Put(source.data(),0xb0,int{3});Put(source.data(),0xa0,provider.data());Put(provider.data(),8,bindings[1].identity.source_weak);
 // An equal numeric value in an unrelated request field is not a pointer.
 Put(source.data(),0x30,std::uintptr_t{1});const auto original=source;const auto original_provider=provider;
 Sc6ReplayVfxState::ManagerSlotProjection out;
 assert(a.ProjectManagerSlot(0,bindings,out).ok());
 assert(At<std::uintptr_t>(out.bytes.data(),0)==3&&At<std::uintptr_t>(out.bytes.data(),0x68)==4);
 assert((At<std::array<int,2>>(out.provider.data(),8)==bindings[1].identity.target_weak));
 assert(Provider(out.bytes.data()+0x10)==out.provider.data());
 auto expected=original;Put(expected.data(),0,std::uintptr_t{3});Put(expected.data(),0x68,std::uintptr_t{4});Put(expected.data(),0xa0,out.provider.data());
 auto expected_provider=original_provider;Put(expected_provider.data(),8,bindings[1].identity.target_weak);
 assert(out.bytes==expected&&out.provider==expected_provider&&source==original&&provider==original_provider);
 assert(a.ProjectManagerSlot(0,{},out).ok());expected=original;Put(expected.data(),0xa0,out.provider.data());
 assert(out.bytes==expected&&out.provider==original_provider);
 assert(!a.ProjectManagerSlot(1,bindings,out).ok());
 Put(source.data(),0xb0,int{2});assert(!a.ProjectManagerSlot(0,bindings,out).ok());
 Put(source.data(),0xb0,int{0});assert(!a.ProjectManagerSlot(0,bindings,out).ok());
 Put(source.data(),0xa0,std::uintptr_t{0});assert(a.ProjectManagerSlot(0,bindings,out).ok());
 Put(source.data(),0xb0,int{3});assert(!a.ProjectManagerSlot(0,bindings,out).ok());
 Put(source.data(),0xa0,std::uintptr_t{1});assert(!a.ProjectManagerSlot(0,bindings,out).ok());
}
