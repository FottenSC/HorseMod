#include <array>
#include <vector>
#include <cstdint>
#include <cassert>
struct Child {bool destroyed=true,witness=true;bool native_destroyed()const{return destroyed;}bool FreshGpuDestructionWitness()const{return witness;}};
struct Entry {Child* replacement;bool reconstructed=true;struct {std::uintptr_t target=123;std::array<int,2> target_weak{4,5};} identity;std::size_t ordinal=1;void* component=reinterpret_cast<void*>(123);};
struct Owner {std::uintptr_t address=123;std::array<int,2> weak{4,5};std::vector<std::uintptr_t> emitters{999,0};};
struct Observed {std::vector<Owner> components_{Owner{}};};
bool image=false,dead=false;bool gpu_storage_=true;
const void* FindImage(const Observed&,const Entry&,bool){return image?reinterpret_cast<void*>(1):nullptr;}
bool FreshComponentDeath(std::uintptr_t,const std::array<int,2>&){return dead;}
bool Check(const Observed& observed,const Entry& entry){
#include "member.inl"
return destroyed_member(entry);}
int main(){Child child;Entry entry{&child};Observed observed;
assert(Check(observed,entry));child.witness=false;assert(!Check(observed,entry));child.witness=true;
child.destroyed=false;assert(!Check(observed,entry));child.destroyed=true;
image=true;assert(!Check(observed,entry));image=false;
observed.components_[0].emitters[1]=7;assert(!Check(observed,entry));observed.components_[0].emitters[1]=0;
entry.reconstructed=false;assert(!Check(observed,entry));entry.reconstructed=true;
observed.components_.clear();assert(!Check(observed,entry));dead=true;assert(Check(observed,entry));
}
