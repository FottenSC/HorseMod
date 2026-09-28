#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>

namespace Horse::Deterministic
{
// Native 140333550 accumulates +138 without initialization by allocation,
// 140333E80 configuration or 140333130 preparation. Define the origin once,
// at first preparation. Restoring an already prepared node preserves its
// captured distance. No reference trajectory or historical image is read here.
class NativeWindConstruction final
{
public:
    bool Bind(std::uintptr_t base)
    {
        if (active_ || hook_) return false;
        constexpr std::array<unsigned char,12> signature{
            0x48,0x89,0x5c,0x24,0x08,0x55,0x48,0x8d,0x6c,0x24,0xa9,0x48};
        if (std::memcmp(reinterpret_cast<void*>(base+0x333130),signature.data(),signature.size())) return false;
        base_=base; thread_=GetCurrentThreadId(); initialized_=0; failed_=false;
        active_=this;
        hook_=std::make_unique<PLH::x64Detour>(base+0x333130,
            reinterpret_cast<std::uint64_t>(&Prepare),&original_);
        if(hook_->hook()) return true;
        hook_.reset(); active_=nullptr; return false;
    }
    bool Stop()
    {
        if(hook_ && !hook_->unHook()) return false;
        hook_.reset(); if(active_==this) active_=nullptr; return true;
    }
    ~NativeWindConstruction() { if(!Stop()) __fastfail(FAST_FAIL_INVALID_ARG); }
    bool failed() const noexcept {return failed_;}
    std::uint64_t initialized() const noexcept {return initialized_;}
private:
    inline static NativeWindConstruction* active_{};
    std::unique_ptr<PLH::x64Detour> hook_;
    std::uintptr_t base_{};
    std::uint64_t original_{}, initialized_{};
    DWORD thread_{};
    bool failed_{};
    static void Prepare(std::byte* node)
    {
        auto& self=*active_;
        const auto root=*reinterpret_cast<std::uintptr_t*>(self.base_+0x470e038);
        if(GetCurrentThreadId()!=self.thread_ || !node || !root
            || *reinterpret_cast<std::uintptr_t*>(node)!=self.base_+0x3e88ce8
            || *reinterpret_cast<std::uintptr_t*>(node+0x28)!=root
            || *reinterpret_cast<unsigned*>(node+0x68)!=0) self.failed_=true;
        else {
            // The root dispatch owns this linked, unprepared node. Confirm
            // membership before modifying even this constructor-owned scalar.
            auto* current=*reinterpret_cast<std::byte**>(root);
            unsigned count{};
            while(current && current!=node && count++<64)
                current=*reinterpret_cast<std::byte**>(current+0x10);
            if(current!=node) self.failed_=true;
            else {*reinterpret_cast<float*>(node+0x138)=0.0f; ++self.initialized_;}
        }
        reinterpret_cast<void (*)(std::byte*)>(self.original_)(node);
    }
};
}
