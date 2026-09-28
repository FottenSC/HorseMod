// Compile the actual visitor with a bounded fake UObject inventory. Native
// lifetime checks stay in the real callers; this fixture checks selection.
#include <array>
#include <cstdint>
#include <cwchar>
#include <cstdio>
namespace RC {
enum class LoopAction { Continue, Break };
namespace Unreal {
using int32=std::int32_t;
struct FName { const wchar_t* text; FName(const wchar_t* p=L"Object"):text(p){}
    bool Equals(const FName& rhs) const {return std::wcscmp(text,rhs.text)==0;} };
enum EObjectFlags {RF_ClassDefaultObject=1,RF_ArchetypeObject=2};
struct UStruct {FName name;UStruct* parent{};
    static inline unsigned name_calls{};
    FName GetNamePrivate() const {++name_calls;return name;}
    UStruct* GetSuperStruct() const {return parent;}};
struct UClass:UStruct {};
struct UObject {
    UClass* type{};unsigned flags{};bool class_object{};
    static inline unsigned isa_calls{};
    UClass* GetClassPrivate() const {return type;}
    bool HasAnyFlags(EObjectFlags mask) const {return (flags&mask)!=0;}
    template<class T> bool IsA() const {++isa_calls;return class_object;}
};
namespace UObjectGlobals {
inline std::array<UObject*,12> objects{};
template<class Visitor> void ForEachUObject(Visitor visit) {
    for(unsigned i=0;i<objects.size();++i)
        if(visit(objects[i],0,i)==LoopAction::Break)break;
}
}
}}
#include "deterministic/Sc6ReplayObjectVisit.hpp"
#include "deterministic/ReplayHudOwnerRoute.hpp"
int main() {
    using namespace RC::Unreal;
    UClass root{{L"Object",nullptr}},wanted{{L"Wanted",&root}},derived{{L"Derived",&wanted}},other{{L"Other",&root}};
    UObject direct{&wanted},child{&derived},unrelated{&other},no_class{},cdo{&wanted,RF_ClassDefaultObject},
        archetype{&derived,RF_ArchetypeObject},metadata{&wanted,0,true};
    UObjectGlobals::objects={nullptr,&unrelated,&no_class,&cdo,&direct,&archetype,&metadata,&child,&unrelated,&unrelated,&unrelated,nullptr};
    std::array<UObject*,2> selected{};unsigned count{};
    if(!Horse::Deterministic::VisitReplayObjectsOfClass(L"Wanted",[&](UObject* p){
        if(count==selected.size())return false;selected[count++]=p;return true;
    }) || count!=2 || selected[0]!=&direct || selected[1]!=&child || UObject::isa_calls!=3)return 1;
    UObject::isa_calls=0;count=0;
    if(Horse::Deterministic::VisitReplayObjectsOfClass(L"Wanted",[&](UObject* p){
        ++count;return p!=&direct;
    }) || count!=1 || UObject::isa_calls!=1)return 2;
    UObject::isa_calls=0;
    if(!Horse::Deterministic::VisitReplayObjectsOfClass(L"Absent",[](UObject*){return false;}) || UObject::isa_calls)return 3;
    // Repeated classes share ancestry work, but object exclusions/visitor calls
    // remain per object. A new inventory must observe a changed class chain.
    UObjectGlobals::objects.fill(&unrelated);UStruct::name_calls=0;
    if(!Horse::Deterministic::VisitReplayObjectsOfClass(L"Wanted",[](UObject*){return false;})
        || UStruct::name_calls!=2)return 4;
    other.parent=&wanted;count=0;
    if(!Horse::Deterministic::VisitReplayObjectsOfClass(L"Wanted",[&](UObject*){++count;return true;})
        || count!=UObjectGlobals::objects.size())return 5;
    other.parent=&root;
    // Find two genuine class addresses sharing a direct-cache bucket. Alternate
    // positive and negative matches to prove collision keys cannot alias.
    std::array<UClass,129> types{};
    std::array<UClass*,64> buckets{};UClass* positive=nullptr;UClass* negative=nullptr;
    for(auto& type:types) {
        const auto key=reinterpret_cast<std::uintptr_t>(&type);
        auto& prior=buckets[((key>>4)^(key>>16))%buckets.size()];
        if(prior) {positive=prior;negative=&type;break;}
        prior=&type;
    }
    if(!positive || !negative)return 6;
    positive->name=FName(L"Wanted");negative->name=FName(L"Other");
    UObject yes{positive},no{negative};
    for(unsigned i=0;i<UObjectGlobals::objects.size();++i)UObjectGlobals::objects[i]=i%2?&no:&yes;
    count=0;UStruct::name_calls=0;
    if(!Horse::Deterministic::VisitReplayObjectsOfClass(L"Wanted",[&](UObject* p){++count;return p==&yes;})
        || count!=6)return 7;
    std::printf("Alternating collision ancestry reads=%u expected=2\n",UStruct::name_calls);
    const bool ancestry_repeated=UStruct::name_calls!=2;
    Horse::Deterministic::ReplayHudOwnerCensus census({});unsigned classifications{};
    for(unsigned i=0;i<12;++i){
        const auto key=i%2?0x1400u:0x1000u;
        if(census.MatchClass(key,[&](std::uintptr_t t){++classifications;return t==0x1000?1u:2u;})!=(i%2?2u:1u))return 9;
    }
    std::printf("Alternating HUD collision classifications=%u expected=2\n",classifications);
    if(ancestry_repeated || classifications!=2)return 10;
    // Capacity pressure may recompute, never alias a different class result.
    Horse::Deterministic::ReplayHudOwnerCensus crowded({});
    for(unsigned i=0;i<12;++i){const auto key=0x1000u+(i%3)*0x400u;
        if(crowded.MatchClass(key,[](std::uintptr_t t){return unsigned(t);})!=key)return 11;}
    std::puts("Object visitor selection, exclusions and early rejection passed; collisions and per-call invalidation passed");
}
