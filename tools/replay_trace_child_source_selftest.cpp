#define NOMINMAX
#include <Windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
struct Object {std::array<std::byte,0x1000> bytes{};bool live{true};};
struct Id {Object* object{};};
struct Ref {std::byte* state{};std::byte* controller{};};
struct State {Ref ref;Id actor,attachment,mesh,animation;};
struct Root {Id component,chara,manager,scene;};
struct Image {std::byte* address{};std::vector<std::byte> bytes;};
struct Array {std::byte* data{};int count{},capacity{};};
struct DynamicImage {enum class Kind {Map,WeakReferences,ChildStrongReferences};std::byte* address{};std::array<std::byte,0x50> header{};std::vector<std::byte> bytes;Kind kind{};};
struct Status {bool good{true};bool ok() const{return good;}static Status success(){return {};}};
template<class T>T& At(void* p,std::size_t offset){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
struct Fixture {
 bool captured_{true};DWORD thread_{GetCurrentThreadId()};
 std::vector<Root> roots_;std::vector<State> states_;std::vector<Image> values_,bindings_;
 std::array<DynamicImage,10> dynamic_{};std::size_t dynamic_count_{1};
 static bool Live(const Id& id){return !id.object || id.object->live;}
 static Id Identify(Object* p){return {p&&p->live?p:nullptr};}
 static Status Fail(const char*){return {false};}
#include "trace_child_source.inl"
};
template<class T> void Saved(std::vector<Image>& into,void* owner,std::size_t offset,const T& v){
 Image image{reinterpret_cast<std::byte*>(owner)+offset,std::vector<std::byte>(sizeof(T))};
 std::memcpy(image.bytes.data(),&v,sizeof(T));into.push_back(std::move(image));
}
int main(){
 auto* expired=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));assert(expired);
 std::array<Object,10> objects{};auto* root=&objects[0];auto* asset=&objects[1];auto* list=&objects[2];
 auto* parts=&objects[3];auto* kinds=&objects[4];auto* kind=&objects[5];auto* meshdata=&objects[6];
 auto* mesh=&objects[7];auto* cls=&objects[8];
 std::array<Object*,2> part_entries{parts,nullptr};std::array<std::byte,48> settings{};
 std::array<std::byte,32> table{};
 At<Object*>(root,0x488)=asset;At<Object*>(asset,0x30)=list;
 At<Array>(list,0x30)={reinterpret_cast<std::byte*>(part_entries.data()),1,2};At<unsigned char>(parts,0x30)=7;
 At<Array>(parts,0x50)={settings.data(),2,2};At<Object*>(settings.data()+24,0x10)=kinds;
 At<unsigned>(settings.data()+24,4)=0x3f800000;At<unsigned char>(settings.data()+24,0)=3;
 At<Array>(kinds,0x30)={table.data(),1,2};At<unsigned char>(table.data(),0)=9;At<Object*>(table.data(),8)=kind;
 At<Object*>(kind,0x30)=meshdata;At<int>(kind,0x48)=-1;
 At<Object*>(meshdata,0x30)=mesh;At<Object*>(meshdata,0x38)=cls;At<int>(meshdata,0x50)=12;
 Fixture f;f.roots_={{{root},{&objects[9]},{&objects[9]}}};
 Ref ref{expired+16,expired};auto* historical_mesh=reinterpret_cast<Object*>(expired+0x1000);
 auto* historical_animation=reinterpret_cast<Object*>(expired+0x2000);
 f.states_={{ref,{},{},{historical_mesh},{historical_animation}}};
 auto& d=f.dynamic_[0];d.kind=DynamicImage::Kind::ChildStrongReferences;d.address=reinterpret_cast<std::byte*>(root)+0x428;
 At<Array>(d.header.data(),0)={expired+0x3000,1,1};d.bytes.resize(16);std::memcpy(d.bytes.data(),&ref,16);
 Saved(f.bindings_,root,0x488,asset);Saved(f.bindings_,root,0x49c,(unsigned char)0);
 Saved(f.bindings_,historical_mesh,0x910,mesh);Saved(f.bindings_,historical_animation,0x10,cls);
 Saved(f.bindings_,ref.state,0x58,(void*)nullptr);Saved(f.bindings_,ref.state,0x60,(void*)nullptr);Saved(f.bindings_,ref.state,0x68,(void*)nullptr);
 Saved(f.values_,ref.state,0,(unsigned char)7);Saved(f.values_,ref.state,1,(unsigned char)9);
 Saved(f.values_,ref.state,0x20,12);Saved(f.values_,ref.state,0xb0,-1);
 Saved(f.values_,ref.state,0x50,0x3f800000u);Saved(f.values_,ref.state,0x54,(unsigned char)3);
 int calls{};auto check=[&](){calls=0;return f.VisitHistoricalChildSources([&](const Fixture::ChildSource& s){
  ++calls;assert(s.historical.state==ref.state&&s.component==root&&s.parts==parts&&s.kind==kind&&s.mesh_asset==mesh&&s.animation_class==cls);return Status::success();
 }).ok();};
 assert(check()&&calls==1);
 const auto original=f;const auto originals=objects;const auto original_table=table;
 for(unsigned fault=0;fault<14;++fault){f=original;objects=originals;table=original_table;
  switch(fault){
   case 0:f.captured_=false;break;
   case 1:root->live=false;break;
   case 2:At<Object*>(root,0x488)=parts;break;
   case 3:At<unsigned char>(root,0x49c)=1;break;
   case 4:At<Array>(list,0x30).count=257;break;
   case 5:part_entries[1]=parts;At<Array>(list,0x30).count=2;break;
   case 6:At<Array>(parts,0x50).count=1;break;
   case 7:At<Object*>(table.data(),8)=nullptr;break;
   case 8:At<Object*>(meshdata,0x30)=cls;break;
   case 9:At<Object*>(meshdata,0x38)=mesh;break;
   case 10:At<int>(kind,0x48)=0;break;
   case 11:At<int>(meshdata,0x50)=13;break;
   case 12:f.bindings_.push_back(f.bindings_[0]);break;
   case 13:At<Array>(kinds,0x30).count=2;At<unsigned char>(table.data(),16)=9;At<Object*>(table.data(),8)=nullptr;At<Object*>(table.data(),24)=kind;break;
  }
  assert(!check()&&calls==0);
 }
 f=original;objects=originals;table=original_table;
 for(unsigned char charge:{1,2}){At<unsigned char>(root,0x49c)=charge;f.bindings_[1].bytes[0]=std::byte(charge);assert(check()&&calls==1);}
 assert(!f.VisitHistoricalChildSources([](const auto&){return Status{false};}).ok());
 VirtualFree(expired,0,MEM_RELEASE);
 std::puts("historical child source resolves without retired storage; malformed/ambiguous/changed assets reject");
}
