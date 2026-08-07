// ============================================================================
// Horse::RollbackHgCpuSnapshot
//
// Guarded access to SC6's native HgCpuDirect battle-state snapshot pair.
// This owns only the rollback-facing wrapper; ReplayScrub keeps its larger
// replay-seek-specific guards and historical restore logic.
// ============================================================================

#pragma once

#include "RollbackHgCpuPeerBreakdown.hpp"
#include "RollbackHgCpuCanonical.hpp"
#include "RollbackAiPaletteDiagnostics.hpp"
#include "RollbackCharaAnimationState.hpp"
#include "RollbackMotionBankCanonical.hpp"
#include "RollbackSecondaryEventStack.hpp"
#include "RollbackStateHash.hpp"

#if !defined(HORSE_ROLLBACK_HGCPU_PURE_STATE_ONLY)
#include "NativeBinding.hpp"
#include "ReplayDebugTrace.hpp"
#include "RollbackPreallocatedHgCpuCapacity.hpp"
#include "RollbackSnapshot.hpp"
#include "SafeMemoryRead.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horse
{
    static constexpr size_t kRollbackHgCpuSnapshotBytes = 0x28018;
    static constexpr size_t kRollbackTimerMaximumSnapshotNodes = 17;
    static constexpr uintptr_t kRollbackRVA_ExecMoveChangeAndPost = 0x3841E0;
    static constexpr uintptr_t kRollbackRVA_ExecFinalizeAndPost = 0x384540;
    static constexpr size_t kRollbackHgCpuFallbackPerCharaBytes = 0x1400C;
    static constexpr size_t kRollbackHgCpuAiResetSlotEnd = 0x79AC;
    static constexpr size_t kRollbackHgCpuHitAreaLocalStart = 0x79AC;
    static constexpr size_t kRollbackHgCpuHitAreaFixedBytes = 0x41C;
    static_assert(kRollbackHgCpuHitAreaLocalStart
            + kRollbackHgCpuHitAreaFixedBytes
        == kRollbackHgCpuCanonicalKHitNodeStreamStart);
    static constexpr size_t kRollbackHgCpuHitAreaRelocBytes = 0x90;
    static constexpr uintptr_t kRollbackKHitListControlStart = 0x44470;
    static constexpr size_t kRollbackKHitListControlBytes = 0x50;
    static constexpr size_t kRollbackKHitNodeImageBytes = 0xA0;
    static constexpr size_t kRollbackKHitMaximumSnapshotNodes = 3u * 256u;
    static constexpr uintptr_t kRollbackCharaHitAreaFixedStart = 0x44078;
    static constexpr uintptr_t kRollbackPrimaryMotionBankCharaOffset = 0x35A0;
    static constexpr uintptr_t kRollbackSecondaryMotionBankCharaOffset = 0x27760;
    static constexpr size_t kRollbackPrimaryMotionBankSnapshotLocal = 0x3590;
    static constexpr size_t kRollbackSecondaryMotionBankSnapshotLocal = 0x4DD0;
    static constexpr size_t kRollbackMotionBankControlBytes = 0x38;

    struct RollbackMotionBankSpec
    {
        uintptr_t chara_offset {0};
        size_t bytes {0};
    };

    static constexpr RollbackMotionBankSpec kRollbackMotionBankSpecs[] = {
        {kRollbackPrimaryMotionBankCharaOffset, kRollbackPrimaryMotionBankBytes},
        {kRollbackSecondaryMotionBankCharaOffset, kRollbackSecondaryMotionBankBytes},
    };

    static constexpr uintptr_t kRollbackMoveVMMotionTailCharaOffset = 0x96490;
    static constexpr size_t kRollbackMoveVMMotionTailBytes = 0x1000;
    // FLuxCharaVfxEffectAnchorBlock begins at chara+0x95FA0. Its nine
    // extra-bone matrices begin at anchor+0x5C0, hence motion-tail+0xD0.
    // SolveBonePose can copy these retained matrices directly into bones
    // 23..31 and KHit consumes those bones in the same tick.
    static constexpr size_t kRollbackMotionTailExtraBoneCacheOffset = 0xD0;
    static constexpr size_t kRollbackMotionTailExtraBoneCacheBytes = 0x240;
    // LuxMoveVM_ResetAISlotStateArray reaches the skeleton runtime while the
    // native HgCpu reader rebuilds AI-owned state. The native writer does not
    // serialize this runtime: a 0x200-byte owner/control header followed by an
    // embedded 0x20B4-byte motion state. The enclosing inline region ends at
    // the next independently serialized chara block (+0x2B3E0). Its list
    // pointers own additional fixed-arena KAuxBone and spring objects, which
    // are captured separately with exact vtable-derived sizes below.
    static constexpr uintptr_t kRollbackSkeletonRuntimeCharaOffset = 0x29120;
    static constexpr size_t kRollbackSkeletonRuntimeBytes = 0x22C0;
    static constexpr size_t kRollbackSkeletonChainBytes = 0x60;
    static constexpr size_t kRollbackSkeletonMaxAuxNodes = 512;
    static constexpr size_t kRollbackSkeletonMaxChains = 128;
    static constexpr size_t kRollbackSkeletonMaxSpringNodes = 512;
    static constexpr uintptr_t kRollbackRVA_LuxMoveVM_TimerConfig = 0x470DF28;
    static constexpr uintptr_t
        kRollbackRVA_LuxEffectSystemInstance = 0x470DF08;
    static constexpr size_t kRollbackTimerNodeRootBytes = 0x2F0;
    static constexpr size_t kRollbackTimerNodeBackingBytes = 0x41E0;
    static constexpr size_t kRollbackTimerNodeChildPtrOffset = 0x10;
    static constexpr size_t kRollbackTimerNodeChildPtrCount = 0x11;
    static constexpr size_t kRollbackTimerIndexedObjectBytes = 0x310;

    static inline bool RollbackTimerActionManagerAliasValid(
        uintptr_t timer_root,
        uintptr_t action_manager_root) noexcept
    {
        return timer_root != 0 && timer_root == action_manager_root;
    }

    static inline size_t RollbackKHitSnapshotWriterBytes(
        uint8_t tag,
        uintptr_t vtable,
        uintptr_t image_base) noexcept
    {
        if (tag == 0) return 0x26;
        if (tag == 1) return 0x42;
        if (tag == 2) return 0x32;

        const uintptr_t rva =
            image_base != 0 && vtable >= image_base ? vtable - image_base : 0;
        if (rva == 0x3E877F0) return 0x26;
        if (rva == 0x3E877A8) return 0x42;
        if (rva == 0x3E87760) return 0x32;
        return 2;
    }

    static inline bool RollbackKHitSourceOffsetForSerializedOffset(
        uint8_t tag,
        size_t serialized_offset,
        uintptr_t* out_node_offset,
        size_t* out_contiguous_bytes) noexcept
    {
        if (out_node_offset) *out_node_offset = 0;
        if (out_contiguous_bytes) *out_contiguous_bytes = 0;
        if (serialized_offset < 2)
        {
            if (out_node_offset)
                *out_node_offset = 0x14 + serialized_offset;
            if (out_contiguous_bytes)
                *out_contiguous_bytes = 2 - serialized_offset;
            return true;
        }

        const size_t rel = serialized_offset - 2;
        if (tag == 0)
        {
            if (rel < 0x10)
            {
                if (out_node_offset) *out_node_offset = 0x50 + rel;
                if (out_contiguous_bytes)
                    *out_contiguous_bytes = 0x10 - rel;
                return true;
            }
            if (rel < 0x20)
            {
                if (out_node_offset) *out_node_offset = 0x60 + (rel - 0x10);
                if (out_contiguous_bytes)
                    *out_contiguous_bytes = 0x20 - rel;
                return true;
            }
            if (rel < 0x24)
            {
                if (out_node_offset) *out_node_offset = 0x70 + (rel - 0x20);
                if (out_contiguous_bytes)
                    *out_contiguous_bytes = 0x24 - rel;
                return true;
            }
        }
        else if (tag == 1)
        {
            if (rel < 0x40)
            {
                if (out_node_offset) *out_node_offset = 0x50 + rel;
                if (out_contiguous_bytes)
                    *out_contiguous_bytes = 0x40 - rel;
                return true;
            }
        }
        else if (tag == 2)
        {
            if (rel < 0x30)
            {
                if (out_node_offset) *out_node_offset = 0x60 + rel;
                if (out_contiguous_bytes)
                    *out_contiguous_bytes = 0x30 - rel;
                return true;
            }
        }
        return false;
    }

#if !defined(HORSE_ROLLBACK_HGCPU_PURE_STATE_ONLY)
    using RollbackHgCpuExecFn =
        void* (__fastcall*)(class RollbackHgCpuBufferShim*);

    class RollbackHgCpuBufferShim
    {
    public:
        RollbackHgCpuBufferShim() noexcept
            : m_vtable_ptr(s_vtable)
        {
        }

        void retarget(uint8_t* data, size_t capacity) noexcept
        {
            m_data = data;
            m_capacity = capacity;
            m_cursor = 0;
            m_overflow = false;
        }

        size_t cursor() const noexcept { return m_cursor; }
        bool overflowed() const noexcept { return m_overflow; }

    private:
        const void* const* m_vtable_ptr;
        uint8_t* m_data {nullptr};
        size_t m_capacity {0};
        size_t m_cursor {0};
        bool m_overflow {false};

        static const void* const* s_vtable;

        static const void* const* build_vtable() noexcept
        {
            static const void* table[9] = {
                reinterpret_cast<const void*>(&RollbackHgCpuBufferShim::vt_dtor1),
                reinterpret_cast<const void*>(&RollbackHgCpuBufferShim::vt_dtor2),
                reinterpret_cast<const void*>(&RollbackHgCpuBufferShim::vt_init),
                reinterpret_cast<const void*>(&RollbackHgCpuBufferShim::vt_begin_write),
                reinterpret_cast<const void*>(&RollbackHgCpuBufferShim::vt_begin_read),
                reinterpret_cast<const void*>(&RollbackHgCpuBufferShim::vt_write),
                reinterpret_cast<const void*>(&RollbackHgCpuBufferShim::vt_read),
                reinterpret_cast<const void*>(&RollbackHgCpuBufferShim::vt_get_cursor),
                reinterpret_cast<const void*>(&RollbackHgCpuBufferShim::vt_validate),
            };
            return table;
        }

        static void __fastcall vt_dtor1(
            RollbackHgCpuBufferShim* /*self*/) noexcept {}
        static void __fastcall vt_dtor2(
            RollbackHgCpuBufferShim* /*self*/) noexcept {}

        static void __fastcall vt_init(RollbackHgCpuBufferShim* self) noexcept
        {
            self->m_cursor = 0;
            self->m_overflow = false;
        }

        static void __fastcall vt_begin_write(
            RollbackHgCpuBufferShim* self,
            int64_t offset) noexcept
        {
            self->m_cursor = offset > 0 ? static_cast<size_t>(offset) : 0;
            self->m_overflow = false;
        }

        static void __fastcall vt_begin_read(
            RollbackHgCpuBufferShim* self,
            int64_t offset) noexcept
        {
            self->m_cursor = offset > 0 ? static_cast<size_t>(offset) : 0;
            self->m_overflow = false;
        }

        static int64_t __fastcall vt_write(
            RollbackHgCpuBufferShim* self,
            const void* src,
            size_t bytes) noexcept
        {
            if (!self || !self->m_data || !src) return 0;
            if (self->m_cursor > self->m_capacity
                || bytes > self->m_capacity - self->m_cursor)
            {
                self->m_overflow = true;
                return 0;
            }
            const size_t prev = self->m_cursor;
            std::memcpy(self->m_data + self->m_cursor, src, bytes);
            self->m_cursor += bytes;
            return static_cast<int64_t>(prev);
        }

        static int64_t __fastcall vt_read(
            RollbackHgCpuBufferShim* self,
            void* dst,
            size_t bytes) noexcept
        {
            if (!self || !self->m_data || !dst) return 0;
            if (self->m_cursor > self->m_capacity
                || bytes > self->m_capacity - self->m_cursor)
            {
                self->m_overflow = true;
                return 0;
            }
            const size_t prev = self->m_cursor;
            std::memcpy(dst, self->m_data + self->m_cursor, bytes);
            self->m_cursor += bytes;
            return static_cast<int64_t>(prev);
        }

        static int64_t __fastcall vt_get_cursor(
            RollbackHgCpuBufferShim* self) noexcept
        {
            return self ? static_cast<int64_t>(self->m_cursor) : 0;
        }

        static int32_t __fastcall vt_validate(
            RollbackHgCpuBufferShim* /*self*/) noexcept
        {
            return 1;
        }
    };

    inline const void* const* RollbackHgCpuBufferShim::s_vtable =
        RollbackHgCpuBufferShim::build_vtable();
#endif

    struct RollbackHgCpuSnapshotFrame
    {
        struct KHitNodeImage
        {
            uintptr_t address {0};
            size_t stream_start_local {0};
            size_t writer_bytes {0};
            uint16_t node_index {0};
            uint8_t list_index {0};
            uint8_t writer_tag {0xff};
            std::array<uint8_t, kRollbackKHitNodeImageBytes> bytes {};
        };

        struct KHitCharaTopology
        {
            bool ok {false};
            uintptr_t chara {0};
            size_t node_stream_bytes {0};
            std::array<uint8_t, kRollbackKHitListControlBytes> list_control {};
            std::vector<KHitNodeImage> nodes;

            void clear()
            {
                ok = false;
                chara = 0;
                node_stream_bytes = 0;
                list_control.fill(0);
                nodes.clear();
            }
        };

        struct MotionBankHistory
        {
            bool ok {false};
            uintptr_t chara[kRollbackMotionBankPlayerCount] {};
            uintptr_t bank[
                kRollbackMotionBankPlayerCount][kRollbackMotionBankCount] {};
            uintptr_t current[
                kRollbackMotionBankPlayerCount][kRollbackMotionBankCount] {};
            uintptr_t provider[
                kRollbackMotionBankPlayerCount][kRollbackMotionBankCount] {};
            int current_slot[
                kRollbackMotionBankPlayerCount][kRollbackMotionBankCount] {};
            int provider_slot[
                kRollbackMotionBankPlayerCount][kRollbackMotionBankCount] {};
            uintptr_t buffer[
                kRollbackMotionBankPlayerCount]
                [kRollbackMotionBankCount]
                [kRollbackMotionBankBufferCount] {};
            std::vector<uint8_t> control_bytes;
            std::vector<uint8_t> bytes;
            uint64_t hash {0};

            void clear()
            {
                ok = false;
                hash = 0;
                control_bytes.clear();
                bytes.clear();
                std::memset(chara, 0, sizeof(chara));
                std::memset(bank, 0, sizeof(bank));
                std::memset(current, 0, sizeof(current));
                std::memset(provider, 0, sizeof(provider));
                std::memset(buffer, 0, sizeof(buffer));
                for (auto& per_player : current_slot)
                {
                    for (int& slot : per_player)
                        slot = -1;
                }
                for (auto& per_player : provider_slot)
                {
                    for (int& slot : per_player)
                        slot = -1;
                }
            }
        };

        struct MotionTailHistory
        {
            bool ok {false};
            uintptr_t chara[2] {};
            std::vector<uint8_t> bytes;
            uint64_t hash {0};

            void clear()
            {
                ok = false;
                std::memset(chara, 0, sizeof(chara));
                bytes.clear();
                hash = 0;
            }
        };

        struct SkeletonRuntimeHistory
        {
            struct NodeImage
            {
                uintptr_t address {0};
                uintptr_t vtable {0};
                uintptr_t next {0};
                std::vector<uint8_t> bytes;
            };

            struct ChainImage
            {
                uintptr_t address {0};
                uintptr_t next {0};
                uintptr_t child {0};
                std::array<uint8_t, kRollbackSkeletonChainBytes> bytes {};
            };

            bool ok {false};
            uintptr_t chara[2] {};
            uintptr_t aux_head[2][2] {};
            uintptr_t chain_head[2][2] {};
            std::vector<uint8_t> inline_bytes;
            std::vector<NodeImage> aux_nodes;
            std::vector<ChainImage> chains;
            std::vector<NodeImage> spring_nodes;
            uint64_t hash {0};

            void clear()
            {
                ok = false;
                std::memset(chara, 0, sizeof(chara));
                std::memset(aux_head, 0, sizeof(aux_head));
                std::memset(chain_head, 0, sizeof(chain_head));
                inline_bytes.clear();
                for (NodeImage& node : aux_nodes)
                {
                    node.address = 0;
                    node.vtable = 0;
                    node.next = 0;
                    node.bytes.clear();
                }
                for (ChainImage& chain : chains) chain = {};
                for (NodeImage& node : spring_nodes)
                {
                    node.address = 0;
                    node.vtable = 0;
                    node.next = 0;
                    node.bytes.clear();
                }
                hash = 0;
            }
        };

        struct TimerNodeHistory
        {
            struct NodeImage
            {
                uintptr_t root {0};
                uintptr_t backing {0};
                std::array<uint8_t, kRollbackTimerNodeRootBytes> root_bytes {};
                std::array<uint8_t, kRollbackTimerNodeBackingBytes>
                    backing_bytes {};
            };

            bool ok {false};
            uintptr_t timer_config {0};
            uintptr_t root {0};
            uintptr_t backing {0};
            uintptr_t indexed_table {0};
            uintptr_t indexed_root[0x10] {};
            uintptr_t indexed_vtable[0x10] {};
            uintptr_t indexed_writer[0x10] {};
            bool indexed_captured[0x10] {};
            bool indexed_object_captured[0x10] {};
            std::array<
                std::array<uint8_t, kRollbackTimerIndexedObjectBytes>,
                0x10> indexed_object_bytes {};
            uint32_t indexed_nonzero_count {0};
            uint32_t indexed_captured_count {0};
            uint32_t indexed_object_captured_count {0};
            uintptr_t child[kRollbackTimerNodeChildPtrCount] {};
            std::vector<uint8_t> root_bytes;
            std::vector<uint8_t> backing_bytes;
            std::vector<NodeImage> nodes;
            uint64_t hash {0};

            void clear()
            {
                ok = false;
                timer_config = 0;
                root = 0;
                backing = 0;
                indexed_table = 0;
                indexed_nonzero_count = 0;
                indexed_captured_count = 0;
                indexed_object_captured_count = 0;
                std::memset(indexed_root, 0, sizeof(indexed_root));
                std::memset(indexed_vtable, 0, sizeof(indexed_vtable));
                std::memset(indexed_writer, 0, sizeof(indexed_writer));
                std::memset(indexed_captured, 0, sizeof(indexed_captured));
                std::memset(
                    indexed_object_captured,
                    0,
                    sizeof(indexed_object_captured));
                hash = 0;
                std::memset(child, 0, sizeof(child));
                root_bytes.clear();
                backing_bytes.clear();
                for (NodeImage& node : nodes)
                {
                    node.root = 0;
                    node.backing = 0;
                    node.root_bytes.fill(0);
                    node.backing_bytes.fill(0);
                }
            }
        };

        std::vector<uint8_t> bytes;
        size_t used_bytes {0};
        uint64_t byte_hash {0};
        uint64_t khit_topology_hash {0};
        uint64_t motion_bank_hash {0};
        uint64_t motion_tail_hash {0};
        uint64_t secondary_event_stack_hash {0};
        uint64_t chara_animation_hash {0};
        uint64_t skeleton_runtime_hash {0};
        uint64_t timer_node_hash {0};
        // Peer-canonical gameplay digest. Native writer-selected fields and
        // logical topology/shape are covered; Horse restore-only object images
        // and process addresses are excluded and verified by local integrity.
        uint64_t canonical_hash {0};
        uint64_t hash {0};
        bool khit_topology_ok {false};
        KHitCharaTopology khit_topology[2] {};
        MotionBankHistory motion_banks {};
        MotionTailHistory motion_tail {};
        RollbackSecondaryEventStackHistory secondary_event_stack {};
        RollbackCharaAnimationStateHistory chara_animation {};
        SkeletonRuntimeHistory skeleton_runtime {};
        TimerNodeHistory timer_node {};

        void clear()
        {
            bytes.clear();
            reset_metadata();
        }

        // Arena recycling keeps the fixed native buffer live. The writer
        // overwrites every byte up to used_bytes, so clearing 0x28018 bytes
        // before every Save is wasted work.
        void recycle_for_capture()
        {
            used_bytes = 0;
            byte_hash = 0;
            khit_topology_hash = 0;
            motion_bank_hash = 0;
            motion_tail_hash = 0;
            secondary_event_stack_hash = 0;
            chara_animation_hash = 0;
            skeleton_runtime_hash = 0;
            timer_node_hash = 0;
            canonical_hash = 0;
            hash = 0;
            khit_topology_ok = false;
            khit_topology[0].clear();
            khit_topology[1].clear();
            motion_banks.ok = false;
            motion_banks.hash = 0;
            motion_tail.ok = false;
            motion_tail.hash = 0;
            secondary_event_stack.recycle_for_capture();
            chara_animation.recycle_for_capture();
            skeleton_runtime.ok = false;
            skeleton_runtime.hash = 0;
            timer_node.ok = false;
            timer_node.hash = 0;
        }

    private:
        void reset_metadata()
        {
            used_bytes = 0;
            byte_hash = 0;
            khit_topology_hash = 0;
            motion_bank_hash = 0;
            motion_tail_hash = 0;
            secondary_event_stack_hash = 0;
            chara_animation_hash = 0;
            skeleton_runtime_hash = 0;
            timer_node_hash = 0;
            canonical_hash = 0;
            hash = 0;
            khit_topology_ok = false;
            khit_topology[0].clear();
            khit_topology[1].clear();
            motion_banks.clear();
            motion_tail.clear();
            secondary_event_stack.clear();
            chara_animation.clear();
            skeleton_runtime.clear();
            timer_node.clear();
        }

    public:
    };

    static constexpr uint32_t kRollbackHgCpuPeerMismatchChara0 = 1u << 0;
    static constexpr uint32_t kRollbackHgCpuPeerMismatchChara1 = 1u << 1;
    static constexpr uint32_t kRollbackHgCpuPeerMismatchKHit0 = 1u << 2;
    static constexpr uint32_t kRollbackHgCpuPeerMismatchKHit1 = 1u << 3;
    static constexpr uint32_t kRollbackHgCpuPeerMismatchMotion = 1u << 4;
    static constexpr uint32_t kRollbackHgCpuPeerMismatchSecondary = 1u << 5;
    static constexpr uint32_t kRollbackHgCpuPeerMismatchTimer = 1u << 6;
    static constexpr uint32_t kRollbackHgCpuPeerMismatchSkeleton = 1u << 7;
    static constexpr uint32_t kRollbackHgCpuPeerMismatchShape = 1u << 8;

#if !defined(HORSE_ROLLBACK_HGCPU_PURE_STATE_ONLY)
    struct RollbackHgCpuSnapshotReport
    {
        bool ok {false};
        bool context_ready {false};
        bool overflowed {false};
        size_t capacity {0};
        size_t cursor {0};
        uintptr_t image_base {0};
        uintptr_t function_address {0};
        uintptr_t chara_p1 {0};
        uintptr_t chara_p2 {0};
        uint64_t hash {0};
        uint64_t emergency_capture_nanoseconds {0};
        uint64_t native_capture_nanoseconds {0};
        uint64_t emergency_restore_nanoseconds {0};
        uint64_t khit_capture_nanoseconds {0};
        uint64_t timer_capture_nanoseconds {0};
        uint64_t hash_finalize_nanoseconds {0};
        NativeCallFault fault {};
        const char* failure {"not-run"};
    };

    struct RollbackHgCpuRoundTripReport
    {
        bool ok {false};
        bool hash_match {false};
        bool policy_match {false};
        bool topology_match {false};
        size_t bytes_compared {0};
        size_t mismatch_count {0};
        size_t unignored_mismatch_count {0};
        size_t ignored_mismatch_count {0};
        size_t first_mismatch_offset {0};
        uint8_t first_mismatch_before {0};
        uint8_t first_mismatch_after {0};
        size_t first_unignored_mismatch_offset {0};
        uint8_t first_unignored_mismatch_before {0};
        uint8_t first_unignored_mismatch_after {0};
        const char* first_ignored_reason {"none"};
        uint64_t before_hash {0};
        uint64_t after_hash {0};
        uint64_t before_topology_hash {0};
        uint64_t after_topology_hash {0};
        RollbackHgCpuSnapshotReport capture {};
        RollbackHgCpuSnapshotReport restore {};
        RollbackHgCpuSnapshotReport recapture {};
    };
#endif

    // Capture, restore validation, and terminal-checkpoint validation must use
    // one aggregate formula. In particular, character animation history is
    // part of exact local rewind integrity even though its pointer-normalized
    // canonical contribution is handled separately.
    static inline uint64_t RollbackHashHgCpuIntegrityComponents(
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        return RollbackHashCombine(
            RollbackHashCombine(
                RollbackHashCombine(
                    RollbackHashCombine(
                        frame.byte_hash, frame.khit_topology_hash),
                    frame.motion_bank_hash),
                RollbackHashCombine(
                    frame.motion_tail_hash,
                    RollbackHashCombine(
                        frame.secondary_event_stack_hash,
                        frame.chara_animation_hash))),
            RollbackHashCombine(
                frame.skeleton_runtime_hash, frame.timer_node_hash));
    }

    static inline size_t RollbackHgCpuCharaRecordBytes(
        const RollbackHgCpuSnapshotFrame* frame,
        size_t player) noexcept
    {
        if (frame && frame->khit_topology_ok && player < 2
            && frame->khit_topology[player].ok)
        {
            return kRollbackHgCpuAiResetSlotEnd
                + kRollbackHgCpuHitAreaFixedBytes
                + frame->khit_topology[player].node_stream_bytes
                + kRollbackHgCpuHitAreaRelocBytes;
        }
        return kRollbackHgCpuFallbackPerCharaBytes;
    }

    static inline bool RollbackHgCpuSnapshotCharaBase(
        const RollbackHgCpuSnapshotFrame& frame,
        size_t player,
        size_t& out_base) noexcept
    {
        out_base = 0;
        if (player == 0)
            return true;
        if (player != 1)
            return false;
        out_base = RollbackHgCpuCharaRecordBytes(&frame, 0);
        return out_base < frame.bytes.size();
    }

    static inline size_t RollbackHgCpuEffectiveBytes(
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        if (frame.used_bytes > 0 && frame.used_bytes <= frame.bytes.size())
            return frame.used_bytes;
        return frame.bytes.size();
    }

    static inline bool RollbackHgCpuRoundTripOffsetIgnored(
        size_t offset,
        const char** reason_out = nullptr,
        const RollbackHgCpuSnapshotFrame* frame = nullptr) noexcept
    {
        const size_t p2_base = RollbackHgCpuCharaRecordBytes(frame, 0);
        const size_t bases[2] = {0, p2_base};
        if (frame && frame->khit_topology_ok)
        {
            for (size_t player = 0; player < 2; ++player)
            {
                const size_t start = bases[player]
                    + kRollbackHgCpuHitAreaLocalStart
                    + kRollbackHgCpuHitAreaFixedBytes;
                const size_t end = bases[player]
                    + RollbackHgCpuCharaRecordBytes(frame, player);
                if (offset >= start && offset < end)
                {
                    if (reason_out)
                    {
                        *reason_out =
                            "KHit node stream is verified by pointer-free topology";
                    }
                    return true;
                }
            }
        }
        for (size_t player = 0; player < 2; ++player)
        {
            if (offset < bases[player]) continue;
            const char* ignore_reason = nullptr;
            if (RollbackHgCpuRestoreLocalOffsetIgnored(
                    offset - bases[player], &ignore_reason))
            {
                if (reason_out) *reason_out = ignore_reason;
                return true;
            }
        }
        if (reason_out) *reason_out = nullptr;
        return false;
    }

    static inline bool RollbackAddHgCpuCanonicalCharaRecord(
        RollbackFastHash& hash,
        const RollbackHgCpuSnapshotFrame& frame,
        size_t player,
        size_t effective,
        size_t local_begin = 0,
        size_t local_limit = static_cast<size_t>(-1)) noexcept
    {
        size_t base = 0;
        if (player >= 2
            || !RollbackHgCpuSnapshotCharaBase(frame, player, base))
        {
            return false;
        }
        const size_t record = RollbackHgCpuCharaRecordBytes(&frame, player);
        const size_t effective_local = effective > base
            ? (std::min)(record, effective - base)
            : 0;
        const size_t record_limit = (std::min)(
            effective_local, (std::min)(record, local_limit));
        const size_t khit_node_stream_start =
            kRollbackHgCpuHitAreaLocalStart
            + kRollbackHgCpuHitAreaFixedBytes;
        // Native HgCpu appends a variable KHit node stream after the fixed
        // record. Its pointer-free topology is hashed separately below.
        return RollbackAddHgCpuCanonicalCharaBytes(
            hash, frame.bytes.data() + base, record_limit, local_begin,
            khit_node_stream_start);
    }

    static inline bool RollbackAddHgCpuCanonicalCharaRecordFromBytes(
        RollbackFastHash& hash,
        const uint8_t* bytes,
        size_t effective,
        const RollbackHgCpuSnapshotFrame& layout,
        size_t player) noexcept
    {
        if (!bytes || player >= 2) return false;
        size_t base = 0;
        if (!RollbackHgCpuSnapshotCharaBase(layout, player, base))
            return false;
        const size_t record =
            RollbackHgCpuCharaRecordBytes(&layout, player);
        if (base > effective || record > effective - base)
            return false;
        return RollbackAddHgCpuCanonicalCharaBytes(
            hash, bytes + base, record, 0,
            kRollbackHgCpuHitAreaLocalStart
                + kRollbackHgCpuHitAreaFixedBytes);
    }

    static inline uint64_t RollbackHashHgCpuCanonicalCharaChunk(
        const RollbackHgCpuSnapshotFrame& frame,
        size_t player,
        size_t local_begin,
        size_t local_limit) noexcept
    {
        const size_t effective = RollbackHgCpuEffectiveBytes(frame);
        size_t base = 0;
        if (player >= 2
            || !RollbackHgCpuSnapshotCharaBase(frame, player, base))
        {
            return 0;
        }
        const size_t record = RollbackHgCpuCharaRecordBytes(&frame, player);
        const size_t effective_local = effective > base
            ? (std::min)(record, effective - base)
            : 0;
        return RollbackHashHgCpuCanonicalCharaChunkBytes(
            frame.bytes.data() + base, effective_local, player,
            local_begin, local_limit,
            kRollbackHgCpuHitAreaLocalStart
                + kRollbackHgCpuHitAreaFixedBytes);
    }

    static inline void RollbackAddHgCpuMotionSlotPeerState(
        RollbackFastHash& hash,
        const RollbackHgCpuSnapshotFrame::MotionBankHistory& motion,
        bool& ok) noexcept
    {
        ok = RollbackAddMotionBankPeerState(hash, motion);
    }

    static inline uint64_t RollbackHashHgCpuMotionSlotPeerState(
        const RollbackHgCpuSnapshotFrame::MotionBankHistory& motion) noexcept
    {
        return RollbackHashMotionBankPeerState(motion);
    }

    static inline void RollbackAddHgCpuTimerShapePeerState(
        RollbackFastHash& hash,
        const RollbackHgCpuSnapshotFrame::TimerNodeHistory& timer) noexcept
    {
        hash.add_scalar(timer.indexed_nonzero_count);
        hash.add_scalar(timer.indexed_captured_count);
        hash.add_scalar(timer.indexed_object_captured_count);
        for (size_t slot = 0; slot < 0x10; ++slot)
        {
            hash.add_scalar(slot);
            hash.add_scalar(timer.indexed_captured[slot]);
            hash.add_scalar(timer.indexed_object_captured[slot]);
        }
        hash.add_scalar(timer.nodes.size());
    }

    static inline uint64_t RollbackHashHgCpuTimerShapePeerState(
        const RollbackHgCpuSnapshotFrame::TimerNodeHistory& timer) noexcept
    {
        if (!timer.ok) return 0;
        RollbackFastHash hash {};
        RollbackAddHgCpuTimerShapePeerState(hash, timer);
        return hash.finish();
    }

    static inline void RollbackAddHgCpuSkeletonShapePeerState(
        RollbackFastHash& hash,
        const RollbackHgCpuSnapshotFrame::SkeletonRuntimeHistory& skeleton)
        noexcept
    {
        hash.add_scalar(skeleton.inline_bytes.size());
        hash.add_scalar(skeleton.aux_nodes.size());
        hash.add_scalar(skeleton.chains.size());
        hash.add_scalar(skeleton.spring_nodes.size());
    }

    static inline uint64_t RollbackHashHgCpuSkeletonShapePeerState(
        const RollbackHgCpuSnapshotFrame::SkeletonRuntimeHistory& skeleton)
        noexcept
    {
        if (!skeleton.ok) return 0;
        RollbackFastHash hash {};
        RollbackAddHgCpuSkeletonShapePeerState(hash, skeleton);
        return hash.finish();
    }

    static inline bool RollbackAddHgCpuKHitPlayerPeerState(
        RollbackFastHash& hash,
        const RollbackHgCpuSnapshotFrame& frame,
        size_t player) noexcept
    {
        if (player >= 2 || !frame.khit_topology_ok) return false;
        const auto& topology = frame.khit_topology[player];
        hash.add_scalar(topology.nodes.size());
        for (const auto& node : topology.nodes)
        {
            hash.add_scalar(node.list_index);
            hash.add_scalar(node.node_index);
            hash.add_scalar(node.writer_tag);
            hash.add_scalar(node.writer_bytes);
            for (size_t serialized = 0; serialized < node.writer_bytes;)
            {
                uintptr_t source_offset = 0;
                size_t contiguous = 0;
                if (!RollbackKHitSourceOffsetForSerializedOffset(
                        node.writer_tag, serialized,
                        &source_offset, &contiguous)
                    || contiguous == 0
                    || source_offset >= node.bytes.size())
                {
                    return false;
                }
                const size_t remaining = node.writer_bytes - serialized;
                const size_t bytes = (std::min)(contiguous, remaining);
                if (bytes > node.bytes.size() - source_offset) return false;
                hash.add_bytes(node.bytes.data() + source_offset, bytes);
                serialized += bytes;
            }
        }
        return true;
    }

    static inline uint64_t RollbackHashHgCpuKHitPlayerPeerState(
        const RollbackHgCpuSnapshotFrame& frame,
        size_t player) noexcept
    {
        RollbackFastHash hash {};
        hash.add_scalar(player);
        return RollbackAddHgCpuKHitPlayerPeerState(hash, frame, player)
            ? hash.finish() : 0;
    }

    enum class RollbackKHitPeerDiagnosticPart : uint8_t
    {
        Full = 0,
        ActiveWord = 1,
        PayloadLane0 = 2,
        PayloadLane1 = 3,
        PayloadLane2 = 4,
        PayloadLane3 = 5,
        WriterTag0 = 6,
        WriterTag1 = 7,
        WriterTag2 = 8,
    };

    static inline uint64_t RollbackHashHgCpuKHitListDiagnostic(
        const RollbackHgCpuSnapshotFrame& frame,
        size_t player,
        size_t list_index,
        RollbackKHitPeerDiagnosticPart part) noexcept
    {
        if (player >= 2 || list_index >= kRollbackHgCpuKHitListCount
            || !frame.khit_topology_ok
            || !frame.khit_topology[player].ok)
        {
            return 0;
        }
        RollbackFastHash hash {};
        hash.add_scalar(player);
        hash.add_scalar(list_index);
        hash.add_scalar(static_cast<uint8_t>(part));
        size_t matched_nodes = 0;
        for (const auto& node : frame.khit_topology[player].nodes)
        {
            if (node.list_index != list_index) continue;
            if (part >= RollbackKHitPeerDiagnosticPart::WriterTag0)
            {
                const uint8_t required_tag =
                    static_cast<uint8_t>(part)
                    - static_cast<uint8_t>(
                        RollbackKHitPeerDiagnosticPart::WriterTag0);
                if (node.writer_tag != required_tag) continue;
            }
            ++matched_nodes;
            hash.add_scalar(node.node_index);
            hash.add_scalar(node.writer_tag);
            hash.add_scalar(node.writer_bytes);

            size_t serialized_begin = 0;
            size_t serialized_end = node.writer_bytes;
            if (part == RollbackKHitPeerDiagnosticPart::ActiveWord)
            {
                serialized_end = (std::min)(size_t {2}, node.writer_bytes);
            }
            else if (part >= RollbackKHitPeerDiagnosticPart::PayloadLane0
                && part <= RollbackKHitPeerDiagnosticPart::PayloadLane3)
            {
                const size_t lane = static_cast<size_t>(part)
                    - static_cast<size_t>(
                        RollbackKHitPeerDiagnosticPart::PayloadLane0);
                serialized_begin = 2 + lane * 0x10;
                serialized_end = (std::min)(
                    serialized_begin + 0x10, node.writer_bytes);
                const bool present = serialized_begin < node.writer_bytes;
                hash.add_scalar(present);
                if (!present) continue;
            }

            for (size_t serialized = serialized_begin;
                 serialized < serialized_end;)
            {
                uintptr_t source_offset = 0;
                size_t contiguous = 0;
                if (!RollbackKHitSourceOffsetForSerializedOffset(
                        node.writer_tag, serialized,
                        &source_offset, &contiguous)
                    || contiguous == 0
                    || source_offset >= node.bytes.size())
                {
                    return 0;
                }
                const size_t remaining = serialized_end - serialized;
                const size_t bytes = (std::min)(contiguous, remaining);
                if (bytes > node.bytes.size() - source_offset) return 0;
                hash.add_bytes(node.bytes.data() + source_offset, bytes);
                serialized += bytes;
            }
        }
        hash.add_scalar(matched_nodes);
        return hash.finish();
    }

    static inline uint64_t RollbackHashHgCpuKHitSourceMatrices(
        const RollbackHgCpuSnapshotFrame& frame,
        size_t player,
        size_t list_index,
        uint16_t* bone_min_out,
        uint16_t* bone_max_out) noexcept
    {
        if (bone_min_out) *bone_min_out = 0;
        if (bone_max_out) *bone_max_out = 0;
        if (player >= 2 || list_index >= kRollbackHgCpuKHitListCount
            || !frame.khit_topology_ok
            || !frame.khit_topology[player].ok)
        {
            return 0;
        }

        size_t current_offset = 0;
        size_t current_bytes = 0;
        if (!RollbackMotionBankLogicalCurrentLocation(
                frame.motion_banks, player, 0,
                current_offset, current_bytes))
        {
            return 0;
        }

        RollbackFastHash hash {};
        hash.add_scalar(player);
        hash.add_scalar(list_index);
        uint32_t bone_min = (std::numeric_limits<uint32_t>::max)();
        uint32_t bone_max = 0;
        size_t matrix_count = 0;
        for (const auto& node : frame.khit_topology[player].nodes)
        {
            if (node.list_index != list_index) continue;
            uint32_t bones[2] {};
            size_t bone_count = 0;
            if (node.writer_tag == 0)
            {
                std::memcpy(&bones[0], node.bytes.data() + 0x7c,
                    sizeof(bones[0]));
                bone_count = 1;
            }
            else if (node.writer_tag == 1)
            {
                std::memcpy(&bones[0], node.bytes.data() + 0x90,
                    sizeof(bones[0]));
                std::memcpy(&bones[1], node.bytes.data() + 0x94,
                    sizeof(bones[1]));
                bone_count = 2;
            }
            for (size_t index = 0; index < bone_count; ++index)
            {
                const size_t matrix_offset =
                    static_cast<size_t>(bones[index])
                    * kRollbackMotionBankMatrixBytes;
                if (matrix_offset > current_bytes
                    || kRollbackMotionBankMatrixBytes
                        > current_bytes - matrix_offset)
                {
                    return 0;
                }
                hash.add_scalar(node.node_index);
                hash.add_scalar(node.writer_tag);
                hash.add_scalar(bones[index]);
                hash.add_bytes(
                    frame.motion_banks.bytes.data()
                        + current_offset + matrix_offset,
                    kRollbackMotionBankMatrixBytes);
                bone_min = (std::min)(bone_min, bones[index]);
                bone_max = (std::max)(bone_max, bones[index]);
                ++matrix_count;
            }
        }
        hash.add_scalar(matrix_count);
        if (matrix_count != 0)
        {
            if (bone_min_out)
                *bone_min_out = static_cast<uint16_t>((std::min)(
                    bone_min, static_cast<uint32_t>(0xffff)));
            if (bone_max_out)
                *bone_max_out = static_cast<uint16_t>((std::min)(
                    bone_max, static_cast<uint32_t>(0xffff)));
        }
        return hash.finish();
    }

    static inline RollbackHgCpuPeerBreakdown RollbackBuildHgCpuPeerBreakdown(
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        RollbackHgCpuPeerBreakdown out {};
        const size_t effective = RollbackHgCpuEffectiveBytes(frame);
        out.effective_bytes = static_cast<uint32_t>((std::min)(
            effective,
            static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        for (size_t player = 0; player < 2; ++player)
        {
            out.chara_stream_hash[player] =
                RollbackHashHgCpuCanonicalCharaChunk(
                    frame, player, 0, static_cast<size_t>(-1));
            for (size_t chunk = 0;
                 chunk < kRollbackHgCpuPeerCharaChunkCount; ++chunk)
            {
                const size_t begin = chunk * kRollbackHgCpuPeerCharaChunkBytes;
                out.chara_chunk_hash[player][chunk] =
                    RollbackHashHgCpuCanonicalCharaChunk(
                        frame, player, begin,
                        begin + kRollbackHgCpuPeerCharaChunkBytes);
            }
            out.khit_hash[player] =
                RollbackHashHgCpuKHitPlayerPeerState(frame, player);
            for (size_t list = 0;
                 list < kRollbackHgCpuKHitListCount; ++list)
            {
                out.khit_list_hash[player][list] =
                    RollbackHashHgCpuKHitListDiagnostic(
                        frame, player, list,
                        RollbackKHitPeerDiagnosticPart::Full);
                out.khit_source_matrix_hash[player][list] =
                    RollbackHashHgCpuKHitSourceMatrices(
                        frame, player, list,
                        &out.khit_source_bone_min[player][list],
                        &out.khit_source_bone_max[player][list]);
                for (size_t lane = 0;
                     lane < kRollbackHgCpuKHitPayloadLaneCount; ++lane)
                {
                    out.khit_payload_lane_hash[player][list][lane] =
                        RollbackHashHgCpuKHitListDiagnostic(
                            frame, player, list,
                            static_cast<RollbackKHitPeerDiagnosticPart>(
                                static_cast<uint8_t>(
                                    RollbackKHitPeerDiagnosticPart::PayloadLane0)
                                + static_cast<uint8_t>(lane)));
                }
            }
            out.khit_node_count[player] = static_cast<uint16_t>((std::min)(
                frame.khit_topology[player].nodes.size(),
                static_cast<size_t>(
                    (std::numeric_limits<uint16_t>::max)())));
        }
        out.motion_slot_hash =
            RollbackHashHgCpuMotionSlotPeerState(frame.motion_banks);
        for (size_t player = 0;
             player < kRollbackMotionBankPlayerCount; ++player)
        {
            size_t current_offset = 0;
            size_t current_bytes = 0;
            if (RollbackMotionBankLogicalCurrentLocation(
                    frame.motion_banks, player, 0,
                    current_offset, current_bytes))
            {
                static constexpr size_t partition_begin[] = {16, 24, 25};
                static constexpr size_t partition_end[] = {24, 25, 26};
                for (size_t partition = 0;
                     partition < kRollbackHgCpuCurrentMatrixPartitionCount;
                     ++partition)
                {
                    const size_t begin = partition_begin[partition]
                        * kRollbackMotionBankMatrixBytes;
                    const size_t end = partition_end[partition]
                        * kRollbackMotionBankMatrixBytes;
                    if (begin > current_bytes || end > current_bytes)
                    {
                        break;
                    }
                    RollbackFastHash matrix_hash {};
                    matrix_hash.add_bytes(
                        frame.motion_banks.bytes.data()
                            + current_offset + begin,
                        end - begin);
                    out.motion_current_partition_hash[player][partition] =
                        matrix_hash.finish();
                }
            }
            for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
            {
                uint32_t age = 0;
                size_t offset = 0;
                size_t bytes = 0;
                if (RollbackMotionBankLogicalPreviousLocation(
                        frame.motion_banks, player, bank, age, offset, bytes))
                {
                    out.motion_provider_age[player][bank] =
                        static_cast<uint8_t>(age);
                    RollbackFastHash image_hash {};
                    image_hash.add_bytes(
                        frame.motion_banks.bytes.data() + offset, bytes);
                    out.motion_provider_hash[player][bank] =
                        image_hash.finish();
                }
            }
        }
        // Keep the fixed peer-breakdown width while expanding this existing
        // animation/event bucket to cover the scheduler that produces the
        // secondary animation-notify stack.
        out.secondary_event_hash = RollbackHashCombine(
            RollbackHashSecondaryEventStackCanonical(
                frame.secondary_event_stack),
            RollbackHashCharaAnimationCanonical(
                frame.chara_animation));
        out.timer_shape_hash =
            RollbackHashHgCpuTimerShapePeerState(frame.timer_node);
        out.skeleton_shape_hash =
            RollbackHashHgCpuSkeletonShapePeerState(frame.skeleton_runtime);
        return out;
    }

    static inline uint32_t RollbackHgCpuPeerBreakdownMismatchMask(
        const RollbackHgCpuPeerBreakdown& local,
        const RollbackHgCpuPeerBreakdown& remote) noexcept
    {
        uint32_t mask = 0;
        if (local.chara_stream_hash[0] != remote.chara_stream_hash[0])
            mask |= kRollbackHgCpuPeerMismatchChara0;
        if (local.chara_stream_hash[1] != remote.chara_stream_hash[1])
            mask |= kRollbackHgCpuPeerMismatchChara1;
        if (local.khit_hash[0] != remote.khit_hash[0])
            mask |= kRollbackHgCpuPeerMismatchKHit0;
        if (local.khit_hash[1] != remote.khit_hash[1])
            mask |= kRollbackHgCpuPeerMismatchKHit1;
        if (local.motion_slot_hash != remote.motion_slot_hash)
            mask |= kRollbackHgCpuPeerMismatchMotion;
        if (local.secondary_event_hash != remote.secondary_event_hash)
            mask |= kRollbackHgCpuPeerMismatchSecondary;
        if (local.timer_shape_hash != remote.timer_shape_hash)
            mask |= kRollbackHgCpuPeerMismatchTimer;
        if (local.skeleton_shape_hash != remote.skeleton_shape_hash)
            mask |= kRollbackHgCpuPeerMismatchSkeleton;
        if (local.effective_bytes != remote.effective_bytes
            || local.khit_node_count[0] != remote.khit_node_count[0]
            || local.khit_node_count[1] != remote.khit_node_count[1])
        {
            mask |= kRollbackHgCpuPeerMismatchShape;
        }
        return mask;
    }

    static inline uint32_t RollbackHgCpuPeerMotionContributionMismatchMask(
        const RollbackHgCpuPeerBreakdown& local,
        const RollbackHgCpuPeerBreakdown& remote) noexcept
    {
        uint32_t mask = 0;
        for (size_t player = 0;
             player < kRollbackMotionBankPlayerCount; ++player)
        {
            for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
            {
                if (local.motion_provider_age[player][bank]
                        != remote.motion_provider_age[player][bank]
                    || local.motion_provider_hash[player][bank]
                        != remote.motion_provider_hash[player][bank])
                {
                    mask |= 1u << static_cast<uint32_t>(
                        player * kRollbackMotionBankCount + bank);
                }
            }
        }
        return mask;
    }

    static inline uint32_t RollbackHgCpuPeerCharaChunkMismatchMask(
        const RollbackHgCpuPeerBreakdown& local,
        const RollbackHgCpuPeerBreakdown& remote) noexcept
    {
        uint32_t mask = 0;
        for (size_t player = 0; player < 2; ++player)
        {
            for (size_t chunk = 0;
                 chunk < kRollbackHgCpuPeerCharaChunkCount; ++chunk)
            {
                if (local.chara_chunk_hash[player][chunk]
                    != remote.chara_chunk_hash[player][chunk])
                {
                    mask |= 1u << static_cast<uint32_t>(
                        player * kRollbackHgCpuPeerCharaChunkCount + chunk);
                }
            }
        }
        return mask;
    }

    static inline uint64_t RollbackHashHgCpuCanonical(
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        if (!frame.khit_topology_ok
            || !frame.motion_banks.ok
            || !frame.motion_tail.ok
            || !frame.secondary_event_stack.ok
            || !frame.chara_animation.ok
            || !frame.skeleton_runtime.ok
            || !frame.timer_node.ok)
        {
            return 0;
        }

        RollbackFastHash hash {};
        const size_t effective = RollbackHgCpuEffectiveBytes(frame);
        hash.add_scalar(effective);

        // Canonicalize the native two-character stream in place logically.
        // Ghidra/native-reader-proven presentation/self-pointer ranges and the
        // variable KHit node stream/trailer are zeroed while preserving
        // layout; their pointer-free logical topology is added below.
        for (size_t player = 0; player < 2; ++player)
        {
            if (!RollbackAddHgCpuCanonicalCharaRecord(
                    hash, frame, player, effective))
                return 0;
        }

        // KHit is serialized in list/index order. Addresses, vtables, next
        // pointers, and raw list-control allocator links are intentionally not
        // part of the peer digest. The first two serialized bytes are the
        // gameplay-owned active gate. The remainder is world-space collision
        // cache and remains canonical while rollback owns gameplay.
        for (size_t player = 0; player < 2; ++player)
            if (!RollbackAddHgCpuKHitPlayerPeerState(hash, frame, player))
                return 0;

        // The native chara stream's solved-pose payload is excluded from its
        // raw peer hash because physical ring slots and presentation pose can
        // differ across peers. Add the four logical primary matrices with
        // proven pre/post-rotation gameplay consumers plus provider age. Full
        // buffers remain captured and locally verified for restore.
        // Timer object images remain local integrity caches, so only their
        // stable shape/ownership metadata is peer-canonical below.
        bool motion_peer_state_ok = false;
        RollbackAddHgCpuMotionSlotPeerState(
            hash, frame.motion_banks, motion_peer_state_ok);
        if (!motion_peer_state_ok) return 0;
        // Unlike the rest of the process-local motion tail, this inline
        // cache is future gameplay state. Replay106 proves bone 25 can reuse
        // it directly and diverge KHit while sampled transform 25 and its
        // parent both match. Include the exact nine-matrix cache for each
        // player in peer authority.
        for (size_t player = 0; player < 2; ++player)
        {
            const size_t cache_offset =
                player * kRollbackMoveVMMotionTailBytes
                + kRollbackMotionTailExtraBoneCacheOffset;
            if (cache_offset > frame.motion_tail.bytes.size()
                || kRollbackMotionTailExtraBoneCacheBytes
                    > frame.motion_tail.bytes.size() - cache_offset)
            {
                return 0;
            }
            hash.add_bytes(
                frame.motion_tail.bytes.data() + cache_offset,
                kRollbackMotionTailExtraBoneCacheBytes);
        }
        const auto& timer = frame.timer_node;
        RollbackAddHgCpuTimerShapePeerState(hash, timer);
        // Skeleton runtime images contain native pointers and remain a
        // same-process restore cache. Only logical shape is peer-canonical;
        // addresses and raw images are covered by local integrity.
        RollbackAddHgCpuSkeletonShapePeerState(
            hash, frame.skeleton_runtime);
        const uint64_t secondary_event_hash =
            RollbackHashSecondaryEventStackCanonical(
                frame.secondary_event_stack);
        const uint64_t chara_animation_hash =
            RollbackHashCharaAnimationCanonical(
                frame.chara_animation);
        if (secondary_event_hash == 0 || chara_animation_hash == 0)
            return 0;
        hash.add_scalar(secondary_event_hash);
        hash.add_scalar(chara_animation_hash);
        return hash.finish();
    }

    static inline bool RollbackAddHgCpuSerializedKHitPlayerPeerState(
        RollbackFastHash& hash,
        const uint8_t* bytes,
        size_t effective,
        const RollbackHgCpuSnapshotFrame& layout,
        size_t player) noexcept
    {
        size_t base = 0;
        if (!bytes || player >= 2 || !layout.khit_topology_ok
            || !layout.khit_topology[player].ok
            || !RollbackHgCpuSnapshotCharaBase(layout, player, base))
        {
            return false;
        }
        const auto& topology = layout.khit_topology[player];
        const size_t record =
            RollbackHgCpuCharaRecordBytes(&layout, player);
        if (base > effective || record > effective - base)
            return false;
        size_t cursor = base + kRollbackHgCpuHitAreaLocalStart
            + kRollbackHgCpuHitAreaFixedBytes;
        const size_t stream_end = cursor + topology.node_stream_bytes;
        if (stream_end > base + record
            || kRollbackHgCpuHitAreaRelocBytes
                > base + record - stream_end)
        {
            return false;
        }
        hash.add_scalar(topology.nodes.size());
        for (const auto& node : topology.nodes)
        {
            if (node.writer_bytes > stream_end - cursor)
                return false;
            hash.add_scalar(node.list_index);
            hash.add_scalar(node.node_index);
            hash.add_scalar(node.writer_tag);
            hash.add_scalar(node.writer_bytes);
            hash.add_bytes(bytes + cursor, node.writer_bytes);
            cursor += node.writer_bytes;
        }
        return cursor == stream_end;
    }

    // Canonicalize a stock CBattleSerializeStream payload using the logical
    // KHit layout captured beside it on the same simulation boundary. Unlike
    // the full Horse HgCpu snapshot, a native MoveVM palette checkpoint owns
    // only this serialized stream; Horse-only motion/timer/skeleton caches do
    // not belong to the checkpoint's future Apply operation.
    static inline uint64_t RollbackHashHgCpuSerializedPayloadCanonical(
        const uint8_t* bytes,
        size_t effective,
        const RollbackHgCpuSnapshotFrame& layout) noexcept
    {
        if (!bytes || effective == 0
            || !layout.khit_topology_ok
            || !layout.khit_topology[0].ok
            || !layout.khit_topology[1].ok)
        {
            return 0;
        }
        const size_t record0 =
            RollbackHgCpuCharaRecordBytes(&layout, 0);
        const size_t record1 =
            RollbackHgCpuCharaRecordBytes(&layout, 1);
        if (record0 > effective || record1 > effective - record0)
            return 0;

        RollbackFastHash hash {};
        hash.add_scalar(effective);
        for (size_t player = 0; player < 2; ++player)
        {
            if (!RollbackAddHgCpuCanonicalCharaRecordFromBytes(
                    hash, bytes, effective, layout, player)
                || !RollbackAddHgCpuSerializedKHitPlayerPeerState(
                    hash, bytes, effective, layout, player))
            {
                return 0;
            }
        }
        const size_t tail_base = record0 + record1;
        hash.add_bytes(bytes + tail_base, effective - tail_base);
        return hash.finish();
    }

#if !defined(HORSE_ROLLBACK_HGCPU_PURE_STATE_ONLY)
    static inline bool RollbackReadCharaPointers(
        uintptr_t image_base,
        uintptr_t& p1,
        uintptr_t& p2) noexcept
    {
        if (!image_base) return false;
        // LuxBattle_InitTwoCharaRuntimeSlots owns fixed backing storage. Do
        // not rediscover production fighters through the legacy pointer
        // globals; derive the schema-verified slots and validate their links.
        p1 = image_base + 0x47156F0;
        p2 = image_base + 0x47ACAE0;
        uint8_t slot_p1 = 0xFF;
        uint8_t slot_p2 = 0xFF;
        void* opponent_p1 = nullptr;
        void* opponent_p2 = nullptr;
        return SafeReadUInt8(reinterpret_cast<const void*>(p1 + 0x23C),
                    &slot_p1)
            && SafeReadUInt8(reinterpret_cast<const void*>(p2 + 0x23C),
                    &slot_p2)
            && SafeReadPtr(reinterpret_cast<const void*>(p1 + 0x973E8),
                    &opponent_p1)
            && SafeReadPtr(reinterpret_cast<const void*>(p2 + 0x973E8),
                    &opponent_p2)
            && slot_p1 == 0 && slot_p2 == 1
            && reinterpret_cast<uintptr_t>(opponent_p1) == p2
            && reinterpret_cast<uintptr_t>(opponent_p2) == p1;
    }

    static inline size_t RollbackSkeletonNodeBytes(
        uintptr_t image_base,
        uintptr_t vtable) noexcept
    {
        if (!image_base || vtable < image_base) return 0;
        switch (vtable - image_base)
        {
        // KAuxBone runtime types used by the frozen replay fixture. Sizes are
        // the exact arena advances in KAuxBone_InitAllTypesFromData.
        case 0x3E89880: return 0x50;  // KAuxBoneMotion
        case 0x3E89AB0: return 0x90;  // KAuxBoneFace
        case 0x3E89A40: return 0xA0;  // KAuxBoneOffsetFix
        case 0x3E88E38: return 0xF0;  // KSwayHitNode
        case 0x3E899D0: return 0xC0;  // KAuxBoneOffsetRotRate
        case 0x3E898F0: return 0xC0;  // KAuxBoneOffsetParentRotX
        case 0x3E896C0: return 0xD0;  // KAuxBoneOffsetSlerpX
        case 0x3E89A08: return 0x90;  // KAuxBoneEye
        case 0x3E89960: return 0x70;  // KAuxBoneParentRotX
        case 0x3E89688: return 0x80;  // KAuxBoneConstRate
        case 0x3E89650: return 0xD0;  // KAuxBoneOffsetConstRate
        // Spring runtime types. These are linked through chain headers and
        // use the same +0x28 intrusive next field as KAuxBone nodes.
        case 0x3E88DB8: return 0x2B0; // KSwayNode
        case 0x3E88DF8: return 0x300; // KAuxBoneOffsetSwayNode
        case 0x3E88D78: return 0x2C0; // KSwayNodeWeapon
        default: return 0;
        }
    }

    static inline uint64_t RollbackHashSkeletonRuntimeHistory(
        const RollbackHgCpuSnapshotFrame::SkeletonRuntimeHistory& history)
        noexcept
    {
        RollbackFastHash hash {};
        hash.add_scalar(history.inline_bytes.size());
        if (!history.inline_bytes.empty())
            hash.add_scalar(RollbackFastIntegrityHashBytes(
                history.inline_bytes.data(), history.inline_bytes.size()));
        for (size_t player = 0; player < 2; ++player)
        {
            hash.add_scalar(history.chara[player]);
            for (size_t list = 0; list < 2; ++list)
            {
                hash.add_scalar(history.aux_head[player][list]);
                hash.add_scalar(history.chain_head[player][list]);
            }
        }
        const auto add_nodes = [&hash](const auto& nodes) noexcept
        {
            hash.add_scalar(nodes.size());
            for (const auto& node : nodes)
            {
                hash.add_scalar(node.address);
                hash.add_scalar(node.vtable);
                hash.add_scalar(node.next);
                hash.add_scalar(node.bytes.size());
                if (!node.bytes.empty())
                    hash.add_scalar(RollbackFastIntegrityHashBytes(
                        node.bytes.data(), node.bytes.size()));
            }
        };
        add_nodes(history.aux_nodes);
        hash.add_scalar(history.chains.size());
        for (const auto& chain : history.chains)
        {
            hash.add_scalar(chain.address);
            hash.add_scalar(chain.next);
            hash.add_scalar(chain.child);
            hash.add_scalar(RollbackFastIntegrityHashBytes(
                chain.bytes.data(), chain.bytes.size()));
        }
        add_nodes(history.spring_nodes);
        return hash.finish();
    }

    static inline bool RollbackSkeletonNodeAlreadyCaptured(
        const std::vector<RollbackHgCpuSnapshotFrame::
            SkeletonRuntimeHistory::NodeImage>& nodes,
        uintptr_t address) noexcept
    {
        return std::any_of(nodes.begin(), nodes.end(),
            [address](const auto& node) { return node.address == address; });
    }

    static inline bool RollbackSkeletonChainAlreadyCaptured(
        const std::vector<RollbackHgCpuSnapshotFrame::
            SkeletonRuntimeHistory::ChainImage>& chains,
        uintptr_t address) noexcept
    {
        return std::any_of(chains.begin(), chains.end(),
            [address](const auto& chain) { return chain.address == address; });
    }

    static inline bool RollbackCaptureSkeletonNodeList(
        uintptr_t image_base,
        uintptr_t head,
        size_t maximum,
        std::vector<RollbackHgCpuSnapshotFrame::
            SkeletonRuntimeHistory::NodeImage>& nodes,
        size_t& captured_count,
        bool require_preallocated = false) noexcept
    {
        try
        {
            uintptr_t address = head;
            while (address)
            {
                if (captured_count >= maximum)
                    return false;
                for (size_t i = 0; i < captured_count; ++i)
                    if (nodes[i].address == address) return false;
                void* vtable_raw = nullptr;
                void* next_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(address),
                        &vtable_raw)
                    || !SafeReadPtr(reinterpret_cast<const void*>(address + 0x28),
                        &next_raw))
                    return false;
                const uintptr_t vtable =
                    reinterpret_cast<uintptr_t>(vtable_raw);
                const size_t bytes = RollbackSkeletonNodeBytes(
                    image_base, vtable);
                if (!bytes) return false;
                if (captured_count == nodes.size())
                {
                    if (require_preallocated
                        && nodes.size() == nodes.capacity())
                        return false;
                    nodes.emplace_back();
                }
                auto& node = nodes[captured_count];
                node.address = address;
                node.vtable = vtable;
                node.next = reinterpret_cast<uintptr_t>(next_raw);
                if (require_preallocated && bytes > node.bytes.capacity())
                    return false;
                node.bytes.resize(bytes);
                if (!SafeReadBytes(reinterpret_cast<const void*>(address),
                        node.bytes.data(), node.bytes.size()))
                    return false;
                ++captured_count;
                address = reinterpret_cast<uintptr_t>(next_raw);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static inline bool RollbackCaptureSkeletonRuntime(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame,
        bool require_preallocated = false) noexcept
    {
        auto& history = frame.skeleton_runtime;
        const size_t expected_aux_nodes = history.aux_nodes.size();
        const size_t expected_chains = history.chains.size();
        const size_t expected_spring_nodes = history.spring_nodes.size();
        history.ok = false;
        history.hash = 0;
        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2)) return false;
        history.chara[0] = p1;
        history.chara[1] = p2;
        const uintptr_t charas[2] = {p1, p2};
        static constexpr uintptr_t kAuxHeadOffsets[2] = {0x08, 0x18};
        static constexpr uintptr_t kChainHeadOffsets[2] = {0x10, 0x20};
        if (require_preallocated
            && 2 * kRollbackSkeletonRuntimeBytes
                > history.inline_bytes.capacity())
            return false;
        try
        {
            history.inline_bytes.resize(2 * kRollbackSkeletonRuntimeBytes);
            if (!require_preallocated && expected_aux_nodes == 0)
                history.aux_nodes.reserve(384);
            if (!require_preallocated && expected_chains == 0)
                history.chains.reserve(64);
            if (!require_preallocated && expected_spring_nodes == 0)
                history.spring_nodes.reserve(256);
        }
        catch (...)
        {
            return false;
        }

        size_t aux_node_count = 0;
        size_t chain_count = 0;
        size_t spring_node_count = 0;
        for (size_t player = 0; player < 2; ++player)
        {
            const uintptr_t runtime = charas[player]
                + kRollbackSkeletonRuntimeCharaOffset;
            if (!SafeReadBytes(reinterpret_cast<const void*>(runtime),
                    history.inline_bytes.data()
                        + player * kRollbackSkeletonRuntimeBytes,
                    kRollbackSkeletonRuntimeBytes))
                return false;
            for (size_t list = 0; list < 2; ++list)
            {
                void* aux_raw = nullptr;
                void* chain_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(
                        runtime + kAuxHeadOffsets[list]), &aux_raw)
                    || !SafeReadPtr(reinterpret_cast<const void*>(
                        runtime + kChainHeadOffsets[list]), &chain_raw))
                    return false;
                history.aux_head[player][list] =
                    reinterpret_cast<uintptr_t>(aux_raw);
                history.chain_head[player][list] =
                    reinterpret_cast<uintptr_t>(chain_raw);
                if (!RollbackCaptureSkeletonNodeList(image_base,
                        history.aux_head[player][list],
                        kRollbackSkeletonMaxAuxNodes, history.aux_nodes,
                        aux_node_count, require_preallocated))
                    return false;

                uintptr_t chain_address =
                    history.chain_head[player][list];
                while (chain_address)
                {
                    if (chain_count >= kRollbackSkeletonMaxChains)
                        return false;
                    for (size_t i = 0; i < chain_count; ++i)
                        if (history.chains[i].address == chain_address)
                            return false;
                    void* next_raw = nullptr;
                    void* child_raw = nullptr;
                    if (!SafeReadPtr(reinterpret_cast<const void*>(
                            chain_address + 0x40), &next_raw)
                        || !SafeReadPtr(reinterpret_cast<const void*>(
                            chain_address + 0x48), &child_raw))
                        return false;
                    if (chain_count == history.chains.size())
                    {
                        if (require_preallocated
                            && history.chains.size()
                                == history.chains.capacity())
                            return false;
                        history.chains.emplace_back();
                    }
                    auto& chain = history.chains[chain_count];
                    chain.address = chain_address;
                    chain.next = reinterpret_cast<uintptr_t>(next_raw);
                    chain.child = reinterpret_cast<uintptr_t>(child_raw);
                    if (!SafeReadBytes(reinterpret_cast<const void*>(
                            chain_address), chain.bytes.data(),
                            chain.bytes.size()))
                        return false;
                    ++chain_count;
                    if (!RollbackCaptureSkeletonNodeList(image_base,
                            reinterpret_cast<uintptr_t>(child_raw),
                            kRollbackSkeletonMaxSpringNodes,
                            history.spring_nodes, spring_node_count,
                            require_preallocated))
                        return false;
                    chain_address = reinterpret_cast<uintptr_t>(next_raw);
                }
            }
        }
        if ((expected_aux_nodes != 0
                && aux_node_count != expected_aux_nodes)
            || (expected_chains != 0 && chain_count != expected_chains)
            || (expected_spring_nodes != 0
                && spring_node_count != expected_spring_nodes))
        {
            return false;
        }
        history.hash = RollbackHashSkeletonRuntimeHistory(history);
        history.ok = history.hash != 0;
        if (!history.ok) return false;
        frame.skeleton_runtime_hash = history.hash;
        return true;
    }

    static inline bool RollbackSkeletonRuntimeTemplateReady(
        const RollbackHgCpuSnapshotFrame::SkeletonRuntimeHistory& history)
        noexcept
    {
        if (!history.chara[0] || !history.chara[1]
            || history.inline_bytes.size()
                != 2 * kRollbackSkeletonRuntimeBytes)
            return false;
        for (const auto& node : history.aux_nodes)
            if (!node.address || !node.vtable || node.bytes.empty())
                return false;
        for (const auto& chain : history.chains)
            if (!chain.address) return false;
        for (const auto& node : history.spring_nodes)
            if (!node.address || !node.vtable || node.bytes.empty())
                return false;
        return true;
    }

    // Active rollback has already accepted this round's immutable skeleton
    // topology. Re-read the fixed object images and validate pointer order;
    // do not rediscover linked lists on every Save.
    static inline bool RollbackCaptureSkeletonRuntimeFromTemplate(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        auto& history = frame.skeleton_runtime;
        if (!RollbackSkeletonRuntimeTemplateReady(history)) return false;

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2)
            || p1 != history.chara[0] || p2 != history.chara[1])
            return false;
        const uintptr_t charas[2] = {p1, p2};
        static constexpr uintptr_t kAuxHeadOffsets[2] = {0x08, 0x18};
        static constexpr uintptr_t kChainHeadOffsets[2] = {0x10, 0x20};
        for (size_t player = 0; player < 2; ++player)
        {
            const uintptr_t runtime = charas[player]
                + kRollbackSkeletonRuntimeCharaOffset;
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(runtime),
                    history.inline_bytes.data()
                        + player * kRollbackSkeletonRuntimeBytes,
                    kRollbackSkeletonRuntimeBytes))
                return false;
            for (size_t list = 0; list < 2; ++list)
            {
                void* aux_raw = nullptr;
                void* chain_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(
                        runtime + kAuxHeadOffsets[list]), &aux_raw)
                    || !SafeReadPtr(reinterpret_cast<const void*>(
                        runtime + kChainHeadOffsets[list]), &chain_raw)
                    || reinterpret_cast<uintptr_t>(aux_raw)
                        != history.aux_head[player][list]
                    || reinterpret_cast<uintptr_t>(chain_raw)
                        != history.chain_head[player][list])
                    return false;
            }
        }

        const auto capture_nodes = [image_base](auto& nodes) noexcept
        {
            for (auto& node : nodes)
            {
                void* vtable_raw = nullptr;
                void* next_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(node.address),
                        &vtable_raw)
                    || !SafeReadPtr(reinterpret_cast<const void*>(
                        node.address + 0x28), &next_raw)
                    || reinterpret_cast<uintptr_t>(vtable_raw) != node.vtable
                    || reinterpret_cast<uintptr_t>(next_raw) != node.next
                    || RollbackSkeletonNodeBytes(image_base, node.vtable)
                        != node.bytes.size()
                    || !SafeReadBytes(
                        reinterpret_cast<const void*>(node.address),
                        node.bytes.data(), node.bytes.size()))
                    return false;
            }
            return true;
        };
        if (!capture_nodes(history.aux_nodes)
            || !capture_nodes(history.spring_nodes))
            return false;
        for (auto& chain : history.chains)
        {
            void* next_raw = nullptr;
            void* child_raw = nullptr;
            if (!SafeReadPtr(reinterpret_cast<const void*>(
                    chain.address + 0x40), &next_raw)
                || !SafeReadPtr(reinterpret_cast<const void*>(
                    chain.address + 0x48), &child_raw)
                || reinterpret_cast<uintptr_t>(next_raw) != chain.next
                || reinterpret_cast<uintptr_t>(child_raw) != chain.child
                || !SafeReadBytes(
                    reinterpret_cast<const void*>(chain.address),
                    chain.bytes.data(), chain.bytes.size()))
                return false;
        }
        history.hash = RollbackHashSkeletonRuntimeHistory(history);
        history.ok = history.hash != 0;
        frame.skeleton_runtime_hash = history.hash;
        return history.ok;
    }

    static inline bool RollbackRestoreSkeletonRuntime(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame,
        bool verify_integrity = true) noexcept
    {
        const auto& history = frame.skeleton_runtime;
        if (!history.ok
            || history.inline_bytes.size()
                != 2 * kRollbackSkeletonRuntimeBytes
            || history.hash == 0
            || (verify_integrity
                && history.hash
                    != RollbackHashSkeletonRuntimeHistory(history)))
            return false;

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2)
            || p1 != history.chara[0] || p2 != history.chara[1])
            return false;
        const uintptr_t charas[2] = {p1, p2};
        static constexpr uintptr_t kAuxHeadOffsets[2] = {0x08, 0x18};
        static constexpr uintptr_t kChainHeadOffsets[2] = {0x10, 0x20};
        for (size_t player = 0; player < 2; ++player)
        {
            const uintptr_t runtime = charas[player]
                + kRollbackSkeletonRuntimeCharaOffset;
            for (size_t list = 0; list < 2; ++list)
            {
                void* aux_raw = nullptr;
                void* chain_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(
                        runtime + kAuxHeadOffsets[list]), &aux_raw)
                    || !SafeReadPtr(reinterpret_cast<const void*>(
                        runtime + kChainHeadOffsets[list]), &chain_raw)
                    || reinterpret_cast<uintptr_t>(aux_raw)
                        != history.aux_head[player][list]
                    || reinterpret_cast<uintptr_t>(chain_raw)
                        != history.chain_head[player][list])
                    return false;
            }
        }
        const auto validate_nodes = [image_base](const auto& nodes) noexcept
        {
            for (const auto& node : nodes)
            {
                void* vtable_raw = nullptr;
                void* next_raw = nullptr;
                if (!node.address || node.bytes.empty()
                    || RollbackSkeletonNodeBytes(image_base, node.vtable)
                        != node.bytes.size()
                    || !SafeReadPtr(reinterpret_cast<const void*>(node.address),
                        &vtable_raw)
                    || !SafeReadPtr(reinterpret_cast<const void*>(
                        node.address + 0x28), &next_raw)
                    || reinterpret_cast<uintptr_t>(vtable_raw) != node.vtable
                    || reinterpret_cast<uintptr_t>(next_raw) != node.next)
                    return false;
            }
            return true;
        };
        if (!validate_nodes(history.aux_nodes)
            || !validate_nodes(history.spring_nodes))
            return false;
        for (const auto& chain : history.chains)
        {
            void* next_raw = nullptr;
            void* child_raw = nullptr;
            if (!chain.address
                || !SafeReadPtr(reinterpret_cast<const void*>(
                    chain.address + 0x40), &next_raw)
                || !SafeReadPtr(reinterpret_cast<const void*>(
                    chain.address + 0x48), &child_raw)
                || reinterpret_cast<uintptr_t>(next_raw) != chain.next
                || reinterpret_cast<uintptr_t>(child_raw) != chain.child)
                return false;
        }

        bool ok = true;
        for (size_t player = 0; player < 2; ++player)
            ok &= SafeWriteBytes(reinterpret_cast<void*>(charas[player]
                    + kRollbackSkeletonRuntimeCharaOffset),
                history.inline_bytes.data()
                    + player * kRollbackSkeletonRuntimeBytes,
                kRollbackSkeletonRuntimeBytes);
        for (const auto& node : history.aux_nodes)
            ok &= SafeWriteBytes(reinterpret_cast<void*>(node.address),
                node.bytes.data(), node.bytes.size());
        for (const auto& chain : history.chains)
            ok &= SafeWriteBytes(reinterpret_cast<void*>(chain.address),
                chain.bytes.data(), chain.bytes.size());
        for (const auto& node : history.spring_nodes)
            ok &= SafeWriteBytes(reinterpret_cast<void*>(node.address),
                node.bytes.data(), node.bytes.size());
        return ok;
    }

    static inline bool RollbackKHitTopologyHasNode(
        const RollbackHgCpuSnapshotFrame::KHitCharaTopology& topology,
        uintptr_t address) noexcept
    {
        for (const auto& node : topology.nodes)
        {
            if (node.address == address)
                return true;
        }
        return false;
    }

    static inline uint64_t RollbackHashKHitTopology(
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        RollbackFastHash h {};
        h.add_scalar(frame.khit_topology_ok);
        for (const auto& topology : frame.khit_topology)
        {
            h.add_scalar(topology.ok);
            h.add_scalar(topology.chara);
            h.add_scalar(topology.node_stream_bytes);
            h.add_bytes(
                topology.list_control.data(),
                topology.list_control.size());
            const size_t node_count = topology.nodes.size();
            h.add_scalar(node_count);
            for (const auto& node : topology.nodes)
            {
                h.add_scalar(node.address);
                h.add_scalar(node.stream_start_local);
                h.add_scalar(node.writer_bytes);
                h.add_scalar(node.node_index);
                h.add_scalar(node.list_index);
                h.add_scalar(node.writer_tag);
                h.add_bytes(node.bytes.data(), node.bytes.size());
            }
        }
        return h.finish();
    }

    static inline uint64_t RollbackHashMotionBankHistory(
        const RollbackHgCpuSnapshotFrame::MotionBankHistory& history) noexcept
    {
        RollbackFastHash h {};
        h.add_scalar(history.ok);
        h.add_bytes(history.chara, sizeof(history.chara));
        h.add_bytes(history.bank, sizeof(history.bank));
        h.add_bytes(history.current, sizeof(history.current));
        h.add_bytes(history.provider, sizeof(history.provider));
        h.add_bytes(history.current_slot, sizeof(history.current_slot));
        h.add_bytes(history.provider_slot, sizeof(history.provider_slot));
        h.add_bytes(history.buffer, sizeof(history.buffer));
        const size_t control_bytes = history.control_bytes.size();
        h.add_scalar(control_bytes);
        h.add_bytes(history.control_bytes.data(), control_bytes);
        const size_t payload_bytes = history.bytes.size();
        h.add_scalar(payload_bytes);
        h.add_bytes(history.bytes.data(), payload_bytes);
        return h.finish();
    }

    static inline uint64_t RollbackHashMotionTailHistory(
        const RollbackHgCpuSnapshotFrame::MotionTailHistory& history) noexcept
    {
        RollbackFastHash h {};
        h.add_scalar(history.ok);
        h.add_bytes(history.chara, sizeof(history.chara));
        const size_t payload_bytes = history.bytes.size();
        h.add_scalar(payload_bytes);
        h.add_bytes(history.bytes.data(), payload_bytes);
        return h.finish();
    }

    static inline uint64_t RollbackHashTimerNodeHistory(
        const RollbackHgCpuSnapshotFrame::TimerNodeHistory& history) noexcept
    {
        RollbackFastHash h {};
        h.add_scalar(history.ok);
        h.add_scalar(history.timer_config);
        h.add_scalar(history.root);
        h.add_scalar(history.backing);
        h.add_scalar(history.indexed_table);
        h.add_bytes(history.indexed_root, sizeof(history.indexed_root));
        h.add_bytes(history.indexed_vtable, sizeof(history.indexed_vtable));
        h.add_bytes(history.indexed_writer, sizeof(history.indexed_writer));
        h.add_bytes(history.indexed_captured, sizeof(history.indexed_captured));
        h.add_bytes(
            history.indexed_object_captured,
            sizeof(history.indexed_object_captured));
        for (size_t i = 0; i < std::size(history.indexed_object_bytes); ++i)
        {
            if (!history.indexed_object_captured[i]) continue;
            h.add_scalar(i);
            const auto& bytes = history.indexed_object_bytes[i];
            h.add_bytes(bytes.data(), bytes.size());
        }
        h.add_scalar(history.indexed_nonzero_count);
        h.add_scalar(history.indexed_captured_count);
        h.add_scalar(history.indexed_object_captured_count);
        h.add_bytes(history.child, sizeof(history.child));
        const size_t node_count = history.nodes.size();
        h.add_scalar(node_count);
        for (const auto& node : history.nodes)
        {
            h.add_scalar(node.root);
            h.add_scalar(node.backing);
            const size_t node_root_bytes = node.root_bytes.size();
            h.add_scalar(node_root_bytes);
            h.add_bytes(node.root_bytes.data(), node_root_bytes);
            const size_t node_backing_bytes = node.backing_bytes.size();
            h.add_scalar(node_backing_bytes);
            h.add_bytes(node.backing_bytes.data(), node_backing_bytes);
        }
        return h.finish();
    }

    static inline bool RollbackCaptureKHitTopology(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame,
        bool require_preallocated = false) noexcept
    {
        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;

        static constexpr uintptr_t kListHeads[] = {
            0x44478, 0x44498, 0x444B8,
        };
        const uintptr_t charas[2] = {p1, p2};
        bool ok = true;
        frame.khit_topology_ok = false;
        for (auto& topology : frame.khit_topology)
            topology.clear();

        for (size_t player = 0; player < 2; ++player)
        {
            auto& topology = frame.khit_topology[player];
            bool player_ok = true;
            size_t stream_cursor = kRollbackHgCpuHitAreaLocalStart
                + kRollbackHgCpuHitAreaFixedBytes;
            topology.chara = charas[player];
            auto* chara = reinterpret_cast<uint8_t*>(charas[player]);
            if (!SafeReadBytes(
                    chara + kRollbackKHitListControlStart,
                    topology.list_control.data(),
                    topology.list_control.size()))
            {
                player_ok = false;
                ok = false;
                continue;
            }

            for (size_t list_index = 0;
                 list_index < (sizeof(kListHeads) / sizeof(kListHeads[0]));
                 ++list_index)
            {
                const uintptr_t head_offset = kListHeads[list_index];
                void* node_raw = nullptr;
                if (!SafeReadPtr(chara + head_offset, &node_raw))
                {
                    player_ok = false;
                    ok = false;
                    continue;
                }

                for (int node_index = 0;
                     node_raw && node_index < 256;
                     ++node_index)
                {
                    const uintptr_t node_address =
                        reinterpret_cast<uintptr_t>(node_raw);
                    if (RollbackKHitTopologyHasNode(
                            topology, node_address))
                    {
                        player_ok = false;
                        ok = false;
                        break;
                    }

                    RollbackHgCpuSnapshotFrame::KHitNodeImage image {};
                    image.address = node_address;
                    image.list_index = static_cast<uint8_t>(list_index);
                    image.node_index = static_cast<uint16_t>(node_index);
                    if (!SafeReadBytes(
                            node_raw,
                            image.bytes.data(),
                            image.bytes.size()))
                    {
                        player_ok = false;
                        ok = false;
                        break;
                    }

                    uintptr_t vtable = 0;
                    void* next_raw = nullptr;
                    uint8_t tag = 0xff;
                    std::memcpy(
                        &vtable,
                        image.bytes.data() + 0x00,
                        sizeof(vtable));
                    std::memcpy(
                        &tag,
                        image.bytes.data() + 0x16,
                        sizeof(tag));
                    std::memcpy(
                        &next_raw,
                        image.bytes.data() + 0x18,
                        sizeof(next_raw));
                    const size_t writer_bytes =
                        RollbackKHitSnapshotWriterBytes(
                            tag, vtable, image_base);
                    image.writer_tag = tag <= 2 ? tag
                        : static_cast<uint8_t>(
                            writer_bytes == 0x26 ? 0
                            : (writer_bytes == 0x42 ? 1
                               : (writer_bytes == 0x32 ? 2 : tag)));
                    image.writer_bytes = writer_bytes;
                    image.stream_start_local = stream_cursor;
                    topology.node_stream_bytes += writer_bytes;
                    stream_cursor += writer_bytes;
                    if (require_preallocated
                        && topology.nodes.size()
                            == topology.nodes.capacity())
                    {
                        player_ok = false;
                        ok = false;
                        break;
                    }
                    try
                    {
                        topology.nodes.push_back(image);
                    }
                    catch (...)
                    {
                        player_ok = false;
                        ok = false;
                        break;
                    }

                    if (next_raw == node_raw)
                    {
                        player_ok = false;
                        ok = false;
                        break;
                    }
                    node_raw = next_raw;
                    if (node_index == 255 && node_raw)
                    {
                        player_ok = false;
                        ok = false;
                    }
                }
            }
            topology.ok = player_ok;
        }

        frame.khit_topology_ok =
            ok && frame.khit_topology[0].ok && frame.khit_topology[1].ok;
        frame.khit_topology_hash = RollbackHashKHitTopology(frame);
        return frame.khit_topology_ok;
    }

    static inline bool RollbackRestoreKHitTopology(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        if (!image_base || !frame.khit_topology_ok)
            return false;
        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2)) return false;
        static constexpr uintptr_t kListHeads[] = {
            0x44478, 0x44498, 0x444B8,
        };
        const uintptr_t charas[2] = {p1, p2};
        bool ok = true;
        for (size_t player = 0; player < 2; ++player)
        {
            const auto& saved = frame.khit_topology[player];
            if (!saved.ok || !charas[player])
            {
                ok = false;
                continue;
            }
            size_t saved_index = 0;
            for (size_t list_index = 0;
                 list_index < std::size(kListHeads); ++list_index)
            {
                void* node_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(
                        charas[player] + kListHeads[list_index]),
                        &node_raw))
                {
                    ok = false;
                    continue;
                }
                for (uint16_t node_index = 0; node_raw; ++node_index)
                {
                    if (node_index >= 256
                        || saved_index >= saved.nodes.size())
                    {
                        ok = false;
                        break;
                    }
                    const auto& source = saved.nodes[saved_index++];
                    std::array<uint8_t, kRollbackKHitNodeImageBytes>
                        current_bytes {};
                    if (!SafeReadBytes(node_raw, current_bytes.data(),
                            current_bytes.size()))
                    {
                        ok = false;
                        break;
                    }
                    uintptr_t vtable = 0;
                    void* next_raw = nullptr;
                    uint8_t tag = 0xff;
                    std::memcpy(&vtable, current_bytes.data(), sizeof(vtable));
                    std::memcpy(&tag, current_bytes.data() + 0x16,
                        sizeof(tag));
                    std::memcpy(&next_raw, current_bytes.data() + 0x18,
                        sizeof(next_raw));
                    const size_t writer_bytes =
                        RollbackKHitSnapshotWriterBytes(
                            tag, vtable, image_base);
                    const uint8_t writer_tag = tag <= 2 ? tag
                        : static_cast<uint8_t>(
                            writer_bytes == 0x26 ? 0
                            : (writer_bytes == 0x42 ? 1
                               : (writer_bytes == 0x32 ? 2 : tag)));
                    if (source.list_index != list_index
                        || source.node_index != node_index
                        || source.writer_tag != writer_tag
                        || source.writer_bytes != writer_bytes)
                    {
                        ok = false;
                        break;
                    }
                    for (size_t serialized = 0;
                         serialized < source.writer_bytes;)
                    {
                        uintptr_t node_offset = 0;
                        size_t contiguous = 0;
                        if (!RollbackKHitSourceOffsetForSerializedOffset(
                                source.writer_tag, serialized,
                                &node_offset, &contiguous)
                            || contiguous == 0
                            || node_offset >= source.bytes.size())
                        {
                            ok = false;
                            break;
                        }
                        const size_t remaining =
                            source.writer_bytes - serialized;
                        const size_t bytes =
                            (std::min)(contiguous, remaining);
                        if (bytes > source.bytes.size() - node_offset
                            || !SafeWriteBytes(
                                reinterpret_cast<void*>(
                                    reinterpret_cast<uintptr_t>(node_raw)
                                        + node_offset),
                                source.bytes.data() + node_offset,
                                bytes))
                        {
                            ok = false;
                            break;
                        }
                        serialized += bytes;
                    }
                    if (!ok) break;
                    if (next_raw == node_raw)
                    {
                        ok = false;
                        break;
                    }
                    node_raw = next_raw;
                }
                if (!ok) break;
            }
            if (saved_index != saved.nodes.size()) ok = false;
        }
        return ok;
    }

    static inline bool RollbackSafeInvokeHgCpuExec(
        RollbackHgCpuExecFn fn,
        RollbackHgCpuBufferShim* shim,
        NativeCallFault* fault = nullptr) noexcept
    {
        if (fault) *fault = NativeCallFault{};
        if (!fn || !shim) return false;
        __try
        {
            fn(shim);
            return true;
        }
        __except (CaptureNativeCallFault(
            GetExceptionCode(), GetExceptionInformation(), fault))
        {
            return false;
        }
    }

    static inline RollbackHgCpuSnapshotReport RollbackInvokeHgCpuSnapshot(
        uintptr_t image_base,
        uintptr_t function_rva,
        std::vector<uint8_t>& bytes,
        bool writing_snapshot) noexcept
    {
        RollbackHgCpuSnapshotReport report{};
        report.failure = "ok";
        report.capacity = bytes.size();
        report.image_base = image_base;

        if (!image_base)
        {
            report.failure = "native-binding-not-ready";
            return report;
        }

        report.function_address = image_base + function_rva;
        report.context_ready = RollbackReadCharaPointers(
            image_base, report.chara_p1, report.chara_p2);
        if (!report.context_ready)
        {
            report.failure = "battle-context-not-ready";
            return report;
        }

        if (bytes.size() != kRollbackHgCpuSnapshotBytes)
        {
            report.failure = "bad-buffer-size";
            return report;
        }

        RollbackHgCpuBufferShim shim {};
        shim.retarget(bytes.data(), bytes.size());
        const auto fn = reinterpret_cast<RollbackHgCpuExecFn>(
            report.function_address);
        report.ok = RollbackSafeInvokeHgCpuExec(fn, &shim, &report.fault);
        report.cursor = shim.cursor();
        report.overflowed = shim.overflowed();
        if (!report.ok)
        {
            report.failure = "native-call-fault";
            return report;
        }
        if (report.overflowed)
        {
            report.ok = false;
            report.failure = writing_snapshot
                ? "native-write-overflow"
                : "native-read-overflow";
            return report;
        }
        if (report.cursor == 0)
        {
            report.ok = false;
            report.failure = writing_snapshot
                ? "native-wrote-zero-bytes"
                : "native-read-zero-bytes";
            return report;
        }

        const size_t hash_bytes = report.cursor <= bytes.size()
            ? report.cursor
            : bytes.size();
        report.hash = RollbackFastIntegrityHashBytes(
            bytes.data(), hash_bytes);
        return report;
    }

    static inline int32_t RollbackHgCpuLocalForCharaOffset(
        uintptr_t chara_offset) noexcept
    {
        if (chara_offset >= 0x10 && chara_offset < 0x90)
            return static_cast<int32_t>(chara_offset - 0x10);
        if (chara_offset >= 0x90 && chara_offset < 0x35A0)
            return 0x80 + static_cast<int32_t>(chara_offset - 0x90);
        return -1;
    }

    static inline bool RollbackRestoreHgCpuPostReadExactFields(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        if (!image_base || frame.bytes.size() != kRollbackHgCpuSnapshotBytes)
            return false;

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;

        static constexpr size_t kAiResetSlotLocalStart = 0x794C;
        static constexpr size_t kAiResetSlotLocalEnd = kAiResetSlotLocalStart + 0x60;
        static constexpr size_t kAiPaletteScalarLocalStart =
            kAiResetSlotLocalStart + 0x18;
        static constexpr size_t kAiPaletteScalarBytes =
            kAiResetSlotLocalEnd - kAiPaletteScalarLocalStart;
        static constexpr uintptr_t kAiPaletteScalarCharaOffset =
            0x971E8 + 0x18;
        static constexpr size_t kHgCpuHitAreaFixedValueBytes = 0x400;

        auto khit_source_offset_for_serialized_offset =
            [](uint8_t tag,
               size_t serialized_offset,
               uintptr_t* out_node_offset,
               size_t* out_contiguous_bytes) noexcept -> bool
        {
            if (out_node_offset) *out_node_offset = 0;
            if (out_contiguous_bytes) *out_contiguous_bytes = 0;
            if (serialized_offset < 2)
            {
                if (out_node_offset)
                    *out_node_offset = 0x14 + serialized_offset;
                if (out_contiguous_bytes)
                    *out_contiguous_bytes = 2 - serialized_offset;
                return true;
            }

            const size_t rel = serialized_offset - 2;
            if (tag == 0)
            {
                if (rel < 0x10)
                {
                    if (out_node_offset) *out_node_offset = 0x50 + rel;
                    if (out_contiguous_bytes)
                        *out_contiguous_bytes = 0x10 - rel;
                    return true;
                }
                if (rel < 0x20)
                {
                    if (out_node_offset)
                        *out_node_offset = 0x60 + (rel - 0x10);
                    if (out_contiguous_bytes)
                        *out_contiguous_bytes = 0x20 - rel;
                    return true;
                }
                if (rel < 0x24)
                {
                    if (out_node_offset)
                        *out_node_offset = 0x70 + (rel - 0x20);
                    if (out_contiguous_bytes)
                        *out_contiguous_bytes = 0x24 - rel;
                    return true;
                }
            }
            else if (tag == 1)
            {
                if (rel < 0x40)
                {
                    if (out_node_offset) *out_node_offset = 0x50 + rel;
                    if (out_contiguous_bytes)
                        *out_contiguous_bytes = 0x40 - rel;
                    return true;
                }
            }
            else if (tag == 2)
            {
                if (rel < 0x30)
                {
                    if (out_node_offset) *out_node_offset = 0x60 + rel;
                    if (out_contiguous_bytes)
                        *out_contiguous_bytes = 0x30 - rel;
                    return true;
                }
            }
            return false;
        };

        const uintptr_t charas[2] = {p1, p2};
        bool ok = true;
        for (size_t player = 0; player < 2; ++player)
        {
            uint8_t* chara = reinterpret_cast<uint8_t*>(charas[player]);
            size_t base = 0;
            if (!RollbackHgCpuSnapshotCharaBase(frame, player, base))
            {
                ok = false;
                continue;
            }

            auto restore_chara_bytes =
                [&](uintptr_t chara_offset, size_t bytes) noexcept
            {
                const int32_t local =
                    RollbackHgCpuLocalForCharaOffset(chara_offset);
                if (local < 0)
                {
                    ok = false;
                    return;
                }
                const size_t src_offset =
                    base + static_cast<size_t>(local);
                if (src_offset + bytes > frame.bytes.size())
                {
                    ok = false;
                    return;
                }
                ok &= SafeWriteBytes(
                    chara + chara_offset,
                    frame.bytes.data() + src_offset,
                    bytes);
            };

            restore_chara_bytes(0x244, 4);
            restore_chara_bytes(0x248, 4);
            restore_chara_bytes(0x324, 4);
            restore_chara_bytes(0x1FF8, 0x88);

            const size_t ai_src = base + kAiPaletteScalarLocalStart;
            if (ai_src + kAiPaletteScalarBytes > frame.bytes.size())
            {
                ok = false;
                continue;
            }
            ok &= SafeWriteBytes(
                chara + kAiPaletteScalarCharaOffset,
                frame.bytes.data() + ai_src,
                kAiPaletteScalarBytes);

            const size_t fixed_src = base + kRollbackHgCpuHitAreaLocalStart;
            if (fixed_src + kHgCpuHitAreaFixedValueBytes > frame.bytes.size())
            {
                ok = false;
                continue;
            }
            ok &= SafeWriteBytes(
                chara + kRollbackCharaHitAreaFixedStart,
                frame.bytes.data() + fixed_src,
                kHgCpuHitAreaFixedValueBytes);

            struct ListSpec
            {
                uintptr_t head_offset;
            };
            static constexpr ListSpec kLists[] = {
                {0x44478}, {0x44498}, {0x444B8},
            };
            size_t cursor = kRollbackHgCpuHitAreaLocalStart
                + kRollbackHgCpuHitAreaFixedBytes;
            for (const ListSpec& list : kLists)
            {
                void* node_raw = nullptr;
                if (!SafeReadPtr(chara + list.head_offset, &node_raw))
                {
                    ok = false;
                    continue;
                }
                for (int node_index = 0;
                     node_raw && node_index < 256;
                     ++node_index)
                {
                    uint8_t* node = static_cast<uint8_t*>(node_raw);
                    void* vtable_raw = nullptr;
                    void* next_raw = nullptr;
                    uint8_t tag = 0xff;
                    if (!SafeReadPtr(node + 0x00, &vtable_raw)
                        || !SafeReadUInt8(node + 0x16, &tag)
                        || !SafeReadPtr(node + 0x18, &next_raw))
                    {
                        ok = false;
                        break;
                    }

                    const size_t bytes = RollbackKHitSnapshotWriterBytes(
                        tag, reinterpret_cast<uintptr_t>(vtable_raw),
                        image_base);
                    const uint8_t writer_tag =
                        (tag <= 2) ? tag
                            : (bytes == 0x26 ? 0
                               : (bytes == 0x42 ? 1
                                  : (bytes == 0x32 ? 2 : tag)));
                    const size_t expected_src = base + cursor;
                    if (expected_src + bytes > frame.bytes.size())
                    {
                        ok = false;
                        break;
                    }
                    const uint8_t* expected =
                        frame.bytes.data() + expected_src;

                    size_t rel = 0;
                    while (rel < bytes)
                    {
                        uintptr_t node_offset = 0;
                        size_t contiguous = 0;
                        if (!khit_source_offset_for_serialized_offset(
                                writer_tag, rel, &node_offset, &contiguous)
                            || contiguous == 0)
                        {
                            ok = false;
                            break;
                        }
                        size_t chunk = contiguous;
                        if (chunk > bytes - rel)
                            chunk = bytes - rel;
                        ok &= SafeWriteBytes(
                            node + node_offset, expected + rel, chunk);
                        rel += chunk;
                    }

                    cursor += bytes;
                    if (next_raw == node_raw)
                    {
                        ok = false;
                        break;
                    }
                    node_raw = next_raw;
                    if (node_index == 255 && node_raw)
                        ok = false;
                }
            }
        }
        return ok;
    }

    static constexpr size_t kRollbackMotionBankSnapshotLocals[] = {
        kRollbackPrimaryMotionBankSnapshotLocal,
        kRollbackSecondaryMotionBankSnapshotLocal,
    };

    static inline size_t RollbackMotionBankControlTotalBytes() noexcept
    {
        return kRollbackMotionBankPlayerCount
            * kRollbackMotionBankCount
            * kRollbackMotionBankControlBytes;
    }

    static inline size_t RollbackMotionBankControlOffset(
        size_t player,
        size_t bank) noexcept
    {
        return (player * kRollbackMotionBankCount + bank)
            * kRollbackMotionBankControlBytes;
    }

    static inline bool RollbackCaptureMotionBankHistory(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame,
        bool require_preallocated = false) noexcept
    {
        auto& history = frame.motion_banks;
        history.ok = false;
        history.hash = 0;
        std::memset(history.chara, 0, sizeof(history.chara));
        std::memset(history.bank, 0, sizeof(history.bank));
        std::memset(history.current, 0, sizeof(history.current));
        std::memset(history.provider, 0, sizeof(history.provider));
        std::memset(history.buffer, 0, sizeof(history.buffer));
        for (auto& per_player : history.current_slot)
            for (int& slot : per_player) slot = -1;
        for (auto& per_player : history.provider_slot)
            for (int& slot : per_player) slot = -1;

        const size_t required_bytes = RollbackMotionBankTotalBytes();
        const size_t required_control_bytes =
            RollbackMotionBankControlTotalBytes();
        if (require_preallocated
            && (required_bytes > history.bytes.capacity()
                || required_control_bytes
                    > history.control_bytes.capacity()))
            return false;
        try
        {
            history.bytes.resize(required_bytes);
            history.control_bytes.resize(required_control_bytes);
        }
        catch (const std::bad_alloc&)
        {
            history.bytes.clear();
            history.control_bytes.clear();
            return false;
        }

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[kRollbackMotionBankPlayerCount] = {p1, p2};

        bool ok = true;
        for (size_t player = 0;
             player < kRollbackMotionBankPlayerCount;
             ++player)
        {
            history.chara[player] = charas[player];
            if (!charas[player])
            {
                ok = false;
                continue;
            }

            auto* chara = reinterpret_cast<uint8_t*>(charas[player]);
            for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
            {
                const auto& spec = kRollbackMotionBankSpecs[bank];
                auto* bank_ptr = chara + spec.chara_offset;
                history.bank[player][bank] =
                    reinterpret_cast<uintptr_t>(bank_ptr);
                const size_t control_dst =
                    RollbackMotionBankControlOffset(player, bank);
                if (control_dst + kRollbackMotionBankControlBytes
                        > history.control_bytes.size()
                    || !SafeReadBytes(
                        bank_ptr,
                        history.control_bytes.data() + control_dst,
                        kRollbackMotionBankControlBytes))
                {
                    ok = false;
                }

                void* current_raw = nullptr;
                if (SafeReadPtr(bank_ptr + 0x28, &current_raw)
                    && current_raw)
                {
                    history.current[player][bank] =
                        reinterpret_cast<uintptr_t>(current_raw);
                }
                void* provider_raw = nullptr;
                if (SafeReadPtr(bank_ptr + 0x30, &provider_raw)
                    && provider_raw)
                {
                    history.provider[player][bank] =
                        reinterpret_cast<uintptr_t>(provider_raw);
                }

                int current_slot = -1;
                int provider_slot = -1;
                for (size_t buffer = 0;
                     buffer < kRollbackMotionBankBufferCount;
                     ++buffer)
                {
                    void* buffer_raw = nullptr;
                    if (!SafeReadPtr(
                            bank_ptr + 0x08
                                + buffer * sizeof(void*),
                            &buffer_raw)
                        || !buffer_raw)
                    {
                        ok = false;
                        continue;
                    }

                    const uintptr_t buffer_addr =
                        reinterpret_cast<uintptr_t>(buffer_raw);
                    history.buffer[player][bank][buffer] = buffer_addr;
                    if (buffer_addr == history.current[player][bank])
                        current_slot = static_cast<int>(buffer);
                    if (buffer_addr == history.provider[player][bank])
                        provider_slot = static_cast<int>(buffer);

                    const size_t dst = RollbackMotionBankByteOffset(
                        player, bank, buffer);
                    if (dst + spec.bytes > history.bytes.size()
                        || !SafeReadBytes(
                            reinterpret_cast<const void*>(buffer_addr),
                            history.bytes.data() + dst,
                            spec.bytes))
                    {
                        ok = false;
                    }
                }
                history.current_slot[player][bank] = current_slot;
                history.provider_slot[player][bank] = provider_slot;
                if (current_slot < 0)
                    ok = false;
                if (provider_slot < 0)
                    ok = false;
            }
        }

        history.ok = ok;
        if (ok)
        {
            history.hash = RollbackHashMotionBankHistory(history);
        }
        else
        {
            history.hash = 0;
        }
        frame.motion_bank_hash = history.hash;
        return ok;
    }

    static inline bool RollbackMotionBankRestorePreflight(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        const auto& history = frame.motion_banks;
        if (!history.ok || history.bytes.empty()
            || history.control_bytes.empty())
            return false;

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[kRollbackMotionBankPlayerCount] = {p1, p2};

        for (size_t player = 0;
             player < kRollbackMotionBankPlayerCount;
             ++player)
        {
            if (charas[player] != history.chara[player])
                return false;
            auto* chara = reinterpret_cast<uint8_t*>(charas[player]);

            for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
            {
                const auto& spec = kRollbackMotionBankSpecs[bank];
                auto* bank_ptr = chara + spec.chara_offset;
                const size_t control_src =
                    RollbackMotionBankControlOffset(player, bank);
                if (control_src + kRollbackMotionBankControlBytes
                        > history.control_bytes.size())
                    return false;

                for (size_t buffer = 0;
                     buffer < kRollbackMotionBankBufferCount;
                     ++buffer)
                {
                    void* live_raw = nullptr;
                    if (!SafeReadPtr(
                            bank_ptr + 0x08
                                + buffer * sizeof(void*),
                            &live_raw)
                        || !live_raw
                        || reinterpret_cast<uintptr_t>(live_raw)
                            != history.buffer[player][bank][buffer])
                        return false;

                    const size_t src = RollbackMotionBankByteOffset(
                        player, bank, buffer);
                    if (src + spec.bytes > history.bytes.size()) return false;
                }
            }
        }
        return true;
    }

    static inline bool RollbackRestoreMotionBankHistory(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        if (!RollbackMotionBankRestorePreflight(image_base, frame))
            return false;
        const auto& history = frame.motion_banks;
        for (size_t player = 0;
             player < kRollbackMotionBankPlayerCount; ++player)
        {
            auto* chara = reinterpret_cast<uint8_t*>(history.chara[player]);
            for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
            {
                const auto& spec = kRollbackMotionBankSpecs[bank];
                auto* bank_ptr = chara + spec.chara_offset;
                const size_t control_src =
                    RollbackMotionBankControlOffset(player, bank);
                if (!SafeWriteBytes(bank_ptr,
                        history.control_bytes.data() + control_src,
                        kRollbackMotionBankControlBytes))
                    return false;
                for (size_t buffer = 0;
                     buffer < kRollbackMotionBankBufferCount; ++buffer)
                {
                    const size_t src = RollbackMotionBankByteOffset(
                        player, bank, buffer);
                    if (!SafeWriteBytes(
                            reinterpret_cast<void*>(
                                history.buffer[player][bank][buffer]),
                            history.bytes.data() + src, spec.bytes))
                        return false;
                }
            }
        }
        return true;
    }

    static inline bool RollbackCaptureMoveVMMotionTail(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame,
        bool require_preallocated = false) noexcept
    {
        auto& history = frame.motion_tail;
        history.ok = false;
        history.hash = 0;
        std::memset(history.chara, 0, sizeof(history.chara));

        const size_t required_bytes = kRollbackMoveVMMotionTailBytes * 2;
        if (require_preallocated
            && required_bytes > history.bytes.capacity())
            return false;
        try
        {
            history.bytes.resize(required_bytes);
        }
        catch (const std::bad_alloc&)
        {
            history.bytes.clear();
            return false;
        }

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[2] = {p1, p2};

        bool ok = true;
        for (size_t player = 0; player < 2; ++player)
        {
            history.chara[player] = charas[player];
            if (!charas[player])
            {
                ok = false;
                continue;
            }
            const auto* src = reinterpret_cast<const uint8_t*>(
                charas[player] + kRollbackMoveVMMotionTailCharaOffset);
            uint8_t* dst = history.bytes.data()
                + player * kRollbackMoveVMMotionTailBytes;
            ok &= SafeReadBytes(src, dst, kRollbackMoveVMMotionTailBytes);
        }

        history.ok = ok;
        if (ok)
        {
            history.hash = RollbackHashMotionTailHistory(history);
        }
        else
        {
            history.hash = 0;
        }
        frame.motion_tail_hash = history.hash;
        return ok;
    }

    static inline bool RollbackRestoreMoveVMMotionTail(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        const auto& history = frame.motion_tail;
        if (!history.ok
            || history.bytes.size()
                != kRollbackMoveVMMotionTailBytes * 2)
        {
            return false;
        }

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[2] = {p1, p2};

        bool ok = true;
        for (size_t player = 0; player < 2; ++player)
        {
            if (charas[player] != history.chara[player])
            {
                ok = false;
                continue;
            }
            auto* dst = reinterpret_cast<uint8_t*>(
                charas[player] + kRollbackMoveVMMotionTailCharaOffset);
            const uint8_t* src = history.bytes.data()
                + player * kRollbackMoveVMMotionTailBytes;
            ok &= SafeWriteBytes(dst, src, kRollbackMoveVMMotionTailBytes);
        }
        return ok;
    }

    static inline bool RollbackCaptureSecondaryEventStack(
        uintptr_t image_base,
        RollbackSecondaryEventStackHistory& history) noexcept
    {
        history.recycle_for_capture();

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[2] = {p1, p2};

        bool ok = true;
        for (size_t player = 0; player < 2; ++player)
        {
            history.chara[player] = charas[player];
            auto* stack = reinterpret_cast<uint8_t*>(
                charas[player] + kRollbackSecondaryEventStackCharaOffset);
            if (!charas[player]
                || !SafeReadBytes(
                    stack,
                    history.bytes[player].data(),
                    history.bytes[player].size()))
            {
                ok = false;
                continue;
            }

            void* table_header_raw = nullptr;
            void* event_headers_raw = nullptr;
            void* event_payloads_raw = nullptr;
            if (!SafeReadPtr(
                    stack + kRollbackSecondaryEventPointerBlockOffset,
                    &table_header_raw)
                || !SafeReadPtr(
                    stack + kRollbackSecondaryEventPointerBlockOffset + 0x08,
                    &event_headers_raw)
                || !SafeReadPtr(
                    stack + kRollbackSecondaryEventPointerBlockOffset + 0x10,
                    &event_payloads_raw)
                || !table_header_raw || !event_headers_raw
                || !event_payloads_raw)
            {
                ok = false;
                continue;
            }
            history.table_header[player] =
                reinterpret_cast<uintptr_t>(table_header_raw);
            history.event_headers[player] =
                reinterpret_cast<uintptr_t>(event_headers_raw);
            history.event_payloads[player] =
                reinterpret_cast<uintptr_t>(event_payloads_raw);

            int32_t count = 0;
            if (!SafeReadBytes(
                    reinterpret_cast<const uint8_t*>(table_header_raw)
                        + kRollbackSecondaryEventHeaderCountOffset,
                    &count,
                    sizeof(count))
                || count < 0
                || count > static_cast<int32_t>(
                    kRollbackSecondaryEventMaxHeaders))
            {
                history.headers_truncated[player] =
                    count > static_cast<int32_t>(
                        kRollbackSecondaryEventMaxHeaders);
                ok = false;
                continue;
            }
            history.header_count[player] = static_cast<uint32_t>(count);
            for (uint32_t index = 0;
                 index < history.header_count[player];
                 ++index)
            {
                if (!SafeReadBytes(
                        reinterpret_cast<const uint8_t*>(event_headers_raw)
                            + static_cast<size_t>(index)
                                * kRollbackSecondaryEventHeaderStride
                            + kRollbackSecondaryEventHeaderCursorOffset,
                        &history.header_cursors[player][index],
                        sizeof(uint16_t)))
                {
                    ok = false;
                    break;
                }
            }
        }

        history.ok = ok;
        history.hash = ok
            ? RollbackHashSecondaryEventStackHistory(history) : 0;
        return ok;
    }

    static inline bool RollbackCaptureSecondaryEventStack(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        const bool ok = RollbackCaptureSecondaryEventStack(
            image_base, frame.secondary_event_stack);
        frame.secondary_event_stack_hash =
            frame.secondary_event_stack.hash;
        return ok;
    }

    static inline bool RollbackSecondaryEventStackRestorePreflight(
        uintptr_t image_base,
        const RollbackSecondaryEventStackHistory& history) noexcept
    {
        if (!history.ok) return false;

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[2] = {p1, p2};

        for (size_t player = 0; player < 2; ++player)
        {
            if (charas[player] != history.chara[player])
                return false;
            auto* stack = reinterpret_cast<uint8_t*>(
                charas[player] + kRollbackSecondaryEventStackCharaOffset);

            void* table_header_raw = nullptr;
            void* event_headers_raw = nullptr;
            void* event_payloads_raw = nullptr;
            const bool identity_ok =
                SafeReadPtr(
                    stack + kRollbackSecondaryEventPointerBlockOffset,
                    &table_header_raw)
                && SafeReadPtr(
                    stack + kRollbackSecondaryEventPointerBlockOffset + 0x08,
                    &event_headers_raw)
                && SafeReadPtr(
                    stack + kRollbackSecondaryEventPointerBlockOffset + 0x10,
                    &event_payloads_raw)
                && reinterpret_cast<uintptr_t>(table_header_raw)
                    == history.table_header[player]
                && reinterpret_cast<uintptr_t>(event_headers_raw)
                    == history.event_headers[player]
                && reinterpret_cast<uintptr_t>(event_payloads_raw)
                    == history.event_payloads[player];
            int32_t header_count = -1;
            if (!identity_ok
                || !SafeReadBytes(
                    reinterpret_cast<const uint8_t*>(table_header_raw)
                        + kRollbackSecondaryEventHeaderCountOffset,
                    &header_count, sizeof(header_count))
                || header_count < 0
                || static_cast<uint32_t>(header_count)
                    != history.header_count[player])
            {
                return false;
            }
        }
        return true;
    }

    static inline bool RollbackSecondaryEventStackRestorePreflight(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        return RollbackSecondaryEventStackRestorePreflight(
            image_base, frame.secondary_event_stack);
    }

    static inline bool RollbackRestoreSecondaryEventStack(
        uintptr_t image_base,
        const RollbackSecondaryEventStackHistory& history) noexcept
    {
        if (!RollbackSecondaryEventStackRestorePreflight(
                image_base, history))
            return false;

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[2] = {p1, p2};

        bool ok = true;
        for (size_t player = 0; player < 2; ++player)
        {
            auto* stack = reinterpret_cast<uint8_t*>(
                charas[player] + kRollbackSecondaryEventStackCharaOffset);
            void* event_headers_raw = nullptr;
            if (!SafeReadPtr(
                    stack + kRollbackSecondaryEventPointerBlockOffset + 0x08,
                    &event_headers_raw)
                || reinterpret_cast<uintptr_t>(event_headers_raw)
                    != history.event_headers[player])
            {
                return false;
            }

            ok &= SafeWriteBytes(
                stack,
                history.bytes[player].data(),
                kRollbackSecondaryEventSlotsBytes);
            ok &= SafeWriteBytes(
                stack + kRollbackSecondaryEventScalarOffset,
                history.bytes[player].data()
                    + kRollbackSecondaryEventScalarOffset,
                kRollbackSecondaryEventScalarBytes);
            for (uint32_t index = 0;
                 index < history.header_count[player];
                 ++index)
            {
                ok &= SafeWriteBytes(
                    reinterpret_cast<uint8_t*>(event_headers_raw)
                        + static_cast<size_t>(index)
                            * kRollbackSecondaryEventHeaderStride
                        + kRollbackSecondaryEventHeaderCursorOffset,
                    &history.header_cursors[player][index],
                    sizeof(uint16_t));
            }
        }
        return ok;
    }

    static inline bool RollbackRestoreSecondaryEventStack(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        return RollbackRestoreSecondaryEventStack(
            image_base, frame.secondary_event_stack);
    }

    static inline bool RollbackCaptureCharaAnimationState(
        uintptr_t image_base,
        RollbackCharaAnimationStateHistory& history) noexcept
    {
        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[2] = {p1, p2};
        return CaptureRollbackCharaAnimationState(charas, history);
    }

    static inline bool RollbackCaptureCharaAnimationState(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        const bool ok = RollbackCaptureCharaAnimationState(
            image_base, frame.chara_animation);
        frame.chara_animation_hash =
            frame.chara_animation.integrity_hash;
        return ok;
    }

    static inline bool RollbackCharaAnimationRestorePreflight(
        uintptr_t image_base,
        const RollbackCharaAnimationStateHistory& history) noexcept
    {
        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[2] = {p1, p2};
        return RollbackCharaAnimationRestorePreflight(charas, history);
    }

    static inline RollbackCharaAnimationPreflightFailure
    RollbackCharaAnimationRestorePreflightFailure(
        uintptr_t image_base,
        const RollbackCharaAnimationStateHistory& history) noexcept
    {
        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return RollbackCharaAnimationPreflightFailure::LiveCaptureFailed;
        const uintptr_t charas[2] = {p1, p2};
        return RollbackCharaAnimationRestorePreflightFailure(charas, history);
    }

    static inline bool RollbackRestoreCharaAnimationState(
        uintptr_t image_base,
        const RollbackCharaAnimationStateHistory& history) noexcept
    {
        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[2] = {p1, p2};
        return RestoreRollbackCharaAnimationState(charas, history);
    }

    static inline bool RollbackRestoreCharaAnimationState(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        return RollbackRestoreCharaAnimationState(
            image_base, frame.chara_animation);
    }

    static inline bool RollbackRestoreMotionBankHistoryFromTimeline(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame* timeline,
        size_t timeline_count,
        size_t target_index) noexcept
    {
        if (!image_base || !timeline || timeline_count == 0
            || target_index >= timeline_count)
        {
            return false;
        }

        uintptr_t p1 = 0;
        uintptr_t p2 = 0;
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;
        const uintptr_t charas[kRollbackMotionBankPlayerCount] = {p1, p2};

        bool ok = true;
        for (size_t player = 0;
             player < kRollbackMotionBankPlayerCount;
             ++player)
        {
            if (!charas[player])
            {
                ok = false;
                continue;
            }

            auto* chara = reinterpret_cast<uint8_t*>(charas[player]);
            for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
            {
                const auto& spec = kRollbackMotionBankSpecs[bank];
                auto* bank_ptr = chara + spec.chara_offset;

                uintptr_t buffers[kRollbackMotionBankBufferCount] {};
                int current_slot = -1;
                for (size_t slot = 0;
                     slot < kRollbackMotionBankBufferCount;
                     ++slot)
                {
                    void* buffer_raw = nullptr;
                    if (!SafeReadPtr(
                            bank_ptr + 0x08 + slot * sizeof(void*),
                            &buffer_raw)
                        || !buffer_raw)
                    {
                        ok = false;
                        continue;
                    }
                    buffers[slot] = reinterpret_cast<uintptr_t>(buffer_raw);
                }

                void* current_raw = nullptr;
                if (SafeReadPtr(bank_ptr + 0x28, &current_raw)
                    && current_raw)
                {
                    const uintptr_t current =
                        reinterpret_cast<uintptr_t>(current_raw);
                    for (size_t slot = 0;
                         slot < kRollbackMotionBankBufferCount;
                         ++slot)
                    {
                        if (buffers[slot] == current)
                        {
                            current_slot = static_cast<int>(slot);
                            break;
                        }
                    }
                }
                if (current_slot < 0)
                {
                    ok = false;
                    continue;
                }

                for (size_t age = 1;
                     age < kRollbackMotionBankBufferCount;
                     ++age)
                {
                    if (target_index < age)
                        continue;

                    const RollbackHgCpuSnapshotFrame& source =
                        timeline[target_index - age];
                    const uint8_t* source_bytes = nullptr;
                    const auto& source_history = source.motion_banks;
                    if (source_history.ok
                        && source_history.chara[player] == charas[player])
                    {
                        // The timeline entry represents frame t-age after its
                        // producer has rotated and written the current slot.
                        // provider_slot already represents t-age-1, so using
                        // it here shifts reconstructed history one extra frame
                        // into the past.
                        const int source_slot =
                            RollbackMotionBankTimelineFrameSlot(
                                source_history, player, bank);
                        if (source_slot >= 0
                            && source_slot < static_cast<int>(
                                kRollbackMotionBankBufferCount))
                        {
                            const size_t history_offset =
                                RollbackMotionBankByteOffset(
                                    player,
                                    bank,
                                    static_cast<size_t>(source_slot));
                            if (history_offset + spec.bytes
                                <= source_history.bytes.size())
                            {
                                source_bytes =
                                    source_history.bytes.data()
                                    + history_offset;
                            }
                        }
                    }
                    if (!source_bytes)
                    {
                        size_t source_base = 0;
                        if (!RollbackHgCpuSnapshotCharaBase(
                                source, player, source_base))
                        {
                            ok = false;
                            continue;
                        }

                        const size_t source_offset =
                            source_base
                            + kRollbackMotionBankSnapshotLocals[bank];
                        if (source_offset + spec.bytes > source.bytes.size())
                        {
                            ok = false;
                            continue;
                        }
                        source_bytes = source.bytes.data() + source_offset;
                    }

                    const int target_slot =
                        (current_slot + static_cast<int>(age))
                        % static_cast<int>(kRollbackMotionBankBufferCount);
                    const uintptr_t target = buffers[target_slot];
                    if (!target
                        || !SafeWriteBytes(
                            reinterpret_cast<void*>(target),
                            source_bytes,
                            spec.bytes))
                    {
                        ok = false;
                    }
                }
            }
        }
        return ok;
    }

    static inline bool RollbackReadTimerNodePointers(
        uintptr_t image_base,
        uintptr_t& timer_config,
        uintptr_t& root,
        uintptr_t& backing) noexcept
    {
        timer_config = 0;
        root = 0;
        backing = 0;
        if (!image_base) return false;

        void* timer_config_raw = nullptr;
        if (!SafeReadPtr(
                reinterpret_cast<const void*>(
                    image_base + kRollbackRVA_LuxMoveVM_TimerConfig),
                &timer_config_raw)
            || !timer_config_raw)
        {
            return false;
        }

        void* root_raw = nullptr;
        if (!SafeReadPtr(
                static_cast<const uint8_t*>(timer_config_raw) + 0xA0,
                &root_raw)
            || !root_raw)
        {
            return false;
        }

        void* backing_raw = nullptr;
        if (!SafeReadPtr(
                static_cast<const uint8_t*>(root_raw) + 0x08,
                &backing_raw)
            || !backing_raw)
        {
            return false;
        }

        void* effect_system_raw = nullptr;
        void* action_manager_root_raw = nullptr;
        if (!SafeReadPtr(
                reinterpret_cast<const void*>(
                    image_base
                        + kRollbackRVA_LuxEffectSystemInstance),
                &effect_system_raw)
            || !effect_system_raw
            || !SafeReadPtr(
                static_cast<const uint8_t*>(effect_system_raw) + 0x7A0,
                &action_manager_root_raw)
            || !RollbackTimerActionManagerAliasValid(
                reinterpret_cast<uintptr_t>(root_raw),
                reinterpret_cast<uintptr_t>(
                    action_manager_root_raw)))
        {
            return false;
        }

        timer_config = reinterpret_cast<uintptr_t>(timer_config_raw);
        root = reinterpret_cast<uintptr_t>(root_raw);
        backing = reinterpret_cast<uintptr_t>(backing_raw);
        return true;
    }

    static inline bool RollbackCaptureTimerNodeHistory(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame,
        bool require_preallocated = false) noexcept
    {
        auto& history = frame.timer_node;
        history.ok = false;
        history.timer_config = 0;
        history.root = 0;
        history.backing = 0;
        history.indexed_table = 0;
        history.indexed_nonzero_count = 0;
        history.indexed_captured_count = 0;
        history.indexed_object_captured_count = 0;
        std::memset(history.indexed_root, 0, sizeof(history.indexed_root));
        std::memset(history.indexed_vtable, 0, sizeof(history.indexed_vtable));
        std::memset(history.indexed_writer, 0, sizeof(history.indexed_writer));
        std::memset(
            history.indexed_captured, 0, sizeof(history.indexed_captured));
        std::memset(
            history.indexed_object_captured,
            0,
            sizeof(history.indexed_object_captured));
        std::memset(history.child, 0, sizeof(history.child));
        history.hash = 0;
        size_t captured_node_count = 0;

        if (!RollbackReadTimerNodePointers(
                image_base, history.timer_config, history.root,
                history.backing))
        {
            return false;
        }

        auto capture_node =
            [&](uintptr_t root,
                bool required) noexcept -> bool
        {
            if (!root) return !required;
            for (size_t i = 0; i < captured_node_count; ++i)
            {
                if (history.nodes[i].root == root)
                    return true;
            }

            void* backing_raw = nullptr;
            if (!SafeReadPtr(
                    reinterpret_cast<const uint8_t*>(root) + 0x08,
                    &backing_raw)
                || !backing_raw)
            {
                return !required;
            }

            const bool appended =
                captured_node_count == history.nodes.size();
            if (appended)
            {
                if (history.nodes.size()
                    >= kRollbackTimerMaximumSnapshotNodes)
                {
                    return false;
                }
                if (require_preallocated
                    && history.nodes.size() == history.nodes.capacity())
                    return false;
                history.nodes.emplace_back();
            }
            auto& node = history.nodes[captured_node_count];
            node.root = root;
            node.backing = reinterpret_cast<uintptr_t>(backing_raw);

            const bool ok =
                SafeReadBytes(
                    reinterpret_cast<const void*>(node.root),
                    node.root_bytes.data(),
                    node.root_bytes.size())
                && SafeReadBytes(
                    reinterpret_cast<const void*>(node.backing),
                    node.backing_bytes.data(),
                    node.backing_bytes.size());
            if (!ok)
            {
                // Optional indexed nodes may disappear between reads. Do not
                // retain a half-captured logical node; the fixed outer arena
                // keeps its capacity for a later capture.
                node.root = 0;
                node.backing = 0;
                if (appended)
                {
                    history.nodes.pop_back();
                }
                return !required;
            }

            ++captured_node_count;
            return true;
        };

        bool ok = capture_node(history.root, true);
        for (size_t i = 0; i < kRollbackTimerNodeChildPtrCount; ++i)
        {
            void* child_raw = nullptr;
            if (SafeReadPtr(
                    reinterpret_cast<const uint8_t*>(history.root)
                        + kRollbackTimerNodeChildPtrOffset
                        + i * sizeof(void*),
                    &child_raw))
            {
                history.child[i] = reinterpret_cast<uintptr_t>(child_raw);
            }
        }

        void* table_raw = nullptr;
        if (SafeReadPtr(
                reinterpret_cast<const uint8_t*>(history.timer_config) + 0x90,
                &table_raw)
            && table_raw)
        {
            history.indexed_table = reinterpret_cast<uintptr_t>(table_raw);
            for (size_t i = 0; i < 0x10; ++i)
            {
                void* node_raw = nullptr;
                if (SafeReadPtr(
                        static_cast<const uint8_t*>(table_raw)
                            + 0x270 + i * sizeof(void*),
                        &node_raw)
                    && node_raw)
                {
                    const uintptr_t node_addr =
                        reinterpret_cast<uintptr_t>(node_raw);
                    history.indexed_root[i] = node_addr;
                    ++history.indexed_nonzero_count;
                    if (SafeReadBytes(
                            node_raw,
                            history.indexed_object_bytes[i].data(),
                            history.indexed_object_bytes[i].size()))
                    {
                        history.indexed_object_captured[i] = true;
                        ++history.indexed_object_captured_count;
                    }

                    void* vtable_raw = nullptr;
                    if (SafeReadPtr(node_raw, &vtable_raw) && vtable_raw)
                    {
                        history.indexed_vtable[i] =
                            reinterpret_cast<uintptr_t>(vtable_raw);
                        void* writer_raw = nullptr;
                        if (SafeReadPtr(
                                static_cast<const uint8_t*>(vtable_raw)
                                    + 0x100,
                                &writer_raw))
                        {
                            history.indexed_writer[i] =
                                reinterpret_cast<uintptr_t>(writer_raw);
                        }
                    }

                    const size_t before_count = captured_node_count;
                    ok &= capture_node(node_addr, false);
                    history.indexed_captured[i] =
                        captured_node_count != before_count
                        || node_addr == history.root;
                    if (history.indexed_captured[i])
                        ++history.indexed_captured_count;
                }
            }
        }

        // Logical timer nodes may appear or disappear during gameplay. The
        // vector capacity is fixed at activation and each node owns fixed
        // byte arrays, so trimming the logical size cannot allocate.
        history.nodes.resize(captured_node_count);
        history.ok = ok;
        if (ok)
        {
            history.hash = RollbackHashTimerNodeHistory(history);
        }
        frame.timer_node_hash = history.hash;
        return ok;
    }

    static inline bool RollbackRestoreTimerNodeHistory(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        const auto& history = frame.timer_node;
        if (!history.ok || history.nodes.empty()
            || history.nodes[0].root != history.root
            || history.nodes[0].backing != history.backing)
        {
            return false;
        }

        uintptr_t timer_config = 0;
        uintptr_t root = 0;
        uintptr_t backing = 0;
        if (!RollbackReadTimerNodePointers(
                image_base, timer_config, root, backing))
        {
            return false;
        }
        if (timer_config != history.timer_config
            || root != history.root
            || backing != history.backing)
        {
            return false;
        }

        // Complete the replaceable-generation preflight before the first
        // restore write. The indexed table is the camera director's 16-slot
        // component table; a component may be destroyed/replaced while the
        // process-global timer root itself remains unchanged.
        void* indexed_table_raw = nullptr;
        if (!SafeReadPtr(
                reinterpret_cast<const uint8_t*>(timer_config) + 0x90,
                &indexed_table_raw)
            || reinterpret_cast<uintptr_t>(indexed_table_raw)
                != history.indexed_table)
        {
            return false;
        }
        for (size_t i = 0; i < 0x10; ++i)
        {
            void* indexed_root_raw = nullptr;
            if (history.indexed_table
                && !SafeReadPtr(
                    reinterpret_cast<const uint8_t*>(history.indexed_table)
                        + 0x270 + i * sizeof(void*),
                    &indexed_root_raw))
            {
                return false;
            }
            const uintptr_t indexed_root =
                reinterpret_cast<uintptr_t>(indexed_root_raw);
            if (indexed_root != history.indexed_root[i]) return false;
            if (!indexed_root)
            {
                if (history.indexed_vtable[i]
                    || history.indexed_writer[i]
                    || history.indexed_captured[i]
                    || history.indexed_object_captured[i])
                {
                    return false;
                }
                continue;
            }

            void* vtable_raw = nullptr;
            void* writer_raw = nullptr;
            if (!SafeReadPtr(
                    reinterpret_cast<const void*>(indexed_root),
                    &vtable_raw)
                || reinterpret_cast<uintptr_t>(vtable_raw)
                    != history.indexed_vtable[i]
                || !vtable_raw
                || !SafeReadPtr(
                    static_cast<const uint8_t*>(vtable_raw) + 0x100,
                    &writer_raw)
                || reinterpret_cast<uintptr_t>(writer_raw)
                    != history.indexed_writer[i])
            {
                return false;
            }
        }
        for (const auto& node : history.nodes)
        {
            void* live_backing_raw = nullptr;
            if (!node.root || !node.backing
                || !SafeReadPtr(
                    reinterpret_cast<const uint8_t*>(node.root) + 0x08,
                    &live_backing_raw)
                || reinterpret_cast<uintptr_t>(live_backing_raw)
                    != node.backing)
            {
                return false;
            }
        }

        bool ok = SafeWriteBytes(
            reinterpret_cast<void*>(history.root),
            history.nodes[0].root_bytes.data(),
            history.nodes[0].root_bytes.size());
        ok &= SafeWriteBytes(
            reinterpret_cast<void*>(history.backing),
            history.nodes[0].backing_bytes.data(),
            history.nodes[0].backing_bytes.size());
        // The indexed roots are effect-camera components. Keep their raw
        // images for diagnostics and generation evidence only: those images
        // contain subtype-specific pointers and caches that the native
        // component deserializers deliberately omit. The separately ordered
        // RollbackBattleCameraSnapshot restores the exact +0x100 serializer
        // projection and rebinds/clears identity fields using the live round
        // generation. Never bulk-write these object images.
        for (const auto& node : history.nodes)
        {
            if (node.root == history.root)
                continue;
            ok &= SafeWriteBytes(
                reinterpret_cast<void*>(node.root),
                node.root_bytes.data(),
                node.root_bytes.size());
            ok &= SafeWriteBytes(
                reinterpret_cast<void*>(node.backing),
                node.backing_bytes.data(),
                node.backing_bytes.size());
        }
        return ok;
    }

    static inline const char* RollbackHgCpuPreallocatedCaptureFailure(
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        return RollbackPreallocatedHgCpuCapacityFailure(
            frame,
            kRollbackHgCpuSnapshotBytes,
            kRollbackKHitMaximumSnapshotNodes,
            RollbackMotionBankTotalBytes(),
            RollbackMotionBankControlTotalBytes(),
            kRollbackMoveVMMotionTailBytes * 2,
            2 * kRollbackSkeletonRuntimeBytes,
            kRollbackTimerNodeRootBytes,
            kRollbackTimerNodeBackingBytes,
            kRollbackTimerMaximumSnapshotNodes);
    }

    static inline bool RollbackHgCpuPreallocatedCaptureReady(
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        return std::strcmp(
            RollbackHgCpuPreallocatedCaptureFailure(frame), "ok") == 0;
    }

    static inline RollbackHgCpuSnapshotReport CaptureRollbackHgCpuSnapshot(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& out,
        RollbackHgCpuSnapshotFrame* emergency_scratch = nullptr,
        bool require_preallocated = false) noexcept
    {
        if (require_preallocated
            && (!emergency_scratch
                || !RollbackHgCpuPreallocatedCaptureReady(out)
                || !RollbackHgCpuPreallocatedCaptureReady(
                    *emergency_scratch)))
        {
            RollbackHgCpuSnapshotReport report {};
            report.failure = "hgcpu-preallocated-capture-preflight-failed";
            return report;
        }
        out.recycle_for_capture();
        auto phase_started = std::chrono::steady_clock::now();
        const auto finish_phase = [&phase_started]() noexcept {
            const auto now = std::chrono::steady_clock::now();
            const uint64_t elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now - phase_started).count());
            phase_started = now;
            return elapsed;
        };
        // The native writer is observably mutating. Keep the presentation and
        // MoveVM histories in a separate emergency frame until the writer has
        // returned and cleanup has succeeded on every exit path.
        RollbackHgCpuSnapshotFrame local_emergency {};
        RollbackHgCpuSnapshotFrame& emergency = emergency_scratch
            ? *emergency_scratch : local_emergency;
        emergency.recycle_for_capture();
        if (!RollbackCaptureMotionBankHistory(
                image_base, emergency, require_preallocated))
        {
            RollbackHgCpuSnapshotReport report {};
            report.ok = false;
            report.failure = "motion-bank-history-capture-failed";
            return report;
        }
        if (!RollbackCaptureMoveVMMotionTail(
                image_base, emergency, require_preallocated))
        {
            RollbackHgCpuSnapshotReport report {};
            report.ok = false;
            report.failure = "motion-tail-capture-failed";
            return report;
        }
        if (!RollbackCaptureSecondaryEventStack(image_base, emergency))
        {
            RollbackHgCpuSnapshotReport report {};
            report.ok = false;
            report.failure = "secondary-event-stack-capture-failed";
            return report;
        }
        if (!RollbackCaptureCharaAnimationState(image_base, emergency))
        {
            RollbackHgCpuSnapshotReport report {};
            report.ok = false;
            report.failure = "chara-animation-state-capture-failed";
            return report;
        }
        const bool skeleton_template_ready =
            RollbackSkeletonRuntimeTemplateReady(
                emergency.skeleton_runtime);
        if (require_preallocated && !skeleton_template_ready)
        {
            RollbackHgCpuSnapshotReport report {};
            report.ok = false;
            report.failure = "skeleton-runtime-preallocated-template-missing";
            return report;
        }
        if (!(skeleton_template_ready
                ? RollbackCaptureSkeletonRuntimeFromTemplate(
                    image_base, emergency)
                : RollbackCaptureSkeletonRuntime(
                    image_base, emergency, require_preallocated)))
        {
            RollbackHgCpuSnapshotReport report {};
            report.ok = false;
            report.failure = "skeleton-runtime-capture-failed";
            return report;
        }
        const uint64_t emergency_capture_nanoseconds = finish_phase();

        if (require_preallocated
            && kRollbackHgCpuSnapshotBytes > out.bytes.capacity())
        {
            RollbackHgCpuSnapshotReport report {};
            report.failure = "hgcpu-preallocated-capacity-exceeded";
            return report;
        }
        try
        {
            if (out.bytes.size() != kRollbackHgCpuSnapshotBytes)
                out.bytes.resize(kRollbackHgCpuSnapshotBytes);
        }
        catch (...)
        {
            out.clear();
            RollbackHgCpuSnapshotReport report {};
            report.failure = "hgcpu-snapshot-allocation-failed";
            return report;
        }
        std::array<RollbackAiPaletteDiagnostics, 2>
            ai_palette_before_native_writer {};
        if (!CaptureRollbackAiPaletteDiagnostics(
                image_base, ai_palette_before_native_writer))
        {
            out.clear();
            RollbackHgCpuSnapshotReport report {};
            report.failure =
                "ai-palette-pre-native-writer-capture-failed";
            return report;
        }
        RollbackHgCpuSnapshotReport report = RollbackInvokeHgCpuSnapshot(
            image_base, kRollbackRVA_ExecMoveChangeAndPost, out.bytes, true);
        report.emergency_capture_nanoseconds =
            emergency_capture_nanoseconds;
        report.native_capture_nanoseconds = finish_phase();
        const bool ai_palette_restored =
            RestoreRollbackAiPaletteDiagnostics(
                image_base, ai_palette_before_native_writer);
        const bool motion_bank_restored =
            RollbackRestoreMotionBankHistory(image_base, emergency);
        const bool motion_tail_restored =
            RollbackRestoreMoveVMMotionTail(image_base, emergency);
        const bool secondary_event_stack_restored =
            RollbackRestoreSecondaryEventStack(image_base, emergency);
        const bool chara_animation_restored =
            RollbackRestoreCharaAnimationState(image_base, emergency);
        const bool skeleton_runtime_restored =
            RollbackRestoreSkeletonRuntime(
                image_base, emergency, false);
        report.emergency_restore_nanoseconds = finish_phase();
        if (!report.ok)
        {
            out.clear();
            if (!ai_palette_restored
                || !motion_bank_restored || !motion_tail_restored
                || !secondary_event_stack_restored
                || !chara_animation_restored
                || !skeleton_runtime_restored)
                report.failure = "native-capture-and-emergency-restore-failed";
            return report;
        }
        if (!ai_palette_restored)
        {
            out.clear();
            report.ok = false;
            report.failure =
                "ai-palette-restore-after-native-writer-failed";
            return report;
        }
        if (!motion_bank_restored)
        {
            out.clear();
            report.ok = false;
            report.failure = "motion-bank-history-restore-after-capture-failed";
            return report;
        }
        if (!motion_tail_restored)
        {
            out.clear();
            report.ok = false;
            report.failure = "motion-tail-restore-after-capture-failed";
            return report;
        }
        if (!secondary_event_stack_restored)
        {
            out.clear();
            report.ok = false;
            report.failure =
                "secondary-event-stack-restore-after-capture-failed";
            return report;
        }
        if (!chara_animation_restored)
        {
            out.clear();
            report.ok = false;
            report.failure =
                "chara-animation-restore-after-capture-failed";
            return report;
        }
        if (!skeleton_runtime_restored)
        {
            out.clear();
            report.ok = false;
            report.failure =
                "skeleton-runtime-restore-after-capture-failed";
            return report;
        }

        const uint64_t motion_bank_hash = emergency.motion_bank_hash;
        const uint64_t motion_tail_hash = emergency.motion_tail_hash;
        const uint64_t secondary_event_stack_hash =
            emergency.secondary_event_stack_hash;
        const uint64_t chara_animation_hash =
            emergency.chara_animation_hash;
        const uint64_t skeleton_runtime_hash =
            emergency.skeleton_runtime_hash;
        if (emergency_scratch)
        {
            // Both frames own baseline-sized buffers. Exchange their captured
            // histories instead of deep-copying four containers every tick;
            // the scratch frame receives the output slot's cleared buffers
            // for the next capture, so capacities remain fixed.
            std::swap(out.motion_banks, emergency.motion_banks);
            std::swap(out.motion_tail, emergency.motion_tail);
            std::swap(
                out.secondary_event_stack,
                emergency.secondary_event_stack);
            std::swap(
                out.chara_animation,
                emergency.chara_animation);
            std::swap(out.skeleton_runtime, emergency.skeleton_runtime);
        }
        else
        {
            out.motion_banks = std::move(emergency.motion_banks);
            out.motion_tail = std::move(emergency.motion_tail);
            out.secondary_event_stack =
                std::move(emergency.secondary_event_stack);
            out.chara_animation =
                std::move(emergency.chara_animation);
            out.skeleton_runtime =
                std::move(emergency.skeleton_runtime);
        }
        out.motion_bank_hash = motion_bank_hash;
        out.motion_tail_hash = motion_tail_hash;
        out.secondary_event_stack_hash =
            secondary_event_stack_hash;
        out.chara_animation_hash = chara_animation_hash;
        out.skeleton_runtime_hash = skeleton_runtime_hash;

        if (!RollbackCaptureKHitTopology(
                image_base, out, require_preallocated))
        {
            out.clear();
            report.ok = false;
            report.failure = "khit-topology-capture-failed";
            return report;
        }
        report.khit_capture_nanoseconds = finish_phase();
        if (!RollbackCaptureTimerNodeHistory(
                image_base, out, require_preallocated))
        {
            out.clear();
            report.ok = false;
            report.failure = "timer-node-history-capture-failed";
            return report;
        }
        report.timer_capture_nanoseconds = finish_phase();

        out.used_bytes = report.cursor;
        out.byte_hash = report.hash;
        out.hash = RollbackHashHgCpuIntegrityComponents(out);
        out.canonical_hash = RollbackHashHgCpuCanonical(out);
        report.hash_finalize_nanoseconds = finish_phase();
        if (out.canonical_hash == 0)
        {
            out.clear();
            report.ok = false;
            report.failure = "canonical-hash-failed";
            return report;
        }
        return report;
    }

    static inline RollbackHgCpuSnapshotReport RestoreRollbackHgCpuSnapshot(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        // Refuse an unsupported scheduler reconfiguration before the native
        // reader or any companion history mutates live state.
        const RollbackCharaAnimationPreflightFailure animation_preflight =
            RollbackCharaAnimationRestorePreflightFailure(
                image_base, frame.chara_animation);
        if (animation_preflight
            != RollbackCharaAnimationPreflightFailure::None)
        {
            RollbackHgCpuSnapshotReport report {};
            report.failure = RollbackCharaAnimationPreflightFailureName(
                animation_preflight);
            report.capacity = frame.bytes.size();
            report.image_base = image_base;
            return report;
        }
        if (!RollbackRestoreKHitTopology(image_base, frame))
        {
            RollbackHgCpuSnapshotReport report {};
            report.failure = "khit-topology-restore-failed";
            report.capacity = frame.bytes.size();
            report.image_base = image_base;
            report.context_ready = frame.khit_topology_ok;
            return report;
        }

        std::vector<uint8_t>& bytes =
            const_cast<std::vector<uint8_t>&>(frame.bytes);
        RollbackHgCpuSnapshotReport report = RollbackInvokeHgCpuSnapshot(
            image_base, kRollbackRVA_ExecFinalizeAndPost, bytes, false);
        if (report.ok
            && !RollbackRestoreHgCpuPostReadExactFields(image_base, frame))
        {
            report.ok = false;
            report.failure = "post-read-exact-field-restore-failed";
        }
        if (report.ok
            && !RollbackRestoreSkeletonRuntime(image_base, frame))
        {
            report.ok = false;
            report.failure = "skeleton-runtime-restore-failed";
        }
        if (report.ok
            && !RollbackRestoreTimerNodeHistory(image_base, frame))
        {
            report.ok = false;
            report.failure = "timer-node-history-restore-failed";
        }
        if (report.ok
            && !RollbackRestoreCharaAnimationState(image_base, frame))
        {
            report.ok = false;
            report.failure = "chara-animation-state-restore-failed";
        }
        if (report.ok
            && !RollbackRestoreSecondaryEventStack(image_base, frame))
        {
            report.ok = false;
            report.failure = "secondary-event-stack-restore-failed";
        }
        if (report.ok
            && !RollbackRestoreMoveVMMotionTail(image_base, frame))
        {
            report.ok = false;
            report.failure = "motion-tail-restore-failed";
        }
        if (report.ok
            && !RollbackRestoreMotionBankHistory(image_base, frame))
        {
            report.ok = false;
            report.failure = "motion-bank-history-restore-failed";
        }
        return report;
    }

    static inline RollbackHgCpuRoundTripReport RollbackHgCpuRoundTrip(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& snapshot) noexcept
    {
        RollbackHgCpuRoundTripReport report{};
        report.capture = CaptureRollbackHgCpuSnapshot(image_base, snapshot);
        report.before_hash = snapshot.hash;
        report.before_topology_hash = snapshot.khit_topology_hash;
        if (report.capture.ok)
            report.restore = RestoreRollbackHgCpuSnapshot(image_base, snapshot);
        if (report.capture.ok && report.restore.ok)
        {
            RollbackHgCpuSnapshotFrame after {};
            report.recapture =
                CaptureRollbackHgCpuSnapshot(image_base, after);
            report.after_hash = after.hash;
            report.after_topology_hash = after.khit_topology_hash;
            if (report.recapture.ok)
            {
                report.bytes_compared = (std::min)(
                    RollbackHgCpuEffectiveBytes(snapshot),
                    RollbackHgCpuEffectiveBytes(after));
                bool first_set = false;
                bool first_unignored_set = false;
                for (size_t i = 0; i < report.bytes_compared; ++i)
                {
                    if (snapshot.bytes[i] == after.bytes[i])
                        continue;
                    ++report.mismatch_count;
                    if (!first_set)
                    {
                        first_set = true;
                        report.first_mismatch_offset = i;
                        report.first_mismatch_before = snapshot.bytes[i];
                        report.first_mismatch_after = after.bytes[i];
                    }
                    const char* ignore_reason = nullptr;
                    if (RollbackHgCpuRoundTripOffsetIgnored(
                            i, &ignore_reason, &snapshot))
                    {
                        ++report.ignored_mismatch_count;
                        if (std::strcmp(
                                report.first_ignored_reason, "none") == 0
                            && ignore_reason)
                        {
                            report.first_ignored_reason = ignore_reason;
                        }
                        continue;
                    }
                    ++report.unignored_mismatch_count;
                    if (!first_unignored_set)
                    {
                        first_unignored_set = true;
                        report.first_unignored_mismatch_offset = i;
                        report.first_unignored_mismatch_before =
                            snapshot.bytes[i];
                        report.first_unignored_mismatch_after =
                            after.bytes[i];
                    }
                }
                const size_t snapshot_effective =
                    RollbackHgCpuEffectiveBytes(snapshot);
                const size_t after_effective =
                    RollbackHgCpuEffectiveBytes(after);
                const size_t size_mismatch =
                    snapshot_effective > report.bytes_compared
                    ? snapshot_effective - report.bytes_compared
                    : after_effective - report.bytes_compared;
                report.mismatch_count += size_mismatch;
                report.unignored_mismatch_count += size_mismatch;
            }
        }
        report.hash_match =
            report.before_hash != 0
            && report.before_hash == report.after_hash;
        report.topology_match =
            report.before_topology_hash != 0
            && report.before_topology_hash == report.after_topology_hash;
        report.policy_match =
            report.recapture.ok && report.topology_match
            && report.unignored_mismatch_count == 0;
        report.ok = report.capture.ok && report.restore.ok
            && report.recapture.ok
            && (report.hash_match || report.policy_match);
        return report;
    }
#endif
}
