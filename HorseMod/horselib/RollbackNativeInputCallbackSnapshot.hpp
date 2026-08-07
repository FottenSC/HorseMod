#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    // LuxBattleChara's input-callback target owns a small action-slot runtime
    // at +0x478..+0x4A7. The table and callback object are match identity;
    // the indices, action control block, and 0x20-byte subelement records are
    // mutable gameplay state and must follow Save/Load.
    static constexpr size_t kRollbackNativeInputCallbackElementStride = 0x20;
    static constexpr size_t kRollbackNativeInputCallbackMaxElements = 256;
    static constexpr int32_t kRollbackNativeInputCallbackMaxCapacity = 4096;
    static constexpr int32_t kRollbackNativeEventMaskCount = 2;
    static constexpr int32_t kRollbackNativeEventMaskMaxCapacity = 16;

    struct RollbackNativeInputCallbackSnapshot
    {
        uintptr_t object {0};
        uintptr_t slot_table {0};
        uintptr_t captured_array_owner {0};
        int32_t table_index {-1};
        int32_t slot_index {-1};
        uint8_t action_mode {0};
        int32_t mode_frame_counter {0};
        uint8_t pending_window_gate {0};
        uint8_t special_slot_dirty {0};
        uint8_t event_window_gate {0};
        uint16_t mode_state_reserved {0};
        int32_t completion_delay_frames {0};
        int32_t element_count {0};
        int32_t captured_capacity {0};
        uintptr_t event_mask_owner {0};
        int32_t event_mask_count {0};
        int32_t event_mask_capacity {0};
        std::array<uint64_t, kRollbackNativeEventMaskCount> event_masks {};
        std::array<uint8_t,
            kRollbackNativeInputCallbackMaxElements
                * kRollbackNativeInputCallbackElementStride> elements {};
        uint64_t semantic_hash {0};
        uint64_t integrity_hash {0};
        bool valid {false};

        void clear() noexcept { *this = {}; }
    };

    static inline size_t RollbackNativeInputCallbackElementBytes(
        const RollbackNativeInputCallbackSnapshot& state) noexcept
    {
        return state.element_count < 0
            ? 0
            : static_cast<size_t>(state.element_count)
                * kRollbackNativeInputCallbackElementStride;
    }

    static inline uint64_t HashRollbackNativeInputCallbackSemantic(
        const RollbackNativeInputCallbackSnapshot& state) noexcept
    {
        if (!state.valid || state.element_count < 0
            || state.element_count
                > static_cast<int32_t>(
                    kRollbackNativeInputCallbackMaxElements))
            return 0;
        RollbackHash hash {};
        hash.add_scalar(state.table_index);
        hash.add_scalar(state.slot_index);
        hash.add_scalar(state.action_mode);
        hash.add_scalar(state.mode_frame_counter);
        hash.add_scalar(state.pending_window_gate);
        hash.add_scalar(state.special_slot_dirty);
        hash.add_scalar(state.event_window_gate);
        hash.add_scalar(state.mode_state_reserved);
        hash.add_scalar(state.completion_delay_frames);
        hash.add_scalar(state.element_count);
        hash.add_bytes(state.event_masks.data(),
            sizeof(state.event_masks));
        const size_t bytes = RollbackNativeInputCallbackElementBytes(state);
        if (bytes != 0) hash.add_bytes(state.elements.data(), bytes);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackNativeInputCallbackIntegrity(
        const RollbackNativeInputCallbackSnapshot& state) noexcept
    {
        if (!state.valid) return 0;
        const uint64_t semantic =
            HashRollbackNativeInputCallbackSemantic(state);
        if (semantic == 0) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.object);
        hash.add_scalar(state.slot_table);
        hash.add_scalar(state.captured_array_owner);
        hash.add_scalar(state.captured_capacity);
        hash.add_scalar(state.event_mask_owner);
        hash.add_scalar(state.event_mask_count);
        hash.add_scalar(state.event_mask_capacity);
        hash.add_scalar(semantic);
        return hash.value ? hash.value : 1;
    }

    static inline bool ValidateRollbackNativeInputCallbackSnapshot(
        const RollbackNativeInputCallbackSnapshot& state) noexcept
    {
        if (!state.valid || state.object == 0 || state.slot_table == 0
            || state.table_index < -1 || state.slot_index < -1
            || state.action_mode > 9
            || state.element_count < 0
            || state.element_count
                > static_cast<int32_t>(
                    kRollbackNativeInputCallbackMaxElements)
            || state.captured_capacity < state.element_count
            || state.captured_capacity
                > kRollbackNativeInputCallbackMaxCapacity
            || (state.captured_capacity != 0
                && state.captured_array_owner == 0)
            || state.event_mask_owner == 0
            || state.event_mask_count != kRollbackNativeEventMaskCount
            || state.event_mask_capacity < kRollbackNativeEventMaskCount
            || state.event_mask_capacity
                > kRollbackNativeEventMaskMaxCapacity)
            return false;
        return state.semantic_hash
                == HashRollbackNativeInputCallbackSemantic(state)
            && state.integrity_hash
                == HashRollbackNativeInputCallbackIntegrity(state);
    }

    template <typename ReadFn>
    static inline bool CaptureRollbackNativeInputCallbackSnapshotWith(
        uintptr_t object, ReadFn&& read,
        RollbackNativeInputCallbackSnapshot& out) noexcept
    {
        out.clear();
        if (object == 0) return false;
        out.object = object;
        if (!read(object + 0x470, &out.slot_table,
                sizeof(out.slot_table))
            || !read(object + 0x478, &out.table_index,
                sizeof(out.table_index))
            || !read(object + 0x47C, &out.slot_index,
                sizeof(out.slot_index))
            || !read(object + 0x480, &out.action_mode,
                sizeof(out.action_mode))
            || !read(object + 0x484, &out.mode_frame_counter,
                sizeof(out.mode_frame_counter))
            || !read(object + 0x488, &out.pending_window_gate,
                sizeof(out.pending_window_gate))
            || !read(object + 0x490, &out.special_slot_dirty,
                sizeof(out.special_slot_dirty))
            || !read(object + 0x491, &out.event_window_gate,
                sizeof(out.event_window_gate))
            || !read(object + 0x492, &out.mode_state_reserved,
                sizeof(out.mode_state_reserved))
            || !read(object + 0x494, &out.completion_delay_frames,
                sizeof(out.completion_delay_frames))
            || !read(object + 0x498, &out.captured_array_owner,
                sizeof(out.captured_array_owner))
            || !read(object + 0x4A0, &out.element_count,
                sizeof(out.element_count))
            || !read(object + 0x4A4, &out.captured_capacity,
                sizeof(out.captured_capacity))
            || !read(object + 0x4A8, &out.event_mask_owner,
                sizeof(out.event_mask_owner))
            || !read(object + 0x4B0, &out.event_mask_count,
                sizeof(out.event_mask_count))
            || !read(object + 0x4B4, &out.event_mask_capacity,
                sizeof(out.event_mask_capacity)))
            return false;
        if (out.slot_table == 0 || out.table_index < -1
            || out.slot_index < -1 || out.action_mode > 9
            || out.element_count < 0
            || out.element_count
                > static_cast<int32_t>(
                    kRollbackNativeInputCallbackMaxElements)
            || out.captured_capacity < out.element_count
            || out.captured_capacity
                > kRollbackNativeInputCallbackMaxCapacity
            || (out.captured_capacity != 0
                && out.captured_array_owner == 0)
            || out.event_mask_owner == 0
            || out.event_mask_count != kRollbackNativeEventMaskCount
            || out.event_mask_capacity < kRollbackNativeEventMaskCount
            || out.event_mask_capacity
                > kRollbackNativeEventMaskMaxCapacity)
            return false;
        const size_t bytes = RollbackNativeInputCallbackElementBytes(out);
        if (bytes != 0
            && !read(out.captured_array_owner, out.elements.data(), bytes))
            return false;
        if (!read(out.event_mask_owner, out.event_masks.data(),
                sizeof(out.event_masks)))
            return false;
        out.valid = true;
        out.semantic_hash = HashRollbackNativeInputCallbackSemantic(out);
        out.integrity_hash = HashRollbackNativeInputCallbackIntegrity(out);
        return ValidateRollbackNativeInputCallbackSnapshot(out);
    }

    static inline bool CaptureRollbackNativeInputCallbackSnapshot(
        uintptr_t object,
        RollbackNativeInputCallbackSnapshot& out) noexcept
    {
        return CaptureRollbackNativeInputCallbackSnapshotWith(
            object,
            [](uintptr_t address, void* destination, size_t size) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, size);
            },
            out);
    }

    template <typename ReadFn, typename WriteFn>
    static inline bool RestoreRollbackNativeInputCallbackSnapshotWith(
        const RollbackNativeInputCallbackSnapshot& state,
        ReadFn&& read, WriteFn&& write) noexcept
    {
        if (!ValidateRollbackNativeInputCallbackSnapshot(state)) return false;
        uintptr_t live_slot_table = 0;
        uintptr_t live_array_owner = 0;
        uintptr_t live_event_mask_owner = 0;
        int32_t live_event_mask_count = 0;
        int32_t live_event_mask_capacity = 0;
        int32_t live_capacity = 0;
        if (!read(state.object + 0x470, &live_slot_table,
                sizeof(live_slot_table))
            || live_slot_table != state.slot_table
            || !read(state.object + 0x498, &live_array_owner,
                sizeof(live_array_owner))
            || !read(state.object + 0x4A4, &live_capacity,
                sizeof(live_capacity))
            || live_capacity < state.element_count
            || live_capacity > kRollbackNativeInputCallbackMaxCapacity
            || (state.element_count != 0 && live_array_owner == 0))
            return false;
        if (!read(state.object + 0x4A8, &live_event_mask_owner,
                sizeof(live_event_mask_owner))
            || live_event_mask_owner != state.event_mask_owner
            || !read(state.object + 0x4B0, &live_event_mask_count,
                sizeof(live_event_mask_count))
            || live_event_mask_count != kRollbackNativeEventMaskCount
            || !read(state.object + 0x4B4, &live_event_mask_capacity,
                sizeof(live_event_mask_capacity))
            || live_event_mask_capacity < kRollbackNativeEventMaskCount
            || live_event_mask_capacity
                > kRollbackNativeEventMaskMaxCapacity)
            return false;

        const size_t bytes = RollbackNativeInputCallbackElementBytes(state);
        if (!write(live_event_mask_owner, state.event_masks.data(),
                sizeof(state.event_masks))
            || (bytes != 0
                && !write(live_array_owner, state.elements.data(), bytes))
            || !write(state.object + 0x478, &state.table_index,
                sizeof(state.table_index))
            || !write(state.object + 0x47C, &state.slot_index,
                sizeof(state.slot_index))
            || !write(state.object + 0x480, &state.action_mode,
                sizeof(state.action_mode))
            || !write(state.object + 0x484, &state.mode_frame_counter,
                sizeof(state.mode_frame_counter))
            || !write(state.object + 0x488, &state.pending_window_gate,
                sizeof(state.pending_window_gate))
            || !write(state.object + 0x490, &state.special_slot_dirty,
                sizeof(state.special_slot_dirty))
            || !write(state.object + 0x491, &state.event_window_gate,
                sizeof(state.event_window_gate))
            || !write(state.object + 0x492, &state.mode_state_reserved,
                sizeof(state.mode_state_reserved))
            || !write(state.object + 0x494,
                &state.completion_delay_frames,
                sizeof(state.completion_delay_frames))
            || !write(state.object + 0x4A0, &state.element_count,
                sizeof(state.element_count)))
            return false;

        RollbackNativeInputCallbackSnapshot verification {};
        return CaptureRollbackNativeInputCallbackSnapshotWith(
                state.object, read, verification)
            && verification.semantic_hash == state.semantic_hash;
    }

    static inline bool RestoreRollbackNativeInputCallbackSnapshot(
        const RollbackNativeInputCallbackSnapshot& state) noexcept
    {
        return RestoreRollbackNativeInputCallbackSnapshotWith(
            state,
            [](uintptr_t address, void* destination, size_t size) noexcept {
                return SafeReadBytes(
                    reinterpret_cast<const void*>(address),
                    destination, size);
            },
            [](uintptr_t address, const void* source, size_t size) noexcept {
                return SafeWriteBytes(
                    reinterpret_cast<void*>(address), source, size);
            });
    }
}
