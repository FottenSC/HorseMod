#include "../HorseMod/horselib/RollbackBattleCameraSnapshot.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    template <size_t N>
    bool copy_from(const std::array<uint8_t, N>& bytes, uintptr_t base,
        uintptr_t address, void* destination, size_t count) noexcept
    {
        if (address < base || count > N || address - base > N - count)
            return false;
        std::memcpy(destination, bytes.data() + address - base, count);
        return true;
    }

    template <size_t N>
    bool copy_to(std::array<uint8_t, N>& bytes, uintptr_t base,
        uintptr_t address, const void* source, size_t count) noexcept
    {
        if (address < base || count > N || address - base > N - count)
            return false;
        std::memcpy(bytes.data() + address - base, source, count);
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
    constexpr uintptr_t director_base =
        image_base + Horse::kRollbackRvaLuxCameraDirector;
    constexpr uintptr_t component_base = 0x70000000;
    constexpr uintptr_t component_vtable_base = 0x60000300;
    constexpr uintptr_t chara_p1 = 0x71000000;
    constexpr uintptr_t chara_p2 = 0x72000000;
    constexpr uintptr_t vectors_base =
        image_base + Horse::kRollbackRvaLuxBattleCameraFrameVectors;
    constexpr uintptr_t yaw_base =
        image_base + Horse::kRollbackRvaLuxBattleCameraYawTurns;
    constexpr uintptr_t mode_base =
        image_base + Horse::kRollbackRvaLuxBattleCameraMode;

    std::array<uint8_t, 0x7A8> director {};
    std::array<uint8_t, 0x310> component {};
    std::array<uint8_t, 0x110> component_vtable {};
    std::array<uint8_t, 8> chara_slot_p1 {};
    std::array<uint8_t, 8> chara_slot_p2 {};
    std::array<uint8_t, 0x60> vectors {};
    std::array<uint8_t, 4> yaw {};
    std::array<uint8_t, 4> mode {};
    put(director, 0x00, uintptr_t {0x60000000});
    put(director, 0x10, uintptr_t {0x60000100});
    put(director, 0x7A0, uintptr_t {0x60000200});
    put(director, 0x270, component_base);
    put(component, 0x00, component_vtable_base);
    put(component_vtable, 0x100,
        image_base + Horse::kRollbackRvaCameraSerializeBase);
    put(chara_slot_p1, 0, chara_p1);
    put(chara_slot_p2, 0, chara_p2);
    for (size_t i = 0; i < director.size(); ++i)
    {
        if ((i >= 0x2F0 && i < 0x338)
            || (i >= 0x350 && i < 0x360)
            || (i >= 0x220 && i < 0x26C))
            director[i] = static_cast<uint8_t>(i * 7u + 3u);
    }
    // Rewrite identities after the deterministic fill above.
    put(director, 0x7A0, uintptr_t {0x60000200});
    for (size_t i = 0; i < component.size(); ++i)
        component[i] = static_cast<uint8_t>(i * 13u + 9u);
    put(component, 0x00, component_vtable_base);
    for (size_t i = 0; i < vectors.size(); ++i)
        vectors[i] = static_cast<uint8_t>(i * 5u + 1u);
    put(yaw, 0, uint32_t {0x3E800000u});
    put(mode, 0, uint32_t {3});

    const auto read = [&](uintptr_t address, void* destination,
                          size_t count) noexcept {
        return copy_from(director, director_base, address, destination, count)
            || copy_from(component, component_base, address, destination, count)
            || copy_from(component_vtable, component_vtable_base,
                address, destination, count)
            || copy_from(chara_slot_p1,
                image_base + Horse::kRollbackRvaLuxBattleCharaP1,
                address, destination, count)
            || copy_from(chara_slot_p2,
                image_base + Horse::kRollbackRvaLuxBattleCharaP2,
                address, destination, count)
            || copy_from(vectors, vectors_base, address, destination, count)
            || copy_from(yaw, yaw_base, address, destination, count)
            || copy_from(mode, mode_base, address, destination, count);
    };
    size_t writes = 0;
    int last_phase = 0;
    bool ordered = true;
    const auto write = [&](uintptr_t address, const void* source,
                           size_t count) noexcept {
        int phase = 0;
        if ((address >= director_base + 0x2F0
                && address < director_base + 0x360)) phase = 1;
        else if (address >= component_base
            && address < component_base + component.size()) phase = 2;
        else phase = 3;
        ordered &= phase >= last_phase;
        last_phase = phase;
        ++writes;
        return copy_to(director, director_base, address, source, count)
            || copy_to(component, component_base, address, source, count)
            || copy_to(vectors, vectors_base, address, source, count)
            || copy_to(yaw, yaw_base, address, source, count)
            || copy_to(mode, mode_base, address, source, count);
    };

    Horse::RollbackBattleCameraSnapshot snapshot {};
    if (!Horse::CaptureRollbackBattleCameraSnapshotWith(
            image_base, read, snapshot))
    {
        std::printf("battle-camera capture failed\n");
        return 1;
    }

    using Serialization =
        Horse::RollbackBattleCameraComponentSerialization;
    if (Horse::RollbackBattleCameraSerializationForWriter(
            image_base,
            image_base + Horse::kRollbackRvaCameraSerializeBase)
            != Serialization::Base
        || Horse::RollbackBattleCameraSerializationForWriter(
            image_base,
            image_base + Horse::kRollbackRvaCameraSerializeStateBuffer)
            != Serialization::StateBuffer
        || Horse::RollbackBattleCameraSerializationForWriter(
            image_base,
            image_base + Horse::kRollbackRvaCameraSerializePlayerWatch)
            != Serialization::PlayerWatch
        || Horse::RollbackBattleCameraSerializationForWriter(
            image_base,
            image_base + Horse::kRollbackRvaCameraSerializeAttention)
            != Serialization::Attention
        || Horse::RollbackBattleCameraSerializationForWriter(
            image_base,
            image_base + Horse::kRollbackRvaCameraSerializeStay)
            != Serialization::Stay)
    {
        std::printf("battle-camera serializer classification failed\n");
        return 1;
    }
    const auto expected_director = director;
    const auto expected_component = component;
    const auto expected_vectors = vectors;
    director[0x2F0] ^= 0x7F;
    director[0x220] ^= 0x7F;
    component[0x0C] ^= 0x7F;
    component[0x88] ^= 0x7F;
    component[0xF0] ^= 0x7F;
    component[0x100] ^= 0x7F;
    component[0x140] ^= 0x7F;
    vectors[0x5F] ^= 0x7F;
    yaw[0] ^= 0x7F;
    mode[0] ^= 0x7F;
    if (!Horse::RestoreRollbackBattleCameraSnapshotWith(
            image_base, snapshot, read, write)
        || !ordered || writes == 0
        || director[0x2F0] != expected_director[0x2F0]
        || director[0x220] != expected_director[0x220]
        || component[0x0C] != expected_component[0x0C]
        || component[0x88] != expected_component[0x88]
        || component[0xF0] != expected_component[0xF0]
        || component[0x100] != expected_component[0x100]
        || component[0x140] != expected_component[0x140]
        || vectors != expected_vectors)
    {
        std::printf("battle-camera ordered restore failed\n");
        return 1;
    }

    put(component_vtable, 0x100,
        image_base + Horse::kRollbackRvaCameraSerializePlayerWatch);
    put(component, 0x1E8, uintptr_t {0x73000000});
    put(component, 0x1F0, chara_p2);
    Horse::RollbackBattleCameraSnapshot player_watch_snapshot {};
    if (!Horse::CaptureRollbackBattleCameraSnapshotWith(
            image_base, read, player_watch_snapshot))
    {
        std::printf("battle-camera PlayerWatch capture failed\n");
        return 1;
    }
    const uint8_t expected_player_watch_common = component[0x88];
    const uint8_t expected_player_watch_derived = component[0x1D0];
    component[0x88] ^= 0x55;
    component[0x1D0] ^= 0x55;
    put(component, 0x1E8, uintptr_t {0x74000000});
    put(component, 0x1F0, chara_p1);
    writes = 0;
    last_phase = 0;
    ordered = true;
    if (!Horse::RestoreRollbackBattleCameraSnapshotWith(
            image_base, player_watch_snapshot, read, write)
        || !ordered || component[0x88] != expected_player_watch_common
        || component[0x1D0] != expected_player_watch_derived)
    {
        std::printf("battle-camera PlayerWatch restore failed\n");
        return 1;
    }
    uintptr_t restored_pointer = 1;
    std::memcpy(&restored_pointer, component.data() + 0x1E8,
        sizeof(restored_pointer));
    if (restored_pointer != 0)
    {
        std::printf("battle-camera PlayerWatch transient pointer survived\n");
        return 1;
    }
    std::memcpy(&restored_pointer, component.data() + 0x1F0,
        sizeof(restored_pointer));
    if (restored_pointer != chara_p2)
    {
        std::printf("battle-camera PlayerWatch identity rebind failed\n");
        return 1;
    }

    put(component_vtable, 0x100,
        image_base + Horse::kRollbackRvaCameraSerializeAttention);
    writes = 0;
    if (Horse::RestoreRollbackBattleCameraSnapshotWith(
            image_base, snapshot, read, write)
        || writes != 0)
    {
        std::printf("battle-camera writer identity preflight failed\n");
        return 1;
    }
    put(component_vtable, 0x100,
        image_base + Horse::kRollbackRvaCameraSerializeBase);

    put(director, 0x270, uintptr_t {component_base + 0x1000});
    writes = 0;
    if (Horse::RestoreRollbackBattleCameraSnapshotWith(
            image_base, snapshot, read, write)
        || writes != 0)
    {
        std::printf("battle-camera atomic identity preflight failed\n");
        return 1;
    }

    std::printf("rollback battle-camera snapshot self-test passed\n");
    return 0;
}
