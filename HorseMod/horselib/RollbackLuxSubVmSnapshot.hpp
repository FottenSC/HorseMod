#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uintptr_t kRollbackRvaLuxMoveSchedState = 0x4715400;

    struct RollbackLuxSubVmClass
    {
        uintptr_t vtable_rva;
        uint8_t extent;
    };

    // Extracted from the complete LuxMoveVM_CreateCpuDirectState factory.
    // Unknown factory/helper classes are deliberately rejected.
    static constexpr RollbackLuxSubVmClass kRollbackLuxSubVmClasses[] = {
        {0x3e863d0,0x68},{0x3e85608,0x78},{0x3e868f0,0x78},
        {0x3e85698,0x78},{0x3e85d10,0x68},{0x3e857b8,0x68},
        {0x3e86c50,0x68},{0x3e86bc0,0x68},{0x3e865d8,0x68},
        {0x3e860b8,0x68},{0x3e85c38,0x70},{0x3e862f8,0x68},
        {0x3e85c80,0x68},{0x3e85770,0x68},{0x3e86418,0x68},
        {0x3e85e30,0x68},{0x3e86028,0x78},{0x3e85f08,0x78},
        {0x3e861d8,0x78},{0x3e86788,0x68},{0x3e86d28,0x68},
        {0x3e86ff8,0x68},{0x3e86818,0x70},{0x3e85d58,0x68},
        {0x3e86190,0x68},{0x3e864f0,0x68},{0x3e85a40,0x68},
        {0x3e86b78,0x78},{0x3e86938,0x68},{0x3e86548,0x68},
        {0x3e858d8,0x68},{0x3e85848,0x68},{0x3e866b0,0x78},
        {0x3e86100,0x78},{0x3e85da0,0x78},{0x3e85578,0x78},
        {0x3e85cc8,0x78},{0x3e85f50,0x68},{0x3e86860,0x68},
        {0x3e859f8,0x70},{0x3e868a8,0x68},{0x3e86f68,0x68},
        {0x3e85ba8,0x68},{0x3e856e0,0x68},{0x3e85ad0,0x68},
        {0x3e864a8,0x68},{0x3e85de8,0x68},{0x3e86668,0x68},
        {0x3e867d0,0x68},{0x3e86070,0x78},{0x3e869c8,0x78},
        {0x3e86d70,0x70},{0x3e85a88,0x78},{0x3e85b18,0x78},
        {0x3e86c98,0x68},{0x3e859b0,0x68},{0x3e86e90,0x68},
        {0x3e86db8,0x68},{0x3e86e48,0x68},{0x3e866f8,0x68},
        {0x3e85e78,0x68},{0x3e86ce0,0x68},{0x3e86620,0x68},
        {0x3e86220,0x78},{0x3e85920,0x78},{0x3e86b30,0x68},
        {0x3e855c0,0x68},{0x3e86a10,0x68},{0x3e86340,0x68},
        {0x3e86590,0x68},{0x3e86e00,0x68},{0x3e86268,0x68},
        {0x3e86fb0,0x68},{0x3e85bf0,0x68},
        {0x3e891b8,0x80},{0x3e89248,0x80},
        {0x3e85ec0,0x70},
    };

    static constexpr uint8_t RollbackLuxSubVmExtentForVtable(
        uintptr_t image_base, uintptr_t vtable) noexcept
    {
        if (!image_base || vtable < image_base) return 0;
        const uintptr_t rva = vtable - image_base;
        for (const auto& entry : kRollbackLuxSubVmClasses)
            if (entry.vtable_rva == rva) return entry.extent;
        return 0;
    }

    struct RollbackLuxSubVmSlotSnapshot
    {
        uintptr_t scheduler {0};
        uintptr_t subvm {0};
        uintptr_t vtable {0};
        uintptr_t chara {0};
        uintptr_t opponent {0};
        uintptr_t owner_scheduler {0};
        uint8_t extent {0};
        // Native construction initializes/carries only the command word at
        // +0x08.  +0x0C..+0x0F is allocator residue and must remain outside
        // peer-canonical state.
        std::array<uint8_t, 4> input_command {};
        // Proven mutable common state is +0x20..+0x5B.  +0x5C..+0x5F is a
        // second uninitialized allocator-residue gap.
        std::array<uint8_t, 0x3C> common {};
        std::array<uint8_t, 0x18> derived {};
    };

    struct RollbackLuxSubVmSnapshot
    {
        uintptr_t image_base {0};
        std::array<RollbackLuxSubVmSlotSnapshot, 2> slots {};
        uint64_t semantic_hash {0};
        uint64_t integrity_hash {0};
        bool valid {false};

        void clear() noexcept { *this = {}; }
    };

    static inline uint64_t HashRollbackLuxSubVmSemantic(
        const RollbackLuxSubVmSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        RollbackHash hash {};
        for (const auto& slot : state.slots)
        {
            // Class identity is gameplay-semantic, but the process address is
            // not. Authenticated peers run the same image, so compare the
            // vtable RVA alongside the typed class extent.
            hash.add_scalar(slot.vtable - state.image_base);
            hash.add_scalar(slot.extent);
            hash.add_bytes(
                slot.input_command.data(), slot.input_command.size());
            hash.add_bytes(slot.common.data(), slot.common.size());
            const size_t derived_bytes = slot.extent > 0x68
                ? static_cast<size_t>(slot.extent - 0x68) : 0;
            // The +0x7C..+0x7F tail of the 0x80 AllGuardCount class is not
            // initialized by its constructor and is not a verified field.
            const size_t canonical_derived = slot.extent == 0x80
                ? 0x14 : derived_bytes;
            if (canonical_derived)
                hash.add_bytes(slot.derived.data(), canonical_derived);
        }
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackLuxSubVmIntegrity(
        const RollbackLuxSubVmSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        const uint64_t semantic = HashRollbackLuxSubVmSemantic(state);
        if (!semantic) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.image_base);
        for (const auto& slot : state.slots)
        {
            hash.add_scalar(slot.scheduler);
            hash.add_scalar(slot.subvm);
            hash.add_scalar(slot.vtable);
            hash.add_scalar(slot.chara);
            hash.add_scalar(slot.opponent);
            hash.add_scalar(slot.owner_scheduler);
        }
        hash.add_scalar(semantic);
        return hash.value ? hash.value : 1;
    }

    static inline bool ValidateRollbackLuxSubVmSnapshot(
        const RollbackLuxSubVmSnapshot& state) noexcept
    {
        if (!state.valid || !state.image_base) return false;
        for (const auto& slot : state.slots)
        {
            if (!slot.scheduler || !slot.subvm || !slot.vtable
                || !slot.chara || !slot.opponent
                || slot.owner_scheduler != slot.scheduler
                || RollbackLuxSubVmExtentForVtable(
                    state.image_base, slot.vtable) != slot.extent)
                return false;
        }
        return state.semantic_hash == HashRollbackLuxSubVmSemantic(state)
            && state.integrity_hash == HashRollbackLuxSubVmIntegrity(state);
    }

    template <typename ReadFn>
    static inline bool CaptureRollbackLuxSubVmSnapshotWith(
        uintptr_t image_base, ReadFn&& read,
        RollbackLuxSubVmSnapshot& out) noexcept
    {
        out.clear();
        if (!image_base) return false;
        out.image_base = image_base;
        const uintptr_t sched_base =
            image_base + kRollbackRvaLuxMoveSchedState;
        for (size_t index = 0; index < out.slots.size(); ++index)
        {
            auto& slot = out.slots[index];
            slot.scheduler = sched_base + index * 0x60;
            if (!read(slot.scheduler + 0x50, &slot.subvm,
                    sizeof(slot.subvm))
                || !slot.subvm
                || !read(slot.subvm, &slot.vtable, sizeof(slot.vtable))
                || !read(slot.subvm + 0x10, &slot.chara,
                    sizeof(slot.chara))
                || !read(slot.subvm + 0x18, &slot.opponent,
                    sizeof(slot.opponent))
                || !read(slot.subvm + 0x60, &slot.owner_scheduler,
                    sizeof(slot.owner_scheduler)))
                return false;
            slot.extent = RollbackLuxSubVmExtentForVtable(
                image_base, slot.vtable);
            if (!slot.extent || slot.owner_scheduler != slot.scheduler)
                return false;
            if (!read(slot.subvm + 0x08, slot.input_command.data(),
                    slot.input_command.size())
                || !read(slot.subvm + 0x20, slot.common.data(),
                    slot.common.size()))
                return false;
            const size_t derived_bytes = slot.extent > 0x68
                ? static_cast<size_t>(slot.extent - 0x68) : 0;
            if (derived_bytes
                && !read(slot.subvm + 0x68, slot.derived.data(),
                    derived_bytes))
                return false;
        }
        out.valid = true;
        out.semantic_hash = HashRollbackLuxSubVmSemantic(out);
        out.integrity_hash = HashRollbackLuxSubVmIntegrity(out);
        return ValidateRollbackLuxSubVmSnapshot(out);
    }

    static inline bool CaptureRollbackLuxSubVmSnapshot(
        uintptr_t image_base, RollbackLuxSubVmSnapshot& out) noexcept
    {
        return CaptureRollbackLuxSubVmSnapshotWith(
            image_base,
            [](uintptr_t address, void* destination,
               size_t bytes) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, bytes);
            }, out);
    }

    template <typename ReadFn>
    static inline bool RollbackLuxSubVmGenerationMatchesWith(
        const RollbackLuxSubVmSnapshot& state, ReadFn&& read) noexcept
    {
        if (!ValidateRollbackLuxSubVmSnapshot(state)) return false;
        for (const auto& slot : state.slots)
        {
            uintptr_t subvm = 0, vtable = 0, chara = 0, opponent = 0;
            uintptr_t owner = 0;
            if (!read(slot.scheduler + 0x50, &subvm, sizeof(subvm))
                || subvm != slot.subvm
                || !read(subvm, &vtable, sizeof(vtable))
                || vtable != slot.vtable
                || !read(subvm + 0x10, &chara, sizeof(chara))
                || chara != slot.chara
                || !read(subvm + 0x18, &opponent, sizeof(opponent))
                || opponent != slot.opponent
                || !read(subvm + 0x60, &owner, sizeof(owner))
                || owner != slot.owner_scheduler)
                return false;
        }
        return true;
    }

    static inline bool RollbackLuxSubVmGenerationMatches(
        const RollbackLuxSubVmSnapshot& state) noexcept
    {
        return RollbackLuxSubVmGenerationMatchesWith(
            state,
            [](uintptr_t address, void* destination,
               size_t bytes) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, bytes);
            });
    }

    template <typename ReadFn, typename WriteFn>
    static inline bool RestoreRollbackLuxSubVmSnapshotWith(
        const RollbackLuxSubVmSnapshot& state,
        ReadFn&& read, WriteFn&& write) noexcept
    {
        if (!RollbackLuxSubVmGenerationMatchesWith(state, read)) return false;
        for (const auto& slot : state.slots)
        {
            if (!write(slot.subvm + 0x08, slot.input_command.data(),
                    slot.input_command.size())
                || !write(slot.subvm + 0x20, slot.common.data(),
                    slot.common.size()))
                return false;
            size_t derived_bytes = slot.extent > 0x68
                ? static_cast<size_t>(slot.extent - 0x68) : 0;
            if (slot.extent == 0x80) derived_bytes = 0x14;
            if (derived_bytes
                && !write(slot.subvm + 0x68, slot.derived.data(),
                    derived_bytes))
                return false;
        }
        RollbackLuxSubVmSnapshot verification {};
        return CaptureRollbackLuxSubVmSnapshotWith(
                state.image_base, read, verification)
            && verification.semantic_hash == state.semantic_hash;
    }

    static inline bool RestoreRollbackLuxSubVmSnapshot(
        const RollbackLuxSubVmSnapshot& state) noexcept
    {
        return RestoreRollbackLuxSubVmSnapshotWith(
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
