#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
#include <intrin.h>
std::uint64_t StubOriginal(std::uint64_t);
static unsigned hooks,forwarded;static bool nested,bad_arguments,null_result;
static std::uintptr_t image;
static std::uintptr_t registration_hook;
static unsigned registrations;
static bool registration_retire,registration_nested;
static constexpr std::array<unsigned char,32> registration_signature{
    0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x57,
    0x48,0x83,0xec,0x40,0x48,0x63,0x41,0x0c,0x33,0xed,0x3b,0x05,0x9c,0x85,0x54,0x02};
static std::array<unsigned char,0x200> input,component,world;
#define private public
#include "NativeReplayNiagaraObservation.hpp"
#undef private
using Native=Horse::Deterministic::NativeReplayNiagaraObservation;
using Observe=Horse::Deterministic::ReplayNiagaraObservation;
using Phase=Horse::Deterministic::ReplaySeekOwnership::Phase;
static void* OriginalAttached(void* asset,void* parent,std::uint64_t name,const void* location,const void* rotation,int mode) {
    ++forwarded;
    bad_arguments|=asset!=world.data() || parent!=input.data() || name!=0x123456789abcdef0ull
        || location!=reinterpret_cast<void*>(0x3333) || rotation!=reinterpret_cast<void*>(0x4444) || mode!=-1234567;
    return null_result?nullptr:component.data();
}
static void* OriginalLocation(void* context,void* asset,const void* location,const void* rotation) {
    ++forwarded;bad_arguments|=context!=input.data() || asset!=world.data() || location!=reinterpret_cast<void*>(0x1111) || rotation!=reinterpret_cast<void*>(0x2222);
    if(nested){nested=false;Native::Attached(world.data(),input.data(),0x123456789abcdef0ull,reinterpret_cast<void*>(0x3333),reinterpret_cast<void*>(0x4444),-1234567);}
    return null_result?nullptr:component.data();
}
static void OriginalRegister(void* object,void* target_world) {
    ++registrations;bad_arguments|=object!=component.data() || target_world!=world.data();
    if(registration_nested){registration_nested=false;
        reinterpret_cast<void(*)(void*,void*)>(registration_hook)(object,target_world);}
    if(registration_retire){
        RC::Unreal::FUObjectArray::items[2].object=nullptr;
        std::memset(component.data(),0xcc,component.size());
    }
}
std::uint64_t StubOriginal(std::uint64_t target) {++hooks;
    if(target==image+0x1d58ba0)return reinterpret_cast<std::uint64_t>(&OriginalRegister);
    return target==image+0x1bcc5a0?reinterpret_cast<std::uint64_t>(&OriginalLocation):reinterpret_cast<std::uint64_t>(&OriginalAttached);}
static void CaptureHook(std::uint64_t target,std::uint64_t replacement){if(target==image+0x1d58ba0)registration_hook=replacement;}
static bool CallBoth() {
    const auto expected=null_result?nullptr:component.data();
    return Native::Location(input.data(),world.data(),reinterpret_cast<void*>(0x1111),reinterpret_cast<void*>(0x2222))==expected
        && Native::Attached(world.data(),input.data(),0x123456789abcdef0ull,reinterpret_cast<void*>(0x3333),reinterpret_cast<void*>(0x4444),-1234567)==expected;
}
int main(int argc,char** argv) {
    const bool registration=argc>1 && std::strncmp(argv[1],"registration",12)==0;
    image=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4800000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!image)return 1;
    for(unsigned i=0;i<2;++i)std::memcpy(reinterpret_cast<void*>(image+Native::rvas_[i]),Native::signatures_[i].data(),32);
    std::memcpy(reinterpret_cast<void*>(image+0x1d58ba0),registration_signature.data(),32);
    if(Native::InstallStartup(image,false) || hooks)return 2;
    *reinterpret_cast<std::uintptr_t*>(image+0x4166720)=1;
    if(Native::InstallStartup(image,true) || hooks)return 3;
    *reinterpret_cast<std::uintptr_t*>(image+0x4166720)=0;
    *reinterpret_cast<unsigned char*>(image+0x1bcc730)=0;
    if(Native::InstallStartup(image,true) || hooks)return 4;
    std::memcpy(reinterpret_cast<void*>(image+0x1bcc730),Native::signatures_[1].data(),32);
    if(argc>1 && std::strcmp(argv[1],"registration-signature")==0){
        *reinterpret_cast<unsigned char*>(image+0x1d58ba0)^=1;
        return Native::InstallStartup(image,true) || hooks?84:0;
    }
    if(!Native::InstallStartup(image,true) || Native::InstallStartup(image,true))return 5;
    if(hooks!=3 || !registration_hook)return 85;
    if(Native::ProcessOwnedBytes()<Observe::OwnedBytes()+3*65536)return 86;
    *reinterpret_cast<std::uint64_t*>(image+0x4197170)=1234;
    unsigned i=1;for(auto* object:{&input,&component,&world}) {
        *reinterpret_cast<int*>(object->data()+0xc)=i;*reinterpret_cast<std::uintptr_t*>(object->data())=image+0x37fa218;
        RC::Unreal::FUObjectArray::items[i].object=object->data();RC::Unreal::FUObjectArray::items[i].serial=20+i;++i;
    }
    *reinterpret_cast<void**>(component.data()+0x1c8)=world.data();
    if(registration){
        if(!Native::Start(L"niagara_observation.json","fixture"))return 80;
        Observe::World(reinterpret_cast<std::uintptr_t>(world.data()));Observe::Tick(211);
        const auto call=reinterpret_cast<void(*)(void*,void*)>(registration_hook?registration_hook:reinterpret_cast<std::uintptr_t>(&OriginalRegister));
        if(std::strcmp(argv[1],"registration-full")==0){
            for(unsigned scope=0;scope<4;++scope){
                if(scope)Observe::Transaction(scope==1?Phase::ExecutionActive:scope==2?Phase::Recovering:Phase::Recovered);
                for(unsigned repeat=0;repeat<4;++repeat){call(component.data(),world.data());if(!CallBoth())return 87;}
            }
            std::uint64_t status[2]{};
            return bad_arguments || registrations!=16 || forwarded!=32 || Observe::count_!=24
                || !Observe::ReadStatus(status,2) || status[0]!=24 || status[1]?88:0;
        }
        registration_nested=registration_hook && std::strcmp(argv[1],"registration-nested")==0;
        const bool nested_expected=registration_nested;
        registration_retire=std::strcmp(argv[1],"registration-retire")==0;
        if(std::strcmp(argv[1],"registration-other")==0)*reinterpret_cast<std::uintptr_t*>(component.data())=image+0x335db28;
        if(std::strcmp(argv[1],"registration-invalid")==0)RC::Unreal::FUObjectArray::items[2].serial=0;
        call(component.data(),world.data());
        if(registrations!=(nested_expected?2:1) || bad_arguments)return 81;
        if(std::strcmp(argv[1],"registration-other")==0)return Observe::count_?82:0;
        // The missing producer fails after the real wrapper/original boundary,
        // never from a missing declaration or an invented lease/recovery fixture.
        if(Observe::count_!=2)return 83;
        return 0;
    }
    if(!CallBoth() || Observe::count_)return 6;
    if(Native::Start(L"niagara_observation.json","invalid_RUN"))return 7;
    if(!Native::Start(L"niagara_observation.json","fixture") || Native::Start(L"other.json","again"))return 8;
    if(argc>1 && std::strcmp(argv[1],"zero")==0)return 0;
    const bool writefail=argc>1 && std::strcmp(argv[1],"writefail")==0;
    if(writefail)CloseHandle(Observe::file_);
    Observe::World(reinterpret_cast<std::uintptr_t>(world.data()));Observe::Tick(211);
    if(argc>1 && std::strcmp(argv[1],"invalid")==0){RC::Unreal::FUObjectArray::items[1].object=nullptr;RC::Unreal::FUObjectArray::items[2].serial=0;}
    if(argc>1 && std::strcmp(argv[1],"null")==0)null_result=true;
    nested=true;
    for(unsigned scope=0;scope<4;++scope) {
        if(scope)Observe::Transaction(scope==1?Phase::ExecutionActive:scope==2?Phase::Recovering:Phase::Recovered);
        for(unsigned repetition=0;repetition<40;++repetition)if(!CallBoth())return 9;
    }
    if(bad_arguments || forwarded!=323 || Observe::count_!=16)return 10;
    std::uint64_t status[2]{};
    if(!Observe::ReadStatus(status,2) || status[0]!=16 || status[1]!=writefail)return 12;
    // Concurrent repeated entries cannot emit additional records or alter calls.
    std::vector<std::thread> workers;
    // Selection/persistence only: native forwarding counters above are owner-thread.
    for(unsigned j=0;j<4;++j)workers.emplace_back([]{for(unsigned k=0;k<100;++k)if(Observe::Select(0,1,2,3,4).selected)std::abort();});
    for(auto& worker:workers)worker.join();
    if(Observe::count_!=16)return 11;
    return 0;
}
