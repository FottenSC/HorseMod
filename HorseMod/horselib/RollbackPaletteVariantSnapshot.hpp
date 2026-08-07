#pragma once

#include "RollbackHgCpuSnapshot.hpp"
#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Horse
{
    // Ghidra: CBattleSerializeStream<char, 163840> is an inline 0x28018-byte
    // object: vtable, write cursor, read cursor, then 0x28000 payload bytes.
    // Four instances start at active-session +0xA8. MoveVM can retain one of
    // these checkpoints across many rollback frames, so restoring only the
    // four public slot scalars is insufficient.
    static constexpr size_t kRollbackPaletteVariantSlotCount = 4;
    static constexpr size_t kRollbackPaletteVariantObjectBytes = 0x28018;
    static constexpr size_t kRollbackPaletteVariantPayloadBytes = 0x28000;
    static constexpr size_t kRollbackPaletteVariantPayloadOffset = 0x18;
    static constexpr size_t kRollbackPaletteVariantTotalPayloadBytes =
        kRollbackPaletteVariantSlotCount
        * kRollbackPaletteVariantPayloadBytes;
    static constexpr uintptr_t kRollbackPaletteVariantSlotsRva = 0x4100B20;
    static constexpr uintptr_t kRollbackPaletteVariantActiveSessionRva =
        0x4843F00;
    static constexpr uintptr_t kRollbackPaletteVariantBufferVtableRva =
        0x3E87118;
    static constexpr uintptr_t kRollbackPaletteVariantFirstBufferOffset = 0xA8;
    static constexpr size_t kRollbackPaletteVariantMaximumNodesPerPlayer =
        kRollbackKHitMaximumSnapshotNodes;
    static constexpr size_t kRollbackPaletteVariantMaximumNodesPerSlot =
        2 * kRollbackPaletteVariantMaximumNodesPerPlayer;
    static constexpr size_t kRollbackPaletteVariantMaximumWriterNodes =
        kRollbackPaletteVariantSlotCount
        * kRollbackPaletteVariantMaximumNodesPerSlot;

    struct RollbackPaletteVariantWriterNode
    {
        uint16_t node_index {0};
        uint16_t writer_bytes {0};
        uint8_t player {0};
        uint8_t list_index {0};
        uint8_t writer_tag {0xff};
        uint8_t reserved {0};
    };
    static_assert(sizeof(RollbackPaletteVariantWriterNode) == 8);

    struct RollbackPaletteVariantWriterLayout
    {
        bool valid {false};
        uint32_t payload_bytes {0};
        uint32_t record_bytes[2] {};
        uint32_t node_stream_bytes[2] {};
        uint16_t node_count[2] {};
        uint64_t payload_identity_hash {0};
        uint64_t descriptor_hash {0};
        uint64_t producer_serial {0};
    };

    // The native palette stream may outlive the KHit topology that authored
    // it. Keep that writer-time, pointer-free layout independently of the
    // current frame so retained payloads are never parsed using later state.
    struct RollbackPaletteVariantWriterRegistry
    {
        uintptr_t active_session {0};
        std::array<RollbackPaletteVariantWriterLayout,
            kRollbackPaletteVariantSlotCount> slots {};
        std::array<bool, kRollbackPaletteVariantSlotCount>
            producer_pending {};
        std::vector<RollbackPaletteVariantWriterNode> nodes;
        uint64_t next_producer_serial {0};

        void reset_metadata() noexcept
        {
            active_session = 0;
            slots = {};
            producer_pending = {};
        }
    };

    struct RollbackPaletteVariantSlotSnapshot
    {
        int32_t state {-1};
        uint32_t payload_bytes {0};
        uint64_t write_cursor {0};
        uint64_t read_cursor {0};
        uint64_t integrity_hash {0};
        uint64_t semantic_hash {0};
        RollbackPaletteVariantWriterLayout writer_layout {};
        bool payload_captured {false};
    };

    struct RollbackPaletteVariantSnapshot
    {
        std::array<RollbackPaletteVariantSlotSnapshot,
            kRollbackPaletteVariantSlotCount> slots {};
        std::vector<uint8_t> payload;
        std::vector<RollbackPaletteVariantWriterNode> writer_nodes;
        uintptr_t active_session {0};
        uint8_t active_mask {0};
        uint64_t integrity_hash {0};
        uint64_t canonical_hash {0};

        void clear()
        {
            slots = {};
            for (auto& slot : slots) slot.state = -1;
            payload.clear();
            writer_nodes.clear();
            active_session = 0;
            active_mask = 0;
            integrity_hash = 0;
            canonical_hash = 0;
        }

        void recycle_for_capture() noexcept
        {
            slots = {};
            for (auto& slot : slots) slot.state = -1;
            active_session = 0;
            active_mask = 0;
            integrity_hash = 0;
            canonical_hash = 0;
        }
    };

    struct RollbackPaletteVariantSnapshotReport
    {
        bool ok {false};
        uint8_t active_mask {0};
        uint32_t copied_bytes {0};
        uintptr_t active_session {0};
        uint64_t semantic_hash {0};
        const char* failure {"not-run"};
    };

    static inline const uint8_t* RollbackPaletteVariantSlotPayload(
        const RollbackPaletteVariantSnapshot& snapshot,
        size_t slot) noexcept
    {
        const size_t offset = slot * kRollbackPaletteVariantPayloadBytes;
        return slot < kRollbackPaletteVariantSlotCount
                && snapshot.payload.size()
                    == kRollbackPaletteVariantTotalPayloadBytes
            ? snapshot.payload.data() + offset : nullptr;
    }

    static inline uint8_t* RollbackPaletteVariantSlotPayload(
        RollbackPaletteVariantSnapshot& snapshot,
        size_t slot) noexcept
    {
        return const_cast<uint8_t*>(RollbackPaletteVariantSlotPayload(
            static_cast<const RollbackPaletteVariantSnapshot&>(snapshot),
            slot));
    }

    static inline size_t RollbackPaletteVariantWriterNodeBase(
        size_t slot,
        size_t player) noexcept
    {
        return slot * kRollbackPaletteVariantMaximumNodesPerSlot
            + player * kRollbackPaletteVariantMaximumNodesPerPlayer;
    }

    static inline const std::vector<RollbackPaletteVariantWriterNode>&
    RollbackPaletteVariantWriterNodes(
        const RollbackPaletteVariantWriterRegistry& storage) noexcept
    {
        return storage.nodes;
    }

    static inline const std::vector<RollbackPaletteVariantWriterNode>&
    RollbackPaletteVariantWriterNodes(
        const RollbackPaletteVariantSnapshot& storage) noexcept
    {
        return storage.writer_nodes;
    }

    template <typename Storage>
    static inline bool PrepareRollbackPaletteVariantStorage(
        Storage& storage,
        bool require_preallocated) noexcept
    {
        if (storage.nodes.size()
            == kRollbackPaletteVariantMaximumWriterNodes)
        {
            return true;
        }
        if (require_preallocated) return false;
        try
        {
            storage.nodes.resize(kRollbackPaletteVariantMaximumWriterNodes);
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    static inline bool PrepareRollbackPaletteVariantStorage(
        RollbackPaletteVariantSnapshot& snapshot,
        bool require_preallocated) noexcept
    {
        if (snapshot.payload.size()
                != kRollbackPaletteVariantTotalPayloadBytes
            || snapshot.writer_nodes.size()
                != kRollbackPaletteVariantMaximumWriterNodes)
        {
            if (require_preallocated) return false;
            try
            {
                snapshot.payload.resize(
                    kRollbackPaletteVariantTotalPayloadBytes);
                snapshot.writer_nodes.resize(
                    kRollbackPaletteVariantMaximumWriterNodes);
            }
            catch (...)
            {
                return false;
            }
        }
        return true;
    }

    static inline uint64_t HashRollbackPaletteVariantPayloadIdentity(
        const uint8_t* bytes,
        uint32_t payload_bytes) noexcept
    {
        if (!bytes || payload_bytes == 0
            || payload_bytes > kRollbackPaletteVariantPayloadBytes)
        {
            return 0;
        }
        RollbackFastHash hash {};
        hash.add_scalar(payload_bytes);
        hash.add_bytes(bytes, payload_bytes);
        return hash.finish();
    }

    template <typename Storage>
    static inline uint64_t HashRollbackPaletteVariantWriterLayout(
        const Storage& storage,
        size_t slot,
        const RollbackPaletteVariantWriterLayout& layout) noexcept
    {
        if (slot >= kRollbackPaletteVariantSlotCount
            || !layout.valid
            || RollbackPaletteVariantWriterNodes(storage).size()
                != kRollbackPaletteVariantMaximumWriterNodes)
        {
            return 0;
        }
        RollbackFastHash hash {};
        hash.add_scalar(layout.payload_bytes);
        for (size_t player = 0; player < 2; ++player)
        {
            if (layout.node_count[player]
                > kRollbackPaletteVariantMaximumNodesPerPlayer)
            {
                return 0;
            }
            hash.add_scalar(layout.record_bytes[player]);
            hash.add_scalar(layout.node_stream_bytes[player]);
            hash.add_scalar(layout.node_count[player]);
            const size_t base =
                RollbackPaletteVariantWriterNodeBase(slot, player);
            for (size_t i = 0; i < layout.node_count[player]; ++i)
            {
                const auto& node =
                    RollbackPaletteVariantWriterNodes(storage)[base + i];
                if (node.player != player || node.writer_bytes == 0)
                    return 0;
                hash.add_scalar(node.player);
                hash.add_scalar(node.list_index);
                hash.add_scalar(node.node_index);
                hash.add_scalar(node.writer_tag);
                hash.add_scalar(node.writer_bytes);
            }
        }
        return hash.finish();
    }

    template <typename Storage>
    static inline bool ValidateRollbackPaletteVariantWriterLayout(
        const Storage& storage,
        size_t slot,
        const RollbackPaletteVariantWriterLayout& layout) noexcept
    {
        if (!layout.valid || layout.payload_bytes == 0
            || layout.payload_bytes > kRollbackPaletteVariantPayloadBytes
            || layout.record_bytes[0] > layout.payload_bytes
            || layout.record_bytes[1]
                > layout.payload_bytes - layout.record_bytes[0]
            || layout.payload_identity_hash == 0
            || layout.descriptor_hash == 0
            || layout.producer_serial == 0)
        {
            return false;
        }
        for (size_t player = 0; player < 2; ++player)
        {
            const uint32_t fixed =
                static_cast<uint32_t>(kRollbackHgCpuAiResetSlotEnd
                    + kRollbackHgCpuHitAreaFixedBytes
                    + kRollbackHgCpuHitAreaRelocBytes);
            if (layout.record_bytes[player] < fixed
                || layout.node_stream_bytes[player]
                    != layout.record_bytes[player] - fixed
                || layout.node_count[player]
                    > kRollbackPaletteVariantMaximumNodesPerPlayer)
            {
                return false;
            }
            uint32_t stream_bytes = 0;
            const size_t base =
                RollbackPaletteVariantWriterNodeBase(slot, player);
            for (size_t i = 0; i < layout.node_count[player]; ++i)
            {
                const auto& node =
                    RollbackPaletteVariantWriterNodes(storage)[base + i];
                if (node.player != player || node.writer_bytes == 0
                    || stream_bytes
                        > layout.node_stream_bytes[player]
                    || node.writer_bytes
                        > layout.node_stream_bytes[player] - stream_bytes)
                {
                    return false;
                }
                stream_bytes += node.writer_bytes;
            }
            if (stream_bytes != layout.node_stream_bytes[player])
                return false;
        }
        return HashRollbackPaletteVariantWriterLayout(
                   storage, slot, layout)
            == layout.descriptor_hash;
    }

    static inline bool CaptureRollbackPaletteVariantWriterLayout(
        size_t slot,
        uint32_t payload_bytes,
        uint64_t payload_identity_hash,
        const RollbackHgCpuSnapshotFrame& current_layout,
        RollbackPaletteVariantWriterRegistry& registry) noexcept
    {
        if (slot >= kRollbackPaletteVariantSlotCount
            || payload_bytes == 0 || payload_identity_hash == 0
            || !current_layout.khit_topology_ok
            || !PrepareRollbackPaletteVariantStorage(registry, true))
        {
            return false;
        }
        RollbackPaletteVariantWriterLayout candidate {};
        candidate.valid = true;
        candidate.payload_bytes = payload_bytes;
        candidate.payload_identity_hash = payload_identity_hash;
        candidate.producer_serial = ++registry.next_producer_serial;
        if (candidate.producer_serial == 0)
            candidate.producer_serial = ++registry.next_producer_serial;
        uint64_t total = 0;
        for (size_t player = 0; player < 2; ++player)
        {
            const auto& topology = current_layout.khit_topology[player];
            if (!topology.ok
                || topology.nodes.size()
                    > kRollbackPaletteVariantMaximumNodesPerPlayer)
            {
                return false;
            }
            const size_t record =
                RollbackHgCpuCharaRecordBytes(&current_layout, player);
            if (record > UINT32_MAX || total + record > payload_bytes)
                return false;
            candidate.record_bytes[player] =
                static_cast<uint32_t>(record);
            candidate.node_stream_bytes[player] =
                static_cast<uint32_t>(topology.node_stream_bytes);
            candidate.node_count[player] =
                static_cast<uint16_t>(topology.nodes.size());
            uint32_t stream_bytes = 0;
            const size_t base =
                RollbackPaletteVariantWriterNodeBase(slot, player);
            for (size_t i = 0; i < topology.nodes.size(); ++i)
            {
                const auto& source = topology.nodes[i];
                if (source.writer_bytes == 0
                    || source.writer_bytes > UINT16_MAX
                    || stream_bytes > topology.node_stream_bytes
                    || source.writer_bytes
                        > topology.node_stream_bytes - stream_bytes)
                {
                    return false;
                }
                registry.nodes[base + i] = {
                    source.node_index,
                    static_cast<uint16_t>(source.writer_bytes),
                    static_cast<uint8_t>(player),
                    source.list_index,
                    source.writer_tag,
                    0};
                stream_bytes += static_cast<uint32_t>(
                    source.writer_bytes);
            }
            if (stream_bytes != topology.node_stream_bytes)
                return false;
            total += record;
        }
        // ExecMoveChangeAndPost writes the two variable-sized chara records
        // first, then appends global/RNG, camera/timer, motion/physics,
        // terrain, and VFX sections. The writer-time KHit layout describes
        // only the chara prefix; the complete payload is still retained for
        // exact same-process restore.
        if (total > payload_bytes) return false;
        candidate.descriptor_hash =
            HashRollbackPaletteVariantWriterLayout(
                registry, slot, candidate);
        if (!ValidateRollbackPaletteVariantWriterLayout(
                registry, slot, candidate))
        {
            return false;
        }
        registry.slots[slot] = candidate;
        registry.producer_pending[slot] = true;
        return true;
    }

    enum class RollbackPaletteVariantWriterObservation : uint8_t
    {
        NotPaletteBuffer,
        Observed,
        StorageNotReady,
        SessionChanged,
        BufferInvalid,
        TopologyInvalid,
    };

    static inline bool RollbackPaletteVariantBufferSlot(
        uintptr_t buffer,
        uintptr_t active_session,
        size_t& slot) noexcept
    {
        slot = 0;
        if (!buffer || !active_session
            || buffer < active_session
                + kRollbackPaletteVariantFirstBufferOffset)
        {
            return false;
        }
        const uintptr_t relative = buffer
            - active_session - kRollbackPaletteVariantFirstBufferOffset;
        if (relative % kRollbackPaletteVariantObjectBytes != 0)
            return false;
        slot =
            static_cast<size_t>(relative / kRollbackPaletteVariantObjectBytes);
        return slot < kRollbackPaletteVariantSlotCount;
    }

    static inline RollbackPaletteVariantWriterObservation
    ObserveRollbackPaletteVariantWriterFromLayout(
        uintptr_t buffer,
        uintptr_t active_session,
        uintptr_t expected_vtable,
        const RollbackHgCpuSnapshotFrame& writer_layout,
        RollbackPaletteVariantWriterRegistry& registry) noexcept
    {
        if (!buffer || !active_session || !expected_vtable)
            return RollbackPaletteVariantWriterObservation::BufferInvalid;
        size_t slot = 0;
        if (!RollbackPaletteVariantBufferSlot(
                buffer, active_session, slot))
            return RollbackPaletteVariantWriterObservation::NotPaletteBuffer;
        if (!PrepareRollbackPaletteVariantStorage(registry, true))
            return RollbackPaletteVariantWriterObservation::StorageNotReady;
        if (registry.active_session != 0
            && registry.active_session != active_session)
        {
            return RollbackPaletteVariantWriterObservation::SessionChanged;
        }
        void* vtable_raw = nullptr;
        uint64_t write_cursor = 0;
        if (!SafeReadPtr(reinterpret_cast<const void*>(buffer), &vtable_raw)
            || reinterpret_cast<uintptr_t>(vtable_raw) != expected_vtable
            || !SafeReadBytes(reinterpret_cast<const void*>(buffer + 8),
                &write_cursor, sizeof(write_cursor))
            || write_cursor == 0
            || write_cursor > kRollbackPaletteVariantPayloadBytes)
        {
            return RollbackPaletteVariantWriterObservation::BufferInvalid;
        }
        RollbackFastHash payload_hash {};
        payload_hash.add_scalar(static_cast<uint32_t>(write_cursor));
        std::array<uint8_t, 4096> chunk {};
        size_t copied = 0;
        while (copied < write_cursor)
        {
            const size_t bytes = (std::min)(
                chunk.size(), static_cast<size_t>(write_cursor) - copied);
            if (!SafeReadBytes(reinterpret_cast<const void*>(
                    buffer + kRollbackPaletteVariantPayloadOffset + copied),
                    chunk.data(), bytes))
            {
                return RollbackPaletteVariantWriterObservation::BufferInvalid;
            }
            payload_hash.add_bytes(chunk.data(), bytes);
            copied += bytes;
        }
        const uint64_t identity = payload_hash.finish();
        registry.active_session = active_session;
        if (!CaptureRollbackPaletteVariantWriterLayout(
                slot, static_cast<uint32_t>(write_cursor), identity,
                writer_layout, registry))
        {
            return RollbackPaletteVariantWriterObservation::TopologyInvalid;
        }
        return RollbackPaletteVariantWriterObservation::Observed;
    }

    static inline bool CopyRollbackPaletteVariantWriterLayout(
        size_t slot,
        const RollbackPaletteVariantWriterRegistry& source,
        const RollbackPaletteVariantWriterLayout& layout,
        RollbackPaletteVariantSnapshot& destination) noexcept
    {
        if (!ValidateRollbackPaletteVariantWriterLayout(
                source, slot, layout)
            || destination.writer_nodes.size()
                != kRollbackPaletteVariantMaximumWriterNodes)
        {
            return false;
        }
        for (size_t player = 0; player < 2; ++player)
        {
            const size_t base =
                RollbackPaletteVariantWriterNodeBase(slot, player);
            std::copy_n(source.nodes.data() + base,
                layout.node_count[player],
                destination.writer_nodes.data() + base);
        }
        destination.slots[slot].writer_layout = layout;
        return true;
    }

    static inline uint64_t HashRollbackPaletteVariantPayloadCanonical(
        const uint8_t* bytes,
        size_t effective,
        size_t slot,
        const RollbackPaletteVariantSnapshot& snapshot,
        const RollbackPaletteVariantWriterLayout& layout) noexcept
    {
        if (!bytes || effective != layout.payload_bytes
            || !ValidateRollbackPaletteVariantWriterLayout(
                snapshot, slot, layout))
        {
            return 0;
        }
        RollbackFastHash hash {};
        hash.add_scalar(effective);
        size_t base = 0;
        for (size_t player = 0; player < 2; ++player)
        {
            const size_t record = layout.record_bytes[player];
            if (base > effective || record > effective - base
                || !RollbackAddHgCpuCanonicalCharaBytes(
                    hash, bytes + base, record, 0,
                    kRollbackHgCpuHitAreaLocalStart
                        + kRollbackHgCpuHitAreaFixedBytes))
            {
                return 0;
            }
            size_t cursor = base + kRollbackHgCpuHitAreaLocalStart
                + kRollbackHgCpuHitAreaFixedBytes;
            const size_t stream_end =
                cursor + layout.node_stream_bytes[player];
            hash.add_scalar(layout.node_count[player]);
            const size_t node_base =
                RollbackPaletteVariantWriterNodeBase(slot, player);
            for (size_t i = 0; i < layout.node_count[player]; ++i)
            {
                const auto& node = snapshot.writer_nodes[node_base + i];
                if (node.writer_bytes > stream_end - cursor)
                    return 0;
                hash.add_scalar(node.list_index);
                hash.add_scalar(node.node_index);
                hash.add_scalar(node.writer_tag);
                hash.add_scalar(node.writer_bytes);
                hash.add_bytes(bytes + cursor, node.writer_bytes);
                cursor += node.writer_bytes;
            }
            if (cursor != stream_end) return 0;
            base += record;
        }
        if (base > effective) return 0;
        // The native segment table relocates pointer-bearing fields while
        // serializing. Conservatively compare the complete remaining
        // global/RNG, camera/timer, motion/physics, terrain, and VFX tail.
        // If a future build exposes a process-local field here, fail closed
        // until that exact field has an evidence-backed canonical policy.
        hash.add_bytes(bytes + base, effective - base);
        return hash.finish();
    }

    static inline uint64_t HashRollbackPaletteVariantIntegrity(
        const RollbackPaletteVariantSnapshot& snapshot) noexcept
    {
        if (snapshot.payload.size()
                != kRollbackPaletteVariantTotalPayloadBytes
            || snapshot.writer_nodes.size()
                != kRollbackPaletteVariantMaximumWriterNodes)
        {
            return 0;
        }
        RollbackHash hash {};
        hash.add_scalar(snapshot.active_session);
        hash.add_scalar(snapshot.active_mask);
        for (size_t slot = 0; slot < snapshot.slots.size(); ++slot)
        {
            const auto& saved = snapshot.slots[slot];
            hash.add_scalar(saved.state);
            hash.add_scalar(saved.payload_bytes);
            hash.add_scalar(saved.write_cursor);
            hash.add_scalar(saved.read_cursor);
            hash.add_scalar(saved.semantic_hash);
            hash.add_scalar(saved.writer_layout.valid);
            hash.add_scalar(saved.writer_layout.payload_bytes);
            hash.add_scalar(saved.writer_layout.record_bytes[0]);
            hash.add_scalar(saved.writer_layout.record_bytes[1]);
            hash.add_scalar(saved.writer_layout.node_stream_bytes[0]);
            hash.add_scalar(saved.writer_layout.node_stream_bytes[1]);
            hash.add_scalar(saved.writer_layout.node_count[0]);
            hash.add_scalar(saved.writer_layout.node_count[1]);
            hash.add_scalar(saved.writer_layout.payload_identity_hash);
            hash.add_scalar(saved.writer_layout.descriptor_hash);
            hash.add_scalar(saved.writer_layout.producer_serial);
            hash.add_scalar(saved.payload_captured);
            if (saved.payload_captured)
            {
                const uint8_t* bytes =
                    RollbackPaletteVariantSlotPayload(snapshot, slot);
                if (!bytes
                    || saved.payload_bytes
                        > kRollbackPaletteVariantPayloadBytes)
                {
                    return 0;
                }
                hash.add_bytes(bytes, saved.payload_bytes);
                for (size_t player = 0; player < 2; ++player)
                {
                    const size_t base =
                        RollbackPaletteVariantWriterNodeBase(slot, player);
                    hash.add_bytes(
                        snapshot.writer_nodes.data() + base,
                        saved.writer_layout.node_count[player]
                            * sizeof(RollbackPaletteVariantWriterNode));
                }
            }
        }
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackPaletteVariantSlotIntegrity(
        const RollbackPaletteVariantSnapshot& snapshot,
        size_t slot) noexcept
    {
        if (slot >= snapshot.slots.size()) return 0;
        const auto& saved = snapshot.slots[slot];
        const uint8_t* bytes =
            RollbackPaletteVariantSlotPayload(snapshot, slot);
        if (!saved.payload_captured || !bytes
            || saved.payload_bytes > kRollbackPaletteVariantPayloadBytes)
        {
            return 0;
        }
        RollbackHash hash {};
        hash.add_scalar(saved.state);
        hash.add_scalar(saved.write_cursor);
        hash.add_scalar(saved.read_cursor);
        hash.add_scalar(saved.writer_layout.payload_identity_hash);
        hash.add_scalar(saved.writer_layout.descriptor_hash);
        hash.add_bytes(bytes, saved.payload_bytes);
        for (size_t player = 0; player < 2; ++player)
        {
            const size_t base =
                RollbackPaletteVariantWriterNodeBase(slot, player);
            hash.add_bytes(
                snapshot.writer_nodes.data() + base,
                saved.writer_layout.node_count[player]
                    * sizeof(RollbackPaletteVariantWriterNode));
        }
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackPaletteVariantCanonical(
        const RollbackPaletteVariantSnapshot& snapshot) noexcept
    {
        // Raw process pointers remain local integrity only. Each active slot's
        // semantic_hash is produced by the same pointer-normalized HgCpu
        // chara/KHit canonicalization used by the ordinary rollback snapshot.
        RollbackHash hash {};
        hash.add_scalar(snapshot.active_mask);
        for (const auto& saved : snapshot.slots)
        {
            hash.add_scalar(saved.state);
            hash.add_scalar(saved.write_cursor);
            hash.add_scalar(saved.read_cursor);
            hash.add_scalar(saved.payload_captured);
            hash.add_scalar(saved.writer_layout.descriptor_hash);
            hash.add_scalar(saved.semantic_hash);
        }
        return hash.value ? hash.value : 1;
    }

    static inline bool ValidateRollbackPaletteVariantSnapshot(
        const RollbackPaletteVariantSnapshot& snapshot) noexcept
    {
        if (snapshot.payload.size()
                != kRollbackPaletteVariantTotalPayloadBytes
            || snapshot.writer_nodes.size()
                != kRollbackPaletteVariantMaximumWriterNodes
            || snapshot.active_session == 0
            || snapshot.integrity_hash == 0
            || snapshot.canonical_hash == 0)
        {
            return false;
        }
        uint8_t active_mask = 0;
        for (size_t slot = 0; slot < snapshot.slots.size(); ++slot)
        {
            const auto& saved = snapshot.slots[slot];
            const bool active = saved.state >= 0;
            if (active) active_mask |= static_cast<uint8_t>(1u << slot);
            if (active != saved.payload_captured
                || saved.payload_bytes
                    > kRollbackPaletteVariantPayloadBytes
                || saved.write_cursor
                    > kRollbackPaletteVariantPayloadBytes
                || saved.read_cursor
                    > kRollbackPaletteVariantPayloadBytes
                || saved.read_cursor > saved.write_cursor
                || saved.payload_bytes != saved.write_cursor)
            {
                return false;
            }
            if (active
                && saved.integrity_hash
                    != HashRollbackPaletteVariantSlotIntegrity(
                        snapshot, slot))
            {
                return false;
            }
            if (active
                && (saved.semantic_hash == 0
                    || !ValidateRollbackPaletteVariantWriterLayout(
                        snapshot, slot, saved.writer_layout)
                    || saved.writer_layout.payload_bytes
                        != saved.payload_bytes
                    || saved.writer_layout.payload_identity_hash
                        != HashRollbackPaletteVariantPayloadIdentity(
                            RollbackPaletteVariantSlotPayload(
                                snapshot, slot),
                            saved.payload_bytes)
                    || saved.semantic_hash
                        != HashRollbackPaletteVariantPayloadCanonical(
                            RollbackPaletteVariantSlotPayload(
                                snapshot, slot),
                            saved.payload_bytes, slot, snapshot,
                            saved.writer_layout)))
            {
                return false;
            }
            if (!active && saved.integrity_hash != 0) return false;
            if (!active && saved.semantic_hash != 0) return false;
            if (!active && saved.writer_layout.valid) return false;
        }
        return active_mask == snapshot.active_mask
            && HashRollbackPaletteVariantIntegrity(snapshot)
                == snapshot.integrity_hash
            && HashRollbackPaletteVariantCanonical(snapshot)
                == snapshot.canonical_hash;
    }

    static inline RollbackPaletteVariantSnapshotReport
    CaptureRollbackPaletteVariantSnapshotFromLayout(
        const int32_t* live_states,
        uintptr_t active_session,
        uintptr_t expected_vtable,
        RollbackPaletteVariantSnapshot& out,
        RollbackPaletteVariantWriterRegistry& writer_registry,
        bool require_preallocated) noexcept
    {
        RollbackPaletteVariantSnapshotReport report {};
        report.failure = "ok";
        if (!live_states || !active_session || !expected_vtable)
        {
            report.failure = "palette-variant-context-not-ready";
            return report;
        }
        if (!PrepareRollbackPaletteVariantStorage(
                out, require_preallocated)
            || !PrepareRollbackPaletteVariantStorage(
                writer_registry, require_preallocated))
        {
            report.failure = require_preallocated
                ? "palette-variant-storage-not-preallocated"
                : "palette-variant-storage-allocation-failed";
            return report;
        }

        out.recycle_for_capture();
        out.active_session = active_session;
        if (writer_registry.active_session != 0
            && writer_registry.active_session != active_session)
        {
            report.failure = "palette-variant-writer-session-changed";
            return report;
        }
        writer_registry.active_session = active_session;
        std::array<int32_t, kRollbackPaletteVariantSlotCount> states {};
        if (!SafeReadBytes(live_states, states.data(), sizeof(states)))
        {
            report.failure = "palette-variant-state-read-failed";
            return report;
        }
        for (size_t slot = 0; slot < states.size(); ++slot)
        {
            auto& saved = out.slots[slot];
            saved.state = states[slot];
            if (saved.state < 0) continue;

            const uintptr_t object = active_session
                + kRollbackPaletteVariantFirstBufferOffset
                + slot * kRollbackPaletteVariantObjectBytes;
            void* vtable_raw = nullptr;
            if (!SafeReadPtr(
                    reinterpret_cast<const void*>(object), &vtable_raw)
                || reinterpret_cast<uintptr_t>(vtable_raw)
                    != expected_vtable
                || !SafeReadBytes(reinterpret_cast<const void*>(object + 8),
                    &saved.write_cursor, sizeof(saved.write_cursor))
                || !SafeReadBytes(reinterpret_cast<const void*>(object + 0x10),
                    &saved.read_cursor, sizeof(saved.read_cursor))
                || saved.write_cursor
                    > kRollbackPaletteVariantPayloadBytes
                || saved.read_cursor
                    > kRollbackPaletteVariantPayloadBytes)
            {
                report.failure = "palette-variant-buffer-header-invalid";
                return report;
            }
            saved.payload_bytes =
                static_cast<uint32_t>(saved.write_cursor);
            if (saved.read_cursor > saved.write_cursor)
            {
                report.failure = "palette-variant-buffer-cursor-invalid";
                return report;
            }
            saved.payload_captured = true;
            uint8_t* destination =
                RollbackPaletteVariantSlotPayload(out, slot);
            if (!destination
                || (saved.payload_bytes != 0
                    && !SafeReadBytes(
                        reinterpret_cast<const void*>(
                            object + kRollbackPaletteVariantPayloadOffset),
                        destination, saved.payload_bytes)))
            {
                report.failure = "palette-variant-payload-read-failed";
                return report;
            }
            const uint64_t payload_identity =
                HashRollbackPaletteVariantPayloadIdentity(
                    destination, saved.payload_bytes);
            if (payload_identity == 0)
            {
                report.failure =
                    "palette-variant-payload-identity-failed";
                return report;
            }
            auto& registered = writer_registry.slots[slot];
            if (!registered.valid
                || registered.payload_identity_hash != payload_identity
                || registered.payload_bytes != saved.payload_bytes)
            {
                report.failure = registered.valid
                    ? "palette-variant-writer-layout-stale"
                    : "palette-variant-writer-layout-missing";
                return report;
            }
            if (!CopyRollbackPaletteVariantWriterLayout(
                    slot, writer_registry, writer_registry.slots[slot],
                    out))
            {
                report.failure =
                    "palette-variant-writer-layout-copy-failed";
                return report;
            }
            writer_registry.producer_pending[slot] = false;
            out.active_mask |= static_cast<uint8_t>(1u << slot);
            saved.semantic_hash =
                HashRollbackPaletteVariantPayloadCanonical(
                    destination, saved.payload_bytes, slot, out,
                    saved.writer_layout);
            if (saved.semantic_hash == 0)
            {
                report.failure =
                    "palette-variant-payload-semantic-hash-failed";
                return report;
            }
            saved.integrity_hash =
                HashRollbackPaletteVariantSlotIntegrity(out, slot);
            report.copied_bytes += saved.payload_bytes;
        }
        for (size_t slot = 0; slot < out.slots.size(); ++slot)
        {
            if (out.slots[slot].state < 0)
                writer_registry.slots[slot] = {};
        }
        out.integrity_hash = HashRollbackPaletteVariantIntegrity(out);
        out.canonical_hash = HashRollbackPaletteVariantCanonical(out);
        report.active_mask = out.active_mask;
        report.active_session = out.active_session;
        report.semantic_hash = out.canonical_hash;
        report.ok = ValidateRollbackPaletteVariantSnapshot(out);
        if (!report.ok)
            report.failure = "palette-variant-capture-integrity-failed";
        return report;
    }

    static inline RollbackPaletteVariantSnapshotReport
    CaptureRollbackPaletteVariantSnapshot(
        uintptr_t image_base,
        RollbackPaletteVariantSnapshot& out,
        RollbackPaletteVariantWriterRegistry& writer_registry,
        bool require_preallocated) noexcept
    {
        RollbackPaletteVariantSnapshotReport report {};
        report.failure = "palette-variant-context-not-ready";
        if (!image_base) return report;

        void* active_session_raw = nullptr;
        if (!SafeReadPtr(reinterpret_cast<const void*>(
                image_base + kRollbackPaletteVariantActiveSessionRva),
                &active_session_raw)
            || !active_session_raw)
        {
            return report;
        }
        return CaptureRollbackPaletteVariantSnapshotFromLayout(
            reinterpret_cast<const int32_t*>(
                image_base + kRollbackPaletteVariantSlotsRva),
            reinterpret_cast<uintptr_t>(active_session_raw),
            image_base + kRollbackPaletteVariantBufferVtableRva,
            out, writer_registry, require_preallocated);
    }

    static inline RollbackPaletteVariantSnapshotReport
    RestoreRollbackPaletteVariantSnapshotToLayout(
        int32_t* live_states,
        uintptr_t active_session,
        uintptr_t expected_vtable,
        const RollbackPaletteVariantSnapshot& snapshot,
        RollbackPaletteVariantWriterRegistry& writer_registry) noexcept
    {
        RollbackPaletteVariantSnapshotReport report {};
        report.failure = "ok";
        if (!live_states || !active_session || !expected_vtable
            || !ValidateRollbackPaletteVariantSnapshot(snapshot))
        {
            report.failure = "palette-variant-restore-preflight-failed";
            return report;
        }
        if (snapshot.active_session != active_session)
        {
            report.failure = "palette-variant-active-session-changed";
            return report;
        }
        if (!PrepareRollbackPaletteVariantStorage(writer_registry, true))
        {
            report.failure =
                "palette-variant-writer-registry-not-preallocated";
            return report;
        }

        for (size_t slot = 0; slot < snapshot.slots.size(); ++slot)
        {
            const auto& saved = snapshot.slots[slot];
            if (!saved.payload_captured) continue;
            const uintptr_t object = active_session
                + kRollbackPaletteVariantFirstBufferOffset
                + slot * kRollbackPaletteVariantObjectBytes;
            void* vtable_raw = nullptr;
            if (!SafeReadPtr(
                    reinterpret_cast<const void*>(object), &vtable_raw)
                || reinterpret_cast<uintptr_t>(vtable_raw)
                    != expected_vtable)
            {
                report.failure = "palette-variant-buffer-restore-preflight-failed";
                return report;
            }
        }

        for (size_t slot = 0; slot < snapshot.slots.size(); ++slot)
        {
            const auto& saved = snapshot.slots[slot];
            if (!saved.payload_captured) continue;
            const uintptr_t object = active_session
                + kRollbackPaletteVariantFirstBufferOffset
                + slot * kRollbackPaletteVariantObjectBytes;
            const uint8_t* source =
                RollbackPaletteVariantSlotPayload(snapshot, slot);
            if (!source
                || (saved.payload_bytes != 0
                    && !SafeWriteBytes(
                        reinterpret_cast<void*>(
                            object + kRollbackPaletteVariantPayloadOffset),
                        source, saved.payload_bytes))
                || !SafeWriteBytes(
                    reinterpret_cast<void*>(object + 8),
                    &saved.write_cursor, sizeof(saved.write_cursor))
                || !SafeWriteBytes(
                    reinterpret_cast<void*>(object + 0x10),
                    &saved.read_cursor, sizeof(saved.read_cursor)))
            {
                report.failure = "palette-variant-buffer-restore-failed";
                return report;
            }
            report.copied_bytes += saved.payload_bytes;
        }

        std::array<int32_t, kRollbackPaletteVariantSlotCount> states {};
        for (size_t slot = 0; slot < states.size(); ++slot)
            states[slot] = snapshot.slots[slot].state;
        if (!SafeWriteBytes(live_states, states.data(), sizeof(states)))
        {
            report.failure = "palette-variant-state-restore-failed";
            return report;
        }
        writer_registry.reset_metadata();
        writer_registry.active_session = active_session;
        for (size_t slot = 0; slot < snapshot.slots.size(); ++slot)
        {
            const auto& saved = snapshot.slots[slot];
            if (!saved.payload_captured) continue;
            if (!ValidateRollbackPaletteVariantWriterLayout(
                    snapshot, slot, saved.writer_layout))
            {
                report.failure =
                    "palette-variant-writer-layout-restore-invalid";
                return report;
            }
            const auto& layout = saved.writer_layout;
            for (size_t player = 0; player < 2; ++player)
            {
                const size_t base =
                    RollbackPaletteVariantWriterNodeBase(slot, player);
                std::copy_n(snapshot.writer_nodes.data() + base,
                    layout.node_count[player],
                    writer_registry.nodes.data() + base);
            }
            writer_registry.slots[slot] = layout;
            writer_registry.producer_pending[slot] = false;
            writer_registry.next_producer_serial = (std::max)(
                writer_registry.next_producer_serial,
                layout.producer_serial);
        }
        report.active_mask = snapshot.active_mask;
        report.active_session = snapshot.active_session;
        report.semantic_hash = snapshot.canonical_hash;
        report.ok = true;
        return report;
    }

    static inline RollbackPaletteVariantSnapshotReport
    RestoreRollbackPaletteVariantSnapshot(
        uintptr_t image_base,
        const RollbackPaletteVariantSnapshot& snapshot,
        RollbackPaletteVariantWriterRegistry& writer_registry) noexcept
    {
        RollbackPaletteVariantSnapshotReport report {};
        report.failure = "palette-variant-context-not-ready";
        if (!image_base) return report;
        void* active_session_raw = nullptr;
        if (!SafeReadPtr(reinterpret_cast<const void*>(
                image_base + kRollbackPaletteVariantActiveSessionRva),
                &active_session_raw)
            || !active_session_raw)
        {
            return report;
        }
        return RestoreRollbackPaletteVariantSnapshotToLayout(
            reinterpret_cast<int32_t*>(
                image_base + kRollbackPaletteVariantSlotsRva),
            reinterpret_cast<uintptr_t>(active_session_raw),
            image_base + kRollbackPaletteVariantBufferVtableRva,
            snapshot, writer_registry);
    }
}
