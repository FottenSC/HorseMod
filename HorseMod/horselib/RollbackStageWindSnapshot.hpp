// ============================================================================
// Horse::RollbackStageWindSnapshot
//
// Logical same-round snapshot for the stage-wind emitter scheduler and native
// wind graph. Ghidra proves the native tick mutates the explicit root fields
// captured below, node state from +0x30 onward, and emitter state from +0x50
// onward. Header ownership is reconstructed from the retained fixed addresses;
// opaque prefixes are never replayed. Peer hashes normalize code/pointers to
// RVAs and list order, while the local integrity hash retains exact addresses.
// ============================================================================

#pragma once

#include "RollbackFrameZeroRngBaseline.hpp"
#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uintptr_t kRollbackStageWindEmitterListRva = 0x470F1C0;
    static constexpr uintptr_t kRollbackStageWindEmitterCountRva = 0x470F1C8;
    static constexpr uintptr_t kRollbackStageWindRootRva = 0x470E038;
    static constexpr uintptr_t kRollbackStageWindCombinedRngStateRva =
        0x470E2B0;
    // Ghidra: IwWind_ScheduleNewWindEffect writes this with MOV dword ptr,
    // while both native wind samplers compare the same dword. Keep the full
    // 32-bit storage even though the accepted semantic domain is {0, 1}.
    static constexpr uintptr_t kRollbackStageWindOutputActiveRva = 0x4846450;
    static constexpr size_t kRollbackStageWindEmitterMaxCount = 32;
    static constexpr size_t kRollbackStageWindPoolMaxNodes = 32;
    static constexpr size_t kRollbackStageWindExternalMaxNodes = 16;
    static constexpr size_t kRollbackStageWindGraphMaxNodes =
        kRollbackStageWindPoolMaxNodes
        + kRollbackStageWindExternalMaxNodes;
    static_assert(kRollbackStageWindPoolMaxNodes <= 32);
    static_assert(kRollbackStageWindExternalMaxNodes < 32);
    static constexpr size_t kRollbackStageWindNodeMaxBytes = 0x200;
    static constexpr size_t kRollbackStageWindNodeMutableOffset = 0x30;
    static constexpr size_t kRollbackStageWindEmitterMutableOffset = 0x50;
    // Ghidra allocation/constructor evidence proves the native emitter is
    // 0xB0 bytes. Peer authority remains frozen at the previously verified
    // 0x50..0xA7 semantic image; the unknown final eight bytes are retained
    // only by byte-exact local rewind/integrity.
    static constexpr size_t kRollbackStageWindEmitterSemanticBytes = 0xA8;
    static constexpr size_t kRollbackStageWindEmitterBytes = 0xB0;
    // Frozen authority payload width. Do not expand the wire image for the
    // newly covered process-local tail.
    static constexpr size_t kRollbackStageWindEmitterMutableBytes =
        kRollbackStageWindEmitterSemanticBytes
        - kRollbackStageWindEmitterMutableOffset;
    static constexpr size_t kRollbackStageWindEmitterLocalMutableBytes =
        kRollbackStageWindEmitterBytes
        - kRollbackStageWindEmitterMutableOffset;

    static constexpr uintptr_t kRollbackStageWindPairParallelAllocReturnRva =
        0x334516;
    static constexpr uintptr_t kRollbackStageWindPairRingOutAllocReturnRva =
        0x3345B4;
    static constexpr uintptr_t kRollbackStageWindRingInAllocReturnRva =
        0x334A1E;
    static constexpr uintptr_t kRollbackStageWindShockWaveAllocReturnRva =
        0x2FC720;
    static constexpr uintptr_t kRollbackStageWindNodeFreeReturnRva =
        0x3340F9;

    // Process-lifetime original allocator routes are published before the
    // GMalloc dispatch and FMemory::Free are patched. FMemory::Malloc is a
    // seven-byte tail thunk into this dispatch and is deliberately not a
    // detour target. Free uses the initialized GMalloc object and its vtable
    // +0x20 function. Publishing malloc last is the acquire/release readiness
    // edge used by callbacks racing installation.
    class RollbackStageWindOriginalAllocatorRoutes
    {
    public:
        bool publish_once(
            uintptr_t malloc_owner,
            uintptr_t malloc_function,
            uintptr_t free_owner,
            uintptr_t free_function) noexcept
        {
            if (!malloc_owner || !malloc_function
                || !free_owner || !free_function)
                return false;
            while (m_publish_lock.test_and_set(std::memory_order_acquire)) {}
            const uintptr_t published_malloc = m_malloc_function.load(
                std::memory_order_relaxed);
            const uintptr_t published_malloc_owner = m_malloc_owner.load(
                std::memory_order_relaxed);
            const uintptr_t published_owner = m_free_owner.load(
                std::memory_order_relaxed);
            const uintptr_t published_free = m_free_function.load(
                std::memory_order_relaxed);
            bool ok = false;
            if (published_malloc_owner || published_malloc
                || published_owner || published_free)
            {
                ok = published_malloc_owner == malloc_owner
                    && published_malloc == malloc_function
                    && published_owner == free_owner
                    && published_free == free_function;
            }
            else
            {
                m_malloc_owner.store(
                    malloc_owner, std::memory_order_relaxed);
                m_free_owner.store(free_owner, std::memory_order_relaxed);
                m_free_function.store(
                    free_function, std::memory_order_relaxed);
                m_malloc_function.store(
                    malloc_function, std::memory_order_release);
                ok = true;
            }
            m_publish_lock.clear(std::memory_order_release);
            return ok;
        }

        bool ready() const noexcept
        {
            return m_malloc_function.load(std::memory_order_acquire) != 0;
        }

        uintptr_t malloc_function() const noexcept
        {
            return m_malloc_function.load(std::memory_order_acquire);
        }

        uintptr_t malloc_owner() const noexcept
        {
            if (!ready()) return 0;
            return m_malloc_owner.load(std::memory_order_relaxed);
        }

        uintptr_t free_owner() const noexcept
        {
            if (!ready()) return 0;
            return m_free_owner.load(std::memory_order_relaxed);
        }

        uintptr_t free_function() const noexcept
        {
            if (!ready()) return 0;
            return m_free_function.load(std::memory_order_relaxed);
        }

    private:
        std::atomic_flag m_publish_lock = ATOMIC_FLAG_INIT;
        std::atomic<uintptr_t> m_malloc_owner {0};
        std::atomic<uintptr_t> m_malloc_function {0};
        std::atomic<uintptr_t> m_free_owner {0};
        std::atomic<uintptr_t> m_free_function {0};
    };

    enum class RollbackStageWindAllocationDisposition : uint8_t
    {
        PassThrough = 0,
        FixedPool,
    };

    static inline RollbackStageWindAllocationDisposition
    ClassifyRollbackStageWindAllocation(
        uintptr_t caller_rva,
        size_t bytes,
        uint32_t alignment,
        bool match_owned,
        bool pool_sealed) noexcept
    {
        // FMemory::Malloc supplies alignment zero before tail-jumping to the
        // GMalloc dispatcher. Direct aligned-dispatch calls are unrelated.
        if (alignment != 0 || !match_owned || !pool_sealed)
            return RollbackStageWindAllocationDisposition::PassThrough;
        if ((caller_rva == kRollbackStageWindPairParallelAllocReturnRva
                || caller_rva
                    == kRollbackStageWindPairRingOutAllocReturnRva)
            && bytes == 0x130)
            return RollbackStageWindAllocationDisposition::FixedPool;
        if (caller_rva == kRollbackStageWindRingInAllocReturnRva
            && bytes == 0x1E0)
            return RollbackStageWindAllocationDisposition::FixedPool;
        if (caller_rva == kRollbackStageWindShockWaveAllocReturnRva
            && bytes == 0x180)
            return RollbackStageWindAllocationDisposition::FixedPool;
        return RollbackStageWindAllocationDisposition::PassThrough;
    }

    template<typename PoolAllocate, typename OriginalAllocate,
        typename FailClosed>
    static inline void* RouteRollbackStageWindAllocation(
        RollbackStageWindAllocationDisposition disposition,
        size_t bytes,
        uint32_t alignment,
        PoolAllocate&& pool_allocate,
        OriginalAllocate&& original_allocate,
        FailClosed&& fail_closed) noexcept
    {
        if (disposition
            != RollbackStageWindAllocationDisposition::FixedPool)
            return original_allocate(bytes, alignment);
        if (void* allocation = pool_allocate(bytes)) return allocation;
        fail_closed("stage-wind-allocation-pool-exhausted");
        return original_allocate(bytes, alignment);
    }

    enum class RollbackStageWindFreeDisposition : uint8_t
    {
        PassThrough = 0,
        Intercept,
        InterceptAndFail,
    };

    static inline bool ShouldRouteRollbackStageWindFree(
        const void* pointer) noexcept
    {
        // Preserve FMemory::Free's native null fast path before consulting the
        // replacement GMalloc route or any rollback ownership state.
        return pointer != nullptr;
    }

    template<typename ReleaseDeferred>
    static inline void ReleaseRollbackStageWindDeferredFreesIfNeeded(
        bool has_deferred_frees,
        ReleaseDeferred&& release_deferred) noexcept
    {
        if (has_deferred_frees) release_deferred();
    }

    static inline RollbackStageWindFreeDisposition
    ClassifyRollbackStageWindFree(
        bool exact_wind_free,
        bool tracked,
        bool match_owned,
        bool pool_pointer) noexcept
    {
        // Pool storage is never passed to the stock allocator, including
        // after logical ownership ends. Baseline external nodes are retained
        // only while rollback owns the match; once inactive, their eventual
        // native retirement belongs to the stock allocator again.
        if (pool_pointer)
            return RollbackStageWindFreeDisposition::Intercept;
        if (tracked && match_owned && exact_wind_free)
            return RollbackStageWindFreeDisposition::Intercept;
        if (tracked && match_owned)
            return RollbackStageWindFreeDisposition::InterceptAndFail;
        return RollbackStageWindFreeDisposition::PassThrough;
    }

    static constexpr uintptr_t kRollbackStageWindParallelVtableRva = 0x3E88C88;
    static constexpr uintptr_t kRollbackStageWindRingOutVtableRva = 0x3E88CB8;
    static constexpr uintptr_t kRollbackStageWindRingInVtableRva = 0x3E88CE8;
    static constexpr uintptr_t kRollbackStageWindShockWaveVtableRva = 0x3E88D18;

    struct RollbackStageWindPoolState
    {
        uint32_t allocated_mask {0};
        uint32_t external_freed_mask {0};
        std::array<uint16_t, kRollbackStageWindPoolMaxNodes> sizes {};
    };

    class RollbackStageWindAllocationPool
    {
    public:
        void reset() noexcept
        {
            m_allocated_mask = 0;
            m_external_freed_mask = 0;
            m_external_count = 0;
            m_sealed = false;
            m_sizes.fill(0);
            m_external.fill(0);
            for (auto& slot : m_slots) slot.fill(0);
        }

        // Begin a new logical rollback session without invalidating native
        // IwWind nodes that still point into this process-lifetime arena.
        // Stock's parallel-family node has no lifetime/active transition in
        // its update virtual, and the match-runtime teardown does not clear
        // the intrusive wind graph. Consequently an allocated pool slot can
        // legitimately remain reachable after logical rollback shutdown.
        //
        // External-node tracking is session-local: after deferred frees have
        // been released, the next authoritative graph capture must rediscover
        // the currently reachable stock allocations before sealing the pool.
        bool begin_session_preserving_live_nodes() noexcept
        {
            if (has_deferred_external_frees()) return false;
            m_external_count = 0;
            m_external.fill(0);
            m_sealed = false;
            return true;
        }

        bool track_initial_node(uintptr_t node) noexcept
        {
            if (!node || contains_pool_pointer(node)) return node != 0;
            if (external_index(node) >= 0) return true;
            if (m_sealed || m_external_count >= m_external.size())
                return false;
            m_external[m_external_count++] = node;
            return true;
        }

        void seal() noexcept { m_sealed = true; }
        bool sealed() const noexcept { return m_sealed; }

        void* allocate(size_t bytes) noexcept
        {
            if (!m_sealed || bytes == 0
                || bytes > kRollbackStageWindNodeMaxBytes)
                return nullptr;
            for (size_t index = 0; index < m_slots.size(); ++index)
            {
                const uint32_t bit = 1u << index;
                if ((m_allocated_mask & bit) != 0) continue;
                m_allocated_mask |= bit;
                m_sizes[index] = static_cast<uint16_t>(bytes);
                m_slots[index].fill(0);
                return m_slots[index].data();
            }
            return nullptr;
        }

        bool intercept_free(void* pointer) noexcept
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
            const int pool = pool_index(address);
            if (pool >= 0)
            {
                const uint32_t bit = 1u << static_cast<uint32_t>(pool);
                if ((m_allocated_mask & bit) == 0) return false;
                m_allocated_mask &= ~bit;
                m_sizes[static_cast<size_t>(pool)] = 0;
                return true;
            }
            const int external = external_index(address);
            if (external < 0) return false;
            const uint32_t bit = 1u << static_cast<uint32_t>(external);
            if ((m_external_freed_mask & bit) != 0) return false;
            m_external_freed_mask |= bit;
            return true;
        }

        bool owns_or_tracks(uintptr_t address) const noexcept
        {
            return pool_index(address) >= 0 || external_index(address) >= 0;
        }

        bool owns_pool_pointer(uintptr_t address) const noexcept
        {
            return pool_index(address) >= 0;
        }

        int32_t pool_slot_index(uintptr_t address) const noexcept
        {
            return pool_index(address);
        }

        uintptr_t pool_slot_address(uint32_t index) const noexcept
        {
            return index < m_slots.size()
                ? reinterpret_cast<uintptr_t>(m_slots[index].data()) : 0;
        }

        bool tracks_external(uintptr_t address) const noexcept
        {
            return external_index(address) >= 0;
        }

        RollbackStageWindPoolState capture_state() const noexcept
        {
            RollbackStageWindPoolState state {};
            state.allocated_mask = m_allocated_mask;
            state.external_freed_mask = m_external_freed_mask;
            state.sizes = m_sizes;
            return state;
        }

        bool can_restore_state(
            const RollbackStageWindPoolState& state) const noexcept
        {
            const uint32_t valid_mask =
                UINT32_MAX >> (32 - kRollbackStageWindPoolMaxNodes);
            const uint32_t external_mask = m_external_count == 0
                ? 0u : (1u << m_external_count) - 1u;
            if ((state.allocated_mask & ~valid_mask) != 0
                || (state.external_freed_mask & ~external_mask) != 0)
                return false;
            for (size_t index = 0; index < state.sizes.size(); ++index)
            {
                const bool allocated =
                    (state.allocated_mask & (1u << index)) != 0;
                if ((allocated && (state.sizes[index] == 0
                        || state.sizes[index]
                            > kRollbackStageWindNodeMaxBytes))
                    || (!allocated && state.sizes[index] != 0))
                    return false;
            }
            return true;
        }

        bool restore_state(const RollbackStageWindPoolState& state) noexcept
        {
            if (!can_restore_state(state)) return false;
            m_allocated_mask = state.allocated_mask;
            m_external_freed_mask = state.external_freed_mask;
            m_sizes = state.sizes;
            return true;
        }

        uint32_t allocated_count() const noexcept
        {
            uint32_t count = 0;
            uint32_t mask = m_allocated_mask;
            while (mask)
            {
                count += mask & 1u;
                mask >>= 1u;
            }
            return count;
        }

        uint32_t external_count() const noexcept { return m_external_count; }

        uint32_t allocated_mask() const noexcept { return m_allocated_mask; }

        uint32_t pool_pointer_mask(uintptr_t address) const noexcept
        {
            const int index = pool_index(address);
            return index < 0 ? 0u : 1u << static_cast<uint32_t>(index);
        }

        bool take_deferred_external_free(uintptr_t& address) noexcept
        {
            address = 0;
            for (uint32_t index = 0; index < m_external_count; ++index)
            {
                const uint32_t bit = 1u << index;
                if ((m_external_freed_mask & bit) == 0) continue;
                m_external_freed_mask &= ~bit;
                address = m_external[index];
                return address != 0;
            }
            return false;
        }

        bool has_deferred_external_frees() const noexcept
        {
            return m_external_freed_mask != 0;
        }

    private:
        struct alignas(16) Slot
        {
            std::array<uint8_t, kRollbackStageWindNodeMaxBytes> bytes {};

            void fill(uint8_t value) noexcept { bytes.fill(value); }
            uint8_t* data() noexcept { return bytes.data(); }
            const uint8_t* data() const noexcept { return bytes.data(); }
        };

        int pool_index(uintptr_t address) const noexcept
        {
            for (size_t index = 0; index < m_slots.size(); ++index)
            {
                if (reinterpret_cast<uintptr_t>(m_slots[index].data())
                    == address)
                    return static_cast<int>(index);
            }
            return -1;
        }

        bool contains_pool_pointer(uintptr_t address) const noexcept
        {
            return pool_index(address) >= 0;
        }

        int external_index(uintptr_t address) const noexcept
        {
            for (uint32_t index = 0; index < m_external_count; ++index)
                if (m_external[index] == address)
                    return static_cast<int>(index);
            return -1;
        }

        std::array<Slot, kRollbackStageWindPoolMaxNodes> m_slots {};
        std::array<uint16_t, kRollbackStageWindPoolMaxNodes> m_sizes {};
        std::array<uintptr_t, kRollbackStageWindExternalMaxNodes> m_external {};
        uint32_t m_allocated_mask {0};
        uint32_t m_external_freed_mask {0};
        uint32_t m_external_count {0};
        bool m_sealed {false};
    };

    struct RollbackStageWindGraphNode
    {
        uintptr_t address {0};
        uintptr_t vtable {0};
        uint32_t vtable_rva {0};
        uint32_t bytes {0};
        std::array<uint8_t, kRollbackStageWindNodeMaxBytes> data {};
    };

    static inline void StampRollbackStageWindGraphNodeHeader(
        RollbackStageWindGraphNode& node,
        uintptr_t root,
        uintptr_t previous,
        uintptr_t next) noexcept
    {
        std::memcpy(node.data.data(), &node.vtable, sizeof(node.vtable));
        std::memcpy(node.data.data() + 0x10, &next, sizeof(next));
        std::memcpy(node.data.data() + 0x18, &previous, sizeof(previous));
        std::memcpy(node.data.data() + 0x28, &root, sizeof(root));
    }

    struct RollbackStageWindRootState
    {
        float strength {0.0f};
        float scene_tick {0.0f};
        std::array<uintptr_t, 16> callbacks {};
        std::array<uint32_t, 16> callback_rvas {};
        uint32_t active_bank {0};
        int32_t pending_count {0};
        int32_t schedule_state {0};
        int32_t effect_pair_scheduled {0};
        std::array<float, 4> schedule_params {};
        std::array<float, 12> output_forces {};
    };

    struct RollbackStageWindGraphSnapshot
    {
        bool valid {false};
        uintptr_t root {0};
        uint32_t count {0};
        RollbackStageWindRootState root_state {};
        std::array<RollbackStageWindGraphNode,
            kRollbackStageWindGraphMaxNodes> nodes {};
        RollbackStageWindPoolState pool {};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};
    };

    struct RollbackStageWindEmitterRecord
    {
        uintptr_t list_node {0};
        uintptr_t emitter {0};
        std::array<
            uint8_t,
            kRollbackStageWindEmitterLocalMutableBytes> data {};
        int32_t active {0};
        int32_t remaining {0};
        float base_timer {0.0f};
        float reload_timer {0.0f};
        float jitter {0.0f};
    };

    struct RollbackStageWindSnapshot
    {
        uint32_t output_active {0};
        std::array<uint32_t, 6> combined_rng_state {};
        uintptr_t sentinel {0};
        uint32_t count {0};
        std::array<
            RollbackStageWindEmitterRecord,
            kRollbackStageWindEmitterMaxCount> emitters {};
        RollbackStageWindGraphSnapshot graph {};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};
    };

    struct RollbackStageWindSnapshotReport
    {
        bool ok {false};
        uint32_t count {0};
        const char* failure {"not-run"};
    };

    struct RollbackStageWindCanonicalBreakdown
    {
        uint32_t output_active {0};
        uint64_t combined_rng {0};
        uint64_t emitters {0};
        uint64_t root_scheduler {0};
        uint64_t root_derived_outputs {0};
        uint64_t graph_nodes {0};
        uint64_t graph {0};
        uint64_t combined {0};
    };

    struct RollbackStageWindNodeCanonicalBreakdown
    {
        bool valid {false};
        uint32_t vtable_rva {0};
        uint32_t bytes {0};
        uint64_t common {0};
        uint64_t body {0};
        uint64_t tail {0};
        uint64_t combined {0};
    };

    struct RollbackStageWindEmitterCheckpoint
    {
        int32_t active {0};
        int32_t remaining {0};
        uint32_t base_timer_bits {0};
        uint32_t reload_timer_bits {0};
        uint32_t jitter_bits {0};
    };

    struct RollbackStageWindNodeCheckpoint
    {
        bool valid {false};
        uint32_t vtable_rva {0};
        uint32_t bytes {0};
        uint32_t life_bits {0};
        uint32_t reset_life_bits {0};
        uint32_t frame_step_bits {0};
        int32_t repeat_remaining {0};
        int32_t oscillator_tick {0};
        uint32_t prepared {0};
        uint32_t active {0};
        uint64_t allocator_residue_hash {0};
    };

    enum class RollbackStageWindCapturePhase : uint8_t
    {
        Unknown = 0,
        ObserverCapture,
        ImmutableBaseline,
        GekkoSaveMinusOne,
        PreAdvanceZero,
        PostAdvanceZero,
        RearmCoordinate,
        LogicalFrame,
    };

    struct RollbackStageWindRngCallerCheckpoint
    {
        uint32_t rva {0};
        uint32_t count {0};
    };

    static constexpr size_t kRollbackStageWindRngCallerMaxCount = 64;

    struct RollbackStageWindCheckpoint
    {
        uint64_t round_generation {0};
        uint64_t round_epoch {0};
        int32_t native_coordinate {-1};
        int32_t logical_frame {-1};
        RollbackStageWindCapturePhase capture_phase {
            RollbackStageWindCapturePhase::Unknown};
        bool owned_simulation {false};
        uint8_t vm_freeze {0};
        uint32_t vm_frame_step_bits {0};
        uint32_t vm_scaled_alpha_bits {0};
        bool rng_valid {false};
        RollbackRngTuple rng {};
        uint64_t rng_total_calls {0};
        uint64_t rng_overflow_calls {0};
        uint32_t rng_caller_count {0};
        std::array<RollbackStageWindRngCallerCheckpoint,
            kRollbackStageWindRngCallerMaxCount> rng_callers {};
        uint32_t emitter_count {0};
        uint32_t node_count {0};
        std::array<RollbackStageWindEmitterCheckpoint,
            kRollbackStageWindEmitterMaxCount> emitters {};
        std::array<RollbackStageWindNodeCheckpoint,
            kRollbackStageWindGraphMaxNodes> nodes {};
    };

    template<typename T>
    static inline T ReadRollbackStageWindCheckpointValue(
        const uint8_t* data, size_t bytes, size_t offset) noexcept
    {
        T value {};
        if (data && offset <= bytes && sizeof(T) <= bytes - offset)
            std::memcpy(&value, data + offset, sizeof(value));
        return value;
    }

    struct RollbackStageWindSemanticRange
    {
        uint16_t offset {0};
        uint16_t bytes {0};
    };

    struct RollbackStageWindSemanticRanges
    {
        std::array<RollbackStageWindSemanticRange, 12> values {};
        uint32_t count {0};
    };

    // Peer-canonical state deliberately differs from the byte-exact local
    // rewind image. Native constructors initialize selected fields rather
    // than clearing the complete allocation. Ghidra and bilateral runtime
    // traces prove that +0x34..+0x3F and several family-specific gaps/W lanes
    // retain allocator or source residue. Ghidra also proves node
    // sampled/output forces at +0x40..+0x5F are derived pose presentation;
    // lifecycle resumes at +0x60. Keep every byte in integrity snapshots, but
    // expose only verified gameplay fields to peer hashing and authority.
    static inline RollbackStageWindSemanticRanges
    RollbackStageWindNodeSemanticRanges(
        uint32_t vtable_rva, uint32_t node_bytes) noexcept
    {
        RollbackStageWindSemanticRanges ranges {};
        ranges.values[ranges.count++] = {0x30, 0x04};
        ranges.values[ranges.count++] = {0x60, 0x10};
        if ((vtable_rva == kRollbackStageWindParallelVtableRva
                || vtable_rva == kRollbackStageWindRingOutVtableRva)
            && node_bytes == 0x130)
        {
            ranges.values[ranges.count++] = {0x70, 0x70};
            // UpdateParallelOscillation publishes yaw, pitch, and shaped
            // height at +0x120..+0x12B. Assembly has no write to the fourth
            // lane at +0x12C, and exact-artifact host/Sandboxie captures
            // observe unrelated allocator values there while XYZ and every
            // oscillator field agree. Preserve that lane only in the local
            // integrity image.
            ranges.values[ranges.count++] = {0x120, 0x0C};
            return ranges;
        }
        if (vtable_rva == kRollbackStageWindRingInVtableRva
            && node_bytes == 0x1E0)
        {
            // +0x70..+0xF3: initialized oscillator, angle, position,
            // velocity, and strength fields. +0xF4 is untouched allocator
            // residue.
            ranges.values[ranges.count++] = {0x70, 0x84};
            // +0xF8..+0x10B: initialized life/velocity and motion XYZ.
            // Motion W at +0x10C copies an uninitialized emitter W lane.
            ranges.values[ranges.count++] = {0xF8, 0x14};
            // +0x110..+0x11B: initialized velocity Y/Z and duration/flags.
            // +0x11C and +0x120..+0x12F are uninitialized/derived visual
            // state at the observer boundary.
            ranges.values[ranges.count++] = {0x110, 0x0C};
            // Frame step and repeat count determine life/RNG admission.
            ranges.values[ranges.count++] = {0x130, 0x04};
            ranges.values[ranges.count++] = {0x148, 0x04};
            // Path-scale XYZ is initialized. W at +0x15C copies the same
            // uninitialized source W lane. Matrices and travel output are
            // derived presentation state.
            ranges.values[ranges.count++] = {0x150, 0x0C};
            return ranges;
        }
        if (vtable_rva == kRollbackStageWindShockWaveVtableRva
            && node_bytes == 0x180)
        {
            // The constructor and initializer leave the motion prefix at
            // +0xE4..+0xEF untouched. Semantic position begins at +0xF0
            // and velocity ends at +0x10F. The prepare/update/sample vtable
            // never reads or writes +0x110..+0x11F. Update publishes only
            // currentAngles.xyz at +0x120..+0x128; the isolated fourth word
            // at +0x12C is also untouched allocator history. All three
            // residue regions remain byte-exact local rewind state only.
            ranges.values[ranges.count++] = {0x70, 0x74};
            ranges.values[ranges.count++] = {0xF0, 0x20};
            ranges.values[ranges.count++] = {0x120, 0x0C};
            ranges.values[ranges.count++] = {0x130, 0x50};
            return ranges;
        }
        return {};
    }

    static inline RollbackStageWindSemanticRanges
    RollbackStageWindEmitterSemanticRanges() noexcept
    {
        RollbackStageWindSemanticRanges ranges {};
        // +0x6C and +0x7C are the W lanes of basis/spawnPosition. The stock
        // emitter source leaves them uninitialized; all other bytes in the
        // mutable 0x50..0xA7 region are typed numeric scheduler/configuration
        // fields.
        ranges.values[ranges.count++] = {0x50, 0x1C};
        ranges.values[ranges.count++] = {0x70, 0x0C};
        ranges.values[ranges.count++] = {0x80, 0x28};
        return ranges;
    }

    static inline bool AddRollbackStageWindEmitterSemanticBytes(
        RollbackHash& hash,
        const RollbackStageWindEmitterRecord& emitter) noexcept
    {
        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindEmitterSemanticRanges();
        for (uint32_t index = 0; index < ranges.count; ++index)
        {
            const RollbackStageWindSemanticRange range =
                ranges.values[index];
            if (range.offset < kRollbackStageWindEmitterMutableOffset
                || range.offset > kRollbackStageWindEmitterSemanticBytes
                || range.bytes
                    > kRollbackStageWindEmitterSemanticBytes - range.offset)
                return false;
            const size_t data_offset =
                range.offset - kRollbackStageWindEmitterMutableOffset;
            hash.add_bytes(
                emitter.data.data() + data_offset, range.bytes);
        }
        return true;
    }

    static inline bool AddRollbackStageWindEmitterGameplayBytes(
        RollbackHash& hash,
        const RollbackStageWindEmitterRecord& emitter) noexcept
    {
        // Only scheduler state can decide whether and when this emitter
        // admits another native wind callback (and therefore shared-RNG
        // work). Basis vectors, spawn positions, and force configuration are
        // presentation inputs.
        hash.add_bytes(emitter.data.data(), 0x10);
        hash.add_bytes(emitter.data.data() + 0x54, 0x04);
        return true;
    }

    template<size_t SourceBytes, size_t DestinationBytes>
    static inline bool CopyRollbackStageWindEmitterSemanticBytes(
        const std::array<uint8_t, SourceBytes>& source,
        std::array<uint8_t, DestinationBytes>& destination) noexcept
    {
        if (source.size() < kRollbackStageWindEmitterMutableBytes
            || destination.size()
                < kRollbackStageWindEmitterMutableBytes)
            return false;
        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindEmitterSemanticRanges();
        for (uint32_t index = 0; index < ranges.count; ++index)
        {
            const RollbackStageWindSemanticRange range =
                ranges.values[index];
            if (range.offset < kRollbackStageWindEmitterMutableOffset
                || range.offset > kRollbackStageWindEmitterSemanticBytes
                || range.bytes
                    > kRollbackStageWindEmitterSemanticBytes - range.offset)
                return false;
            const size_t data_offset =
                range.offset - kRollbackStageWindEmitterMutableOffset;
            std::memcpy(destination.data() + data_offset,
                source.data() + data_offset, range.bytes);
        }
        return true;
    }

    static inline bool AddRollbackStageWindNodeSemanticBytes(
        RollbackHash& hash,
        const RollbackStageWindGraphNode& node) noexcept
    {
        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindNodeSemanticRanges(
                node.vtable_rva, node.bytes);
        if (ranges.count == 0) return false;
        for (uint32_t index = 0; index < ranges.count; ++index)
        {
            const RollbackStageWindSemanticRange range =
                ranges.values[index];
            if (range.bytes == 0 || range.offset > node.bytes
                || range.bytes > node.bytes - range.offset)
                return false;
            hash.add_bytes(node.data.data() + range.offset, range.bytes);
        }
        return true;
    }

    // Gameplay authority is narrower than the node's complete semantic
    // image. Ghidra proves oscillator phases, matrices, sampled forces, and
    // output forces feed only pose/skeleton presentation. Gameplay needs the
    // topology and the fields that admit future lifecycle/RNG work.
    static inline RollbackStageWindSemanticRanges
    RollbackStageWindNodeGameplayRanges(
        uint32_t vtable_rva, uint32_t node_bytes) noexcept
    {
        RollbackStageWindSemanticRanges ranges {};
        ranges.values[ranges.count++] = {0x30, 0x04}; // remaining life
        ranges.values[ranges.count++] = {0x60, 0x10}; // tick/prepared/active
        if (vtable_rva == kRollbackStageWindRingInVtableRva
            && node_bytes == 0x1E0)
        {
            ranges.values[ranges.count++] = {0x130, 0x04}; // frame step
            ranges.values[ranges.count++] = {0x148, 0x04}; // repeat count
        }
        else if (!((vtable_rva == kRollbackStageWindParallelVtableRva
                        || vtable_rva
                            == kRollbackStageWindRingOutVtableRva)
                       && node_bytes == 0x130)
            && !(vtable_rva == kRollbackStageWindShockWaveVtableRva
                && node_bytes == 0x180))
        {
            return {};
        }
        return ranges;
    }

    static inline bool AddRollbackStageWindNodeGameplayBytes(
        RollbackHash& hash,
        const RollbackStageWindGraphNode& node) noexcept
    {
        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindNodeGameplayRanges(
                node.vtable_rva, node.bytes);
        if (ranges.count == 0) return false;
        for (uint32_t index = 0; index < ranges.count; ++index)
        {
            const RollbackStageWindSemanticRange range =
                ranges.values[index];
            if (range.bytes == 0 || range.offset > node.bytes
                || range.bytes > node.bytes - range.offset)
                return false;
            hash.add_bytes(node.data.data() + range.offset, range.bytes);
        }
        return true;
    }

    static inline RollbackStageWindCheckpoint
    BuildRollbackStageWindCheckpoint(
        const RollbackStageWindSnapshot& snapshot,
        uint64_t round_generation,
        uint64_t round_epoch,
        int32_t native_coordinate,
        int32_t logical_frame,
        RollbackStageWindCapturePhase capture_phase,
        bool owned_simulation,
        uint8_t vm_freeze,
        uint32_t vm_frame_step_bits,
        uint32_t vm_scaled_alpha_bits,
        const RollbackRngTuple* rng = nullptr,
        const RollbackStageWindRngCallerCheckpoint* rng_callers = nullptr,
        uint32_t rng_caller_count = 0,
        uint64_t rng_total_calls = 0,
        uint64_t rng_overflow_calls = 0) noexcept
    {
        RollbackStageWindCheckpoint checkpoint {};
        checkpoint.round_generation = round_generation;
        checkpoint.round_epoch = round_epoch;
        checkpoint.native_coordinate = native_coordinate;
        checkpoint.logical_frame = logical_frame;
        checkpoint.capture_phase = capture_phase;
        checkpoint.owned_simulation = owned_simulation;
        checkpoint.vm_freeze = vm_freeze;
        checkpoint.vm_frame_step_bits = vm_frame_step_bits;
        checkpoint.vm_scaled_alpha_bits = vm_scaled_alpha_bits;
        if (rng && rng->valid())
        {
            checkpoint.rng_valid = true;
            checkpoint.rng = *rng;
        }
        checkpoint.rng_total_calls = rng_total_calls;
        checkpoint.rng_overflow_calls = rng_overflow_calls;
        checkpoint.rng_caller_count = (std::min)(
            rng_caller_count,
            static_cast<uint32_t>(checkpoint.rng_callers.size()));
        for (uint32_t index = 0;
             index < checkpoint.rng_caller_count; ++index)
        {
            checkpoint.rng_callers[index] = rng_callers[index];
        }
        checkpoint.emitter_count = (std::min)(
            snapshot.count,
            static_cast<uint32_t>(checkpoint.emitters.size()));
        for (uint32_t index = 0;
             index < checkpoint.emitter_count; ++index)
        {
            const auto& source = snapshot.emitters[index];
            auto& destination = checkpoint.emitters[index];
            destination.active = source.active;
            destination.remaining = source.remaining;
            std::memcpy(&destination.base_timer_bits,
                &source.base_timer, sizeof(destination.base_timer_bits));
            std::memcpy(&destination.reload_timer_bits,
                &source.reload_timer, sizeof(destination.reload_timer_bits));
            std::memcpy(&destination.jitter_bits,
                &source.jitter, sizeof(destination.jitter_bits));
        }
        checkpoint.node_count = snapshot.graph.valid
            ? (std::min)(snapshot.graph.count,
                static_cast<uint32_t>(checkpoint.nodes.size()))
            : 0;
        for (uint32_t index = 0;
             index < checkpoint.node_count; ++index)
        {
            const auto& source = snapshot.graph.nodes[index];
            auto& destination = checkpoint.nodes[index];
            destination.valid =
                RollbackStageWindNodeSemanticRanges(
                    source.vtable_rva, source.bytes).count != 0;
            destination.vtable_rva = source.vtable_rva;
            destination.bytes = source.bytes;
            destination.life_bits =
                ReadRollbackStageWindCheckpointValue<uint32_t>(
                    source.data.data(), source.bytes, 0x30);
            destination.oscillator_tick =
                ReadRollbackStageWindCheckpointValue<int32_t>(
                    source.data.data(), source.bytes, 0x60);
            destination.prepared =
                ReadRollbackStageWindCheckpointValue<uint32_t>(
                    source.data.data(), source.bytes, 0x68);
            destination.active =
                ReadRollbackStageWindCheckpointValue<uint32_t>(
                    source.data.data(), source.bytes, 0x6C);
            if (source.vtable_rva == kRollbackStageWindRingInVtableRva)
            {
                destination.reset_life_bits =
                    ReadRollbackStageWindCheckpointValue<uint32_t>(
                        source.data.data(), source.bytes, 0xF0);
                destination.frame_step_bits =
                    ReadRollbackStageWindCheckpointValue<uint32_t>(
                        source.data.data(), source.bytes, 0x130);
                destination.repeat_remaining =
                    ReadRollbackStageWindCheckpointValue<int32_t>(
                        source.data.data(), source.bytes, 0x148);
            }
            RollbackHash residue {};
            const RollbackStageWindSemanticRanges semantic_ranges =
                RollbackStageWindNodeSemanticRanges(
                    source.vtable_rva, source.bytes);
            for (uint32_t offset = kRollbackStageWindNodeMutableOffset;
                 offset < source.bytes; ++offset)
            {
                bool semantic = false;
                for (uint32_t range_index = 0;
                     range_index < semantic_ranges.count; ++range_index)
                {
                    const auto range =
                        semantic_ranges.values[range_index];
                    if (offset >= range.offset
                        && offset - range.offset < range.bytes)
                    {
                        semantic = true;
                        break;
                    }
                }
                if (!semantic)
                {
                    residue.add_scalar(offset);
                    residue.add_scalar(source.data[offset]);
                }
            }
            destination.allocator_residue_hash = residue.value;
        }
        return checkpoint;
    }

    static inline uint64_t HashRollbackStageWindGraphCanonical(
        const RollbackStageWindGraphSnapshot& graph) noexcept;

    static inline RollbackStageWindCanonicalBreakdown
    BuildRollbackStageWindCanonicalBreakdown(
        const RollbackStageWindSnapshot& snapshot) noexcept;

    static inline uint64_t HashRollbackStageWindCanonical(
        const RollbackStageWindSnapshot& snapshot) noexcept
    {
        if (snapshot.output_active > 1
            || snapshot.count > snapshot.emitters.size()) return 0;
        RollbackHash hash {};
        hash.add_scalar(snapshot.output_active);
        hash.add_bytes(snapshot.combined_rng_state.data(),
            sizeof(snapshot.combined_rng_state));
        hash.add_scalar(snapshot.count);
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            hash.add_scalar(index);
            const RollbackStageWindEmitterRecord& record =
                snapshot.emitters[index];
            if (!AddRollbackStageWindEmitterSemanticBytes(hash, record))
                return 0;
        }
        if (snapshot.graph.valid)
        {
            const uint64_t graph_hash =
                HashRollbackStageWindGraphCanonical(snapshot.graph);
            if (!graph_hash
                || graph_hash != snapshot.graph.canonical_hash)
                return 0;
            hash.add_scalar(graph_hash);
        }
        return hash.value;
    }

    static inline bool ReadRollbackStageWindPointer(
        uintptr_t address,
        uintptr_t& value) noexcept;

    static inline size_t RollbackStageWindNodeBytes(
        uintptr_t image_base,
        uintptr_t vtable) noexcept
    {
        if (!image_base || vtable < image_base) return 0;
        const uintptr_t rva = vtable - image_base;
        if (rva == kRollbackStageWindParallelVtableRva
            || rva == kRollbackStageWindRingOutVtableRva)
            return 0x130;
        if (rva == kRollbackStageWindRingInVtableRva) return 0x1E0;
        if (rva == kRollbackStageWindShockWaveVtableRva) return 0x180;
        return 0;
    }

    static inline uint64_t HashRollbackStageWindGraphIntegrity(
        const RollbackStageWindGraphSnapshot& graph) noexcept
    {
        if (!graph.valid || !graph.root
            || graph.count > graph.nodes.size()
            || graph.root_state.active_bank > 1
            || graph.root_state.pending_count < 0
            || graph.root_state.pending_count > 8)
            return 0;
        RollbackHash hash {};
        hash.add_scalar(graph.root);
        hash.add_scalar(graph.count);
        const RollbackStageWindRootState& root = graph.root_state;
        hash.add_bytes(&root.strength, sizeof(root.strength));
        hash.add_bytes(&root.scene_tick, sizeof(root.scene_tick));
        hash.add_bytes(root.callbacks.data(),
            root.callbacks.size() * sizeof(root.callbacks[0]));
        hash.add_bytes(root.callback_rvas.data(),
            root.callback_rvas.size() * sizeof(root.callback_rvas[0]));
        hash.add_scalar(root.active_bank);
        hash.add_scalar(root.pending_count);
        hash.add_scalar(root.schedule_state);
        hash.add_scalar(root.effect_pair_scheduled);
        hash.add_bytes(root.schedule_params.data(),
            root.schedule_params.size() * sizeof(root.schedule_params[0]));
        hash.add_bytes(root.output_forces.data(),
            root.output_forces.size() * sizeof(root.output_forces[0]));
        hash.add_scalar(graph.pool.allocated_mask);
        hash.add_scalar(graph.pool.external_freed_mask);
        hash.add_bytes(graph.pool.sizes.data(),
            graph.pool.sizes.size() * sizeof(graph.pool.sizes[0]));
        for (uint32_t index = 0; index < graph.count; ++index)
        {
            const RollbackStageWindGraphNode& node = graph.nodes[index];
            if (!node.address || !node.vtable
                || node.bytes <= kRollbackStageWindNodeMutableOffset
                || node.bytes > node.data.size())
                return 0;
            hash.add_scalar(index);
            hash.add_scalar(node.address);
            hash.add_scalar(node.vtable);
            hash.add_scalar(node.vtable_rva);
            hash.add_scalar(node.bytes);
            hash.add_bytes(node.data.data() + kRollbackStageWindNodeMutableOffset,
                node.bytes - kRollbackStageWindNodeMutableOffset);
        }
        return hash.value;
    }

    static inline uint64_t HashRollbackStageWindRootSchedulerCanonical(
        const RollbackStageWindGraphSnapshot& graph) noexcept
    {
        if (!graph.valid || !graph.root
            || graph.count > graph.nodes.size())
            return 0;
        const RollbackStageWindRootState& root = graph.root_state;
        if (root.active_bank > 1 || root.pending_count < 0
            || root.pending_count > 8)
            return 0;
        RollbackHash scheduler_hash {};
        scheduler_hash.add_bytes(&root.strength, sizeof(root.strength));
        scheduler_hash.add_scalar(root.pending_count);
        // Only callbacks in the active bank up to pending_count can execute.
        // The inactive and unused slots retain stale process-local values and
        // are deliberately excluded from the peer-canonical gameplay hash.
        for (int32_t index = 0; index < root.pending_count; ++index)
        {
            const size_t slot = static_cast<size_t>(root.active_bank) * 8u
                + static_cast<size_t>(index);
            if (root.callback_rvas[slot] == 0) return 0;
            scheduler_hash.add_scalar(root.callback_rvas[slot]);
        }
        scheduler_hash.add_scalar(root.schedule_state);
        scheduler_hash.add_scalar(root.effect_pair_scheduled);
        scheduler_hash.add_bytes(root.schedule_params.data(),
            root.schedule_params.size() * sizeof(root.schedule_params[0]));

        return scheduler_hash.value;
    }

    static inline uint64_t HashRollbackStageWindRootGameplayScheduler(
        const RollbackStageWindGraphSnapshot& graph) noexcept
    {
        if (!graph.valid || !graph.root
            || graph.count > graph.nodes.size())
            return 0;
        const RollbackStageWindRootState& root = graph.root_state;
        if (root.active_bank > 1 || root.pending_count < 0
            || root.pending_count > 8)
            return 0;
        RollbackHash scheduler_hash {};
        scheduler_hash.add_scalar(root.pending_count);
        for (int32_t index = 0; index < root.pending_count; ++index)
        {
            const size_t slot = static_cast<size_t>(root.active_bank) * 8u
                + static_cast<size_t>(index);
            if (root.callback_rvas[slot] == 0) return 0;
            scheduler_hash.add_scalar(root.callback_rvas[slot]);
        }
        scheduler_hash.add_scalar(root.schedule_state);
        scheduler_hash.add_scalar(root.effect_pair_scheduled);
        return scheduler_hash.value ? scheduler_hash.value : 1;
    }

    static inline uint64_t HashRollbackStageWindGameplay(
        const RollbackStageWindSnapshot& snapshot) noexcept
    {
        if (snapshot.count > snapshot.emitters.size()) return 0;
        RollbackHash hash {};
        hash.add_scalar(snapshot.count);
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            hash.add_scalar(index);
            if (!AddRollbackStageWindEmitterGameplayBytes(
                    hash, snapshot.emitters[index]))
                return 0;
        }
        hash.add_scalar(snapshot.graph.valid);
        if (snapshot.graph.valid)
        {
            const auto& graph = snapshot.graph;
            if (!graph.root || graph.count > graph.nodes.size())
                return 0;
            const uint64_t scheduler =
                HashRollbackStageWindRootGameplayScheduler(graph);
            if (!scheduler) return 0;
            hash.add_scalar(scheduler);
            hash.add_scalar(graph.count);
            for (uint32_t index = 0; index < graph.count; ++index)
            {
                const auto& node = graph.nodes[index];
                if (!node.address || node.vtable_rva == 0
                    || node.bytes > node.data.size())
                    return 0;
                hash.add_scalar(index);
                hash.add_scalar(node.vtable_rva);
                hash.add_scalar(node.bytes);
                if (!AddRollbackStageWindNodeGameplayBytes(hash, node))
                    return 0;
            }
        }
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackStageWindGraphNodesCanonical(
        const RollbackStageWindGraphSnapshot& graph) noexcept
    {
        if (!graph.valid || !graph.root
            || graph.count > graph.nodes.size())
            return 0;
        RollbackHash node_hash {};
        node_hash.add_scalar(graph.count);
        for (uint32_t index = 0; index < graph.count; ++index)
        {
            const RollbackStageWindGraphNode& node = graph.nodes[index];
            if (!node.address || node.vtable_rva == 0
                || node.bytes <= kRollbackStageWindNodeMutableOffset
                || node.bytes > node.data.size())
                return 0;
            node_hash.add_scalar(index);
            node_hash.add_scalar(node.vtable_rva);
            node_hash.add_scalar(node.bytes);
            if (!AddRollbackStageWindNodeSemanticBytes(node_hash, node))
                return 0;
        }
        return node_hash.value;
    }

    static inline uint64_t HashRollbackStageWindRootDerivedOutputs(
        const RollbackStageWindGraphSnapshot& graph) noexcept
    {
        if (!graph.valid || !graph.root) return 0;
        const RollbackStageWindRootState& root = graph.root_state;
        RollbackHash hash {};
        hash.add_bytes(root.output_forces.data(),
            root.output_forces.size() * sizeof(root.output_forces[0]));
        return hash.value;
    }

    static inline uint64_t HashRollbackStageWindPresentation(
        const RollbackStageWindSnapshot& snapshot) noexcept
    {
        if (snapshot.output_active > 1
            || snapshot.count > snapshot.emitters.size())
            return 0;
        RollbackHash hash {};
        hash.add_scalar(snapshot.output_active);
        hash.add_bytes(snapshot.combined_rng_state.data(),
            sizeof(snapshot.combined_rng_state));
        hash.add_scalar(snapshot.count);
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            hash.add_scalar(index);
            if (!AddRollbackStageWindEmitterSemanticBytes(
                    hash, snapshot.emitters[index]))
                return 0;
        }
        if (snapshot.graph.valid)
        {
            const auto& graph = snapshot.graph;
            if (!graph.root || graph.count > graph.nodes.size())
                return 0;
            hash.add_bytes(&graph.root_state.strength,
                sizeof(graph.root_state.strength));
            hash.add_bytes(&graph.root_state.scene_tick,
                sizeof(graph.root_state.scene_tick));
            hash.add_bytes(graph.root_state.output_forces.data(),
                graph.root_state.output_forces.size()
                    * sizeof(graph.root_state.output_forces[0]));
            hash.add_scalar(graph.count);
            for (uint32_t index = 0; index < graph.count; ++index)
            {
                const auto& node = graph.nodes[index];
                if (!node.address || node.vtable_rva == 0
                    || node.bytes < 0x60
                    || node.bytes > node.data.size())
                    return 0;
                hash.add_scalar(index);
                hash.add_scalar(node.vtable_rva);
                hash.add_scalar(node.bytes);
                hash.add_bytes(node.data.data() + 0x40, 0x20);
                if (!AddRollbackStageWindNodeSemanticBytes(hash, node))
                    return 0;
            }
        }
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackStageWindGraphCanonical(
        const RollbackStageWindGraphSnapshot& graph) noexcept
    {
        if (!graph.valid || !graph.root
            || graph.count > graph.nodes.size())
            return 0;
        const RollbackStageWindRootState& root = graph.root_state;
        if (root.active_bank > 1 || root.pending_count < 0
            || root.pending_count > 8)
            return 0;
        RollbackHash hash {};
        hash.add_scalar(graph.count);
        hash.add_bytes(&root.strength, sizeof(root.strength));
        hash.add_bytes(&root.scene_tick, sizeof(root.scene_tick));
        // active_bank is only a storage-bank selector. Canonical state is the
        // ordered callback sequence selected by it; hashing the raw 0/1 index
        // makes two empty banks or identical selected sequences disagree.
        hash.add_scalar(root.pending_count);
        for (int32_t index = 0; index < root.pending_count; ++index)
        {
            const size_t slot = static_cast<size_t>(root.active_bank) * 8u
                + static_cast<size_t>(index);
            if (root.callback_rvas[slot] == 0) return 0;
            hash.add_scalar(root.callback_rvas[slot]);
        }
        hash.add_scalar(root.schedule_state);
        hash.add_scalar(root.effect_pair_scheduled);
        hash.add_bytes(root.schedule_params.data(),
            root.schedule_params.size() * sizeof(root.schedule_params[0]));
        for (uint32_t index = 0; index < graph.count; ++index)
        {
            const RollbackStageWindGraphNode& node = graph.nodes[index];
            if (!node.address || node.vtable_rva == 0
                || node.bytes <= kRollbackStageWindNodeMutableOffset
                || node.bytes > node.data.size())
                return 0;
            hash.add_scalar(index);
            hash.add_scalar(node.vtable_rva);
            hash.add_scalar(node.bytes);
            if (!AddRollbackStageWindNodeSemanticBytes(hash, node))
                return 0;
        }
        return hash.value;
    }

    static inline RollbackStageWindNodeCanonicalBreakdown
    BuildRollbackStageWindNodeCanonicalBreakdown(
        const RollbackStageWindGraphNode& node) noexcept
    {
        RollbackStageWindNodeCanonicalBreakdown result {};
        if (!node.address || node.vtable_rva == 0
            || node.bytes <= kRollbackStageWindNodeMutableOffset
            || node.bytes > node.data.size())
            return result;
        result.vtable_rva = node.vtable_rva;
        result.bytes = node.bytes;

        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindNodeSemanticRanges(
                node.vtable_rva, node.bytes);
        if (ranges.count < 3) return {};
        RollbackHash common {};
        common.add_bytes(node.data.data() + ranges.values[0].offset,
            ranges.values[0].bytes);
        common.add_bytes(node.data.data() + ranges.values[1].offset,
            ranges.values[1].bytes);
        result.common = common.value;

        RollbackHash body {};
        RollbackHash tail {};
        bool has_body = false;
        bool has_tail = false;
        for (uint32_t index = 2; index < ranges.count; ++index)
        {
            const auto range = ranges.values[index];
            const uint32_t begin = range.offset;
            const uint32_t end = begin + range.bytes;
            const uint32_t body_begin = (std::max)(begin, 0x70u);
            const uint32_t body_end = (std::min)(end, 0x120u);
            if (body_begin < body_end)
            {
                body.add_bytes(node.data.data() + body_begin,
                    body_end - body_begin);
                has_body = true;
            }
            const uint32_t tail_begin = (std::max)(begin, 0x120u);
            if (tail_begin < end)
            {
                tail.add_bytes(node.data.data() + tail_begin,
                    end - tail_begin);
                has_tail = true;
            }
        }
        result.body = has_body ? body.value : 0;
        result.tail = has_tail ? tail.value : 0;
        RollbackHash combined {};
        combined.add_scalar(result.vtable_rva);
        combined.add_scalar(result.bytes);
        combined.add_scalar(result.common);
        combined.add_scalar(result.body);
        combined.add_scalar(result.tail);
        result.combined = combined.value;
        result.valid = true;
        return result;
    }

    static inline RollbackStageWindCanonicalBreakdown
    BuildRollbackStageWindCanonicalBreakdown(
        const RollbackStageWindSnapshot& snapshot) noexcept
    {
        RollbackStageWindCanonicalBreakdown result {};
        if (snapshot.output_active > 1
            || snapshot.count > snapshot.emitters.size()) return result;
        result.output_active = snapshot.output_active;

        RollbackHash rng_hash {};
        rng_hash.add_bytes(snapshot.combined_rng_state.data(),
            sizeof(snapshot.combined_rng_state));
        result.combined_rng = rng_hash.value;

        RollbackHash emitter_hash {};
        emitter_hash.add_scalar(snapshot.count);
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            emitter_hash.add_scalar(index);
            if (!AddRollbackStageWindEmitterSemanticBytes(
                    emitter_hash, snapshot.emitters[index]))
                return {};
        }
        result.emitters = emitter_hash.value;

        if (snapshot.graph.valid)
        {
            const RollbackStageWindGraphSnapshot& graph = snapshot.graph;
            const RollbackStageWindRootState& root = graph.root_state;
            if (!graph.root || graph.count > graph.nodes.size()
                || root.active_bank > 1 || root.pending_count < 0
                || root.pending_count > 8)
                return {};

            result.root_scheduler =
                HashRollbackStageWindRootSchedulerCanonical(graph);

            // Root and node force outputs are presentation-only pose inputs.
            // Keep their independent diagnostic hash, but do not fold it into
            // the peer gameplay graph.
            result.root_derived_outputs =
                HashRollbackStageWindRootDerivedOutputs(graph);

            result.graph_nodes =
                HashRollbackStageWindGraphNodesCanonical(graph);
            if (!result.root_scheduler || !result.root_derived_outputs
                || !result.graph_nodes)
                return {};
            result.graph = HashRollbackStageWindGraphCanonical(graph);
            if (!result.graph || result.graph != graph.canonical_hash)
                return {};
        }
        result.combined = HashRollbackStageWindCanonical(snapshot);
        return result;
    }

    template<typename T>
    static inline bool ReadRollbackStageWindValue(
        uintptr_t address, T& value) noexcept
    {
        return SafeReadBytes(reinterpret_cast<const void*>(address),
            &value, sizeof(value));
    }

    template<typename T>
    static inline bool WriteRollbackStageWindValue(
        uintptr_t address, const T& value) noexcept
    {
        return SafeWriteBytes(reinterpret_cast<void*>(address),
            &value, sizeof(value));
    }

    static inline bool CaptureRollbackStageWindRootState(
        uintptr_t image_base,
        uintptr_t root,
        RollbackStageWindRootState& state) noexcept
    {
        state = {};
        if (!ReadRollbackStageWindValue(root + 0x08, state.strength)
            || !ReadRollbackStageWindValue(root + 0x0C, state.scene_tick)
            || !SafeReadBytes(reinterpret_cast<const void*>(root + 0x18),
                state.callbacks.data(),
                state.callbacks.size() * sizeof(state.callbacks[0]))
            || !ReadRollbackStageWindValue(root + 0x98, state.active_bank)
            || !ReadRollbackStageWindValue(root + 0x9C, state.pending_count)
            || !ReadRollbackStageWindValue(root + 0xA0,
                state.schedule_state)
            || !ReadRollbackStageWindValue(root + 0xA4,
                state.effect_pair_scheduled)
            || !SafeReadBytes(reinterpret_cast<const void*>(root + 0xB0),
                state.schedule_params.data(),
                state.schedule_params.size()
                    * sizeof(state.schedule_params[0]))
            || !SafeReadBytes(reinterpret_cast<const void*>(root + 0xC0),
                state.output_forces.data(),
                state.output_forces.size() * sizeof(state.output_forces[0]))
            || state.active_bank > 1 || state.pending_count < 0
            || state.pending_count > 8)
            return false;
        for (size_t index = 0; index < state.callbacks.size(); ++index)
        {
            const uintptr_t callback = state.callbacks[index];
            if (!callback)
            {
                state.callback_rvas[index] = 0;
            }
            else if (callback >= image_base
                && callback - image_base <= 0xFFFFFFFFull)
            {
                state.callback_rvas[index] = static_cast<uint32_t>(
                    callback - image_base);
            }
            else
            {
                state.callback_rvas[index] = 0;
            }
        }
        const size_t first = static_cast<size_t>(state.active_bank) * 8u;
        for (int32_t index = 0; index < state.pending_count; ++index)
        {
            if (state.callback_rvas[first + static_cast<size_t>(index)] == 0)
                return false;
        }
        return true;
    }

    static inline bool RestoreRollbackStageWindRootState(
        uintptr_t root,
        const RollbackStageWindRootState& state) noexcept
    {
        return state.active_bank <= 1 && state.pending_count >= 0
            && state.pending_count <= 8
            && WriteRollbackStageWindValue(root + 0x08, state.strength)
            && WriteRollbackStageWindValue(root + 0x0C, state.scene_tick)
            && SafeWriteBytes(reinterpret_cast<void*>(root + 0x18),
                state.callbacks.data(),
                state.callbacks.size() * sizeof(state.callbacks[0]))
            && WriteRollbackStageWindValue(root + 0x98,
                state.active_bank)
            && WriteRollbackStageWindValue(root + 0x9C,
                state.pending_count)
            && WriteRollbackStageWindValue(root + 0xA0,
                state.schedule_state)
            && WriteRollbackStageWindValue(root + 0xA4,
                state.effect_pair_scheduled)
            && SafeWriteBytes(reinterpret_cast<void*>(root + 0xB0),
                state.schedule_params.data(),
                state.schedule_params.size()
                    * sizeof(state.schedule_params[0]))
            && SafeWriteBytes(reinterpret_cast<void*>(root + 0xC0),
                state.output_forces.data(),
                state.output_forces.size() * sizeof(state.output_forces[0]));
    }

    static inline RollbackStageWindSnapshotReport
    CaptureRollbackStageWindGraph(
        uintptr_t image_base,
        RollbackStageWindGraphSnapshot& graph,
        RollbackStageWindAllocationPool& pool) noexcept
    {
        RollbackStageWindSnapshotReport report {};
        graph = {};
        uintptr_t root = 0;
        if (!image_base
            || !ReadRollbackStageWindPointer(
                image_base + kRollbackStageWindRootRva, root)
            || !root
            || !CaptureRollbackStageWindRootState(
                image_base, root, graph.root_state))
        {
            report.failure = "stage-wind-root-unreadable";
            return report;
        }
        graph.root = root;
        uint32_t reachable_pool_mask = 0;
        uintptr_t node = 0;
        if (!ReadRollbackStageWindPointer(root, node))
        {
            report.failure = "stage-wind-graph-head-unreadable";
            return report;
        }
        while (node)
        {
            if (graph.count >= graph.nodes.size())
            {
                report.failure = "stage-wind-graph-capacity-exceeded";
                return report;
            }
            RollbackStageWindGraphNode& record = graph.nodes[graph.count];
            record.address = node;
            if (!ReadRollbackStageWindPointer(node, record.vtable))
            {
                report.failure = "stage-wind-node-vtable-unreadable";
                return report;
            }
            record.bytes = static_cast<uint32_t>(
                RollbackStageWindNodeBytes(image_base, record.vtable));
            if (record.bytes == 0 || record.bytes > record.data.size())
            {
                report.failure = "stage-wind-node-vtable-unknown";
                return report;
            }
            record.vtable_rva = static_cast<uint32_t>(
                record.vtable - image_base);
            if ((!pool.sealed() && !pool.track_initial_node(node))
                || (pool.sealed() && !pool.owns_or_tracks(node)))
            {
                report.failure = "stage-wind-node-ownership-unproven";
                return report;
            }
            if (!SafeReadBytes(reinterpret_cast<const void*>(node),
                    record.data.data(), record.bytes))
            {
                report.failure = "stage-wind-node-unreadable";
                return report;
            }
            reachable_pool_mask |= pool.pool_pointer_mask(node);
            uintptr_t next = 0;
            if (!ReadRollbackStageWindPointer(node + 0x10, next)
                || next == node)
            {
                report.failure = "stage-wind-node-link-invalid";
                return report;
            }
            node = next;
            ++graph.count;
        }
        if (!pool.sealed()) pool.seal();
        graph.pool = pool.capture_state();
        if (graph.pool.allocated_mask != reachable_pool_mask)
        {
            report.failure = "stage-wind-pool-reachability-mismatch";
            return report;
        }
        graph.valid = true;
        graph.canonical_hash = HashRollbackStageWindGraphCanonical(graph);
        graph.integrity_hash = HashRollbackStageWindGraphIntegrity(graph);
        if (!graph.canonical_hash || !graph.integrity_hash)
        {
            report.failure = "stage-wind-graph-hash-failed";
            return report;
        }
        report.ok = true;
        report.count = graph.count;
        report.failure = "ok";
        return report;
    }

    static inline RollbackStageWindSnapshotReport
    PreflightRollbackStageWindGraphRestore(
        uintptr_t image_base,
        const RollbackStageWindGraphSnapshot& graph,
        const RollbackStageWindAllocationPool& pool) noexcept
    {
        RollbackStageWindSnapshotReport report {};
        uintptr_t live_root = 0;
        if (!graph.valid
            || HashRollbackStageWindGraphIntegrity(graph)
                != graph.integrity_hash
            || HashRollbackStageWindGraphCanonical(graph)
                != graph.canonical_hash
            || !pool.sealed()
            || !pool.can_restore_state(graph.pool)
            || !ReadRollbackStageWindPointer(
                image_base + kRollbackStageWindRootRva, live_root)
            || live_root != graph.root)
        {
            report.failure = "stage-wind-graph-restore-preflight-invalid";
            return report;
        }
        for (uint32_t index = 0; index < graph.count; ++index)
        {
            const RollbackStageWindGraphNode& node = graph.nodes[index];
            if (!pool.owns_or_tracks(node.address)
                || node.vtable < image_base
                || node.vtable - image_base != node.vtable_rva
                || RollbackStageWindNodeBytes(image_base, node.vtable)
                    != node.bytes)
            {
                report.failure =
                    "stage-wind-node-restore-preflight-invalid";
                return report;
            }
        }
        report.ok = true;
        report.count = graph.count;
        report.failure = "ok";
        return report;
    }

    static inline RollbackStageWindSnapshotReport
    RestoreRollbackStageWindGraph(
        uintptr_t image_base,
        const RollbackStageWindGraphSnapshot& graph,
        RollbackStageWindAllocationPool& pool) noexcept
    {
        RollbackStageWindSnapshotReport report {};
        uintptr_t live_root = 0;
        if (!graph.valid
            || HashRollbackStageWindGraphIntegrity(graph)
                != graph.integrity_hash
            || HashRollbackStageWindGraphCanonical(graph)
                != graph.canonical_hash)
        {
            report.failure = "stage-wind-graph-integrity-failed";
            return report;
        }
        if (!pool.sealed()
            || !ReadRollbackStageWindPointer(
                image_base + kRollbackStageWindRootRva, live_root)
            || live_root != graph.root
            || !pool.restore_state(graph.pool))
        {
            report.failure = "stage-wind-graph-ownership-changed";
            return report;
        }
        for (uint32_t index = 0; index < graph.count; ++index)
        {
            const RollbackStageWindGraphNode& node = graph.nodes[index];
            if (!pool.owns_or_tracks(node.address)
                || node.vtable < image_base
                || RollbackStageWindNodeBytes(image_base, node.vtable)
                    != node.bytes
                || node.vtable - image_base != node.vtable_rva
                || !SafeWriteBytes(
                    reinterpret_cast<void*>(node.address
                        + kRollbackStageWindNodeMutableOffset),
                    node.data.data() + kRollbackStageWindNodeMutableOffset,
                    node.bytes - kRollbackStageWindNodeMutableOffset))
            {
                report.failure = "stage-wind-node-restore-failed";
                return report;
            }
            const uintptr_t previous = index == 0
                ? 0 : graph.nodes[index - 1].address;
            const uintptr_t next = index + 1 < graph.count
                ? graph.nodes[index + 1].address : 0;
            if (!WriteRollbackStageWindValue(node.address, node.vtable)
                || !WriteRollbackStageWindValue(
                    node.address + 0x10, next)
                || !WriteRollbackStageWindValue(
                    node.address + 0x18, previous)
                || !WriteRollbackStageWindValue(
                    node.address + 0x28, graph.root))
            {
                report.failure = "stage-wind-node-links-restore-failed";
                return report;
            }
        }
        const uintptr_t head = graph.count ? graph.nodes[0].address : 0;
        if (!WriteRollbackStageWindValue(graph.root, head)
            || !RestoreRollbackStageWindRootState(
                graph.root, graph.root_state))
        {
            report.failure = "stage-wind-root-restore-failed";
            return report;
        }
        RollbackStageWindGraphSnapshot verified {};
        const RollbackStageWindSnapshotReport verification =
            CaptureRollbackStageWindGraph(image_base, verified, pool);
        if (!verification.ok
            || verified.integrity_hash != graph.integrity_hash)
        {
            report.failure = verification.ok
                ? "stage-wind-graph-post-restore-mismatch"
                : verification.failure;
            return report;
        }
        report.ok = true;
        report.count = graph.count;
        report.failure = "ok";
        return report;
    }

    static inline uint64_t HashRollbackStageWindIntegrity(
        const RollbackStageWindSnapshot& snapshot) noexcept
    {
        if (snapshot.output_active > 1
            || snapshot.count > snapshot.emitters.size()) return 0;
        RollbackHash hash {};
        hash.add_scalar(snapshot.output_active);
        hash.add_bytes(snapshot.combined_rng_state.data(),
            sizeof(snapshot.combined_rng_state));
        hash.add_scalar(snapshot.sentinel);
        hash.add_scalar(snapshot.count);
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            const RollbackStageWindEmitterRecord& record =
                snapshot.emitters[index];
            hash.add_scalar(record.list_node);
            hash.add_scalar(record.emitter);
            hash.add_bytes(record.data.data(), record.data.size());
        }
        const uint64_t graph_hash = snapshot.graph.valid
            ? HashRollbackStageWindGraphIntegrity(snapshot.graph) : 0;
        if (snapshot.graph.valid
            && graph_hash != snapshot.graph.integrity_hash)
            return 0;
        hash.add_scalar(graph_hash);
        return hash.value;
    }

    static inline bool ReadRollbackStageWindPointer(
        uintptr_t address,
        uintptr_t& value) noexcept
    {
        value = 0;
        return address != 0
            && SafeReadBytes(
                reinterpret_cast<const void*>(address),
                &value,
                sizeof(value));
    }

    static inline bool ReadRollbackStageWindRecord(
        uintptr_t node,
        RollbackStageWindEmitterRecord& record) noexcept
    {
        record = {};
        record.list_node = node;
        if (!ReadRollbackStageWindPointer(node + 0x10, record.emitter)
            || record.emitter == 0)
            return false;
        if (!SafeReadBytes(reinterpret_cast<const void*>(record.emitter
                    + kRollbackStageWindEmitterMutableOffset),
                record.data.data(), record.data.size()))
            return false;
        std::memcpy(&record.active, record.data.data(),
            sizeof(record.active));
        std::memcpy(&record.remaining, record.data.data() + 0x04,
            sizeof(record.remaining));
        std::memcpy(&record.base_timer, record.data.data() + 0x08,
            sizeof(record.base_timer));
        std::memcpy(&record.reload_timer, record.data.data() + 0x0C,
            sizeof(record.reload_timer));
        std::memcpy(&record.jitter, record.data.data() + 0x54,
            sizeof(record.jitter));
        return true;
    }

    static inline RollbackStageWindSnapshotReport
    CaptureRollbackStageWindSnapshot(
        uintptr_t image_base,
        RollbackStageWindSnapshot& snapshot,
        RollbackStageWindAllocationPool* pool = nullptr) noexcept
    {
        RollbackStageWindSnapshotReport report {};
        snapshot = {};
        if (!image_base)
        {
            report.failure = "stage-wind-image-base-missing";
            return report;
        }
        if (!ReadRollbackStageWindValue(
                image_base + kRollbackStageWindOutputActiveRva,
                snapshot.output_active)
            || snapshot.output_active > 1
            || !SafeReadBytes(reinterpret_cast<const void*>(
                    image_base + kRollbackStageWindCombinedRngStateRva),
                snapshot.combined_rng_state.data(),
                sizeof(snapshot.combined_rng_state)))
        {
            report.failure = "stage-wind-control-state-invalid";
            return report;
        }
        uint64_t native_count = 0;
        if (!ReadRollbackStageWindPointer(
                image_base + kRollbackStageWindEmitterListRva,
                snapshot.sentinel)
            || snapshot.sentinel == 0
            || !SafeReadBytes(
                reinterpret_cast<const void*>(
                    image_base + kRollbackStageWindEmitterCountRva),
                &native_count,
                sizeof(native_count))
            || native_count > kRollbackStageWindEmitterMaxCount)
        {
            report.failure = "stage-wind-list-header-invalid";
            return report;
        }

        uintptr_t node = 0;
        if (!ReadRollbackStageWindPointer(snapshot.sentinel, node))
        {
            report.failure = "stage-wind-list-head-unreadable";
            return report;
        }
        while (node != snapshot.sentinel)
        {
            if (node == 0 || snapshot.count >= snapshot.emitters.size())
            {
                report.failure = "stage-wind-list-truncated";
                return report;
            }
            RollbackStageWindEmitterRecord& record =
                snapshot.emitters[snapshot.count];
            if (!ReadRollbackStageWindRecord(node, record))
            {
                report.failure = "stage-wind-emitter-unreadable";
                return report;
            }
            ++snapshot.count;
            uintptr_t next = 0;
            if (!ReadRollbackStageWindPointer(node, next) || next == node)
            {
                report.failure = "stage-wind-list-link-invalid";
                return report;
            }
            node = next;
        }
        if (snapshot.count != native_count)
        {
            report.failure = "stage-wind-native-count-mismatch";
            return report;
        }
        if (pool)
        {
            const RollbackStageWindSnapshotReport graph =
                CaptureRollbackStageWindGraph(
                    image_base, snapshot.graph, *pool);
            if (!graph.ok)
            {
                report.failure = graph.failure;
                return report;
            }
        }
        snapshot.canonical_hash = HashRollbackStageWindCanonical(snapshot);
        snapshot.integrity_hash = HashRollbackStageWindIntegrity(snapshot);
        if (snapshot.canonical_hash == 0 || snapshot.integrity_hash == 0)
        {
            report.failure = "stage-wind-hash-failed";
            return report;
        }
        report.ok = true;
        report.count = snapshot.count;
        report.failure = "ok";
        return report;
    }

    // Trace/checkpoint capture must never establish rollback ownership. In
    // particular, stock match startup can legitimately rebuild the wind graph
    // before the frame-zero observer boundary. Capturing through a copy keeps
    // all graph validation (including an already-sealed ownership contract)
    // while preventing diagnostics from tracking nodes or sealing the
    // authoritative production pool.
    static inline RollbackStageWindSnapshotReport
    CaptureRollbackStageWindSnapshotForDiagnostics(
        uintptr_t image_base,
        RollbackStageWindSnapshot& snapshot,
        RollbackStageWindAllocationPool& authoritative_pool) noexcept
    {
        // Once the production pool is sealed, graph capture is observational:
        // the sealed branch neither tracks initial nodes nor changes pool
        // state. It must use the authoritative pool itself because copying the
        // fixed arena changes every slot address, making live pooled nodes
        // appear unowned after the first rollback-time allocation.
        if (authoritative_pool.sealed())
        {
            return CaptureRollbackStageWindSnapshot(
                image_base, snapshot, &authoritative_pool);
        }
        RollbackStageWindAllocationPool diagnostic_pool =
            authoritative_pool;
        return CaptureRollbackStageWindSnapshot(
            image_base, snapshot, &diagnostic_pool);
    }

    static inline RollbackStageWindSnapshotReport
    PreflightRollbackStageWindSnapshotRestore(
        uintptr_t image_base,
        const RollbackStageWindSnapshot& snapshot,
        RollbackStageWindAllocationPool* pool = nullptr) noexcept
    {
        RollbackStageWindSnapshotReport report {};
        if (HashRollbackStageWindCanonical(snapshot)
                != snapshot.canonical_hash
            || HashRollbackStageWindIntegrity(snapshot)
                != snapshot.integrity_hash)
        {
            report.failure = "stage-wind-snapshot-integrity-failed";
            return report;
        }
        if (snapshot.graph.valid)
        {
            if (!pool)
            {
                report.failure = "stage-wind-graph-pool-missing";
                return report;
            }
            const RollbackStageWindSnapshotReport graph =
                PreflightRollbackStageWindGraphRestore(
                    image_base, snapshot.graph, *pool);
            if (!graph.ok)
            {
                report.failure = graph.failure;
                return report;
            }
        }

        // Resolve every live list/emitter identity before the first output,
        // RNG, graph, or emitter write. Inspect only ownership topology here:
        // mutable root/emitter values may legitimately be speculative and
        // are the values this transaction is about to replace.
        uintptr_t live_sentinel = 0;
        uint64_t live_count = 0;
        if (!ReadRollbackStageWindPointer(
                image_base + kRollbackStageWindEmitterListRva,
                live_sentinel)
            || !SafeReadBytes(reinterpret_cast<const void*>(
                    image_base + kRollbackStageWindEmitterCountRva),
                &live_count, sizeof(live_count))
            || live_sentinel != snapshot.sentinel
            || live_count != snapshot.count)
        {
            report.failure = "stage-wind-list-ownership-changed";
            return report;
        }
        uintptr_t live_node = 0;
        if (!ReadRollbackStageWindPointer(live_sentinel, live_node))
        {
            report.failure = "stage-wind-list-head-unreadable";
            return report;
        }
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            uintptr_t live_emitter = 0;
            uintptr_t next = 0;
            if (live_node != snapshot.emitters[index].list_node
                || !ReadRollbackStageWindPointer(
                    live_node + 0x10, live_emitter)
                || live_emitter != snapshot.emitters[index].emitter
                || !ReadRollbackStageWindPointer(live_node, next)
                || next == live_node)
            {
                report.failure = "stage-wind-emitter-ownership-changed";
                return report;
            }
            live_node = next;
        }
        if (live_node != live_sentinel)
        {
            report.failure = "stage-wind-list-ownership-changed";
            return report;
        }
        report.ok = true;
        report.count = snapshot.count;
        report.failure = "ok";
        return report;
    }

    static inline RollbackStageWindSnapshotReport
    RestoreRollbackStageWindSnapshot(
        uintptr_t image_base,
        const RollbackStageWindSnapshot& snapshot,
        RollbackStageWindAllocationPool* pool = nullptr) noexcept
    {
        RollbackStageWindSnapshotReport report {};
        const RollbackStageWindSnapshotReport preflight =
            PreflightRollbackStageWindSnapshotRestore(
                image_base, snapshot, pool);
        if (!preflight.ok)
        {
            report.failure = preflight.failure;
            return report;
        }

        if (!WriteRollbackStageWindValue(
                image_base + kRollbackStageWindOutputActiveRva,
                snapshot.output_active)
            || !SafeWriteBytes(reinterpret_cast<void*>(
                    image_base + kRollbackStageWindCombinedRngStateRva),
                snapshot.combined_rng_state.data(),
                sizeof(snapshot.combined_rng_state)))
        {
            report.failure = "stage-wind-control-state-write-failed";
            return report;
        }

        if (snapshot.graph.valid)
        {
            if (!pool)
            {
                report.failure = "stage-wind-graph-pool-missing";
                return report;
            }
            const RollbackStageWindSnapshotReport graph =
                RestoreRollbackStageWindGraph(
                    image_base, snapshot.graph, *pool);
            if (!graph.ok)
            {
                report.failure = graph.failure;
                return report;
            }
        }

        RollbackStageWindSnapshot live {};
        const RollbackStageWindSnapshotReport captured =
            CaptureRollbackStageWindSnapshot(image_base, live, pool);
        if (!captured.ok || live.count != snapshot.count
            || live.sentinel != snapshot.sentinel)
        {
            report.failure = captured.ok
                ? "stage-wind-list-ownership-changed" : captured.failure;
            return report;
        }
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            const RollbackStageWindEmitterRecord& expected =
                snapshot.emitters[index];
            const RollbackStageWindEmitterRecord& current =
                live.emitters[index];
            if (current.list_node != expected.list_node
                || current.emitter != expected.emitter)
            {
                report.failure = "stage-wind-emitter-ownership-changed";
                return report;
            }
            const bool wrote = SafeWriteBytes(
                reinterpret_cast<void*>(expected.emitter
                    + kRollbackStageWindEmitterMutableOffset),
                expected.data.data(), expected.data.size());
            if (!wrote)
            {
                report.failure = "stage-wind-emitter-write-failed";
                return report;
            }
        }

        RollbackStageWindSnapshot verified {};
        const RollbackStageWindSnapshotReport verification =
            CaptureRollbackStageWindSnapshot(image_base, verified, pool);
        if (!verification.ok
            || verified.integrity_hash != snapshot.integrity_hash
            || verified.canonical_hash != snapshot.canonical_hash)
        {
            report.failure = verification.ok
                ? "stage-wind-post-restore-mismatch" : verification.failure;
            return report;
        }
        report.ok = true;
        report.count = snapshot.count;
        report.failure = "ok";
        return report;
    }
}
