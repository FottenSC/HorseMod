#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    static constexpr uintptr_t kRollbackRvaLuxMoveCommandPlayers =
        0x470F390;
    static constexpr size_t kRollbackLuxMoveCommandSlotCount = 2;
    static constexpr size_t kRollbackLuxMoveCommandSlotStride = 0x3038;

    struct RollbackLuxMoveCommandSemanticRange
    {
        size_t offset;
        size_t bytes;
    };

    // Ghidra FLuxMoveCommandPlayerSlot ownership partition. These ranges are
    // the fields read or written by the command parser, opcode executor,
    // personality refresh, predicate VM, and reaction routes. Pointer-bearing
    // qwords, diagnostic text, and the uninitialized tail are excluded.
    static constexpr std::array<RollbackLuxMoveCommandSemanticRange, 9>
        kRollbackLuxMoveCommandSemanticRanges {{
            {0x0000, 0x0008},
            {0x0018, 0x0010},
            {0x0038, 0x0308},
            {0x0348, 0x0860},
            {0x0BE8, 0x00E0},
            {0x0CD0, 0x0008},
            {0x0CE8, 0x0CB0},
            {0x19A0, 0x1088},
            {0x2AA8, 0x058C},
        }};

    static constexpr std::array<size_t, 17>
        kRollbackLuxMoveCommandIdentityOffsets {{
            0x0008, 0x0010, 0x0028, 0x0030, 0x0340,
            0x0BA8, 0x0BB0, 0x0BB8, 0x0BC0, 0x0BC8,
            0x0BD0, 0x0BD8, 0x0BE0, 0x0CC8, 0x0CD8,
            0x0CE0, 0x1998,
        }};

    static constexpr size_t kRollbackLuxMoveCommandDiagnosticOffset =
        0x2A28;
    static constexpr size_t kRollbackLuxMoveCommandDiagnosticBytes = 0x80;
    static constexpr size_t kRollbackLuxMoveCommandUninitializedOffset =
        0x3034;
    static constexpr size_t kRollbackLuxMoveCommandUninitializedBytes = 4;

    static constexpr size_t RollbackLuxMoveCommandSemanticByteCount()
        noexcept
    {
        size_t total = 0;
        for (const auto& range : kRollbackLuxMoveCommandSemanticRanges)
            total += range.bytes;
        return total;
    }

    static_assert(
        RollbackLuxMoveCommandSemanticByteCount()
            + kRollbackLuxMoveCommandIdentityOffsets.size()
                * sizeof(uintptr_t)
            + kRollbackLuxMoveCommandDiagnosticBytes
            + kRollbackLuxMoveCommandUninitializedBytes
        == kRollbackLuxMoveCommandSlotStride,
        "MoveCommand slot partition must cover exactly 0x3038 bytes");

    struct RollbackLuxMoveCommandSlotSnapshot
    {
        uintptr_t address {0};
        std::array<uintptr_t, 17> identities {};
        std::array<uint8_t, 0x0008> header_state {};
        std::array<uint8_t, 0x0010> header_scalars {};
        std::array<uint8_t, 0x0308> primary_state {};
        std::array<uint8_t, 0x0860> personality_state {};
        std::array<uint8_t, 0x00E0> personality_runtime {};
        std::array<uint8_t, 0x0008> predicate_control {};
        std::array<uint8_t, 0x0CB0> parser_reaction_state {};
        std::array<uint8_t, 0x1088> vm_state {};
        std::array<uint8_t, 0x058C> reaction_state {};
    };

    struct RollbackLuxMoveCommandSnapshot
    {
        uintptr_t image_base {0};
        std::array<RollbackLuxMoveCommandSlotSnapshot,
            kRollbackLuxMoveCommandSlotCount> slots {};
        uint64_t semantic_hash {0};
        uint64_t integrity_hash {0};
        bool valid {false};

        void clear() noexcept { *this = {}; }
    };

    template <typename Fn>
    static inline bool ForEachRollbackLuxMoveCommandSemanticBank(
        RollbackLuxMoveCommandSlotSnapshot& slot, Fn&& fn) noexcept
    {
        return fn(0x0000, slot.header_state.data(),
                slot.header_state.size())
            && fn(0x0018, slot.header_scalars.data(),
                slot.header_scalars.size())
            && fn(0x0038, slot.primary_state.data(),
                slot.primary_state.size())
            && fn(0x0348, slot.personality_state.data(),
                slot.personality_state.size())
            && fn(0x0BE8, slot.personality_runtime.data(),
                slot.personality_runtime.size())
            && fn(0x0CD0, slot.predicate_control.data(),
                slot.predicate_control.size())
            && fn(0x0CE8, slot.parser_reaction_state.data(),
                slot.parser_reaction_state.size())
            && fn(0x19A0, slot.vm_state.data(), slot.vm_state.size())
            && fn(0x2AA8, slot.reaction_state.data(),
                slot.reaction_state.size());
    }

    template <typename Fn>
    static inline bool ForEachRollbackLuxMoveCommandSemanticBank(
        const RollbackLuxMoveCommandSlotSnapshot& slot, Fn&& fn) noexcept
    {
        return fn(0x0000, slot.header_state.data(),
                slot.header_state.size())
            && fn(0x0018, slot.header_scalars.data(),
                slot.header_scalars.size())
            && fn(0x0038, slot.primary_state.data(),
                slot.primary_state.size())
            && fn(0x0348, slot.personality_state.data(),
                slot.personality_state.size())
            && fn(0x0BE8, slot.personality_runtime.data(),
                slot.personality_runtime.size())
            && fn(0x0CD0, slot.predicate_control.data(),
                slot.predicate_control.size())
            && fn(0x0CE8, slot.parser_reaction_state.data(),
                slot.parser_reaction_state.size())
            && fn(0x19A0, slot.vm_state.data(), slot.vm_state.size())
            && fn(0x2AA8, slot.reaction_state.data(),
                slot.reaction_state.size());
    }

    static inline uint64_t HashRollbackLuxMoveCommandSemantic(
        const RollbackLuxMoveCommandSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        RollbackHash hash {};
        for (const auto& slot : state.slots)
        {
            const bool added = ForEachRollbackLuxMoveCommandSemanticBank(
                slot,
                [&hash](size_t, const uint8_t* bytes,
                        size_t count) noexcept {
                    hash.add_bytes(bytes, count);
                    return true;
                });
            if (!added) return 0;
        }
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackLuxMoveCommandIntegrity(
        const RollbackLuxMoveCommandSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        const uint64_t semantic =
            HashRollbackLuxMoveCommandSemantic(state);
        if (!semantic) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.image_base);
        for (const auto& slot : state.slots)
        {
            hash.add_scalar(slot.address);
            hash.add_bytes(slot.identities.data(),
                sizeof(slot.identities));
        }
        hash.add_scalar(semantic);
        return hash.value ? hash.value : 1;
    }

    static inline bool ValidateRollbackLuxMoveCommandSnapshot(
        const RollbackLuxMoveCommandSnapshot& state) noexcept
    {
        if (!state.valid || !state.image_base) return false;
        const uintptr_t arena =
            state.image_base + kRollbackRvaLuxMoveCommandPlayers;
        for (size_t index = 0; index < state.slots.size(); ++index)
        {
            const auto& slot = state.slots[index];
            if (slot.address
                    != arena + index * kRollbackLuxMoveCommandSlotStride
                || !slot.identities[0] || !slot.identities[1])
                return false;
        }
        return state.semantic_hash
                == HashRollbackLuxMoveCommandSemantic(state)
            && state.integrity_hash
                == HashRollbackLuxMoveCommandIntegrity(state);
    }

    template <typename ReadFn>
    static inline bool CaptureRollbackLuxMoveCommandSnapshotWith(
        uintptr_t image_base, ReadFn&& read,
        RollbackLuxMoveCommandSnapshot& out) noexcept
    {
        out.clear();
        if (!image_base) return false;
        out.image_base = image_base;
        const uintptr_t arena =
            image_base + kRollbackRvaLuxMoveCommandPlayers;
        for (size_t index = 0; index < out.slots.size(); ++index)
        {
            auto& slot = out.slots[index];
            slot.address =
                arena + index * kRollbackLuxMoveCommandSlotStride;
            for (size_t identity = 0;
                 identity < slot.identities.size(); ++identity)
            {
                if (!read(
                        slot.address
                            + kRollbackLuxMoveCommandIdentityOffsets[
                                identity],
                        &slot.identities[identity], sizeof(uintptr_t)))
                    return false;
            }
            // +0x08 self and +0x10 opponent are mandatory during the active
            // battle lifecycle. Other identities may legitimately be null.
            if (!slot.identities[0] || !slot.identities[1]) return false;
            if (!ForEachRollbackLuxMoveCommandSemanticBank(
                    slot,
                    [&read, &slot](size_t offset, uint8_t* destination,
                                   size_t count) noexcept {
                        return read(slot.address + offset,
                            destination, count);
                    }))
                return false;
        }
        out.valid = true;
        out.semantic_hash = HashRollbackLuxMoveCommandSemantic(out);
        out.integrity_hash = HashRollbackLuxMoveCommandIntegrity(out);
        return ValidateRollbackLuxMoveCommandSnapshot(out);
    }

    static inline bool CaptureRollbackLuxMoveCommandSnapshot(
        uintptr_t image_base,
        RollbackLuxMoveCommandSnapshot& out) noexcept
    {
        return CaptureRollbackLuxMoveCommandSnapshotWith(
            image_base,
            [](uintptr_t address, void* destination,
               size_t bytes) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, bytes);
            }, out);
    }

    template <typename ReadFn>
    static inline bool RollbackLuxMoveCommandGenerationMatchesWith(
        const RollbackLuxMoveCommandSnapshot& state,
        ReadFn&& read) noexcept
    {
        if (!ValidateRollbackLuxMoveCommandSnapshot(state)) return false;
        for (const auto& slot : state.slots)
        {
            for (size_t identity = 0;
                 identity < slot.identities.size(); ++identity)
            {
                uintptr_t current = 0;
                if (!read(
                        slot.address
                            + kRollbackLuxMoveCommandIdentityOffsets[
                                identity],
                        &current, sizeof(current))
                    || current != slot.identities[identity])
                    return false;
            }
        }
        return true;
    }

    static inline bool RollbackLuxMoveCommandGenerationMatches(
        const RollbackLuxMoveCommandSnapshot& state) noexcept
    {
        return RollbackLuxMoveCommandGenerationMatchesWith(
            state,
            [](uintptr_t address, void* destination,
               size_t bytes) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, bytes);
            });
    }

    template <typename ReadFn, typename WriteFn>
    static inline bool RestoreRollbackLuxMoveCommandSnapshotWith(
        const RollbackLuxMoveCommandSnapshot& state,
        ReadFn&& read, WriteFn&& write) noexcept
    {
        // Check both slots and all identities before the first mutation.
        if (!RollbackLuxMoveCommandGenerationMatchesWith(state, read))
            return false;
        for (const auto& slot : state.slots)
        {
            if (!ForEachRollbackLuxMoveCommandSemanticBank(
                    slot,
                    [&write, &slot](size_t offset,
                                    const uint8_t* source,
                                    size_t count) noexcept {
                        return write(slot.address + offset,
                            source, count);
                    }))
                return false;
        }
        RollbackLuxMoveCommandSnapshot verification {};
        return CaptureRollbackLuxMoveCommandSnapshotWith(
                state.image_base, read, verification)
            && verification.semantic_hash == state.semantic_hash;
    }

    static inline bool RestoreRollbackLuxMoveCommandSnapshot(
        const RollbackLuxMoveCommandSnapshot& state) noexcept
    {
        return RestoreRollbackLuxMoveCommandSnapshotWith(
            state,
            [](uintptr_t address, void* destination,
               size_t bytes) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, bytes);
            },
            [](uintptr_t address, const void* source,
               size_t bytes) noexcept {
                return SafeWriteBytes(
                    reinterpret_cast<void*>(address), source, bytes);
            });
    }
}
