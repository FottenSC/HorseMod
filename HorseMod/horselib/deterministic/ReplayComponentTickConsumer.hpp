#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>

namespace Horse::Deterministic {
struct ReplayComponentTickFailure {std::uintptr_t table{},dispatch{};unsigned reason{};};
// Read-only admission for the concrete generic component tick. This is not a
// lifetime lease or cancellation protocol. Call before native task entry;
// 14215D250 already mutates tick interval state before invoking the component.
inline bool ReplayComponentTickConsumerAdmitted(std::uintptr_t base,
    std::uintptr_t tick_type, std::uintptr_t owner,ReplayComponentTickFailure* failure=nullptr) noexcept
{
    if(failure)*failure={};
    if (tick_type != base + 0x3865f98) return true;
    __try {
        if (!owner) {if(failure)failure->reason=1;return false;}
        const auto table = *reinterpret_cast<const std::uintptr_t*>(owner);
        if(failure)failure->table=table;
        if (!table || table > UINTPTR_MAX - 0x300 || table == base + 0x37fa218) {if(failure)failure->reason=2;return false;}
        const auto dispatch = *reinterpret_cast<const std::uintptr_t*>(table + 0x300);
        if(failure){failure->dispatch=dispatch;failure->reason=!dispatch?3:dispatch==base+0x1bcdb90?4:0;}
        // Niagara may initialize an inactive system, rebind interfaces and
        // consume shared RNG. Its mutable system state is not captured.
        return dispatch && dispatch != base + 0x1bcdb90;
    } __except (EXCEPTION_EXECUTE_HANDLER) {if(failure)failure->reason=5;return false;}
}
}
