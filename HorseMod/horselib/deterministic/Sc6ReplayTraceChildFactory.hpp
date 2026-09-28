#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse::Deterministic {
// Acquisition journal for the exact child branch of native1408D1C00. The
// enclosing trace transaction validates the ChildSource and retains complete B
// before calling this boundary. Returned is acquisition, not publication or a
// lifetime/render-completion proof. No destructor silently retires native state.
struct Sc6ReplayTraceChildFactory final {
    struct Reference {std::byte* state{};std::byte* controller{};};
    enum class Phase : unsigned char {Empty,Calling,Returned,Failed};
    struct Journal {
        Reference reference{};
        std::uintptr_t component{},parts{},mesh_asset{};
        unsigned char kind{};
        Phase phase{Phase::Empty};
    };
    static bool Signature(std::uintptr_t base) noexcept {
        __try {
            constexpr unsigned char expected[]{0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,
                0x48,0x8d,0xac,0x24,0x30,0xff,0xff,0xff,0x48,0x81,0xec,0xd0,0x01,0x00,0x00};
            return base && base<=UINTPTR_MAX-0x8d1c00-sizeof(expected)
                && !std::memcmp(reinterpret_cast<void*>(base+0x8d1c00),expected,sizeof(expected));
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool PreservesSharedMeshBounds(std::uintptr_t base,std::uintptr_t mesh) noexcept {
        // 1408D1FD0 supplies these bounds to 1420BC8A0, which also recomputes
        // extended bounds via 1420A3700. Admit only an idempotent write. The
        // bounded shape has zero extensions; no shared asset is repaired here.
        __try {
            constexpr std::uint32_t zero[6]{};
            constexpr std::uint32_t imported[]{0,0,0,0x44c80000,0x44c80000,0x44c80000,0x452d3480};
            constexpr std::uint32_t extended[]{0,0,0,0x44c80000,0x44c80000,0x44c80000,0x44c80000};
            constexpr std::uint32_t radius_source=0x4aea6000;
            return base && base<=UINTPTR_MAX-0x418ac54 && mesh && mesh<=UINTPTR_MAX-0xa0
                && !std::memcmp(reinterpret_cast<void*>(base+0x418ac48),zero,12)
                && !std::memcmp(reinterpret_cast<void*>(base+0x33625b4),&radius_source,4)
                && !std::memcmp(reinterpret_cast<void*>(mesh+0x50),imported,sizeof(imported))
                && !std::memcmp(reinterpret_cast<void*>(mesh+0x6c),extended,sizeof(extended))
                && !std::memcmp(reinterpret_cast<void*>(mesh+0x88),zero,sizeof(zero));
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool Construct(std::uintptr_t base,std::uintptr_t component,std::uintptr_t parts,
        unsigned char kind,Journal& journal) noexcept {
        if(journal.phase!=Phase::Empty || journal.reference.state || journal.reference.controller
            || !component || !parts || !Signature(base) || !PreservesSharedMeshBounds(base,journal.mesh_asset))return false;
        journal.component=component;journal.parts=parts;journal.kind=kind;
        // The output pair resides in the retained operation before native code
        // runs. An exception poisons the acquisition; a timeout/cancel must not
        // pretend the unknown partial native allocation was undone.
        journal.phase=Phase::Calling;
        __try {
            using Factory=Reference*(*)(void*,Reference*,void*,int,unsigned char);
            auto* result=reinterpret_cast<Factory>(base+0x8d1c00)(reinterpret_cast<void*>(component),
                &journal.reference,reinterpret_cast<void*>(parts),1,kind);
            if(result!=&journal.reference || !journal.reference.controller
                || reinterpret_cast<std::uintptr_t>(journal.reference.controller)>UINTPTR_MAX-16
                || reinterpret_cast<std::uintptr_t>(journal.reference.state)!=reinterpret_cast<std::uintptr_t>(journal.reference.controller)+16) {
                journal.phase=Phase::Failed;return false;
            }
            journal.phase=Phase::Returned;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {journal.phase=Phase::Failed;return false;}
    }
};
}
