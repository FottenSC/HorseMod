#include "RollbackCharaPresentation.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <utility>

int main()
{
    using namespace Horse;

    constexpr std::array<std::pair<RollbackCharaPresentationOperation,
        uint16_t>, 10> cases {{
        {RollbackCharaPresentationOperation::SetupWeaponBones, 0},
        {RollbackCharaPresentationOperation::SetupWeaponActors, 0},
        {RollbackCharaPresentationOperation::MovePhaseActiveLatch, 0},
        {RollbackCharaPresentationOperation::MovePhaseActive, 0},
        {RollbackCharaPresentationOperation::SoulChargeState, 8},
        {RollbackCharaPresentationOperation::EffectColorFade, 64},
        {RollbackCharaPresentationOperation::
            ResetBreakAndAttackPresentation, 0},
        {RollbackCharaPresentationOperation::PlayerVisibility, 8},
        {RollbackCharaPresentationOperation::WeaponNodeAlpha, 12},
        {RollbackCharaPresentationOperation::MaterialChargeRate, 12},
    }};

    for (const auto& [operation, bytes] : cases)
    {
        RollbackCharaPresentationInvocation invocation {};
        invocation.operation = operation;
        invocation.chara_role = 0;
        invocation.value_bytes = bytes;
        assert(invocation.valid());
        invocation.chara_role = 1;
        assert(invocation.valid());
        invocation.chara_role = 2;
        assert(!invocation.valid());
        invocation.chara_role = 0;
        invocation.value_bytes = static_cast<uint16_t>(bytes + 1u);
        assert(!invocation.valid());
    }

    RollbackCharaPresentationInvocation unknown {};
    unknown.operation = static_cast<RollbackCharaPresentationOperation>(0);
    unknown.value_bytes = 0;
    assert(!unknown.valid());
    return 0;
}
