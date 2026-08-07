#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
#pragma pack(push, 1)
    enum class RollbackCharaPresentationOperation : uint8_t
    {
        SetupWeaponBones = 1,
        SetupWeaponActors,
        MovePhaseActiveLatch,
        MovePhaseActive,
        SoulChargeState,
        EffectColorFade,
        ResetBreakAndAttackPresentation,
        PlayerVisibility,
        WeaponNodeAlpha,
        MaterialChargeRate,
    };

    struct RollbackCharaPresentationInvocation
    {
        RollbackCharaPresentationOperation operation {
            RollbackCharaPresentationOperation::SetupWeaponBones};
        uint8_t chara_role {0};
        uint16_t value_bytes {0};
        std::array<uint8_t, 64> value {};

        static constexpr uint16_t expected_value_bytes(
            RollbackCharaPresentationOperation operation) noexcept
        {
            switch (operation)
            {
            case RollbackCharaPresentationOperation::SetupWeaponBones:
            case RollbackCharaPresentationOperation::SetupWeaponActors:
            case RollbackCharaPresentationOperation::MovePhaseActiveLatch:
            case RollbackCharaPresentationOperation::MovePhaseActive:
            case RollbackCharaPresentationOperation::
                    ResetBreakAndAttackPresentation:
                return 0;
            case RollbackCharaPresentationOperation::SoulChargeState:
            case RollbackCharaPresentationOperation::PlayerVisibility:
                return 8;
            case RollbackCharaPresentationOperation::WeaponNodeAlpha:
            case RollbackCharaPresentationOperation::MaterialChargeRate:
                return 12;
            case RollbackCharaPresentationOperation::EffectColorFade:
                return 64;
            }
            return UINT16_MAX;
        }

        bool valid() const noexcept
        {
            return chara_role < 2
                && value_bytes == expected_value_bytes(operation)
                && value_bytes <= value.size();
        }
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackCharaPresentationInvocation) == 68);
}
