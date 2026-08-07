#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
#pragma pack(push, 1)
    struct RollbackAiPaletteScalars
    {
        int32_t current_mode {0};
        int32_t ai_mode {0};
        float phase {0.0f};
        float phase_velocity {0.0f};
        float phase_scale {0.0f};
        int32_t repeat_count {0};
        float random_threshold {0.0f};
        int32_t disabled {0};
        int32_t publish_disabled {0};
        std::array<uint8_t, 8> transition_scratch {};
        float sub_blend_scale {0.0f};
        int32_t lookup_index {0};
        float auxiliary_blend_weight {0.0f};
        float previous_auxiliary_blend_weight {0.0f};
        float complement_input {0.0f};
        uint32_t publish_tick_count {0};
    };
#pragma pack(pop)
    static_assert(sizeof(RollbackAiPaletteScalars) == 0x44);

    struct RollbackAiPaletteDiagnostics
    {
        bool readable {false};
        uint64_t scalar_hash {0};
        uint64_t active_motion_bank_rva {0};
        uint64_t key_buffer_rva {0};
        uint64_t bone_data_bank_rva {0};
        RollbackAiPaletteScalars scalars {};
    };

    static inline uint64_t RollbackAiPalettePointerCoordinate(
        uintptr_t pointer,
        uintptr_t image_base) noexcept
    {
        if (!pointer) return 0;
        return image_base && pointer >= image_base
            ? static_cast<uint64_t>(pointer - image_base)
            : static_cast<uint64_t>(pointer);
    }

    static inline RollbackAiPaletteDiagnostics
    CaptureRollbackAiPaletteDiagnosticsFromLayout(
        const void* palette_state,
        uintptr_t image_base) noexcept
    {
        RollbackAiPaletteDiagnostics result {};
        if (!palette_state) return result;

        std::array<uintptr_t, 3> pointers {};
        if (!SafeReadBytes(
                palette_state, pointers.data(), sizeof(pointers))
            || !SafeReadBytes(
                static_cast<const uint8_t*>(palette_state) + 0x18,
                &result.scalars, sizeof(result.scalars)))
        {
            return result;
        }

        RollbackFastHash hash {};
        hash.add_bytes(&result.scalars, sizeof(result.scalars));
        result.scalar_hash = hash.finish();
        result.active_motion_bank_rva =
            RollbackAiPalettePointerCoordinate(pointers[0], image_base);
        result.key_buffer_rva =
            RollbackAiPalettePointerCoordinate(pointers[1], image_base);
        result.bone_data_bank_rva =
            RollbackAiPalettePointerCoordinate(pointers[2], image_base);
        result.readable = result.scalar_hash != 0;
        return result;
    }

    static inline bool CaptureRollbackAiPaletteDiagnostics(
        uintptr_t image_base,
        std::array<RollbackAiPaletteDiagnostics, 2>& result) noexcept
    {
        result = {};
        if (!image_base) return false;
        static constexpr uintptr_t kCharaRvas[2] = {
            0x47156F0, 0x47ACAE0,
        };
        static constexpr uintptr_t kPaletteStateOffset = 0x971E8;
        for (size_t player = 0; player < result.size(); ++player)
        {
            result[player] =
                CaptureRollbackAiPaletteDiagnosticsFromLayout(
                    reinterpret_cast<const void*>(
                        image_base + kCharaRvas[player]
                        + kPaletteStateOffset),
                    image_base);
            if (!result[player].readable) return false;
        }
        return true;
    }

    static inline bool RestoreRollbackAiPaletteDiagnosticsToLayout(
        void* palette_state,
        uintptr_t image_base,
        const RollbackAiPaletteDiagnostics& expected) noexcept
    {
        if (!palette_state || !expected.readable
            || !SafeWriteBytes(
                static_cast<uint8_t*>(palette_state) + 0x18,
                &expected.scalars, sizeof(expected.scalars)))
        {
            return false;
        }
        const RollbackAiPaletteDiagnostics observed =
            CaptureRollbackAiPaletteDiagnosticsFromLayout(
                palette_state, image_base);
        return observed.readable
            && observed.scalar_hash == expected.scalar_hash
            && observed.active_motion_bank_rva
                == expected.active_motion_bank_rva
            && observed.key_buffer_rva == expected.key_buffer_rva
            && observed.bone_data_bank_rva
                == expected.bone_data_bank_rva;
    }

    static inline bool RestoreRollbackAiPaletteDiagnostics(
        uintptr_t image_base,
        const std::array<RollbackAiPaletteDiagnostics, 2>& expected) noexcept
    {
        if (!image_base) return false;
        static constexpr uintptr_t kCharaRvas[2] = {
            0x47156F0, 0x47ACAE0,
        };
        static constexpr uintptr_t kPaletteStateOffset = 0x971E8;
        bool ok = true;
        for (size_t player = 0; player < expected.size(); ++player)
        {
            ok = RestoreRollbackAiPaletteDiagnosticsToLayout(
                    reinterpret_cast<void*>(
                        image_base + kCharaRvas[player]
                        + kPaletteStateOffset),
                    image_base, expected[player])
                && ok;
        }
        return ok;
    }
}
