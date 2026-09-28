// Compile the production flat iterator with controllable framework storage.
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <cstdio>
using int32=std::int32_t;
using uint8_t=std::uint8_t;
enum class LoopAction {Continue,Break};
struct UObject {int id;};
struct FUObjectItem {
    UObject* object{};bool unreachable{};
    static unsigned UEP_TotalSize(){return sizeof(FUObjectItem);}
    UObject* GetUObject()const{return object;}
    bool IsUnreachable()const{return unreachable || !object;}
};
struct TUObjectArray {
    FUObjectItem** data{};int32 count{};
    mutable unsigned count_reads{},data_reads{};
    const auto& GetNumElements()const{++count_reads;return count;}
    const auto& GetObjects()const{++data_reads;return data;}
};
struct FUObjectArray {
    TUObjectArray array;
    const auto& GetObjObjects()const{return array;}
};
FUObjectArray* GUObjectArray{};
#define GUOBJECTARRAY_PROFILE_ITER_BEGIN()
#define GUOBJECTARRAY_PROFILE_ITER_COUNT()
#define GUOBJECTARRAY_PROFILE_ITER_END()
#include "replay_flat_object_iterator.inl"
int main(){
    unsigned calls{};
    ForEachUObject_NonChunked([&](UObject*,int32,int32){++calls;return LoopAction::Continue;});
    if(calls)return 1;
    std::array<UObject,4> objects{{{1},{2},{3},{4}}};
    std::array<FUObjectItem,5> first{{{&objects[0]},{nullptr},{&objects[1],true}}};
    auto second=first;second[3]={&objects[2]};second[4]={&objects[3]};
    FUObjectArray root{{reinterpret_cast<FUObjectItem**>(first.data()),3}};
    GUObjectArray=&root;
    std::array<int,3> ids{},indices{};
    ForEachUObject_NonChunked([&](UObject* p,int32 chunk,int32 index){
        if(chunk || calls==ids.size())return LoopAction::Break;
        ids[calls]=p->id;indices[calls++]=index;
        if(index==0){root.array.data=reinterpret_cast<FUObjectItem**>(second.data());root.array.count=5;}
        return LoopAction::Continue;
    });
    if(calls!=3 || ids!=std::array<int,3>{1,3,4} || indices!=std::array<int,3>{0,3,4}
        || root.array.count_reads!=1 || root.array.data_reads!=1)return 2;
    calls=0;
    ForEachUObject_NonChunked([&](UObject*,int32,int32){++calls;return LoopAction::Break;});
    if(calls!=1)return 3;
    calls=0;
    ForEachUObject_NonChunked([&](UObject*,int32,int32){++calls;root.array.count=1;return LoopAction::Continue;});
    if(calls!=1)return 4;
    root.array.count=0;
    ForEachUObject_NonChunked([&](UObject*,int32,int32){++calls;return LoopAction::Continue;});
    if(calls!=1)return 5;
    std::puts("Flat inventory live growth, relocation, shrink, exclusions and early break passed");
}
