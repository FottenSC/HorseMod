// Exercise the production world Read/Write boundary on bounded native storage.
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cassert>
enum class FailureCode { IdentityMismatch, IllegalTransition, CapturePreflightFailed, ContextUnavailable };
struct Status {bool good; bool ok()const{return good;} static Status success(){return {true};} static Status failure(FailureCode){return {false};}};
template<class T> T ReadAt(const void* p,std::size_t o){T v;std::memcpy(&v,static_cast<const std::byte*>(p)+o,sizeof(v));return v;}
struct Sc6ReplayWorldState {
 struct Header {void* data;int count,capacity;};
 struct CameraBinding {};
 #include "foot_values.inl"
 static Status ReadCamera(std::uintptr_t,void*,CameraBinding&,Values&){return Status::success();}
 static bool ValidateCamera(std::uintptr_t,void*,const CameraBinding&){return true;}
 static bool WriteCamera(const CameraBinding&,const Values&){return true;}
 static Status Read(std::uintptr_t,void*,Values&,void*&,void*&,std::array<Header,3>&,CameraBinding&) noexcept;
 static bool Write(std::uintptr_t,void*,void*,const Values&,const std::array<Header,3>&,std::uint64_t,const CameraBinding&) noexcept;
};
#include "foot_read_write.inl"
int main(){
 auto* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(image);
 const auto base=reinterpret_cast<std::uintptr_t>(image);
 std::array<std::byte,0xa00> world{};std::array<std::byte,0x130> manager{};
 void* mp=manager.data();std::memcpy(world.data()+0x430,&mp,8);auto vt=base+0x39df498;std::memcpy(manager.data(),&vt,8);
 auto* phase=reinterpret_cast<int*>(image+0x470e920);
 phase[0]=1;phase[1]=4;phase[2]=1;phase[3]=4;
 // Distinct bits in every captured lane; neighboring unknown scalars stay B.
 std::memset(image+0x470e890,0x13,128);std::memset(image+0x470e910,0x21,16);
 std::memset(image+0x470e930,0x31,16);std::memset(image+0x470e948,0x41,16);
 std::memset(image+0x470e970,0x01,2);std::memset(image+0x470e99c,0x51,8);
 const std::array<std::uint32_t,4> contact_a{390,391,400,401}, contact_b{423,422,421,420};
 std::memcpy(image+0x470e984,contact_a.data(),sizeof(contact_a));
 Sc6ReplayWorldState::Values a{},b{},c{};Sc6ReplayWorldState::CameraBinding camera{};
 std::array<Sc6ReplayWorldState::Header,3> arrays{};void* m{};void* owner{};
 assert(Sc6ReplayWorldState::Read(base,world.data(),a,m,owner,arrays,camera).ok());
 phase[0]=4;phase[1]=1;phase[2]=4;phase[3]=1;
 std::memset(image+0x470e890,0x73,128);std::memset(image+0x470e910,0x71,16);
 std::memset(image+0x470e930,0x72,16);std::memset(image+0x470e948,0x74,16);
 std::memset(image+0x470e970,0x00,2);std::memset(image+0x470e99c,0x75,8);
 std::memcpy(image+0x470e984,contact_b.data(),sizeof(contact_b));
 std::memset(image+0x470e940,0x7a,8);
 std::memset(image+0x470e994,0x6b,8);
 assert(Sc6ReplayWorldState::Read(base,world.data(),b,m,owner,arrays,camera).ok());
 assert(Sc6ReplayWorldState::Write(base,world.data(),m,a,arrays,0,camera));
 assert(!std::memcmp(image+0x470e984,contact_a.data(),sizeof(contact_a)));
 // Native 14038E4C0 below its lower threshold: phase 4 emits a landing,
 // phase 1 does not. B's phase must not invent a new A footstep.
 const bool duplicate_landing=phase[0]==4;phase[0]=1;assert(!duplicate_landing);
 assert(phase[1]==4&&phase[2]==1&&phase[3]==4);
 assert(Sc6ReplayWorldState::Read(base,world.data(),c,m,owner,arrays,camera).ok());assert(c==a);
 assert(image[0x470e940]==std::byte{0x7a});
 assert(image[0x470e994]==std::byte{0x6b});
 assert(Sc6ReplayWorldState::Write(base,world.data(),m,b,arrays,0,camera));assert(phase[0]==4&&phase[1]==1&&phase[2]==4&&phase[3]==1);
 assert(!std::memcmp(image+0x470e984,contact_b.data(),sizeof(contact_b)));
 assert(Sc6ReplayWorldState::Write(base,world.data(),m,b,arrays,0,camera));
 assert(Sc6ReplayWorldState::Read(base,world.data(),c,m,owner,arrays,camera).ok());assert(c==b);
 // A partial write reports failure; B remains a separate recoverable image.
 DWORD old{};assert(VirtualProtect(image+0x470e000,0x1000,PAGE_READONLY,&old));
 assert(!Sc6ReplayWorldState::Write(base,world.data(),m,a,arrays,0,camera));
 assert(VirtualProtect(image+0x470e000,0x1000,old,&old));
 assert(Sc6ReplayWorldState::Write(base,world.data(),m,b,arrays,0,camera));
 assert(Sc6ReplayWorldState::Read(base,world.data(),c,m,owner,arrays,camera).ok());assert(c==b);
 VirtualFree(image,0,MEM_RELEASE);
}
