#include "RollbackStageWindSnapshot.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    template<typename T>
    void write(uint8_t* address, const T& value)
    {
        std::memcpy(address, &value, sizeof(value));
    }

    void seed_emitter(
        uint8_t* emitter,
        int32_t active,
        int32_t remaining,
        float base_timer,
        float reload_timer,
        float jitter)
    {
        write(emitter + 0x50, active);
        write(emitter + 0x54, remaining);
        write(emitter + 0x58, base_timer);
        write(emitter + 0x5C, reload_timer);
        write(emitter + 0xA4, jitter);
    }
}

int main()
{
    constexpr size_t region_bytes = 0x4722000;
    auto* base = static_cast<uint8_t*>(::VirtualAlloc(
        nullptr, region_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!base) return 1;

    auto release = [&]() { ::VirtualFree(base, 0, MEM_RELEASE); };
    uint8_t* sentinel = base + 0x1000;
    uint8_t* node0 = base + 0x1100;
    uint8_t* node1 = base + 0x1200;
    uint8_t* emitter0 = base + 0x2000;
    uint8_t* emitter1 = base + 0x2200;
    write(base + Horse::kRollbackStageWindEmitterListRva,
          reinterpret_cast<uintptr_t>(sentinel));
    write(base + Horse::kRollbackStageWindEmitterCountRva, uint64_t {2});
    write(sentinel, reinterpret_cast<uintptr_t>(node0));
    write(node0, reinterpret_cast<uintptr_t>(node1));
    write(node1, reinterpret_cast<uintptr_t>(sentinel));
    write(node0 + 0x10, reinterpret_cast<uintptr_t>(emitter0));
    write(node1 + 0x10, reinterpret_cast<uintptr_t>(emitter1));
    seed_emitter(emitter0, 1, 4, 0.25f, 0.75f, 1.5f);
    seed_emitter(emitter1, 0, 7, 0.5f, 1.25f, 2.0f);

    Horse::RollbackStageWindSnapshot snapshot {};
    auto report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), snapshot);
    if (!report.ok || report.count != 2
        || snapshot.canonical_hash == 0 || snapshot.integrity_hash == 0)
    {
        release();
        return 2;
    }

    Horse::RollbackStageWindSnapshot relocated = snapshot;
    relocated.sentinel += 0x100;
    relocated.emitters[0].list_node += 0x100;
    relocated.emitters[0].emitter += 0x100;
    relocated.integrity_hash =
        Horse::HashRollbackStageWindIntegrity(relocated);
    if (Horse::HashRollbackStageWindCanonical(relocated)
            != snapshot.canonical_hash
        || relocated.integrity_hash == snapshot.integrity_hash)
    {
        release();
        return 3;
    }

    seed_emitter(emitter0, 0, 99, 8.0f, 9.0f, 10.0f);
    report = Horse::RestoreRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), snapshot);
    if (!report.ok)
    {
        release();
        return 4;
    }
    Horse::RollbackStageWindSnapshot verified {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), verified);
    if (!report.ok
        || verified.integrity_hash != snapshot.integrity_hash
        || verified.canonical_hash != snapshot.canonical_hash)
    {
        release();
        return 5;
    }

    write(node0 + 0x10, reinterpret_cast<uintptr_t>(emitter1));
    report = Horse::RestoreRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), snapshot);
    if (report.ok
        || std::strcmp(report.failure,
            "stage-wind-emitter-ownership-changed") != 0)
    {
        release();
        return 6;
    }

    release();
    std::cout << "rollback stage wind snapshot self-test passed\n";
    return 0;
}
