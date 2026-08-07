#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Horse
{
    // SoulcaliburVI.exe g_abLuxMoveVMSlotParamArray @ image + 0x470E0C0.
    // Ghidra establishes two pointer-free 0x2C-byte records. Native arithmetic
    // consumes the semantic prefix +0x00..+0x27. The final dword at +0x28 is
    // initialization-only stride padding, so it is neither restored nor
    // peer-canonicalized.
    static constexpr uintptr_t kRollbackRvaLuxMoveVmSlotParamArray =
        0x470E0C0;

    struct RollbackLuxMoveVmSlotParam
    {
        int32_t mode {0};
        int32_t next_mode {0};
        float current_value {0.0f};
        float target_value {0.0f};
        float intermediate_value {0.0f};
        float rate_per_frame {0.0f};
        int32_t delay_frames {0};
        int32_t first_duration_frames {0};
        int32_t saved_delay_frames {0};
        float rate_divisor {0.0f};
        uint32_t stride_padding {0};
    };

    static_assert(sizeof(RollbackLuxMoveVmSlotParam) == 0x2C);
    static_assert(std::is_trivially_copyable_v<RollbackLuxMoveVmSlotParam>);

    struct RollbackLuxMoveVmSlotParamSnapshot
    {
        uintptr_t address {0};
        std::array<RollbackLuxMoveVmSlotParam, 2> lanes {};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};
        bool valid {false};

        void clear() noexcept { *this = {}; }
    };

    static_assert(
        sizeof(RollbackLuxMoveVmSlotParamSnapshot::lanes) == 0x58);

    static constexpr size_t kRollbackLuxMoveVmSlotParamSemanticBytes =
        offsetof(RollbackLuxMoveVmSlotParam, stride_padding);

    static inline uint64_t HashRollbackLuxMoveVmSlotParamCanonical(
        const RollbackLuxMoveVmSlotParamSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        RollbackHash hash {};
        for (const auto& lane : state.lanes)
            hash.add_bytes(
                &lane, kRollbackLuxMoveVmSlotParamSemanticBytes);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackLuxMoveVmSlotParamIntegrity(
        const RollbackLuxMoveVmSlotParamSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        const uint64_t canonical =
            HashRollbackLuxMoveVmSlotParamCanonical(state);
        if (!canonical || !state.address) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.address);
        hash.add_scalar(canonical);
        return hash.value ? hash.value : 1;
    }

    static inline bool ValidateRollbackLuxMoveVmSlotParamSnapshot(
        const RollbackLuxMoveVmSlotParamSnapshot& state) noexcept
    {
        return state.valid && state.address
            && state.canonical_hash
                == HashRollbackLuxMoveVmSlotParamCanonical(state)
            && state.integrity_hash
                == HashRollbackLuxMoveVmSlotParamIntegrity(state);
    }

    template <typename ReadFn>
    static inline bool CaptureRollbackLuxMoveVmSlotParamSnapshotWith(
        uintptr_t address, ReadFn&& read,
        RollbackLuxMoveVmSlotParamSnapshot& out) noexcept
    {
        out.clear();
        if (!address
            || !read(address, out.lanes.data(), sizeof(out.lanes)))
        {
            return false;
        }
        out.address = address;
        for (auto& lane : out.lanes) lane.stride_padding = 0;
        out.valid = true;
        out.canonical_hash =
            HashRollbackLuxMoveVmSlotParamCanonical(out);
        out.integrity_hash =
            HashRollbackLuxMoveVmSlotParamIntegrity(out);
        return ValidateRollbackLuxMoveVmSlotParamSnapshot(out);
    }

    static inline bool CaptureRollbackLuxMoveVmSlotParamSnapshot(
        uintptr_t image_base,
        RollbackLuxMoveVmSlotParamSnapshot& out) noexcept
    {
        return image_base
            && CaptureRollbackLuxMoveVmSlotParamSnapshotWith(
                image_base + kRollbackRvaLuxMoveVmSlotParamArray,
                [](uintptr_t address, void* destination,
                   size_t bytes) noexcept {
                    return SafeReadBytes(
                        reinterpret_cast<const void*>(address),
                        destination, bytes);
                }, out);
    }

    template <typename ReadFn, typename WriteFn>
    static inline bool RestoreRollbackLuxMoveVmSlotParamSnapshotWith(
        const RollbackLuxMoveVmSlotParamSnapshot& state,
        ReadFn&& read, WriteFn&& write) noexcept
    {
        if (!ValidateRollbackLuxMoveVmSlotParamSnapshot(state))
            return false;
        for (size_t lane = 0; lane < state.lanes.size(); ++lane)
        {
            if (!write(
                    state.address
                        + lane * sizeof(RollbackLuxMoveVmSlotParam),
                    &state.lanes[lane],
                    kRollbackLuxMoveVmSlotParamSemanticBytes))
            {
                return false;
            }
        }

        RollbackLuxMoveVmSlotParamSnapshot verification {};
        return CaptureRollbackLuxMoveVmSlotParamSnapshotWith(
                state.address, read, verification)
            && verification.integrity_hash == state.integrity_hash;
    }

    static inline bool RestoreRollbackLuxMoveVmSlotParamSnapshot(
        const RollbackLuxMoveVmSlotParamSnapshot& state) noexcept
    {
        return RestoreRollbackLuxMoveVmSlotParamSnapshotWith(
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
