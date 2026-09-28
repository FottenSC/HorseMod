#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse::Deterministic {
// Native142ED0A20 registers the UI data transaction dispatcher in the global
// tickable list.142F21FD0 owns request execution, completion callbacks and store
// commit. An empty streamable/latent queue says nothing about this dispatcher.
// Admission is read-only: never call its constructor, Tick or synchronous drain.
class ReplayUiTransactionAdmission final {
    template<class T> static T Read(const void* p,std::size_t offset=0) {
        T value{};std::memcpy(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));return value;
    }
public:
    struct Diagnostic {unsigned check{};std::uintptr_t owner{};int count{};};
    static bool Signature(std::uintptr_t base) noexcept {
        constexpr unsigned char code[]{0x48,0x89,0x5c,0x24,0x20,0x56,0x48,0x83,0xec,0x20,0x48,0x8d,0x71,0x08,0x48,0x8b};
        __try {return base && !std::memcmp(reinterpret_cast<const void*>(base+0x2f21fd0),code,sizeof(code));}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    // Separate address binding from traversal so the actual production policy
    // can be tested against native-layout storage without a game process.
    static bool CheckRegistry(const void* registry,const unsigned char* traversing,
        std::uintptr_t dispatcher_vtable,Diagnostic& diagnostic) noexcept {
        diagnostic={40,reinterpret_cast<std::uintptr_t>(registry),0};
        __try {
            if(!registry || !traversing || !dispatcher_vtable || *traversing)return false;
            const auto count=Read<int>(registry,8),capacity=Read<int>(registry,12);
            auto* const* entries=Read<void* const*>(registry);
            diagnostic.count=count;
            if(count<0 || capacity<count || capacity>65536 || (capacity && !entries))return false;
            for(int i=0;i<count;++i) {
                const auto* dispatcher=entries[i];
                if(!dispatcher || Read<std::uintptr_t>(dispatcher)!=dispatcher_vtable)continue;
                diagnostic={41,reinterpret_cast<std::uintptr_t>(dispatcher),Read<int>(dispatcher,16)};
                const auto queued=diagnostic.count,allocated=Read<int>(dispatcher,20);
                const auto* storage=Read<void*>(dispatcher,8);
                // A zero-request backend can still occupy the dispatcher list
                // until native retirement. Conservatively wait for that owner
                // to leave as well; do not infer completion from backend+B0.
                if(queued!=0 || allocated<0 || allocated>65536 || (allocated && !storage))return false;
            }
            if(*traversing || count!=Read<int>(registry,8) || capacity!=Read<int>(registry,12)
                || entries!=Read<void* const*>(registry))return false;
            diagnostic={};return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Check(std::uintptr_t base,Diagnostic& diagnostic) noexcept {
        if(!base){diagnostic={40,0,0};return false;}
        return CheckRegistry(reinterpret_cast<const void*>(base+0x439b6a0),
            reinterpret_cast<const unsigned char*>(base+0x439b570),base+0x3d8f5a0,diagnostic);
    }
};
}
