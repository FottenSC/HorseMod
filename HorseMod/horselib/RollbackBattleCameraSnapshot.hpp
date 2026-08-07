#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uintptr_t kRollbackRvaLuxCameraDirector = 0x470E9F0;
    static constexpr uintptr_t kRollbackRvaLuxBattleCameraFrameVectors =
        0x470D1A0;
    static constexpr uintptr_t kRollbackRvaLuxBattleCameraYawTurns =
        0x470D0DC;
    static constexpr uintptr_t kRollbackRvaLuxBattleCameraMode = 0x470D198;
    static constexpr uintptr_t kRollbackRvaLuxBattleCharaP1 = 0x470DE90;
    static constexpr uintptr_t kRollbackRvaLuxBattleCharaP2 = 0x470DE98;

    static constexpr uintptr_t kRollbackRvaCameraSerializeBase = 0x33FBC0;
    static constexpr uintptr_t kRollbackRvaCameraSerializeStateBuffer =
        0x318860;
    static constexpr uintptr_t kRollbackRvaCameraSerializePlayerWatch =
        0x317BD0;
    static constexpr uintptr_t kRollbackRvaCameraSerializeAttention =
        0x340ED0;
    static constexpr uintptr_t kRollbackRvaCameraSerializeStay = 0x341110;

    enum class RollbackBattleCameraComponentSerialization : uint8_t
    {
        None,
        Base,
        StateBuffer,
        PlayerWatch,
        Attention,
        Stay,
    };

    static constexpr size_t kRollbackBattleCameraCommonSemanticBytes =
        0x134;
    static constexpr size_t kRollbackBattleCameraMaximumDerivedBytes =
        0x140;

    struct RollbackBattleCameraComponentState
    {
        uintptr_t object {0};
        uintptr_t vtable {0};
        uintptr_t writer {0};
        RollbackBattleCameraComponentSerialization serialization {
            RollbackBattleCameraComponentSerialization::None};
        uint16_t derived_size {0};
        int8_t tracked_chara_slot {-1};
        std::array<uint8_t, kRollbackBattleCameraCommonSemanticBytes>
            common_semantic {};
        std::array<uint8_t, kRollbackBattleCameraMaximumDerivedBytes>
            derived_semantic {};
    };

    struct RollbackBattleCameraSnapshot
    {
        uintptr_t director {0};
        uintptr_t director_vtable {0};
        uintptr_t scratch_vtable {0};
        uintptr_t timer_action_root {0};
        std::array<uintptr_t, 2> chara_identity {};
        std::array<uint8_t, 0x48> transition_and_blend_controls {};
        std::array<uint8_t, 0x10> post_blend_controls {};
        std::array<uint8_t, 0x4C> director_published_output {};
        std::array<RollbackBattleCameraComponentState, 16> components {};
        std::array<uint8_t, 0x60> frame_vectors {};
        float yaw_turns {0.0f};
        uint32_t camera_mode {0};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};
        bool valid {false};

        void clear() noexcept { *this = {}; }
    };

    struct RollbackBattleCameraSemanticField
    {
        uint16_t offset;
        uint16_t bytes;
    };

    // Exact field order used by LuxEffectCameraComponent_Serialize
    // @ 0x14033FBC0 and its inverse @ 0x14033FF40. Gaps are intentionally
    // omitted because the native serializer does not treat them as state.
    static constexpr RollbackBattleCameraSemanticField
        kRollbackBattleCameraComponentSemanticFields[] {
            {0x008, 0x04}, {0x00C, 0x04}, {0x010, 0x04},
            {0x014, 0x04}, {0x020, 0x10}, {0x030, 0x04},
            {0x040, 0x10}, {0x050, 0x10}, {0x060, 0x10},
            {0x070, 0x04}, {0x074, 0x04}, {0x078, 0x04},
            {0x07C, 0x04}, {0x080, 0x04}, {0x084, 0x04},
            {0x088, 0x04}, {0x08C, 0x04}, {0x090, 0x04},
            {0x094, 0x04}, {0x098, 0x04}, {0x09C, 0x04},
            {0x0A0, 0x10}, {0x0B0, 0x04}, {0x0B4, 0x04},
            {0x0B8, 0x04}, {0x0C0, 0x10}, {0x0D0, 0x04},
            {0x0D4, 0x04}, {0x0E0, 0x10}, {0x0F0, 0x10},
            {0x100, 0x04}, {0x110, 0x10}, {0x120, 0x04},
            {0x130, 0x10}, {0x140, 0x04}, {0x158, 0x04},
            {0x15C, 0x04}, {0x160, 0x10}, {0x170, 0x10},
            {0x180, 0x04}, {0x184, 0x04},
        };

    static inline RollbackBattleCameraComponentSerialization
    RollbackBattleCameraSerializationForWriter(
        uintptr_t image_base, uintptr_t writer) noexcept
    {
        if (writer == image_base + kRollbackRvaCameraSerializeBase)
            return RollbackBattleCameraComponentSerialization::Base;
        if (writer == image_base
                + kRollbackRvaCameraSerializeStateBuffer)
            return RollbackBattleCameraComponentSerialization::StateBuffer;
        if (writer == image_base
                + kRollbackRvaCameraSerializePlayerWatch)
            return RollbackBattleCameraComponentSerialization::PlayerWatch;
        if (writer == image_base + kRollbackRvaCameraSerializeAttention)
            return RollbackBattleCameraComponentSerialization::Attention;
        if (writer == image_base + kRollbackRvaCameraSerializeStay)
            return RollbackBattleCameraComponentSerialization::Stay;
        return RollbackBattleCameraComponentSerialization::None;
    }

    static inline uint16_t RollbackBattleCameraDerivedSize(
        RollbackBattleCameraComponentSerialization serialization) noexcept
    {
        switch (serialization)
        {
        case RollbackBattleCameraComponentSerialization::StateBuffer:
            return 0x140;
        case RollbackBattleCameraComponentSerialization::PlayerWatch:
            return 0x14;
        case RollbackBattleCameraComponentSerialization::Attention:
            return 0x10;
        case RollbackBattleCameraComponentSerialization::Stay:
            return 0x0C;
        default:
            return 0;
        }
    }

    static inline uint64_t HashRollbackBattleCameraCanonical(
        const RollbackBattleCameraSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        RollbackHash hash {};
        hash.add_bytes(state.transition_and_blend_controls.data(),
            state.transition_and_blend_controls.size());
        hash.add_bytes(state.post_blend_controls.data(),
            state.post_blend_controls.size());
        hash.add_bytes(state.director_published_output.data(),
            state.director_published_output.size());
        for (size_t slot = 0; slot < state.components.size(); ++slot)
        {
            hash.add_scalar(slot);
            const bool present = state.components[slot].object != 0;
            hash.add_scalar(present);
            if (present)
            {
                const auto& component = state.components[slot];
                hash.add_scalar(component.serialization);
                hash.add_scalar(component.derived_size);
                hash.add_scalar(component.tracked_chara_slot);
                hash.add_bytes(component.common_semantic.data(),
                    component.common_semantic.size());
                hash.add_bytes(component.derived_semantic.data(),
                    component.derived_size);
            }
        }
        hash.add_bytes(state.frame_vectors.data(), state.frame_vectors.size());
        hash.add_scalar(state.yaw_turns);
        hash.add_scalar(state.camera_mode);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackBattleCameraIntegrity(
        const RollbackBattleCameraSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        const uint64_t canonical = HashRollbackBattleCameraCanonical(state);
        if (!canonical) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.director);
        hash.add_scalar(state.director_vtable);
        hash.add_scalar(state.scratch_vtable);
        hash.add_scalar(state.timer_action_root);
        hash.add_bytes(state.chara_identity.data(),
            sizeof(state.chara_identity));
        for (const auto& component : state.components)
        {
            hash.add_scalar(component.object);
            hash.add_scalar(component.vtable);
            hash.add_scalar(component.writer);
        }
        hash.add_scalar(canonical);
        return hash.value ? hash.value : 1;
    }

    static inline bool ValidateRollbackBattleCameraSnapshot(
        const RollbackBattleCameraSnapshot& state) noexcept
    {
        if (!(state.valid && state.director && state.director_vtable
            && state.scratch_vtable && state.timer_action_root
            && state.chara_identity[0] && state.chara_identity[1]
            && state.canonical_hash == HashRollbackBattleCameraCanonical(state)
            && state.integrity_hash == HashRollbackBattleCameraIntegrity(state)))
            return false;
        for (const auto& component : state.components)
        {
            if (!component.object)
            {
                if (component.vtable || component.writer
                    || component.serialization
                        != RollbackBattleCameraComponentSerialization::None)
                    return false;
                continue;
            }
            if (!component.vtable || !component.writer
                || component.serialization
                    == RollbackBattleCameraComponentSerialization::None
                || component.derived_size
                    != RollbackBattleCameraDerivedSize(
                        component.serialization)
                || component.tracked_chara_slot < -1
                || component.tracked_chara_slot > 1
                || (component.serialization
                        != RollbackBattleCameraComponentSerialization::PlayerWatch
                    && component.tracked_chara_slot != -1))
                return false;
        }
        return true;
    }

    template <typename ReadFn>
    static inline bool CaptureRollbackBattleCameraSnapshotWith(
        uintptr_t image_base, ReadFn&& read,
        RollbackBattleCameraSnapshot& out) noexcept
    {
        out.clear();
        if (!image_base) return false;
        out.director = image_base + kRollbackRvaLuxCameraDirector;
        if (!read(image_base + kRollbackRvaLuxBattleCharaP1,
                &out.chara_identity[0], sizeof(out.chara_identity[0]))
            || !read(image_base + kRollbackRvaLuxBattleCharaP2,
                &out.chara_identity[1], sizeof(out.chara_identity[1]))
            || !out.chara_identity[0] || !out.chara_identity[1]
            || out.chara_identity[0] == out.chara_identity[1])
            return false;
        if (!read(out.director, &out.director_vtable,
                sizeof(out.director_vtable))
            || !read(out.director + 0x10, &out.scratch_vtable,
                sizeof(out.scratch_vtable))
            || !read(out.director + 0x7A0, &out.timer_action_root,
                sizeof(out.timer_action_root))
            || !out.director_vtable || !out.scratch_vtable
            || !out.timer_action_root
            || !read(out.director + 0x2F0,
                out.transition_and_blend_controls.data(),
                out.transition_and_blend_controls.size())
            || !read(out.director + 0x350,
                out.post_blend_controls.data(),
                out.post_blend_controls.size())
            || !read(out.director + 0x220,
                out.director_published_output.data(),
                out.director_published_output.size()))
        {
            return false;
        }

        for (size_t slot = 0; slot < out.components.size(); ++slot)
        {
            auto& component = out.components[slot];
            if (!read(out.director + 0x270 + slot * sizeof(uintptr_t),
                    &component.object, sizeof(component.object)))
                return false;
            if (!component.object) continue;
            if (!read(component.object, &component.vtable,
                    sizeof(component.vtable))
                || !component.vtable
                || !read(component.vtable + 0x100, &component.writer,
                    sizeof(component.writer))
                || !component.writer)
                return false;
            component.serialization =
                RollbackBattleCameraSerializationForWriter(
                    image_base, component.writer);
            if (component.serialization
                == RollbackBattleCameraComponentSerialization::None)
                return false;
            component.derived_size = RollbackBattleCameraDerivedSize(
                component.serialization);
            size_t cursor = 0;
            for (const auto& field :
                 kRollbackBattleCameraComponentSemanticFields)
            {
                if (cursor + field.bytes
                        > component.common_semantic.size()
                    || !read(component.object + field.offset,
                        component.common_semantic.data() + cursor,
                        field.bytes))
                    return false;
                cursor += field.bytes;
            }
            if (cursor != component.common_semantic.size()) return false;

            switch (component.serialization)
            {
            case RollbackBattleCameraComponentSerialization::StateBuffer:
                if (!read(component.object + 0x1D0,
                        component.derived_semantic.data(), 0x140))
                    return false;
                break;
            case RollbackBattleCameraComponentSerialization::PlayerWatch:
            {
                if (!read(component.object + 0x1D0,
                        component.derived_semantic.data(), 0x14))
                    return false;
                uintptr_t tracked = 0;
                if (!read(component.object + 0x1F0, &tracked,
                        sizeof(tracked)))
                    return false;
                component.tracked_chara_slot = tracked == 0
                    ? -1
                    : tracked == out.chara_identity[0]
                        ? 0
                        : tracked == out.chara_identity[1] ? 1 : -2;
                if (component.tracked_chara_slot == -2) return false;
                break;
            }
            case RollbackBattleCameraComponentSerialization::Attention:
                if (!read(component.object + 0x1D0,
                        component.derived_semantic.data(), 0x0C)
                    || !read(component.object + 0x1E8,
                        component.derived_semantic.data() + 0x0C, 0x04))
                    return false;
                break;
            case RollbackBattleCameraComponentSerialization::Stay:
                if (!read(component.object + 0x1D0,
                        component.derived_semantic.data(), 0x0C))
                    return false;
                break;
            default:
                break;
            }
        }

        if (!read(image_base + kRollbackRvaLuxBattleCameraFrameVectors,
                out.frame_vectors.data(), out.frame_vectors.size())
            || !read(image_base + kRollbackRvaLuxBattleCameraYawTurns,
                &out.yaw_turns, sizeof(out.yaw_turns))
            || !read(image_base + kRollbackRvaLuxBattleCameraMode,
                &out.camera_mode, sizeof(out.camera_mode)))
        {
            return false;
        }
        out.valid = true;
        out.canonical_hash = HashRollbackBattleCameraCanonical(out);
        out.integrity_hash = HashRollbackBattleCameraIntegrity(out);
        return ValidateRollbackBattleCameraSnapshot(out);
    }

    static inline bool CaptureRollbackBattleCameraSnapshot(
        uintptr_t image_base, RollbackBattleCameraSnapshot& out) noexcept
    {
        return CaptureRollbackBattleCameraSnapshotWith(
            image_base,
            [](uintptr_t address, void* destination,
               size_t bytes) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, bytes);
            }, out);
    }

    template <typename ReadFn>
    static inline bool RollbackBattleCameraGenerationMatchesWith(
        const RollbackBattleCameraSnapshot& state,
        ReadFn&& read) noexcept
    {
        if (!ValidateRollbackBattleCameraSnapshot(state)) return false;
        if (state.director < kRollbackRvaLuxCameraDirector) return false;
        const uintptr_t image_base =
            state.director - kRollbackRvaLuxCameraDirector;
        uintptr_t live = 0;
        for (size_t player = 0; player < state.chara_identity.size(); ++player)
        {
            if (!read(image_base + (player == 0
                        ? kRollbackRvaLuxBattleCharaP1
                        : kRollbackRvaLuxBattleCharaP2),
                    &live, sizeof(live))
                || live != state.chara_identity[player])
                return false;
        }
        if (!read(state.director, &live, sizeof(live))
            || live != state.director_vtable
            || !read(state.director + 0x10, &live, sizeof(live))
            || live != state.scratch_vtable
            || !read(state.director + 0x7A0, &live, sizeof(live))
            || live != state.timer_action_root)
            return false;
        for (size_t slot = 0; slot < state.components.size(); ++slot)
        {
            const auto& component = state.components[slot];
            if (!read(state.director + 0x270 + slot * sizeof(uintptr_t),
                    &live, sizeof(live))
                || live != component.object)
                return false;
            if (component.object
                && (!read(component.object, &live, sizeof(live))
                    || live != component.vtable
                    || !read(component.vtable + 0x100,
                        &live, sizeof(live))
                    || live != component.writer))
                return false;
        }
        return true;
    }

    static inline bool RollbackBattleCameraGenerationMatches(
        const RollbackBattleCameraSnapshot& state) noexcept
    {
        return RollbackBattleCameraGenerationMatchesWith(
            state,
            [](uintptr_t address, void* destination,
               size_t bytes) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, bytes);
            });
    }

    template <typename ReadFn, typename WriteFn>
    static inline bool RestoreRollbackBattleCameraSnapshotWith(
        uintptr_t image_base,
        const RollbackBattleCameraSnapshot& state,
        ReadFn&& read, WriteFn&& write) noexcept
    {
        if (!image_base
            || state.director
                != image_base + kRollbackRvaLuxCameraDirector
            || !RollbackBattleCameraGenerationMatchesWith(state, read))
            return false;

        // Source/controller state first.
        if (!write(state.director + 0x2F0,
                state.transition_and_blend_controls.data(),
                state.transition_and_blend_controls.size())
            || !write(state.director + 0x350,
                state.post_blend_controls.data(),
                state.post_blend_controls.size()))
            return false;

        // Component semantic state and clocks second. Identity/vtable bytes
        // are never written.
        for (const auto& component : state.components)
        {
            if (!component.object) continue;
            size_t cursor = 0;
            for (const auto& field :
                 kRollbackBattleCameraComponentSemanticFields)
            {
                if (!write(component.object + field.offset,
                        component.common_semantic.data() + cursor,
                        field.bytes))
                    return false;
                cursor += field.bytes;
            }

            const uintptr_t zero_pointer = 0;
            switch (component.serialization)
            {
            case RollbackBattleCameraComponentSerialization::StateBuffer:
                if (!write(component.object + 0x1D0,
                        component.derived_semantic.data(), 0x140))
                    return false;
                break;
            case RollbackBattleCameraComponentSerialization::PlayerWatch:
            {
                if (!write(component.object + 0x1D0,
                        component.derived_semantic.data(), 0x14)
                    || !write(component.object + 0x1E8,
                        &zero_pointer, sizeof(zero_pointer)))
                    return false;
                const uintptr_t tracked = component.tracked_chara_slot < 0
                    ? 0
                    : state.chara_identity[
                        static_cast<size_t>(component.tracked_chara_slot)];
                if (!write(component.object + 0x1F0,
                        &tracked, sizeof(tracked)))
                    return false;
                break;
            }
            case RollbackBattleCameraComponentSerialization::Attention:
                if (!write(component.object + 0x1D0,
                        component.derived_semantic.data(), 0x0C)
                    || !write(component.object + 0x1E8,
                        component.derived_semantic.data() + 0x0C, 0x04)
                    || !write(component.object + 0x1E0,
                        &zero_pointer, sizeof(zero_pointer)))
                    return false;
                break;
            case RollbackBattleCameraComponentSerialization::Stay:
                if (!write(component.object + 0x1D0,
                        component.derived_semantic.data(), 0x0C)
                    || !write(component.object + 0x1E0,
                        &zero_pointer, sizeof(zero_pointer)))
                    return false;
                break;
            case RollbackBattleCameraComponentSerialization::Base:
                break;
            default:
                return false;
            }
        }

        // Published director output and gameplay-facing frame last.
        if (!write(state.director + 0x220,
                state.director_published_output.data(),
                state.director_published_output.size())
            || !write(image_base + kRollbackRvaLuxBattleCameraFrameVectors,
                state.frame_vectors.data(), state.frame_vectors.size())
            || !write(image_base + kRollbackRvaLuxBattleCameraYawTurns,
                &state.yaw_turns, sizeof(state.yaw_turns))
            || !write(image_base + kRollbackRvaLuxBattleCameraMode,
                &state.camera_mode, sizeof(state.camera_mode)))
            return false;

        RollbackBattleCameraSnapshot verification {};
        return CaptureRollbackBattleCameraSnapshotWith(
                image_base, read, verification)
            && verification.integrity_hash == state.integrity_hash;
    }

    static inline bool RestoreRollbackBattleCameraSnapshot(
        uintptr_t image_base,
        const RollbackBattleCameraSnapshot& state) noexcept
    {
        return RestoreRollbackBattleCameraSnapshotWith(
            image_base, state,
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
