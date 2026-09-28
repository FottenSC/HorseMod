#pragma once
#include "ReplaySourceRegistration.hpp"
#include "Sc6ReplayObjectLease.hpp"
#include <Windows.h>

namespace Horse::Deterministic {
class Sc6ReplaySourceRegistrationState final {
    using Storage=ReplaySourceRegistration;
    std::uintptr_t base_{},manager_{};
    Storage::Image image_{};
    Sc6ReplayObjectLease lease_;
    static bool Read(void*,std::uintptr_t p,void* out,std::size_t size) noexcept {
        __try {std::memcpy(out,reinterpret_cast<void*>(p),size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Writable(std::uintptr_t p,std::size_t size) noexcept {
        if(!p || size>UINTPTR_MAX-p)return false;
        const auto end=p+size;
        while(p<end){
            MEMORY_BASIC_INFORMATION info{};
            if(!VirtualQuery(reinterpret_cast<void*>(p),&info,sizeof(info)) || info.State!=MEM_COMMIT
                || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))
                || !(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
            const auto next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
            if(next<=p)return false;p=next;
        }
        return true;
    }
    static bool Write(void*,std::uintptr_t p,const void* from,std::size_t size) noexcept {
        if(!Writable(p,size))return false;
        __try {std::memcpy(reinterpret_cast<void*>(p),from,size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Bindings() const noexcept {
        std::uintptr_t input{},tracker{};
        return lease_.Validate().ok() && Read(nullptr,manager_+0x478,&input,8) && input==image_.input
            && Read(nullptr,image_.source+0x390,&tracker,8) && tracker==base_+0x3290d20
            && Writable(image_.input+0x43d0,16) && Writable(image_.source+0x3c8,8);
    }
    static bool Bound(void* self) noexcept {return static_cast<Sc6ReplaySourceRegistrationState*>(self)->Bindings();}
    static void* Allocate(void* self,std::size_t size) {
        return reinterpret_cast<void*(*)(std::size_t)>(static_cast<Sc6ReplaySourceRegistrationState*>(self)->base_+0x4a61c0)(size);
    }
    static void Free(void* self,void* p) {
        reinterpret_cast<void(*)(void*)>(static_cast<Sc6ReplaySourceRegistrationState*>(self)->base_+0xd46a00)(p);
    }
    static bool CopyWrapper(void* self,std::uintptr_t owner,std::uint64_t token,void* destination) noexcept {
        const auto base=static_cast<Sc6ReplaySourceRegistrationState*>(self)->base_;
        // Native copy consumes exactly class/owner/token. It allocates no new
        // sequence ID and owns construction of the destination inline wrapper.
        alignas(16) std::array<std::uintptr_t,4> source{base+0x3298810,owner,0,token};
        __try {reinterpret_cast<void(*)(const void*,void*)>(base+0x418da0)(source.data(),destination);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static void DestroyWrapper(void* self,void* wrapper) {
        reinterpret_cast<void(*)(void*,std::uint64_t)>(static_cast<Sc6ReplaySourceRegistrationState*>(self)->base_+0x4f81f0)(wrapper,0);
    }
    Storage::Ops Ops() const noexcept {
        return {const_cast<Sc6ReplaySourceRegistrationState*>(this),base_,Read,Write,Bound,Allocate,Free,CopyWrapper,DestroyWrapper};
    }
    bool Signatures() const noexcept {
        constexpr std::array<unsigned char,7> alloc{0x33,0xd2,0xe9,0x99,0x95,0x8a,0x00};
        constexpr std::array<unsigned char,12> free{0x48,0x85,0xc9,0x74,0x1d,0x4c,0x8b,0x05,0xbc,0x07,0x45,0x03};
        constexpr std::array<unsigned char,16> copy{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xc2,0x48,0x8b,0xd9,0x48,0x8b,0xc8,0xba};
        constexpr std::array<unsigned char,16> destroy{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8d,0x05,0xc3,0xd4,0xd5,0x02,0x48,0x8b,0xd9};
        __try {return !std::memcmp(reinterpret_cast<void*>(base_+0x4a61c0),alloc.data(),alloc.size())
            && !std::memcmp(reinterpret_cast<void*>(base_+0xd46a00),free.data(),free.size())
            && !std::memcmp(reinterpret_cast<void*>(base_+0x418da0),copy.data(),copy.size())
            && !std::memcmp(reinterpret_cast<void*>(base_+0x4f81f0),destroy.data(),destroy.size());}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
public:
    using Prepared=Storage::Prepared;
    std::size_t owned_bytes() const noexcept {return sizeof(*this)+lease_.owned_bytes();}
    Status Capture(std::uintptr_t base,void* manager,std::uintptr_t source,std::size_t budget) noexcept {
        if(lease_.registered() || budget<sizeof(*this)+32768)return Status::failure(FailureCode::CapacityExceeded);
        base_=base;manager_=reinterpret_cast<std::uintptr_t>(manager);
        std::uintptr_t input{};
        if(!Read(nullptr,manager_+0x478,&input,8) || !input || !source || !Signatures())return Status::failure(FailureCode::GenerationMismatch);
        image_={input,source};
        std::array<void*,3> objects{manager,reinterpret_cast<void*>(input),reinterpret_cast<void*>(source)};
        auto status=lease_.Acquire(base,objects,budget-sizeof(*this));
        if(!status.ok())return status;
        return Storage::Capture(Ops(),input,source,image_)?Status::success():Status::failure(FailureCode::UnsupportedContent);
    }
    Status ValidateValues() const noexcept {return Storage::Matches(Ops(),image_)?Status::success():Status::failure(FailureCode::RestoreVerificationFailed);}
    Status Prepare(const Sc6ReplaySourceRegistrationState& original,std::size_t budget,Prepared& out) const noexcept {
        if(base_!=original.base_ || manager_!=original.manager_ || !Bindings() || !original.Bindings())return Status::failure(FailureCode::GenerationMismatch);
        return out.Prepare(original.Ops(),image_,original.image_,budget);
    }
};
}
