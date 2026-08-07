#include "../HorseMod/horselib/RollbackLuxMoveCommandSnapshot.hpp"
#include "../HorseMod/horselib/RollbackLuxMoveSystemSnapshot.hpp"
#include "../HorseMod/horselib/RollbackLuxMoveVmSlotParamSnapshot.hpp"
#include "../HorseMod/horselib/RollbackLuxSubVmSnapshot.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    template <size_t N>
    bool copy_from(
        const std::array<uint8_t, N>& bytes, uintptr_t base,
        uintptr_t address, void* destination, size_t count) noexcept
    {
        if (address < base || count > N || address - base > N - count)
            return false;
        std::memcpy(destination, bytes.data() + (address - base), count);
        return true;
    }

    template <size_t N>
    bool copy_to(
        std::array<uint8_t, N>& bytes, uintptr_t base,
        uintptr_t address, const void* source, size_t count) noexcept
    {
        if (address < base || count > N || address - base > N - count)
            return false;
        std::memcpy(bytes.data() + (address - base), source, count);
        return true;
    }

    template <typename T, size_t N>
    void put(std::array<uint8_t, N>& bytes, size_t offset, T value) noexcept
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
}

int main()
{
    constexpr uintptr_t image_base = 0x10000000;
    constexpr uintptr_t pump_base =
        image_base + Horse::kRollbackRvaLuxMoveSystemVMPumpState;
    constexpr uintptr_t sched_base =
        image_base + Horse::kRollbackRvaLuxMoveSchedState;
    constexpr uintptr_t command_base =
        image_base + Horse::kRollbackRvaLuxMoveCommandPlayers;
    constexpr uintptr_t slot_param_base =
        image_base + Horse::kRollbackRvaLuxMoveVmSlotParamArray;
    constexpr uintptr_t subvm_a_base = 0x20000000;
    constexpr uintptr_t subvm_b_base = 0x20000100;

    std::array<uint8_t, 0x88> pump {};
    for (size_t i = 0; i < pump.size(); ++i)
        pump[i] = static_cast<uint8_t>(i + 1);
    for (const size_t offset : {size_t {0x00}, size_t {0x08},
             size_t {0x10}, size_t {0x18}, size_t {0x40}, size_t {0x48}})
        put(pump, offset, uintptr_t {0x30000000 + offset});
    put(pump, 0x70, int32_t {3});
    put(pump, 0x7C, uint32_t {1});

    const auto pump_read = [&](uintptr_t address, void* destination,
                               size_t count) noexcept {
        return copy_from(pump, pump_base, address, destination, count);
    };
    const auto pump_write = [&](uintptr_t address, const void* source,
                                size_t count) noexcept {
        return copy_to(pump, pump_base, address, source, count);
    };
    Horse::RollbackLuxMoveSystemPumpSnapshot pump_snapshot {};
    if (!Horse::CaptureRollbackLuxMoveSystemPumpSnapshotWith(
            pump_base, pump_read, pump_snapshot))
    {
        std::printf("pump capture failed\n");
        return 1;
    }
    const uintptr_t pump_identity = [](
        const std::array<uint8_t, 0x88>& bytes) noexcept {
        uintptr_t value = 0;
        std::memcpy(&value, bytes.data() + 0x10, sizeof(value));
        return value;
    }(pump);
    pump[0x20] ^= 0x7F;
    pump[0x70] = 1;
    if (!Horse::RestoreRollbackLuxMoveSystemPumpSnapshotWith(
            pump_snapshot, pump_read, pump_write)
        || pump_identity != [](
            const std::array<uint8_t, 0x88>& bytes) noexcept {
                uintptr_t value = 0;
                std::memcpy(&value, bytes.data() + 0x10, sizeof(value));
                return value;
            }(pump))
    {
        std::printf("pump semantic restore or identity preservation failed\n");
        return 1;
    }
    pump[0x10] ^= 1;
    if (Horse::RestoreRollbackLuxMoveSystemPumpSnapshotWith(
            pump_snapshot, pump_read, pump_write))
    {
        std::printf("pump identity mismatch was admitted\n");
        return 1;
    }
    pump[0x10] ^= 1;

    std::array<uint8_t, 0x58> slot_params {};
    for (size_t i = 0; i < slot_params.size(); ++i)
        slot_params[i] = static_cast<uint8_t>((i * 29u + 7u) & 0xFFu);
    const auto slot_param_read = [&](uintptr_t address, void* destination,
                                     size_t count) noexcept {
        return copy_from(
            slot_params, slot_param_base, address, destination, count);
    };
    const auto slot_param_write = [&](uintptr_t address, const void* source,
                                      size_t count) noexcept {
        return copy_to(
            slot_params, slot_param_base, address, source, count);
    };
    Horse::RollbackLuxMoveVmSlotParamSnapshot slot_param_snapshot {};
    if (!Horse::CaptureRollbackLuxMoveVmSlotParamSnapshotWith(
            slot_param_base, slot_param_read, slot_param_snapshot))
    {
        std::printf("move-slot-param capture failed\n");
        return 1;
    }
    const auto original_slot_params = slot_params;
    for (size_t i = 0; i < slot_params.size(); ++i)
        slot_params[i] ^= static_cast<uint8_t>(0xA5u + i);
    const auto mutated_slot_params = slot_params;
    if (!Horse::RestoreRollbackLuxMoveVmSlotParamSnapshotWith(
            slot_param_snapshot, slot_param_read, slot_param_write))
    {
        std::printf("move-slot-param semantic restore failed\n");
        return 1;
    }
    for (size_t lane = 0; lane < 2; ++lane)
    {
        const size_t offset = lane * 0x2C;
        if (std::memcmp(slot_params.data() + offset,
                original_slot_params.data() + offset,
                Horse::kRollbackLuxMoveVmSlotParamSemanticBytes) != 0
            || std::memcmp(slot_params.data() + offset + 0x28,
                mutated_slot_params.data() + offset + 0x28,
                sizeof(uint32_t)) != 0)
        {
            std::printf(
                "move-slot-param semantic restore or padding exclusion failed\n");
            return 1;
        }
    }
    slot_params[0x2C + 0x28] ^= 1;
    Horse::RollbackLuxMoveVmSlotParamSnapshot slot_param_variant {};
    if (!Horse::CaptureRollbackLuxMoveVmSlotParamSnapshotWith(
            slot_param_base, slot_param_read, slot_param_variant)
        || slot_param_variant.canonical_hash
            != slot_param_snapshot.canonical_hash)
    {
        std::printf("move-slot-param padding entered canonical state\n");
        return 1;
    }
    slot_params = original_slot_params;

    std::array<uint8_t,
        Horse::kRollbackLuxMoveCommandSlotCount
            * Horse::kRollbackLuxMoveCommandSlotStride> command {};
    for (size_t i = 0; i < command.size(); ++i)
        command[i] = static_cast<uint8_t>((i * 37u + 11u) & 0xFFu);
    for (size_t slot_index = 0;
         slot_index < Horse::kRollbackLuxMoveCommandSlotCount;
         ++slot_index)
    {
        const size_t slot =
            slot_index * Horse::kRollbackLuxMoveCommandSlotStride;
        for (size_t identity = 0;
             identity < Horse::kRollbackLuxMoveCommandIdentityOffsets.size();
             ++identity)
        {
            put(command,
                slot
                    + Horse::kRollbackLuxMoveCommandIdentityOffsets[
                        identity],
                uintptr_t {0x41000000u
                    + slot_index * 0x10000u + identity * 0x100u});
        }
    }
    const auto command_read = [&](uintptr_t address, void* destination,
                                  size_t count) noexcept {
        return copy_from(
            command, command_base, address, destination, count);
    };
    const auto command_write = [&](uintptr_t address, const void* source,
                                   size_t count) noexcept {
        return copy_to(command, command_base, address, source, count);
    };
    Horse::RollbackLuxMoveCommandSnapshot command_snapshot {};
    if (!Horse::CaptureRollbackLuxMoveCommandSnapshotWith(
            image_base, command_read, command_snapshot))
    {
        std::printf("move-command capture failed\n");
        return 1;
    }
    for (size_t slot_index = 0;
         slot_index < Horse::kRollbackLuxMoveCommandSlotCount;
         ++slot_index)
    {
        const size_t slot =
            slot_index * Horse::kRollbackLuxMoveCommandSlotStride;
        for (const auto& range :
             Horse::kRollbackLuxMoveCommandSemanticRanges)
            command[slot + range.offset] ^= 0x7Fu;
        command[slot + Horse::kRollbackLuxMoveCommandDiagnosticOffset]
            ^= 0x55u;
        command[slot + Horse::kRollbackLuxMoveCommandUninitializedOffset]
            ^= 0x33u;
    }
    const uint8_t excluded_diagnostic =
        command[Horse::kRollbackLuxMoveCommandDiagnosticOffset];
    const uint8_t excluded_command_tail =
        command[Horse::kRollbackLuxMoveCommandUninitializedOffset];
    if (!Horse::RestoreRollbackLuxMoveCommandSnapshotWith(
            command_snapshot, command_read, command_write)
        || command[Horse::kRollbackLuxMoveCommandDiagnosticOffset]
            != excluded_diagnostic
        || command[Horse::kRollbackLuxMoveCommandUninitializedOffset]
            != excluded_command_tail)
    {
        std::printf(
            "move-command semantic restore or exclusion failed\n");
        return 1;
    }
    Horse::RollbackLuxMoveCommandSnapshot command_verification {};
    if (!Horse::CaptureRollbackLuxMoveCommandSnapshotWith(
            image_base, command_read, command_verification)
        || command_verification.semantic_hash
            != command_snapshot.semantic_hash)
    {
        std::printf("move-command semantic verification failed\n");
        return 1;
    }
    constexpr size_t changed_identity_index = 2;
    const size_t changed_identity_offset =
        Horse::kRollbackLuxMoveCommandIdentityOffsets[
            changed_identity_index];
    const uintptr_t original_identity =
        command_snapshot.slots[0].identities[changed_identity_index];
    put(command, changed_identity_offset,
        original_identity + uintptr_t {0x1000});
    Horse::RollbackLuxMoveCommandSnapshot identity_variant {};
    if (!Horse::CaptureRollbackLuxMoveCommandSnapshotWith(
            image_base, command_read, identity_variant)
        || identity_variant.semantic_hash != command_snapshot.semantic_hash
        || identity_variant.integrity_hash == command_snapshot.integrity_hash
        || Horse::RestoreRollbackLuxMoveCommandSnapshotWith(
            command_snapshot, command_read, command_write))
    {
        std::printf(
            "move-command identity exclusion or preflight failed\n");
        return 1;
    }
    put(command, changed_identity_offset, original_identity);

    std::array<uint8_t, 0xC0> schedulers {};
    std::array<uint8_t, 0x80> subvm_a {};
    std::array<uint8_t, 0x80> subvm_b {};
    put(schedulers, 0x50, subvm_a_base);
    put(schedulers, 0xB0, subvm_b_base);
    put(subvm_a, 0x00, image_base + uintptr_t {0x3e85bf0});
    put(subvm_b, 0x00, image_base + uintptr_t {0x3e891b8});
    put(subvm_a, 0x10, uintptr_t {0x31000000});
    put(subvm_a, 0x18, uintptr_t {0x31000100});
    put(subvm_a, 0x60, sched_base);
    put(subvm_b, 0x10, uintptr_t {0x32000000});
    put(subvm_b, 0x18, uintptr_t {0x32000100});
    put(subvm_b, 0x60, sched_base + uintptr_t {0x60});
    for (size_t i = 0x08; i < 0x10; ++i) subvm_a[i] = 0x11;
    for (size_t i = 0x20; i < 0x60; ++i) subvm_a[i] = 0x22;
    for (size_t i = 0x08; i < 0x10; ++i) subvm_b[i] = 0x33;
    for (size_t i = 0x20; i < 0x60; ++i) subvm_b[i] = 0x44;
    for (size_t i = 0x68; i < 0x80; ++i) subvm_b[i] = 0x55;

    const auto subvm_read = [&](uintptr_t address, void* destination,
                                size_t count) noexcept {
        return copy_from(schedulers, sched_base, address, destination, count)
            || copy_from(subvm_a, subvm_a_base, address, destination, count)
            || copy_from(subvm_b, subvm_b_base, address, destination, count);
    };
    const auto subvm_write = [&](uintptr_t address, const void* source,
                                 size_t count) noexcept {
        return copy_to(schedulers, sched_base, address, source, count)
            || copy_to(subvm_a, subvm_a_base, address, source, count)
            || copy_to(subvm_b, subvm_b_base, address, source, count);
    };
    Horse::RollbackLuxSubVmSnapshot subvm_snapshot {};
    if (!Horse::CaptureRollbackLuxSubVmSnapshotWith(
            image_base, subvm_read, subvm_snapshot))
    {
        std::printf("subvm capture failed\n");
        return 1;
    }
    subvm_a[0x20] ^= 0x7F;
    subvm_b[0x68] ^= 0x7F;
    subvm_a[0x0C] ^= 0x7F;
    subvm_a[0x5C] ^= 0x7F;
    subvm_b[0x7C] ^= 0x7F;
    const uint8_t excluded_prefix_gap = subvm_a[0x0C];
    const uint8_t excluded_common_gap = subvm_a[0x5C];
    const uint8_t excluded_tail = subvm_b[0x7C];
    if (!Horse::RestoreRollbackLuxSubVmSnapshotWith(
            subvm_snapshot, subvm_read, subvm_write)
        || subvm_a[0x0C] != excluded_prefix_gap
        || subvm_a[0x5C] != excluded_common_gap
        || subvm_b[0x7C] != excluded_tail)
    {
        std::printf("subvm semantic restore or residue exclusion failed\n");
        return 1;
    }
    put(schedulers, 0x50, uintptr_t {0x20000200});
    if (Horse::RestoreRollbackLuxSubVmSnapshotWith(
            subvm_snapshot, subvm_read, subvm_write))
    {
        std::printf("subvm generation mismatch was admitted\n");
        return 1;
    }

    std::printf("rollback lux move-state self-test passed\n");
    return 0;
}
