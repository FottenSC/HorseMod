#include "RollbackCharaAnimationState.hpp"

#include <Windows.h>

#include <array>
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

    struct Fixture
    {
        uint8_t* region {nullptr};
        uintptr_t charas[2] {};
        uint8_t* scheduler[2] {};
        uint8_t* head[2] {};
        uint8_t* nodes[2][2] {};
        uint8_t* triggers[2][2] {};
        uint8_t* controls[2][2] {};

        bool initialize()
        {
            region = static_cast<uint8_t*>(::VirtualAlloc(
                nullptr, 0x300000, MEM_RESERVE | MEM_COMMIT,
                PAGE_READWRITE));
            if (!region) return false;
            for (size_t player = 0; player < 2; ++player)
            {
                charas[player] = reinterpret_cast<uintptr_t>(
                    region + player * 0x100000);
                scheduler[player] =
                    region + 0x210000 + player * 0x1000;
                head[player] = region + 0x220000 + player * 0x1000;
                for (size_t index = 0; index < 2; ++index)
                {
                    nodes[player][index] =
                        region + 0x230000 + player * 0x1000 + index * 0x40;
                    triggers[player][index] =
                        region + 0x240000 + player * 0x1000 + index * 0x40;
                    controls[player][index] =
                        region + 0x250000 + player * 0x1000 + index * 0x40;
                }
                auto* chara = reinterpret_cast<uint8_t*>(charas[player]);
                auto* packed_data = region
                    + 0x260000 + player * 0x1000;
                auto* clip_data = region
                    + 0x270000 + player * 0x1000;
                write(chara
                        + Horse::kRollbackCharaAnimSlotControllerOffset,
                    reinterpret_cast<uintptr_t>(packed_data));
                auto* clip = chara
                    + Horse::kRollbackCharaAnimClipPlayerOffset;
                write(clip, charas[player]);
                write(clip + 0x08,
                    reinterpret_cast<uintptr_t>(clip_data));
                write(clip + 0x10, float {12.5f + float(player)});
                write(clip + 0x14, uint32_t {0});
                write(clip + 0x18, float {2.0f});
                write(clip + 0x1C, float {42.0f});
                write(clip + 0x20, uint32_t {1});
                write(clip + 0x24, uint32_t {3});
                write(clip + 0x28, uint32_t {1});
                write(clip + 0x2C, uint32_t {2});
                auto* clip_runtime = chara
                    + Horse::kRollbackCharaAnimRuntimeOffset;
                write(clip_runtime,
                    reinterpret_cast<uintptr_t>(clip_data));
                write(clip_runtime + 0x08, int32_t {3});
                write(clip_runtime + 0x0C,
                    float {12.5f + float(player)});

                auto* owner = chara
                    + Horse::kRollbackPoseEventCueOwnerOffset;
                write(owner, uintptr_t {0x140010000 + player * 0x100});
                write(owner + 0x08, uint64_t {0x1122334455667788});
                write(owner + 0x10, int32_t {4});
                write(owner + 0x14, int32_t {5});
                write(owner + 0x18, int32_t {6});
                write(owner + 0x1C, int32_t {-1});
                write(owner + 0x20, int32_t {7});
                write(owner + 0x24, int32_t {8});
                write(owner + 0x28, uintptr_t {0x150020000 + player * 0x100});
                write(owner + 0x30,
                    reinterpret_cast<uintptr_t>(scheduler[player]));

                write(scheduler[player], uintptr_t {0x140020000});
                write(scheduler[player] + 0x08, charas[player]);
                for (size_t offset = 0x10; offset < 0x70; offset += 4)
                    write(scheduler[player] + offset,
                        uint32_t(offset + player));
                write(scheduler[player] + 0x70,
                    reinterpret_cast<uintptr_t>(head[player]));
                write(scheduler[player] + 0x78, uint64_t {2});

                write(head[player],
                    reinterpret_cast<uintptr_t>(nodes[player][0]));
                write(head[player] + 0x08,
                    reinterpret_cast<uintptr_t>(nodes[player][1]));
                write(nodes[player][0],
                    reinterpret_cast<uintptr_t>(nodes[player][1]));
                write(nodes[player][0] + 0x08,
                    reinterpret_cast<uintptr_t>(head[player]));
                write(nodes[player][1],
                    reinterpret_cast<uintptr_t>(head[player]));
                write(nodes[player][1] + 0x08,
                    reinterpret_cast<uintptr_t>(nodes[player][0]));
                for (size_t index = 0; index < 2; ++index)
                {
                    write(nodes[player][index] + 0x10,
                        reinterpret_cast<uintptr_t>(
                            triggers[player][index]));
                    write(nodes[player][index] + 0x18,
                        reinterpret_cast<uintptr_t>(
                            controls[player][index]));
                    write(triggers[player][index], uintptr_t {0x140030000});
                    write(triggers[player][index] + 0x08,
                        int32_t {20 + int32_t(index)});
                    write(triggers[player][index] + 0x0C,
                        int32_t {30 + int32_t(index)});
                    write(triggers[player][index] + 0x10,
                        uint32_t {40 + uint32_t(index)});
                    write(triggers[player][index] + 0x14,
                        int32_t {50 + int32_t(index)});
                    write(triggers[player][index] + 0x18,
                        int32_t {60 + int32_t(index)});
                    write(triggers[player][index] + 0x1C,
                        uint32_t {70 + uint32_t(index)});
                }
            }
            return true;
        }

        ~Fixture()
        {
            if (region) ::VirtualFree(region, 0, MEM_RELEASE);
        }
    };

}

int main()
{
    Fixture fixture {};
    if (!fixture.initialize()) return 1;

    Horse::RollbackCharaAnimationStateHistory baseline {};
    if (!Horse::CaptureRollbackCharaAnimationState(
            fixture.charas, baseline)
        || !Horse::RollbackCharaAnimationHistoryValid(baseline)
        || baseline.players[0].trigger_count != 2)
    {
        return 2;
    }

    auto mutation = baseline;
    mutation.players[0].clip[0x10] ^= 0x40;
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 3;
    mutation = baseline;
    mutation.players[0].clip[0x14] ^= 1;
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 4;
    mutation = baseline;
    mutation.players[0].clip[0x18] ^= 1;
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 5;
    mutation = baseline;
    mutation.players[0].clip_runtime[0x0C] ^= 1;
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 20;
    mutation = baseline;
    std::memset(
        mutation.players[0].clip_runtime.data(),
        0, sizeof(uintptr_t));
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 21;
    // Dormant owner-runtime cleanup is actor-tick-local after the outer clip
    // player becomes inactive. It remains exact local state, but is not
    // peer-semantic until the player is active again.
    auto inactive = baseline;
    write(
        inactive.players[0].clip.data()
            + Horse::kRollbackCharaAnimClipPlayerActiveOffset,
        uint32_t {0});
    inactive.canonical_hash =
        Horse::RollbackHashCharaAnimationCanonical(inactive);
    inactive.integrity_hash =
        Horse::RollbackHashCharaAnimationIntegrity(inactive);
    mutation = inactive;
    std::memset(
        mutation.players[0].clip_runtime.data(),
        0, mutation.players[0].clip_runtime.size());
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            != inactive.canonical_hash
        || Horse::RollbackHashCharaAnimationIntegrity(mutation)
            == inactive.integrity_hash)
        return 23;
    mutation = baseline;
    mutation.players[0].scheduler[0x38] ^= 1;
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 6;
    mutation = baseline;
    mutation.players[0].scheduler[0x5C] ^= 1;
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 7;
    mutation = baseline;
    mutation.players[0].scheduler[
        Horse::kRollbackEnshutsuSchedulerAllocatorResidueOffset] ^= 1;
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            != baseline.canonical_hash
        || Horse::RollbackHashCharaAnimationIntegrity(mutation)
            == baseline.integrity_hash)
        return 22;
    for (size_t offset : {size_t {0x44}, size_t {0x48}, size_t {0x50},
             size_t {0x54}, size_t {0x64}})
    {
        mutation = baseline;
        mutation.players[0].scheduler[offset] ^= 1;
        if (Horse::RollbackHashCharaAnimationCanonical(mutation)
                == baseline.canonical_hash)
            return 8;
    }
    mutation = baseline;
    mutation.players[0].triggers[0].trigger_bytes[0x08] ^= 1;
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 9;
    mutation = baseline;
    std::swap(
        mutation.players[0].triggers[0],
        mutation.players[0].triggers[1]);
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 10;
    mutation = baseline;
    --mutation.players[0].trigger_count;
    if (Horse::RollbackHashCharaAnimationCanonical(mutation)
            == baseline.canonical_hash)
        return 11;
    mutation = baseline;
    mutation.players[0].triggers[1] =
        mutation.players[0].triggers[0];
    if (Horse::RollbackHashCharaAnimationIntegrity(mutation)
            == baseline.integrity_hash)
        return 12;

    auto* clip0 = reinterpret_cast<uint8_t*>(fixture.charas[0]
        + Horse::kRollbackCharaAnimClipPlayerOffset);
    auto* scheduler0 = fixture.scheduler[0];
    clip0[0x10] ^= 0x20;
    auto* clip_runtime0 = reinterpret_cast<uint8_t*>(
        fixture.charas[0] + Horse::kRollbackCharaAnimRuntimeOffset);
    std::memset(clip_runtime0, 0, Horse::kRollbackCharaAnimRuntimeBytes);
    scheduler0[0x38] ^= 0x10;
    scheduler0[
        Horse::kRollbackEnshutsuSchedulerAllocatorResidueOffset] ^= 0x20;
    fixture.triggers[0][0][0x08] ^= 0x08;
    if (!Horse::RestoreRollbackCharaAnimationState(
            fixture.charas, baseline))
        return 13;
    Horse::RollbackCharaAnimationStateHistory restored {};
    if (!Horse::CaptureRollbackCharaAnimationState(
            fixture.charas, restored)
        || restored.integrity_hash != baseline.integrity_hash)
        return 14;

    // The slot controller can select another interior authored clip section
    // without replacing its packed-data allocation. That pointer is local
    // rollback state and must be restored, not rejected as topology churn.
    const uintptr_t alternate_clip_data =
        Horse::RollbackAnimationReadScalar<uintptr_t>(clip0, 0x08) + 0x20;
    write(clip0 + 0x08, alternate_clip_data);
    if (!Horse::RollbackCharaAnimationRestorePreflight(
            fixture.charas, baseline)
        || !Horse::RestoreRollbackCharaAnimationState(
            fixture.charas, baseline)
        || Horse::RollbackAnimationReadScalar<uintptr_t>(clip0, 0x08)
            != Horse::RollbackAnimationReadScalar<uintptr_t>(
                baseline.players[0].clip.data(), 0x08))
        return 24;

    // A native reconfiguration may release all historical nodes. Refuse it
    // before writing any saved scalar or historical pointer graph.
    const uint8_t before_refusal = clip0[0x10];
    write(scheduler0 + 0x78, uint64_t {1});
    if (Horse::RollbackCharaAnimationRestorePreflight(
            fixture.charas, baseline)
        || Horse::RestoreRollbackCharaAnimationState(
            fixture.charas, baseline)
        || clip0[0x10] != before_refusal)
        return 15;
    write(scheduler0 + 0x78, uint64_t {2});

    Horse::RollbackAnimationDispatchLedger ledger {};
    ledger.begin(100, true);
    if (!ledger.admit(
            100, Horse::RollbackAnimationDispatchSite::PerFrameTick)
        || ledger.admit(
            99, Horse::RollbackAnimationDispatchSite::PerTickAdvanceAll)
        || !ledger.owned_direct_iteration_valid())
        return 16;
    if (ledger.admit(
            100, Horse::RollbackAnimationDispatchSite::PerFrameTick)
        || ledger.owned_direct_iteration_valid())
        return 19;
    ledger.begin(101, true);
    if (!ledger.admit(
            101, Horse::RollbackAnimationDispatchSite::PerTickAdvanceAll)
        || ledger.owned_direct_iteration_valid())
        return 17;
    if (!Horse::RollbackMatchStartSchedulerContract(true, true)
        || Horse::RollbackMatchStartSchedulerContract(true, false)
        || Horse::RollbackMatchStartSchedulerContract(false, true))
        return 18;

    std::cout << "rollback chara animation state self-test passed\n";
    return 0;
}
