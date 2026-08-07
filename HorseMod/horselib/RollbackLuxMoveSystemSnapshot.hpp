#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace Horse
{
    static constexpr uintptr_t kRollbackRvaLuxMoveSystemVMPumpState =
        0x4100C70;

    struct RollbackLuxMoveSystemPumpSnapshot
    {
        uintptr_t address {0};
        std::array<uintptr_t, 6> identities {};
        std::array<uint8_t, 0x1C> lane_a {};
        std::array<uint8_t, 0x1C> lane_b {};
        std::array<uint8_t, 0x18> controls {};
        uint64_t semantic_hash {0};
        uint64_t integrity_hash {0};
        bool valid {false};

        void clear() noexcept { *this = {}; }
    };

    static inline uint64_t HashRollbackLuxMoveSystemPumpSemantic(
        const RollbackLuxMoveSystemPumpSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        RollbackHash hash {};
        hash.add_bytes(state.lane_a.data(), state.lane_a.size());
        hash.add_bytes(state.lane_b.data(), state.lane_b.size());
        hash.add_bytes(state.controls.data(), state.controls.size());
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackLuxMoveSystemPumpIntegrity(
        const RollbackLuxMoveSystemPumpSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        const uint64_t semantic =
            HashRollbackLuxMoveSystemPumpSemantic(state);
        if (!semantic) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.address);
        hash.add_bytes(state.identities.data(), sizeof(state.identities));
        hash.add_scalar(semantic);
        return hash.value ? hash.value : 1;
    }

    static inline bool ValidateRollbackLuxMoveSystemPumpSnapshot(
        const RollbackLuxMoveSystemPumpSnapshot& state) noexcept
    {
        return state.valid && state.address != 0
            && state.semantic_hash
                == HashRollbackLuxMoveSystemPumpSemantic(state)
            && state.integrity_hash
                == HashRollbackLuxMoveSystemPumpIntegrity(state);
    }

    template <typename ReadFn>
    static inline bool CaptureRollbackLuxMoveSystemPumpSnapshotWith(
        uintptr_t address, ReadFn&& read,
        RollbackLuxMoveSystemPumpSnapshot& out) noexcept
    {
        out.clear();
        if (!address) return false;
        out.address = address;
        static constexpr size_t kIdentityOffsets[] = {
            0x00, 0x08, 0x10, 0x18, 0x40, 0x48,
        };
        for (size_t i = 0; i < std::size(kIdentityOffsets); ++i)
        {
            if (!read(address + kIdentityOffsets[i], &out.identities[i],
                    sizeof(out.identities[i])))
                return false;
        }
        // Each 0x30-byte lane begins with two identities. The verified scalar
        // record follows for 0x1C bytes; the final four raw bytes remain out
        // of both restore and peer-canonical hashing.
        if (!read(address + 0x20, out.lane_a.data(), out.lane_a.size())
            || !read(address + 0x50, out.lane_b.data(), out.lane_b.size())
            || !read(address + 0x70, out.controls.data(),
                out.controls.size()))
            return false;
        int32_t state = 0;
        uint32_t enabled = 0;
        std::memcpy(&state, out.controls.data(), sizeof(state));
        std::memcpy(&enabled, out.controls.data() + 0x0C,
            sizeof(enabled));
        if (state < 0 || state > 4 || enabled > 1) return false;
        out.valid = true;
        out.semantic_hash = HashRollbackLuxMoveSystemPumpSemantic(out);
        out.integrity_hash = HashRollbackLuxMoveSystemPumpIntegrity(out);
        return ValidateRollbackLuxMoveSystemPumpSnapshot(out);
    }

    static inline bool CaptureRollbackLuxMoveSystemPumpSnapshot(
        uintptr_t image_base,
        RollbackLuxMoveSystemPumpSnapshot& out) noexcept
    {
        return image_base != 0
            && CaptureRollbackLuxMoveSystemPumpSnapshotWith(
                image_base + kRollbackRvaLuxMoveSystemVMPumpState,
                [](uintptr_t address, void* destination,
                   size_t bytes) noexcept {
                    return SafeReadBytes(
                        reinterpret_cast<const void*>(address),
                        destination, bytes);
                }, out);
    }

    template <typename ReadFn>
    static inline bool RollbackLuxMoveSystemPumpGenerationMatchesWith(
        const RollbackLuxMoveSystemPumpSnapshot& state,
        ReadFn&& read) noexcept
    {
        if (!ValidateRollbackLuxMoveSystemPumpSnapshot(state)) return false;
        static constexpr size_t kIdentityOffsets[] = {
            0x00, 0x08, 0x10, 0x18, 0x40, 0x48,
        };
        for (size_t i = 0; i < std::size(kIdentityOffsets); ++i)
        {
            uintptr_t live = 0;
            if (!read(state.address + kIdentityOffsets[i], &live,
                    sizeof(live))
                || live != state.identities[i])
                return false;
        }
        return true;
    }

    static inline bool RollbackLuxMoveSystemPumpGenerationMatches(
        const RollbackLuxMoveSystemPumpSnapshot& state) noexcept
    {
        return RollbackLuxMoveSystemPumpGenerationMatchesWith(
            state,
            [](uintptr_t address, void* destination,
               size_t bytes) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, bytes);
            });
    }

    template <typename ReadFn, typename WriteFn>
    static inline bool RestoreRollbackLuxMoveSystemPumpSnapshotWith(
        const RollbackLuxMoveSystemPumpSnapshot& state,
        ReadFn&& read, WriteFn&& write) noexcept
    {
        if (!RollbackLuxMoveSystemPumpGenerationMatchesWith(state, read))
            return false;
        if (!write(state.address + 0x20, state.lane_a.data(),
                state.lane_a.size())
            || !write(state.address + 0x50, state.lane_b.data(),
                state.lane_b.size())
            || !write(state.address + 0x70, state.controls.data(),
                state.controls.size()))
            return false;
        RollbackLuxMoveSystemPumpSnapshot verification {};
        return CaptureRollbackLuxMoveSystemPumpSnapshotWith(
                state.address, read, verification)
            && verification.semantic_hash == state.semantic_hash;
    }

    static inline bool RestoreRollbackLuxMoveSystemPumpSnapshot(
        const RollbackLuxMoveSystemPumpSnapshot& state) noexcept
    {
        return RestoreRollbackLuxMoveSystemPumpSnapshotWith(
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
