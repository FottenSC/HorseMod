#pragma once
#include "ReplayNiagaraObservation.hpp"
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <polyhook2/Detour/x64Detour.hpp>
#include <intrin.h>
#include <cstring>
#include <memory>

namespace Horse::Deterministic {
// Three unowned entries, installed before native workers in the verified initial
// constructor. Process pinning also covers partial installation. Never unhook.
// Deliberately separate from NativeReplayTraceTaskGuard::Call: observation has
// no retirement match, admission veto, hold, cancellation or callback policy.
class NativeReplayNiagaraObservation final {
public:
    static bool InstallStartup(std::uintptr_t base,bool initial_constructor) {
        if(!base || !initial_constructor || hooks_)return false;
        if(!StartupDomain(base))return false;
        for(unsigned i=0;i<rvas_.size();++i)if(std::memcmp(reinterpret_cast<void*>(base+rvas_[i]),signatures_[i].data(),32))return false;
        HMODULE module{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Location),&module))return false;
        base_=base;hooks_=new Hooks{};
        const std::array<std::uintptr_t,3> entries{reinterpret_cast<std::uintptr_t>(&Location),reinterpret_cast<std::uintptr_t>(&Attached),reinterpret_cast<std::uintptr_t>(&Register)};
        for(unsigned i=0;i<rvas_.size();++i){hooks_->entries[i]=std::make_unique<PLH::x64Detour>(base+rvas_[i],entries[i],&originals_[i]);if(!hooks_->entries[i]->hook())return false;}
        installed_=true;return true;
    }
    static bool Start(const wchar_t* path,const char* run) noexcept {return installed_ && ReplayNiagaraObservation::Start(path,run);}
    static std::size_t ProcessOwnedBytes() noexcept{return ReplayNiagaraObservation::OwnedBytes()+(hooks_?sizeof(Hooks)+3*(sizeof(PLH::x64Detour)+65536):0);}
private:
    static bool StartupDomain(std::uintptr_t base) noexcept {
        __try {for(const auto rva:{0x4166720u,0x4168038u,0x4168040u,0x4148b90u,0x44379e0u})
            if(*reinterpret_cast<const std::uintptr_t*>(base+rva))return false;return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    // Read-only indexed snapshots, NOT leases. Serial0 means no generation was
    // assigned; never allocate a serial or scan the GUObjectArray for logging.
    static bool Identity(std::uintptr_t object,ReplayNiagaraObservation::Record& record,unsigned offset) noexcept {
        auto& v=record.values;v[offset]=object;v[offset+1]=~0ull;
        if(!object)return false;
        __try {
            const auto index=*reinterpret_cast<const std::int32_t*>(object+0xc);if(index<0)return false;
            auto* item=RC::Unreal::FUObjectArray::IndexToObject(index);
            if(!item || reinterpret_cast<std::uintptr_t>(item->GetUObject())!=object || !item->IsValid(false))return false;
            const auto serial=item->GetSerialNumber();
            if(serial<0)return false;
            const auto table=*reinterpret_cast<const std::uintptr_t*>(object);
            if((*reinterpret_cast<const unsigned*>(object+8)&0x18000u) || item->GetSerialNumber()!=serial
                || reinterpret_cast<std::uintptr_t>(item->GetUObject())!=object)return false;
            v[offset+1]=index;v[offset+2]=serial;v[offset+3]=serial>0?1:2;v[offset+4]=table;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static ReplayNiagaraObservation::Token Before(unsigned helper,void* input,std::uintptr_t caller,void* registration_world=nullptr) noexcept {
        if(!ReplayNiagaraObservation::Enabled())return {};
        std::uint64_t epoch{};__try {epoch=*reinterpret_cast<const std::uint64_t*>(base_+0x4197170);}
        __except(EXCEPTION_EXECUTE_HANDLER){epoch=~0ull;}
        auto token=ReplayNiagaraObservation::Select(helper,rvas_[helper],caller,base_,epoch);
        if(token.selected){Identity(reinterpret_cast<std::uintptr_t>(input),token.entry,11);
            if(helper==2){for(unsigned i=0;i<5;++i)token.entry.values[16+i]=token.entry.values[11+i];
                Identity(reinterpret_cast<std::uintptr_t>(registration_world),token.entry,21);}
            else Identity(token.entry.values[9],token.entry,21);
            ReplayNiagaraObservation::Append(token.entry);}
        return token;
    }
    static void After(const ReplayNiagaraObservation::Token& token,void* result) noexcept {
        if(!token.selected)return;
        auto record=token.entry;record.values[2]=1;
        for(unsigned i=16;i<26;++i)record.values[i]=0;
        if(Identity(reinterpret_cast<std::uintptr_t>(result),record,16)) {
            std::uintptr_t world{};__try {world=*reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(result)+0x1c8);}
            __except(EXCEPTION_EXECUTE_HANDLER){}
            Identity(world,record,21);
        }
        ReplayNiagaraObservation::Append(record);
    }
    // Win64: RCX,RDX,R8,R9 pointers; pointer result in RAX.
    __declspec(noinline) static void* Location(void* context,void* asset,const void* location,const void* rotation) {
        const auto token=Before(0,context,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        const auto result=reinterpret_cast<void*(*)(void*,void*,const void*,const void*)>(originals_[0])(context,asset,location,rotation);
        After(token,result);return result;
    }
    // Win64: asset RCX, attach component RDX, packed name R8, location R9,
    // rotation stack+28, attachment mode int stack+30; pointer result RAX.
    __declspec(noinline) static void* Attached(void* asset,void* component,std::uint64_t name,const void* location,const void* rotation,int mode) {
        const auto token=Before(1,component,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        const auto result=reinterpret_cast<void*(*)(void*,void*,std::uint64_t,const void*,const void*,int)>(originals_[1])(asset,component,name,location,rotation,mode);
        After(token,result);return result;
    }
    static bool IsExactNiagara(void* component) noexcept {
        __try {return component && *reinterpret_cast<const std::uintptr_t*>(component)==base_+0x37fa218;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    // 141D58BA0: RCX component, RDX world, void. Native registration can
    // invoke +368, write +1C8, initialize via +280 and recursively register
    // children. Snapshot before those effects, including non-helper callers.
    // Exact-table filter only: unreadable/other tables and subclasses are not
    // a Niagara absence proof. Forward every call, including recursive calls.
    __declspec(noinline) static void Register(void* component,void* world) {
        const auto token=ReplayNiagaraObservation::Enabled() && IsExactNiagara(component)
            ?Before(2,component,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),world)
            :ReplayNiagaraObservation::Token{};
        reinterpret_cast<void(*)(void*,void*)>(originals_[2])(component,world);
        // No return value, success/completion claim, lifetime lease or reread:
        // the callback may have destroyed/reused the input while forwarding.
        if(token.selected){auto record=token.entry;record.values[2]=1;ReplayNiagaraObservation::Append(record);}
    }
    struct Hooks {std::array<std::unique_ptr<PLH::x64Detour>,3> entries;};
    inline static Hooks* hooks_{};
    inline static std::uintptr_t base_{};
    inline static bool installed_{};
    inline static std::array<std::uint64_t,3> originals_{};
    inline static constexpr std::array<std::uintptr_t,3> rvas_{0x1bcc5a0,0x1bcc730,0x1d58ba0};
    inline static constexpr std::array<std::array<unsigned char,32>,3> signatures_{{
        {0x40,0x56,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50,0x4d,0x8b,0xf1,0x4d,0x8b,0xf8,0x48,0x8b,0xf2,0x48,0x85,0xd2,0x0f,0x84,0x5a,0x01,0,0,0x48,0x8b,0xd1,0x48},
        {0x4c,0x89,0x44,0x24,0x18,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x50,0x45,0x33,0xe4,0x4d,0x8b,0xf9,0x48,0x8b,0xfa,0x4c,0x8b,0xf1},
        {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x57,
         0x48,0x83,0xec,0x40,0x48,0x63,0x41,0x0c,0x33,0xed,0x3b,0x05,0x9c,0x85,0x54,0x02}}};
};
}
