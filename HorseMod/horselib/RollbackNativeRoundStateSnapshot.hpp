// Rollback-owned snapshot of SC6's native BattleManager round-state queue.
//
// ProcessRoundStateSequence drains this TArray after PerFrameTick. A
// speculative rollback Advance can therefore enqueue terminal state 3 before
// the outer native consumer runs. The queue is gameplay control state: Load
// must restore it along with the fighter snapshot so a discarded terminal
// prediction cannot survive into the corrected timeline.
#pragma once

#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr int32_t kRollbackNativeRoundStateQueueCapacity = 32;

    struct RollbackNativeRoundStateSnapshot
    {
        uint8_t current_state {0xFF};
        int32_t count {-1};
        std::array<uint8_t, kRollbackNativeRoundStateQueueCapacity> entries {};
        uint64_t hash {0};
        bool valid {false};

        void clear() noexcept { *this = {}; }
    };

    static inline uint64_t HashRollbackNativeRoundStateSnapshot(
        const RollbackNativeRoundStateSnapshot& snapshot) noexcept
    {
        if (!snapshot.valid || snapshot.count < 0
            || snapshot.count > kRollbackNativeRoundStateQueueCapacity)
            return 0;
        RollbackHash hash {};
        hash.add_scalar(snapshot.current_state);
        hash.add_scalar(snapshot.count);
        if (snapshot.count != 0)
            hash.add_bytes(snapshot.entries.data(),
                static_cast<size_t>(snapshot.count));
        return hash.value ? hash.value : 1;
    }

    static inline bool CaptureRollbackNativeRoundStateValues(
        uint8_t current_state,
        const uint8_t* entries,
        int32_t count,
        int32_t capacity,
        RollbackNativeRoundStateSnapshot& out) noexcept
    {
        out.clear();
        if (count < 0 || capacity < 0 || count > capacity
            || count > kRollbackNativeRoundStateQueueCapacity
            || (count != 0 && entries == nullptr))
            return false;
        out.current_state = current_state;
        out.count = count;
        if (count != 0)
            std::memcpy(out.entries.data(), entries,
                static_cast<size_t>(count));
        out.valid = true;
        out.hash = HashRollbackNativeRoundStateSnapshot(out);
        return out.hash != 0;
    }

    static inline bool CaptureRollbackNativeRoundStateSnapshot(
        uintptr_t battle_manager,
        RollbackNativeRoundStateSnapshot& out) noexcept
    {
        out.clear();
        if (battle_manager == 0) return false;
        void* queue_data = nullptr;
        int32_t count = -1;
        int32_t capacity = -1;
        uint8_t current_state = 0xFF;
        if (!SafeReadPtr(reinterpret_cast<const void*>(
                battle_manager + 0x1470), &queue_data)
            || !SafeReadBytes(reinterpret_cast<const void*>(
                    battle_manager + 0x1478), &count, sizeof(count))
            || !SafeReadBytes(reinterpret_cast<const void*>(
                    battle_manager + 0x147C), &capacity, sizeof(capacity))
            || !SafeReadBytes(reinterpret_cast<const void*>(
                    battle_manager + 0x1480), &current_state,
                sizeof(current_state))
            || count < 0 || capacity < 0 || count > capacity
            || count > kRollbackNativeRoundStateQueueCapacity
            || (count != 0 && queue_data == nullptr))
            return false;

        std::array<uint8_t, kRollbackNativeRoundStateQueueCapacity> entries {};
        if (count != 0
            && !SafeReadBytes(queue_data, entries.data(),
                static_cast<size_t>(count)))
            return false;
        return CaptureRollbackNativeRoundStateValues(
            current_state, count == 0 ? nullptr : entries.data(),
            count, capacity, out);
    }

    static inline bool RestoreRollbackNativeRoundStateValues(
        const RollbackNativeRoundStateSnapshot& snapshot,
        uint8_t* live_entries,
        int32_t live_capacity,
        uint8_t& live_state,
        int32_t& live_count) noexcept
    {
        if (!snapshot.valid
            || snapshot.hash != HashRollbackNativeRoundStateSnapshot(snapshot)
            || live_capacity < 0 || snapshot.count < 0
            || snapshot.count > live_capacity
            || (snapshot.count != 0 && live_entries == nullptr))
            return false;
        // Model the production publication order: quiesce first, then data
        // and state, then publish the restored count.
        live_count = 0;
        if (snapshot.count != 0)
            std::memcpy(live_entries, snapshot.entries.data(),
                static_cast<size_t>(snapshot.count));
        live_state = snapshot.current_state;
        live_count = snapshot.count;
        return true;
    }

    static inline bool RestoreRollbackNativeRoundStateSnapshot(
        uintptr_t battle_manager,
        const RollbackNativeRoundStateSnapshot& snapshot) noexcept
    {
        if (battle_manager == 0 || !snapshot.valid
            || snapshot.hash != HashRollbackNativeRoundStateSnapshot(snapshot))
            return false;
        void* queue_data = nullptr;
        int32_t live_capacity = -1;
        if (!SafeReadPtr(reinterpret_cast<const void*>(
                battle_manager + 0x1470), &queue_data)
            || !SafeReadBytes(reinterpret_cast<const void*>(
                    battle_manager + 0x147C), &live_capacity,
                sizeof(live_capacity))
            || live_capacity < 0 || snapshot.count > live_capacity
            || (snapshot.count != 0 && queue_data == nullptr))
            return false;
        const int32_t quiesced_count = 0;
        if (!SafeWriteBytes(reinterpret_cast<void*>(
                battle_manager + 0x1478), &quiesced_count,
                sizeof(quiesced_count)))
            return false;
        if (snapshot.count != 0
            && !SafeWriteBytes(queue_data, snapshot.entries.data(),
                static_cast<size_t>(snapshot.count)))
            return false;
        if (!SafeWriteBytes(reinterpret_cast<void*>(
                battle_manager + 0x1480), &snapshot.current_state,
                sizeof(snapshot.current_state)))
            return false;
        return SafeWriteBytes(reinterpret_cast<void*>(
            battle_manager + 0x1478), &snapshot.count,
            sizeof(snapshot.count));
    }
}
