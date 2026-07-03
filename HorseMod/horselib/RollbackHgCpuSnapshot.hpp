// ============================================================================
// Horse::RollbackHgCpuSnapshot
//
// Guarded access to SC6's native HgCpuDirect battle-state snapshot pair.
// This owns only the rollback-facing wrapper; ReplayScrub keeps its larger
// replay-seek-specific guards and historical restore logic.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "ReplayDebugTrace.hpp"
#include "RollbackSnapshot.hpp"
#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

namespace Horse
{
    static constexpr size_t kRollbackHgCpuSnapshotBytes = 0x28018;
    static constexpr uintptr_t kRollbackRVA_ExecMoveChangeAndPost = 0x3841E0;
    static constexpr uintptr_t kRollbackRVA_ExecFinalizeAndPost = 0x384540;
    static constexpr size_t kRollbackHgCpuFallbackPerCharaBytes = 0x1400C;
    static constexpr size_t kRollbackHgCpuAiResetSlotEnd = 0x79AC;
    static constexpr size_t kRollbackHgCpuHitAreaLocalStart = 0x79AC;
    static constexpr size_t kRollbackHgCpuHitAreaFixedBytes = 0x41C;
    static constexpr size_t kRollbackHgCpuHitAreaRelocBytes = 0x90;
    static constexpr uintptr_t kRollbackKHitListControlStart = 0x44470;
    static constexpr size_t kRollbackKHitListControlBytes = 0x50;
    static constexpr size_t kRollbackKHitNodeImageBytes = 0xA0;
    static constexpr uintptr_t kRollbackCharaHitAreaFixedStart = 0x44078;
    static constexpr size_t kRollbackMotionBankPlayerCount = 2;
    static constexpr size_t kRollbackMotionBankCount = 2;
    static constexpr size_t kRollbackMotionBankBufferCount = 3;
    static constexpr uintptr_t kRollbackPrimaryMotionBankCharaOffset = 0x35A0;
    static constexpr uintptr_t kRollbackSecondaryMotionBankCharaOffset = 0x27760;
    static constexpr size_t kRollbackPrimaryMotionBankSnapshotLocal = 0x3590;
    static constexpr size_t kRollbackSecondaryMotionBankSnapshotLocal = 0x4DD0;
    static constexpr size_t kRollbackPrimaryMotionBankBytes = 0x1840;
    static constexpr size_t kRollbackSecondaryMotionBankBytes = 0x800;
    static constexpr size_t kRollbackMotionBankControlBytes = 0x38;
    static constexpr uintptr_t kRollbackMoveVMMotionTailCharaOffset = 0x96490;
    static constexpr size_t kRollbackMoveVMMotionTailBytes = 0x1000;
    static constexpr uintptr_t kRollbackRVA_LuxMoveVM_TimerConfig = 0x470DF28;
    static constexpr size_t kRollbackTimerNodeRootBytes = 0x2F0;
    static constexpr size_t kRollbackTimerNodeBackingBytes = 0x41E0;
    static constexpr size_t kRollbackTimerNodeChildPtrOffset = 0x10;
    static constexpr size_t kRollbackTimerNodeChildPtrCount = 0x11;
    static constexpr size_t kRollbackTimerIndexedObjectBytes = 0x310;

    using RollbackHgCpuExecFn =
        void* (__fastcall*)(class RollbackHgCpuBufferShim*);

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

        struct TimerNodeHistory
        {
            struct NodeImage
            {
                uintptr_t root {0};
                uintptr_t backing {0};
                std::vector<uint8_t> root_bytes;
                std::vector<uint8_t> backing_bytes;
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
                for (auto& bytes : indexed_object_bytes)
                    bytes.fill(0);
                hash = 0;
                std::memset(child, 0, sizeof(child));
                root_bytes.clear();
                backing_bytes.clear();
                nodes.clear();
            }
        };

        std::vector<uint8_t> bytes;
        size_t used_bytes {0};
        uint64_t byte_hash {0};
        uint64_t khit_topology_hash {0};
        uint64_t motion_bank_hash {0};
        uint64_t motion_tail_hash {0};
        uint64_t timer_node_hash {0};
        uint64_t hash {0};
        bool khit_topology_ok {false};
        KHitCharaTopology khit_topology[2] {};
        MotionBankHistory motion_banks {};
        MotionTailHistory motion_tail {};
        TimerNodeHistory timer_node {};

        void clear()
        {
            bytes.clear();
            used_bytes = 0;
            byte_hash = 0;
            khit_topology_hash = 0;
            motion_bank_hash = 0;
            motion_tail_hash = 0;
            timer_node_hash = 0;
            hash = 0;
            khit_topology_ok = false;
            khit_topology[0].clear();
            khit_topology[1].clear();
            motion_banks.clear();
            motion_tail.clear();
            timer_node.clear();
        }
    };

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

    struct RollbackHgCpuIgnoreRange
    {
        size_t offset {0};
        size_t bytes {0};
        const char* reason {"unspecified"};
    };

    static inline bool RollbackHgCpuRoundTripOffsetIgnored(
        size_t offset,
        const char** reason_out = nullptr,
        const RollbackHgCpuSnapshotFrame* frame = nullptr) noexcept
    {
        static constexpr RollbackHgCpuIgnoreRange kRanges[] = {
            {0x16C0, 0x180, "rebuilt FLuxBattleChara motion-input flag/history"},
            {0x2144, 0x1020, "rebuilt FLuxBattleChara input/command-history window"},
            {0x3590, 0x1840, "motion-bank primary presentation cache; rollback restores control and MoveVM motion tail"},
            {0x4DD0, 0x800, "motion-bank secondary presentation cache; rollback restores control and MoveVM motion tail"},
            {0x290, 0x10, "UpdateLookAtIKTarget rebuilds chara+0x2A0 look-at target vector A"},
            {0x2A0, 0x10, "UpdateLookAtIKTarget rebuilds chara+0x2B0 look-at source vector A"},
            {0x2C0, 0x10, "UpdateLookAtIKTarget rebuilds chara+0x2D0 look-at target vector B"},
            {0x2D0, 0x10, "UpdateLookAtIKTarget rebuilds chara+0x2E0 look-at source/temp vector B"},
            {0x260, 0x0C, "native reader/VFX dispatcher owns chara+0x270 restore-slot flags"},
            {0x3580, 0x10, "native reader/VFX dispatcher owns chara+0x3590 tree holder"},
            {0x5670, 0x30, "native reader rewrites chara+0x43DF0 self-pointer block"},
            {0x5778, 0x10, "native reader rewrites lane-state helper pointers"},
            {0x5BE0, 0x10, "native reader rewrites lane-state helper pointers"},
            {0x6748, 0x11F0, "native reader/VFX dispatcher canonicalizes chara+0x95FA0 effect-anchor block"},
        };

        const size_t p2_base = RollbackHgCpuCharaRecordBytes(frame, 0);
        const size_t bases[2] = {0, p2_base};
        for (const auto& range : kRanges)
        {
            for (size_t player = 0; player < 2; ++player)
            {
                const size_t start = bases[player] + range.offset;
                if (offset >= start && offset < start + range.bytes)
                {
                    if (reason_out) *reason_out = range.reason;
                    return true;
                }
            }
        }
        if (reason_out) *reason_out = nullptr;
        return false;
    }

    static inline bool RollbackReadCharaPointers(
        uintptr_t image_base,
        uintptr_t& p1,
        uintptr_t& p2) noexcept
    {
        p1 = 0;
        p2 = 0;
        if (!image_base) return false;

        void* p1_raw = nullptr;
        void* p2_raw = nullptr;
        const uintptr_t p1_addr = rollback_absolute_from_image_base(
            image_base, 0x14470DE90ull);
        const uintptr_t p2_addr = rollback_absolute_from_image_base(
            image_base, 0x14470DE98ull);
        if (!SafeReadPtr(reinterpret_cast<const void*>(p1_addr), &p1_raw)
            || !SafeReadPtr(reinterpret_cast<const void*>(p2_addr), &p2_raw))
            return false;
        p1 = reinterpret_cast<uintptr_t>(p1_raw);
        p2 = reinterpret_cast<uintptr_t>(p2_raw);
        return p1 != 0 && p2 != 0;
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
        RollbackHash h {};
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
        return h.value;
    }

    static inline bool RollbackCaptureKHitTopology(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame) noexcept
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
                    topology.nodes.push_back(image);

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
        if (!RollbackReadCharaPointers(image_base, p1, p2))
            return false;

        const uintptr_t charas[2] = {p1, p2};
        bool ok = true;
        for (size_t player = 0; player < 2; ++player)
        {
            const auto& topology = frame.khit_topology[player];
            if (!topology.ok || topology.chara != charas[player])
            {
                ok = false;
                continue;
            }

            for (const auto& node : topology.nodes)
            {
                ok &= SafeWriteBytes(
                    reinterpret_cast<void*>(node.address),
                    node.bytes.data(),
                    node.bytes.size());
            }

            ok &= SafeWriteBytes(
                reinterpret_cast<void*>(
                    topology.chara + kRollbackKHitListControlStart),
                topology.list_control.data(),
                topology.list_control.size());
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
        report.hash = RollbackHashBytes(bytes.data(), hash_bytes);
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

    struct RollbackMotionBankSpec
    {
        uintptr_t chara_offset {0};
        size_t bytes {0};
    };

    static constexpr RollbackMotionBankSpec kRollbackMotionBankSpecs[] = {
        {kRollbackPrimaryMotionBankCharaOffset, kRollbackPrimaryMotionBankBytes},
        {kRollbackSecondaryMotionBankCharaOffset, kRollbackSecondaryMotionBankBytes},
    };

    static constexpr size_t kRollbackMotionBankSnapshotLocals[] = {
        kRollbackPrimaryMotionBankSnapshotLocal,
        kRollbackSecondaryMotionBankSnapshotLocal,
    };

    static inline size_t RollbackMotionBankTotalBytes() noexcept
    {
        size_t per_player = 0;
        for (const auto& spec : kRollbackMotionBankSpecs)
            per_player += spec.bytes * kRollbackMotionBankBufferCount;
        return per_player * kRollbackMotionBankPlayerCount;
    }

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

    static inline size_t RollbackMotionBankByteOffset(
        size_t player,
        size_t bank,
        size_t buffer) noexcept
    {
        size_t per_player = 0;
        for (const auto& spec : kRollbackMotionBankSpecs)
            per_player += spec.bytes * kRollbackMotionBankBufferCount;

        size_t bank_base = player * per_player;
        for (size_t i = 0; i < bank && i < kRollbackMotionBankCount; ++i)
            bank_base += kRollbackMotionBankSpecs[i].bytes
                * kRollbackMotionBankBufferCount;
        return bank_base
            + buffer * kRollbackMotionBankSpecs[bank].bytes;
    }

    static inline bool RollbackCaptureMotionBankHistory(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        auto& history = frame.motion_banks;
        history.clear();

        try
        {
            history.bytes.assign(RollbackMotionBankTotalBytes(), 0);
            history.control_bytes.assign(
                RollbackMotionBankControlTotalBytes(), 0);
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
            RollbackHash h {};
            h.add_bytes(history.control_bytes.data(),
                        history.control_bytes.size());
            history.ok = ok;
            history.hash = ok ? h.value : 0;
        }
        else
        {
            history.hash = 0;
        }
        frame.motion_bank_hash = history.hash;
        return ok;
    }

    static inline bool RollbackRestoreMotionBankHistory(
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

        bool ok = true;
        for (size_t player = 0;
             player < kRollbackMotionBankPlayerCount;
             ++player)
        {
            if (charas[player] != history.chara[player])
            {
                ok = false;
                continue;
            }
            auto* chara = reinterpret_cast<uint8_t*>(charas[player]);

            for (size_t bank = 0; bank < kRollbackMotionBankCount; ++bank)
            {
                const auto& spec = kRollbackMotionBankSpecs[bank];
                auto* bank_ptr = chara + spec.chara_offset;
                const size_t control_src =
                    RollbackMotionBankControlOffset(player, bank);
                if (control_src + kRollbackMotionBankControlBytes
                        > history.control_bytes.size()
                    || !SafeWriteBytes(
                        bank_ptr,
                        history.control_bytes.data() + control_src,
                        kRollbackMotionBankControlBytes))
                {
                    ok = false;
                    continue;
                }

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
                    {
                        ok = false;
                        continue;
                    }

                    const size_t src = RollbackMotionBankByteOffset(
                        player, bank, buffer);
                    if (src + spec.bytes > history.bytes.size()
                        || !SafeWriteBytes(
                            live_raw,
                            history.bytes.data() + src,
                            spec.bytes))
                    {
                        ok = false;
                    }
                }
            }
        }
        return ok;
    }

    static inline bool RollbackCaptureMoveVMMotionTail(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        auto& history = frame.motion_tail;
        history.clear();

        try
        {
            history.bytes.assign(
                kRollbackMoveVMMotionTailBytes * 2, 0);
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
            RollbackHash h {};
            h.add_bytes(history.bytes.data(), history.bytes.size());
            history.hash = h.value;
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
                        const int source_slot =
                            source_history.provider_slot[player][bank];
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

        timer_config = reinterpret_cast<uintptr_t>(timer_config_raw);
        root = reinterpret_cast<uintptr_t>(root_raw);
        backing = reinterpret_cast<uintptr_t>(backing_raw);
        return true;
    }

    static inline bool RollbackCaptureTimerNodeHistory(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        auto& history = frame.timer_node;
        history.clear();

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
            for (const auto& node : history.nodes)
            {
                if (node.root == root)
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

            RollbackHgCpuSnapshotFrame::TimerNodeHistory::NodeImage node {};
            node.root = root;
            node.backing = reinterpret_cast<uintptr_t>(backing_raw);
            try
            {
                node.root_bytes.assign(kRollbackTimerNodeRootBytes, 0);
                node.backing_bytes.assign(kRollbackTimerNodeBackingBytes, 0);
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }

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
                return !required;

            try
            {
                history.nodes.push_back(std::move(node));
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
            return true;
        };

        bool ok = capture_node(history.root, true);
        if (!history.nodes.empty())
        {
            history.root_bytes = history.nodes[0].root_bytes;
            history.backing_bytes = history.nodes[0].backing_bytes;
        }

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

                    const size_t before_count = history.nodes.size();
                    ok &= capture_node(node_addr, false);
                    history.indexed_captured[i] =
                        history.nodes.size() != before_count
                        || node_addr == history.root;
                    if (history.indexed_captured[i])
                        ++history.indexed_captured_count;
                }
            }
        }

        history.ok = ok;
        if (ok)
        {
            RollbackHash h {};
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
            for (const auto& bytes : history.indexed_object_bytes)
                h.add_bytes(bytes.data(), bytes.size());
            h.add_scalar(history.indexed_nonzero_count);
            h.add_scalar(history.indexed_captured_count);
            h.add_scalar(history.indexed_object_captured_count);
            h.add_bytes(history.child, sizeof(history.child));
            h.add_scalar(history.nodes.size());
            for (const auto& node : history.nodes)
            {
                h.add_scalar(node.root);
                h.add_scalar(node.backing);
                h.add_bytes(node.root_bytes.data(), node.root_bytes.size());
                h.add_bytes(
                    node.backing_bytes.data(), node.backing_bytes.size());
            }
            history.hash = h.value;
        }
        frame.timer_node_hash = history.hash;
        return ok;
    }

    static inline bool RollbackRestoreTimerNodeHistory(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
        const auto& history = frame.timer_node;
        if (!history.ok
            || history.root_bytes.size() != kRollbackTimerNodeRootBytes
            || history.backing_bytes.size() != kRollbackTimerNodeBackingBytes)
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

        bool ok = SafeWriteBytes(
            reinterpret_cast<void*>(history.root),
            history.root_bytes.data(),
            history.root_bytes.size());
        ok &= SafeWriteBytes(
            reinterpret_cast<void*>(history.backing),
            history.backing_bytes.data(),
            history.backing_bytes.size());
        for (size_t i = 0; i < 0x10; ++i)
        {
            if (!history.indexed_object_captured[i]
                || !history.indexed_root[i])
            {
                continue;
            }
            ok &= SafeWriteBytes(
                reinterpret_cast<void*>(history.indexed_root[i]),
                history.indexed_object_bytes[i].data(),
                history.indexed_object_bytes[i].size());
        }
        for (const auto& node : history.nodes)
        {
            if (node.root == history.root)
                continue;
            void* backing_raw = nullptr;
            const bool backing_ok =
                SafeReadPtr(
                    reinterpret_cast<const uint8_t*>(node.root) + 0x08,
                    &backing_raw)
                && reinterpret_cast<uintptr_t>(backing_raw) == node.backing;
            if (!backing_ok)
            {
                ok = false;
                continue;
            }
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

    static inline RollbackHgCpuSnapshotReport CaptureRollbackHgCpuSnapshot(
        uintptr_t image_base,
        RollbackHgCpuSnapshotFrame& out) noexcept
    {
        out.clear();
        if (!RollbackCaptureMotionBankHistory(image_base, out))
        {
            out.clear();
            RollbackHgCpuSnapshotReport report {};
            report.ok = false;
            report.failure = "motion-bank-history-capture-failed";
            return report;
        }
        if (!RollbackCaptureMoveVMMotionTail(image_base, out))
        {
            out.clear();
            RollbackHgCpuSnapshotReport report {};
            report.ok = false;
            report.failure = "motion-tail-capture-failed";
            return report;
        }

        out.bytes.assign(kRollbackHgCpuSnapshotBytes, 0);
        RollbackHgCpuSnapshotReport report = RollbackInvokeHgCpuSnapshot(
            image_base, kRollbackRVA_ExecMoveChangeAndPost, out.bytes, true);
        if (!report.ok)
        {
            out.clear();
            return report;
        }
        if (!RollbackRestoreMotionBankHistory(image_base, out))
        {
            out.clear();
            report.ok = false;
            report.failure = "motion-bank-history-restore-after-capture-failed";
            return report;
        }
        if (!RollbackRestoreMoveVMMotionTail(image_base, out))
        {
            out.clear();
            report.ok = false;
            report.failure = "motion-tail-restore-after-capture-failed";
            return report;
        }

        if (!RollbackCaptureKHitTopology(image_base, out))
        {
            out.clear();
            report.ok = false;
            report.failure = "khit-topology-capture-failed";
            return report;
        }
        if (!RollbackCaptureTimerNodeHistory(image_base, out))
        {
            out.clear();
            report.ok = false;
            report.failure = "timer-node-history-capture-failed";
            return report;
        }

        out.used_bytes = report.cursor;
        out.byte_hash = report.hash;
        out.hash = RollbackHashCombine(
            RollbackHashCombine(
                RollbackHashCombine(out.byte_hash, out.khit_topology_hash),
                out.motion_bank_hash),
            RollbackHashCombine(out.motion_tail_hash, out.timer_node_hash));
        return report;
    }

    static inline RollbackHgCpuSnapshotReport RestoreRollbackHgCpuSnapshot(
        uintptr_t image_base,
        const RollbackHgCpuSnapshotFrame& frame) noexcept
    {
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
            && !RollbackRestoreTimerNodeHistory(image_base, frame))
        {
            report.ok = false;
            report.failure = "timer-node-history-restore-failed";
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
}
