#include "ReplayGroundDebrisConfiguration.hpp"
#include <Windows.h>
#include <cassert>
#include <cstring>
#include <set>
using Horse::Deterministic::ReplayGroundDebrisConfiguration;
struct Header {std::uintptr_t data{};int count{},capacity{};};
template<class T> void put(std::uintptr_t p,const T& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));}
template<class T> T get(std::uintptr_t p){T value{};std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return value;}
int main(){
 auto* memory=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
 const auto source=reinterpret_cast<std::uintptr_t>(memory),rows=source+0x100,parents=source+0x300;
 put(source,Header{rows,2,2});put(source+0x10,0x3f123456u);put(source+0x70,0x40123456u);
 // Native A617 has material-parent lists even with no auxiliary mesh.
 put(source+0x50,Header{source+0x380,1,2});put(source+0x380,std::uintptr_t{0x5000});
 put(source+0x60,Header{source+0x390,1,2});put(source+0x390,std::uintptr_t{});
 put(rows,std::uintptr_t{0x1000});put(rows+8,Header{parents,2,2});put(rows+0x18,Header{parents+16,1,1});
 put(rows+0x38,0x3fabcdefu);put(rows+0x50,std::uintptr_t{0x2000});
 put(parents,std::uintptr_t{0x3000});put(parents+8,std::uintptr_t{});put(parents+16,std::uintptr_t{0x4000});
 unsigned reads{};
 const auto read=[&](std::uintptr_t p,auto& value){++reads;if(p<source || p>source+4096-sizeof(value))return false;
  std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;};
 ReplayGroundDebrisConfiguration image;assert(image.Capture(read,source,65536));
 assert(image.owned_bytes()>sizeof(image));
 std::set<std::uintptr_t> assets;
 assert(image.Assets([&](auto p){assets.insert(p);return true;}));
 assert((assets==std::set<std::uintptr_t>{0x1000,0x2000,0x3000,0x4000,0x5000}));
 // A failed replacement must leave the previous complete recipe intact.
 assert(!image.Capture(read,source,1));
 put(source+0x38,std::uintptr_t{0x1234});assert(!image.Capture(read,source,65536));put(source+0x38,std::uintptr_t{});
 put(rows+8,Header{parents,33,33});assert(!image.Capture(read,source,65536));put(rows+8,Header{parents,2,2});
 assert(!image.Capture([](std::uintptr_t,auto&){return false;},source,65536));
 const auto reads_before=reads;
 assert(VirtualFree(memory,0,MEM_RELEASE));
 ReplayGroundDebrisConfiguration::NativeView view;
 assert(image.BuildNativeView(view));assert(reads==reads_before);
 const auto config=reinterpret_cast<std::uintptr_t>(view.configuration.data());
 const auto native=get<Header>(config);assert(native.count==2 && native.capacity==2 && native.data!=rows);
 assert(get<unsigned>(config+0x10)==0x3f123456u && get<unsigned>(config+0x70)==0x40123456u);
 const auto auxiliary_base=get<Header>(config+0x50),auxiliary_fade=get<Header>(config+0x60);
 assert(auxiliary_base.count==1 && auxiliary_base.capacity==1 && auxiliary_base.data!=source+0x380);
 assert(get<std::uintptr_t>(auxiliary_base.data)==0x5000);
 assert(auxiliary_fade.count==1 && !get<std::uintptr_t>(auxiliary_fade.data));
 assert(get<std::uintptr_t>(native.data)==0x1000 && get<unsigned>(native.data+0x38)==0x3fabcdefu);
 const auto first=get<Header>(native.data+8),second=get<Header>(native.data+0x18);
 assert(first.data!=parents && first.count==2 && second.data!=parents+16 && second.count==1);
 assert(get<std::uintptr_t>(first.data)==0x3000 && !get<std::uintptr_t>(first.data+8));
 assert(get<std::uintptr_t>(second.data)==0x4000);
 // Copying the immutable recipe must not borrow another recipe's vectors.
 auto copy=image;ReplayGroundDebrisConfiguration::NativeView copied;
 assert(copy.BuildNativeView(copied));
 const auto copied_rows=get<Header>(reinterpret_cast<std::uintptr_t>(copied.configuration.data()));
 assert(get<Header>(copied_rows.data+8).data!=first.data);
 assert(get<Header>(reinterpret_cast<std::uintptr_t>(copied.configuration.data())+0x50).data!=auxiliary_base.data);
}
