#include "../HorseMod/horselib/RollbackPresentationSemanticSnapshot.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    template <size_t N>
    void put_pointer(std::array<uint8_t, N>& object,
        uintptr_t value) noexcept
    {
        std::memcpy(object.data(), &value, sizeof(value));
    }
}

int main()
{
    std::array<uint8_t, 0x500> round_actor {};
    std::array<std::array<uint8_t, 0x500>, 2> active_actor {};
    std::array<std::array<uint8_t, 0xB00>, 2> chara_actor {};
    std::array<std::array<uint8_t, 0x900>, 2> presentation_output {};
    std::array<uint8_t, 8> round_vtable {};
    std::array<std::array<uint8_t, 8>, 2> active_vtable {};
    std::array<std::array<uint8_t, 8>, 2> chara_vtable {};
    std::array<std::array<uint8_t, 8>, 2> battle_manager {};
    std::array<std::array<uint8_t, 8>, 2> presentation_provider {};

    Horse::RollbackPresentationSemanticIdentity identity {};
    identity.hub = 0x12340000;
    identity.round_actor =
        reinterpret_cast<uintptr_t>(round_actor.data());
    identity.round_actor_vtable =
        reinterpret_cast<uintptr_t>(round_vtable.data());
    identity.phase_active_owner_count = 2;
    identity.topology_digest = 0xAABBCCDDEEFF0011ull;
    for (size_t i = 0; i < active_actor.size(); ++i)
    {
        identity.phase_active_owner[i] =
            reinterpret_cast<uintptr_t>(active_actor[i].data());
        identity.phase_active_owner_vtable[i] =
            reinterpret_cast<uintptr_t>(active_vtable[i].data());
        put_pointer(active_actor[i],
            identity.phase_active_owner_vtable[i]);
    }
    identity.chara_owner_count = 2;
    for (size_t i = 0; i < chara_actor.size(); ++i)
    {
        identity.chara_owner[i] =
            reinterpret_cast<uintptr_t>(chara_actor[i].data());
        identity.chara_owner_vtable[i] =
            reinterpret_cast<uintptr_t>(chara_vtable[i].data());
        identity.chara_listener_mask[i] =
            Horse::RollbackPresentationSemanticIdentity::
                kRequiredCharaListenerMask;
        identity.chara_battle_manager[i] =
            reinterpret_cast<uintptr_t>(battle_manager[i].data());
        identity.chara_presentation_provider[i] =
            reinterpret_cast<uintptr_t>(presentation_provider[i].data());
        put_pointer(chara_actor[i], identity.chara_owner_vtable[i]);
        std::memcpy(chara_actor[i].data() + 0x98,
            &identity.chara_battle_manager[i],
            sizeof(identity.chara_battle_manager[i]));
        const uintptr_t output =
            reinterpret_cast<uintptr_t>(presentation_output[i].data());
        std::memcpy(chara_actor[i].data() + 0x390,
            &output, sizeof(output));
        const int32_t player_index = static_cast<int32_t>(i);
        std::memcpy(chara_actor[i].data() + 0x3A0,
            &player_index, sizeof(player_index));
    }
    identity.valid = true;
    put_pointer(round_actor, identity.round_actor_vtable);
    round_actor[0x489] = 1;
    round_actor[0x494] = 0;
    active_actor[0][0x4B8] = 1;
    active_actor[1][0x4B8] = 0;

    Horse::RollbackPresentationSemanticSnapshot captured {};
    if (!Horse::CaptureRollbackPresentationSemanticSnapshot(
            identity, captured)
        || !Horse::ValidateRollbackPresentationSemanticSnapshot(captured))
    {
        std::puts("presentation semantic capture failed");
        return 1;
    }

    round_actor[0x489] = 0;
    round_actor[0x494] = 1;
    active_actor[0][0x4B8] = 0;
    active_actor[1][0x4B8] = 1;
    if (!Horse::RestoreRollbackPresentationSemanticSnapshot(captured)
        || round_actor[0x489] != 1 || round_actor[0x494] != 0
        || active_actor[0][0x4B8] != 1
        || active_actor[1][0x4B8] != 0)
    {
        std::puts("presentation semantic restore failed");
        return 2;
    }

    const uint8_t before_identity_rejection = round_actor[0x489];
    put_pointer(round_actor,
        reinterpret_cast<uintptr_t>(active_vtable[0].data()));
    if (Horse::RestoreRollbackPresentationSemanticSnapshot(captured)
        || round_actor[0x489] != before_identity_rejection)
    {
        std::puts("presentation semantic identity drift was not atomic");
        return 3;
    }
    put_pointer(round_actor, identity.round_actor_vtable);

    auto corrupted = captured;
    corrupted.phase_active[0] ^= 1;
    if (Horse::ValidateRollbackPresentationSemanticSnapshot(corrupted)
        || Horse::RestoreRollbackPresentationSemanticSnapshot(corrupted))
    {
        std::puts("presentation semantic integrity mutation accepted");
        return 4;
    }

    std::puts("rollback presentation semantic snapshot self-test passed");
    return 0;
}
