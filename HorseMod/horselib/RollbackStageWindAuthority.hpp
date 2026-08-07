// ============================================================================
// Horse::RollbackStageWindAuthority
//
// Pointer-free, authenticated transfer of the native wind graph's semantic
// node and emitter value state at a frozen stock round boundary. Root
// scheduling remains strict-equal; derived root/fighter outputs follow the
// owner state. Emitter list nodes and object pointers stay process-local.
// External node addresses stay process-local and their
// count/type/order must match; their gameplay payload is owner-authorized.
// Pool nodes may only be reordered or retired, never synthesized. Peer
// pointers are never copied.
// ============================================================================

#pragma once

#include "RollbackStageWindSnapshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uint8_t kRollbackStageWindAuthorityVersion = 12;
    static constexpr uint32_t kRollbackStageWindAuthorityMagic = 0x31575352u;
    static constexpr size_t kRollbackStageWindAuthorityHeaderBytes = 140;
    static constexpr size_t kRollbackStageWindAuthorityNodeHeaderBytes = 20;
    static constexpr size_t kRollbackStageWindAuthorityImageMaxBytes = 25600;
    static constexpr size_t kRollbackStageWindAuthorityChunkBytes = 1024;
    static constexpr uint8_t kRollbackStageWindAuthorityMaxChunks = 25;
    static constexpr uintptr_t kRollbackStageWindFighterSliceOffset = 0x29310;

    static_assert(kRollbackStageWindAuthorityHeaderBytes
            + kRollbackStageWindEmitterMaxCount
                * kRollbackStageWindEmitterMutableBytes
            + kRollbackStageWindGraphMaxNodes
                * (kRollbackStageWindAuthorityNodeHeaderBytes + 0x1B0)
            <= kRollbackStageWindAuthorityImageMaxBytes);
    static_assert(kRollbackStageWindAuthorityImageMaxBytes <= UINT16_MAX);
    static_assert(kRollbackStageWindAuthorityMaxChunks < 32);

    struct RollbackStageWindAuthorityImage
    {
        std::array<uint8_t, kRollbackStageWindAuthorityImageMaxBytes> bytes {};
        uint16_t byte_count {0};
        uint64_t hash {0};
    };

    class RollbackStageWindAuthorityWriter
    {
    public:
        explicit RollbackStageWindAuthorityWriter(
            RollbackStageWindAuthorityImage& image) noexcept
            : m_image(image) { m_image = {}; }

        template<typename T>
        bool scalar(const T& value) noexcept
        {
            return data(&value, sizeof(value));
        }

        bool data(const void* source, size_t bytes) noexcept
        {
            if ((!source && bytes != 0)
                || m_offset > m_image.bytes.size()
                || bytes > m_image.bytes.size() - m_offset)
                return false;
            if (bytes != 0)
                std::memcpy(m_image.bytes.data() + m_offset, source, bytes);
            m_offset += bytes;
            return true;
        }

        size_t offset() const noexcept { return m_offset; }

    private:
        RollbackStageWindAuthorityImage& m_image;
        size_t m_offset {0};
    };

    class RollbackStageWindAuthorityReader
    {
    public:
        explicit RollbackStageWindAuthorityReader(
            const RollbackStageWindAuthorityImage& image) noexcept
            : m_image(image) {}

        template<typename T>
        bool scalar(T& value) noexcept
        {
            return data(&value, sizeof(value));
        }

        bool data(void* destination, size_t bytes) noexcept
        {
            if ((!destination && bytes != 0)
                || m_offset > m_image.byte_count
                || bytes > m_image.byte_count - m_offset)
                return false;
            if (bytes != 0)
                std::memcpy(destination, m_image.bytes.data() + m_offset,
                    bytes);
            m_offset += bytes;
            return true;
        }

        size_t offset() const noexcept { return m_offset; }

    private:
        const RollbackStageWindAuthorityImage& m_image;
        size_t m_offset {0};
    };

    static inline uint64_t RollbackHashStageWindAuthorityBytes(
        const uint8_t* bytes, size_t byte_count) noexcept
    {
        if (!bytes || byte_count == 0
            || byte_count > kRollbackStageWindAuthorityImageMaxBytes)
            return 0;
        RollbackHash hash {};
        hash.add_scalar(byte_count);
        hash.add_bytes(bytes, byte_count);
        return hash.value;
    }

    static inline size_t RollbackStageWindAuthorityNodePayloadBytes(
        uint32_t vtable_rva, uint32_t node_bytes) noexcept
    {
        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindNodeSemanticRanges(vtable_rva, node_bytes);
        if (ranges.count == 0
            || node_bytes <= kRollbackStageWindNodeMutableOffset
            || node_bytes > kRollbackStageWindNodeMaxBytes)
            return 0;
        // Version 12 keeps the original fixed payload dimensions. Bytes not
        // selected by the semantic table are encoded as zero and rejected if
        // nonzero on receipt; they are never copied into the local object.
        return node_bytes - kRollbackStageWindNodeMutableOffset;
    }

    static inline bool RollbackStageWindSemanticRangeContains(
        const RollbackStageWindSemanticRanges& ranges,
        uint32_t offset) noexcept
    {
        for (uint32_t index = 0; index < ranges.count; ++index)
        {
            const auto range = ranges.values[index];
            if (offset >= range.offset
                && offset - range.offset < range.bytes)
                return true;
        }
        return false;
    }

    static inline uint64_t RollbackHashStageWindAuthorityTopology(
        const RollbackStageWindGraphSnapshot& graph) noexcept
    {
        if (!graph.valid || !graph.root
            || graph.count > graph.nodes.size()) return 0;
        RollbackHash hash {};
        hash.add_scalar(graph.count);
        for (uint32_t index = 0; index < graph.count; ++index)
        {
            const auto& node = graph.nodes[index];
            if (!node.address || !node.vtable || node.vtable_rva == 0
                || RollbackStageWindAuthorityNodePayloadBytes(
                    node.vtable_rva, node.bytes) == 0)
                return 0;
            hash.add_scalar(index);
            hash.add_scalar(node.vtable_rva);
            hash.add_scalar(node.bytes);
        }
        return hash.value;
    }

    static inline bool RollbackStageWindAuthorityWriteNodePayload(
        RollbackStageWindAuthorityWriter& writer,
        const RollbackStageWindGraphNode& node) noexcept
    {
        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindNodeSemanticRanges(
                node.vtable_rva, node.bytes);
        const size_t payload_bytes =
            RollbackStageWindAuthorityNodePayloadBytes(
                node.vtable_rva, node.bytes);
        if (ranges.count < 3 || payload_bytes == 0)
            return false;
        std::array<uint8_t,
            kRollbackStageWindNodeMaxBytes
                - kRollbackStageWindNodeMutableOffset> normalized {};
        for (uint32_t index = 0; index < ranges.count; ++index)
        {
            const auto range = ranges.values[index];
            if (range.offset < kRollbackStageWindNodeMutableOffset
                || range.offset > node.bytes
                || range.bytes > node.bytes - range.offset)
                return false;
            std::memcpy(normalized.data()
                    + range.offset
                    - kRollbackStageWindNodeMutableOffset,
                node.data.data() + range.offset, range.bytes);
        }
        return writer.data(normalized.data(), payload_bytes);
    }

    static inline bool RollbackStageWindAuthorityReadNodePayload(
        RollbackStageWindAuthorityReader& reader,
        RollbackStageWindGraphNode& node) noexcept
    {
        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindNodeSemanticRanges(
                node.vtable_rva, node.bytes);
        const size_t payload_bytes =
            RollbackStageWindAuthorityNodePayloadBytes(
                node.vtable_rva, node.bytes);
        if (ranges.count < 3 || payload_bytes == 0)
            return false;
        std::array<uint8_t,
            kRollbackStageWindNodeMaxBytes
                - kRollbackStageWindNodeMutableOffset> normalized {};
        if (!reader.data(normalized.data(), payload_bytes))
            return false;
        for (uint32_t offset = kRollbackStageWindNodeMutableOffset;
             offset < node.bytes; ++offset)
        {
            if (!RollbackStageWindSemanticRangeContains(ranges, offset)
                && normalized[offset
                    - kRollbackStageWindNodeMutableOffset] != 0)
                return false;
        }
        for (uint32_t index = 0; index < ranges.count; ++index)
        {
            const auto range = ranges.values[index];
            if (range.offset < kRollbackStageWindNodeMutableOffset
                || range.offset > node.bytes
                || range.bytes > node.bytes - range.offset)
                return false;
            std::memcpy(node.data.data() + range.offset,
                normalized.data()
                    + range.offset
                    - kRollbackStageWindNodeMutableOffset,
                range.bytes);
        }
        return true;
    }

    static inline bool RollbackStageWindAuthorityWriteEmitterPayload(
        RollbackStageWindAuthorityWriter& writer,
        const RollbackStageWindEmitterRecord& emitter) noexcept
    {
        std::array<uint8_t, kRollbackStageWindEmitterMutableBytes>
            normalized {};
        if (!CopyRollbackStageWindEmitterSemanticBytes(
                emitter.data, normalized))
            return false;
        return writer.data(normalized.data(), normalized.size());
    }

    static inline bool RollbackStageWindAuthorityReadEmitterPayload(
        RollbackStageWindAuthorityReader& reader,
        std::array<uint8_t, kRollbackStageWindEmitterMutableBytes>&
            semantic) noexcept
    {
        std::array<uint8_t, kRollbackStageWindEmitterMutableBytes>
            normalized {};
        if (!reader.data(normalized.data(), normalized.size()))
            return false;
        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindEmitterSemanticRanges();
        for (uint32_t offset = kRollbackStageWindEmitterMutableOffset;
             offset < kRollbackStageWindEmitterSemanticBytes; ++offset)
        {
            if (!RollbackStageWindSemanticRangeContains(ranges, offset)
                && normalized[offset
                    - kRollbackStageWindEmitterMutableOffset] != 0)
                return false;
        }
        semantic.fill(0);
        return CopyRollbackStageWindEmitterSemanticBytes(
            normalized, semantic);
    }

    static inline bool RollbackStageWindAuthorityNodeTypeMatches(
        const RollbackStageWindGraphNode& left,
        const RollbackStageWindGraphNode& right) noexcept
    {
        return left.vtable_rva == right.vtable_rva
            && left.bytes == right.bytes
            && RollbackStageWindAuthorityNodePayloadBytes(
                left.vtable_rva, left.bytes) != 0;
    }

    static inline bool RollbackStageWindAuthorityCopyNodePayload(
        const RollbackStageWindGraphNode& source,
        RollbackStageWindGraphNode& destination) noexcept
    {
        if (!RollbackStageWindAuthorityNodeTypeMatches(
                source, destination))
            return false;
        const RollbackStageWindSemanticRanges ranges =
            RollbackStageWindNodeSemanticRanges(
                source.vtable_rva, source.bytes);
        if (ranges.count == 0) return false;
        for (uint32_t index = 0; index < ranges.count; ++index)
        {
            const auto range = ranges.values[index];
            std::memcpy(destination.data.data() + range.offset,
                source.data.data() + range.offset, range.bytes);
        }
        return true;
    }

    static inline bool RollbackStageWindRootFighterOutputsConsistent(
        const RollbackStageWindSnapshot& snapshot,
        uintptr_t fighter0, uintptr_t fighter1) noexcept
    {
        if (!snapshot.graph.valid || !fighter0 || !fighter1
            || fighter0 == fighter1) return false;
        std::array<float, 4> fighter_output {};
        const auto& root = snapshot.graph.root_state.output_forces;
        return SafeReadBytes(reinterpret_cast<const void*>(
                    fighter0 + kRollbackStageWindFighterSliceOffset),
                fighter_output.data(), sizeof(fighter_output))
            && std::memcmp(fighter_output.data(), root.data() + 4,
                sizeof(fighter_output)) == 0
            && SafeReadBytes(reinterpret_cast<const void*>(
                    fighter1 + kRollbackStageWindFighterSliceOffset),
                fighter_output.data(), sizeof(fighter_output))
            && std::memcmp(fighter_output.data(), root.data() + 8,
                sizeof(fighter_output)) == 0;
    }

    static inline bool RollbackStageWindFighterOutputsReadable(
        uintptr_t fighter0, uintptr_t fighter1) noexcept
    {
        if (!fighter0 || !fighter1 || fighter0 == fighter1) return false;
        std::array<float, 4> output {};
        return SafeReadBytes(reinterpret_cast<const void*>(
                    fighter0 + kRollbackStageWindFighterSliceOffset),
                output.data(), sizeof(output))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    fighter1 + kRollbackStageWindFighterSliceOffset),
                output.data(), sizeof(output));
    }

    static inline bool RestoreRollbackStageWindFighterOutputs(
        const RollbackStageWindSnapshot& snapshot,
        uintptr_t fighter0, uintptr_t fighter1) noexcept
    {
        if (!snapshot.graph.valid
            || !RollbackStageWindFighterOutputsReadable(fighter0, fighter1))
            return false;
        const auto& root = snapshot.graph.root_state.output_forces;
        return SafeWriteBytes(reinterpret_cast<void*>(
                    fighter0 + kRollbackStageWindFighterSliceOffset),
                root.data() + 4, 4 * sizeof(float))
            && SafeWriteBytes(reinterpret_cast<void*>(
                    fighter1 + kRollbackStageWindFighterSliceOffset),
                root.data() + 8, 4 * sizeof(float));
    }

    static inline bool RollbackStageWindTopologyAndOwnershipMatch(
        const RollbackStageWindSnapshot& expected,
        const RollbackStageWindSnapshot& observed) noexcept
    {
        if (expected.sentinel != observed.sentinel
            || expected.count != observed.count
            || expected.graph.valid != observed.graph.valid)
            return false;
        for (uint32_t index = 0; index < expected.count; ++index)
        {
            if (expected.emitters[index].list_node
                    != observed.emitters[index].list_node
                || expected.emitters[index].emitter
                    != observed.emitters[index].emitter)
                return false;
        }
        if (!expected.graph.valid) return true;
        if (expected.graph.root != observed.graph.root
            || expected.graph.count != observed.graph.count
            || expected.graph.pool.allocated_mask
                != observed.graph.pool.allocated_mask
            || expected.graph.pool.external_freed_mask
                != observed.graph.pool.external_freed_mask
            || expected.graph.pool.sizes != observed.graph.pool.sizes)
            return false;
        for (uint32_t index = 0; index < expected.graph.count; ++index)
        {
            const auto& left = expected.graph.nodes[index];
            const auto& right = observed.graph.nodes[index];
            if (left.address != right.address || left.vtable != right.vtable
                || left.vtable_rva != right.vtable_rva
                || left.bytes != right.bytes
                // Restore owns the vtable, intrusive next/previous links, and
                // root pointer. +0x08 and +0x20..+0x27 are constructor-local
                // metadata/padding: the native wind tick does not consume
                // them, they are not peer-authoritative, and a newly
                // materialized fixed-pool node legitimately retains its
                // process-local bytes there.
                || std::memcmp(left.data.data() + 0x10,
                    right.data.data() + 0x10, 0x10) != 0
                || std::memcmp(left.data.data() + 0x28,
                    right.data.data() + 0x28, 0x08) != 0)
                return false;
        }
        return true;
    }

    static inline bool RollbackBuildStageWindAuthorityImage(
        const RollbackStageWindSnapshot& snapshot,
        const RollbackStageWindAllocationPool& pool,
        RollbackStageWindAuthorityImage& image) noexcept
    {
        image = {};
        const RollbackStageWindCanonicalBreakdown breakdown =
            BuildRollbackStageWindCanonicalBreakdown(snapshot);
        const uint64_t topology =
            RollbackHashStageWindAuthorityTopology(snapshot.graph);
        const uint64_t nodes =
            HashRollbackStageWindGraphNodesCanonical(snapshot.graph);
        const RollbackStageWindPoolState live_pool = pool.capture_state();
        if (!pool.sealed()
            || live_pool.allocated_mask
                != snapshot.graph.pool.allocated_mask
            || live_pool.external_freed_mask
                != snapshot.graph.pool.external_freed_mask
            || live_pool.sizes != snapshot.graph.pool.sizes
            || !breakdown.combined || !breakdown.combined_rng
            || !breakdown.emitters
            || !breakdown.root_scheduler || !breakdown.root_derived_outputs
            || !topology || !nodes || snapshot.output_active > 1)
            return false;

        RollbackStageWindAuthorityWriter writer(image);
        const uint16_t version = kRollbackStageWindAuthorityVersion;
        const uint16_t header_bytes = kRollbackStageWindAuthorityHeaderBytes;
        const uint32_t node_count = snapshot.graph.count;
        const uint32_t emitter_count = snapshot.count;
        const uint32_t reserved = 0;
        const uint64_t reserved_root_outputs = 0;
        const std::array<float, 12> reserved_output_forces {};
        if (!writer.scalar(kRollbackStageWindAuthorityMagic)
            || !writer.scalar(version) || !writer.scalar(header_bytes)
            || !writer.scalar(node_count)
            || !writer.scalar(snapshot.output_active)
            || !writer.scalar(emitter_count) || !writer.scalar(reserved)
            || !writer.scalar(breakdown.root_scheduler)
            || !writer.scalar(reserved_root_outputs)
            || !writer.scalar(breakdown.emitters)
            || !writer.scalar(topology) || !writer.scalar(nodes)
            || !writer.data(reserved_output_forces.data(),
                sizeof(reserved_output_forces))
            || !writer.scalar(snapshot.graph.root_state.scene_tick)
            || !writer.data(snapshot.combined_rng_state.data(),
                sizeof(snapshot.combined_rng_state))
            || writer.offset() != kRollbackStageWindAuthorityHeaderBytes)
            return false;

        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            if (!RollbackStageWindAuthorityWriteEmitterPayload(
                    writer, snapshot.emitters[index]))
                return false;
        }

        for (uint32_t index = 0; index < snapshot.graph.count; ++index)
        {
            const auto& node = snapshot.graph.nodes[index];
            const uint32_t payload_bytes = static_cast<uint32_t>(
                RollbackStageWindAuthorityNodePayloadBytes(
                    node.vtable_rva, node.bytes));
            const uint32_t pool_owned =
                pool.owns_pool_pointer(node.address) ? 1u : 0u;
            if (!pool.owns_or_tracks(node.address)
                || payload_bytes == 0 || !writer.scalar(index)
                || !writer.scalar(node.vtable_rva)
                || !writer.scalar(node.bytes)
                || !writer.scalar(payload_bytes)
                || !writer.scalar(pool_owned)
                || !RollbackStageWindAuthorityWriteNodePayload(writer, node))
                return false;
        }
        if (writer.offset() == 0 || writer.offset() > image.bytes.size())
            return false;
        image.byte_count = static_cast<uint16_t>(writer.offset());
        image.hash = RollbackHashStageWindAuthorityBytes(
            image.bytes.data(), image.byte_count);
        return image.hash != 0;
    }

    struct RollbackStageWindAuthorityApplyReport
    {
        bool ok {false};
        uint64_t authority_node_hash {0};
        uint64_t authorized_canonical_hash {0};
        uint32_t strict_mismatch_mask {0};
        uint32_t header_mismatch_mask {0};
        uint32_t owner_node_count {0};
        uint32_t local_node_count {0};
        uint32_t owner_emitter_count {0};
        uint32_t local_emitter_count {0};
        bool topology_rebuilt {false};
        const char* failure {"not-run"};
    };

    enum RollbackStageWindAuthorityHeaderMismatch : uint32_t
    {
        RollbackStageWindAuthorityHeaderMagic = 1u << 0,
        RollbackStageWindAuthorityHeaderVersion = 1u << 1,
        RollbackStageWindAuthorityHeaderBytes = 1u << 2,
        RollbackStageWindAuthorityHeaderOffset = 1u << 3,
        RollbackStageWindAuthorityHeaderReserved = 1u << 4,
        RollbackStageWindAuthorityHeaderOutputActive = 1u << 5,
        RollbackStageWindAuthorityHeaderNodeCapacity = 1u << 6,
        RollbackStageWindAuthorityHeaderEmitterCount = 1u << 7,
    };

    enum RollbackStageWindAuthorityStrictMismatch : uint32_t
    {
        RollbackStageWindAuthorityStrictLocalBreakdown = 1u << 0,
        RollbackStageWindAuthorityStrictRootScheduler = 1u << 1,
        RollbackStageWindAuthorityStrictTopology = 1u << 3,
    };

    static inline RollbackStageWindAuthorityApplyReport
    RollbackApplyStageWindAuthorityImage(
        const RollbackStageWindAuthorityImage& image,
        const RollbackStageWindSnapshot& local,
        uintptr_t image_base,
        const RollbackStageWindAllocationPool& pool,
        uintptr_t fighter0, uintptr_t fighter1,
        RollbackStageWindSnapshot& authorized) noexcept
    {
        RollbackStageWindAuthorityApplyReport report {};
        report.local_node_count = local.graph.count;
        report.local_emitter_count = local.count;
        if (!image_base || !pool.sealed())
        {
            report.failure = "stage-wind-authority-local-pool-invalid";
            return report;
        }
        const RollbackStageWindPoolState live_pool = pool.capture_state();
        if (HashRollbackStageWindCanonical(local) != local.canonical_hash
            || HashRollbackStageWindIntegrity(local)
                != local.integrity_hash
            || HashRollbackStageWindGraphCanonical(local.graph)
                != local.graph.canonical_hash
            || HashRollbackStageWindGraphIntegrity(local.graph)
                != local.graph.integrity_hash)
        {
            report.failure = "stage-wind-authority-local-snapshot-invalid";
            return report;
        }
        if (live_pool.allocated_mask
                != local.graph.pool.allocated_mask
            || live_pool.external_freed_mask
                != local.graph.pool.external_freed_mask
            || live_pool.sizes != local.graph.pool.sizes)
        {
            report.failure = "stage-wind-authority-local-pool-state-stale";
            return report;
        }
        for (uint32_t index = 0; index < local.graph.count; ++index)
        {
            const auto& node = local.graph.nodes[index];
            const int32_t slot = pool.pool_slot_index(node.address);
            if (slot < 0) continue;
            const uint32_t pool_index = static_cast<uint32_t>(slot);
            if ((live_pool.allocated_mask & (1u << pool_index)) == 0
                || live_pool.sizes[pool_index] != node.bytes)
            {
                report.failure =
                    "stage-wind-authority-local-pool-node-not-live";
                return report;
            }
        }
        if (image.byte_count < kRollbackStageWindAuthorityHeaderBytes
            || image.byte_count > image.bytes.size()
            || image.hash == 0
            || RollbackHashStageWindAuthorityBytes(
                image.bytes.data(), image.byte_count) != image.hash)
        {
            report.failure = "stage-wind-authority-image-integrity-invalid";
            return report;
        }
        if (!RollbackStageWindRootFighterOutputsConsistent(
                local, fighter0, fighter1))
        {
            report.failure = "stage-wind-derived-fighter-output-mismatch";
            return report;
        }

        RollbackStageWindAuthorityReader reader(image);
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t header_bytes = 0;
        uint32_t node_count = 0;
        uint32_t output_active = 0;
        uint32_t emitter_count = 0;
        uint32_t reserved = 0;
        uint64_t root_scheduler = 0;
        uint64_t root_outputs = 0;
        uint64_t emitters = 0;
        uint64_t topology = 0;
        uint64_t node_hash = 0;
        std::array<float, 12> output_forces {};
        float scene_tick = 0.0f;
        std::array<uint32_t, 6> combined_rng_state {};
        if (!reader.scalar(magic) || !reader.scalar(version)
            || !reader.scalar(header_bytes) || !reader.scalar(node_count)
            || !reader.scalar(output_active) || !reader.scalar(emitter_count)
            || !reader.scalar(reserved) || !reader.scalar(root_scheduler)
            || !reader.scalar(root_outputs) || !reader.scalar(emitters)
            || !reader.scalar(topology) || !reader.scalar(node_hash)
            || !reader.data(output_forces.data(), sizeof(output_forces))
            || !reader.scalar(scene_tick)
            || !reader.data(combined_rng_state.data(),
                sizeof(combined_rng_state)))
        {
            report.failure = "stage-wind-authority-header-decode-invalid";
            return report;
        }
        report.owner_node_count = node_count;
        report.owner_emitter_count = emitter_count;
        if (magic != kRollbackStageWindAuthorityMagic)
            report.header_mismatch_mask |=
                RollbackStageWindAuthorityHeaderMagic;
        if (version != kRollbackStageWindAuthorityVersion)
            report.header_mismatch_mask |=
                RollbackStageWindAuthorityHeaderVersion;
        if (header_bytes != kRollbackStageWindAuthorityHeaderBytes)
            report.header_mismatch_mask |=
                RollbackStageWindAuthorityHeaderBytes;
        if (reader.offset() != header_bytes)
            report.header_mismatch_mask |=
                RollbackStageWindAuthorityHeaderOffset;
        if (reserved != 0)
            report.header_mismatch_mask |=
                RollbackStageWindAuthorityHeaderReserved;
        const std::array<float, 12> reserved_output_forces {};
        if (root_outputs != 0
            || std::memcmp(output_forces.data(),
                reserved_output_forces.data(),
                sizeof(output_forces)) != 0)
            report.header_mismatch_mask |=
                RollbackStageWindAuthorityHeaderReserved;
        if (output_active > 1)
            report.header_mismatch_mask |=
                RollbackStageWindAuthorityHeaderOutputActive;
        if (node_count > kRollbackStageWindGraphMaxNodes)
            report.header_mismatch_mask |=
                RollbackStageWindAuthorityHeaderNodeCapacity;
        if (emitter_count != local.count)
            report.header_mismatch_mask |=
                RollbackStageWindAuthorityHeaderEmitterCount;
        if (report.header_mismatch_mask != 0)
        {
            if ((report.header_mismatch_mask
                    & RollbackStageWindAuthorityHeaderEmitterCount) != 0)
            {
                report.failure =
                    "stage-wind-authority-emitter-count-mismatch";
            }
            else if ((report.header_mismatch_mask
                    & RollbackStageWindAuthorityHeaderNodeCapacity) != 0)
            {
                report.failure =
                    "stage-wind-authority-node-count-over-capacity";
            }
            else
            {
                report.failure = "stage-wind-authority-wire-header-invalid";
            }
            return report;
        }
        const auto local_breakdown =
            BuildRollbackStageWindCanonicalBreakdown(local);
        if (!local_breakdown.combined)
            report.strict_mismatch_mask |=
                RollbackStageWindAuthorityStrictLocalBreakdown;
        if (root_scheduler != local_breakdown.root_scheduler)
            report.strict_mismatch_mask |=
                RollbackStageWindAuthorityStrictRootScheduler;
        if (report.strict_mismatch_mask != 0)
        {
            report.failure = "stage-wind-authority-strict-state-mismatch";
            return report;
        }

        std::array<std::array<uint8_t,
            kRollbackStageWindEmitterMutableBytes>,
            kRollbackStageWindEmitterMaxCount> owner_emitters {};
        RollbackHash owner_emitter_hash {};
        owner_emitter_hash.add_scalar(emitter_count);
        for (uint32_t index = 0; index < emitter_count; ++index)
        {
            if (!RollbackStageWindAuthorityReadEmitterPayload(
                    reader, owner_emitters[index]))
            {
                report.failure =
                    "stage-wind-authority-emitter-payload-invalid";
                return report;
            }
            owner_emitter_hash.add_scalar(index);
            RollbackStageWindEmitterRecord owner_record {};
            if (!CopyRollbackStageWindEmitterSemanticBytes(
                    owner_emitters[index], owner_record.data))
            {
                report.failure =
                    "stage-wind-authority-emitter-payload-invalid";
                return report;
            }
            if (!AddRollbackStageWindEmitterSemanticBytes(
                    owner_emitter_hash, owner_record))
            {
                report.failure =
                    "stage-wind-authority-emitter-payload-invalid";
                return report;
            }
        }
        if (owner_emitter_hash.value != emitters)
        {
            report.failure = "stage-wind-authority-emitter-hash-mismatch";
            return report;
        }

        std::array<RollbackStageWindGraphNode,
            kRollbackStageWindGraphMaxNodes> owner_nodes {};
        std::array<bool, kRollbackStageWindGraphMaxNodes>
            owner_pool_nodes {};
        for (uint32_t expected_index = 0;
             expected_index < node_count; ++expected_index)
        {
            uint32_t index = UINT32_MAX;
            uint32_t vtable_rva = 0;
            uint32_t node_bytes = 0;
            uint32_t payload_bytes = 0;
            uint32_t pool_owned = 0;
            if (!reader.scalar(index) || !reader.scalar(vtable_rva)
                || !reader.scalar(node_bytes) || !reader.scalar(payload_bytes)
                || !reader.scalar(pool_owned)
                || index != expected_index || pool_owned > 1)
            {
                report.failure = "stage-wind-authority-node-header-invalid";
                return report;
            }
            auto& node = owner_nodes[index];
            node.address = static_cast<uintptr_t>(index + 1u);
            node.vtable = vtable_rva;
            node.vtable_rva = vtable_rva;
            node.bytes = node_bytes;
            owner_pool_nodes[index] = pool_owned != 0;
            if (payload_bytes != RollbackStageWindAuthorityNodePayloadBytes(
                    vtable_rva, node_bytes)
                || !RollbackStageWindAuthorityReadNodePayload(reader, node))
            {
                report.failure = "stage-wind-authority-node-topology-mismatch";
                return report;
            }
        }
        if (reader.offset() != image.byte_count)
        {
            report.failure = "stage-wind-authority-trailing-bytes";
            return report;
        }
        RollbackStageWindGraphSnapshot owner_graph {};
        owner_graph.valid = true;
        owner_graph.root = 1;
        owner_graph.count = node_count;
        owner_graph.nodes = owner_nodes;
        if (RollbackHashStageWindAuthorityTopology(owner_graph) != topology
            || HashRollbackStageWindGraphNodesCanonical(owner_graph)
                != node_hash)
        {
            report.failure = "stage-wind-authority-node-hash-mismatch";
            return report;
        }

        authorized = local;
        authorized.output_active = output_active;
        authorized.combined_rng_state = combined_rng_state;
        for (uint32_t index = 0; index < emitter_count; ++index)
        {
            if (!CopyRollbackStageWindEmitterSemanticBytes(
                    owner_emitters[index],
                    authorized.emitters[index].data))
            {
                report.failure =
                    "stage-wind-authority-emitter-payload-invalid";
                return report;
            }
        }
        authorized.graph.root_state.scene_tick = scene_tick;
        authorized.graph.count = node_count;
        uint32_t planned_pool_mask = 0;
        RollbackStageWindPoolState planned_pool = local.graph.pool;
        planned_pool.allocated_mask = 0;
        planned_pool.sizes.fill(0);
        uint32_t next_external = 0;
        const auto next_external_index = [&]() noexcept {
            while (next_external < local.graph.count
                && pool.owns_pool_pointer(
                    local.graph.nodes[next_external].address))
            {
                ++next_external;
            }
            return next_external;
        };
        for (uint32_t owner_index = 0;
             owner_index < node_count; ++owner_index)
        {
            const auto& owner = owner_nodes[owner_index];
            RollbackStageWindGraphNode selected {};
            if (!owner_pool_nodes[owner_index])
            {
                const uint32_t external_index = next_external_index();
                if (external_index >= local.graph.count
                    || !pool.tracks_external(
                        local.graph.nodes[external_index].address)
                    || !RollbackStageWindAuthorityNodeTypeMatches(
                        owner, local.graph.nodes[external_index]))
                {
                    report.strict_mismatch_mask |=
                        RollbackStageWindAuthorityStrictTopology;
                    report.failure =
                        "stage-wind-authority-external-node-mismatch";
                    return report;
                }
                selected = local.graph.nodes[external_index];
                next_external = external_index + 1;
            }
            else
            {
                const RollbackStageWindGraphNode* selected_local = nullptr;
                if (owner_index < local.graph.count)
                {
                    const auto& candidate = local.graph.nodes[owner_index];
                    const int32_t slot = pool.pool_slot_index(
                        candidate.address);
                    if (slot >= 0
                        && (planned_pool_mask
                            & (1u << static_cast<uint32_t>(slot))) == 0
                        && RollbackStageWindAuthorityNodeTypeMatches(
                            owner, candidate))
                        selected_local = &candidate;
                }
                for (uint32_t local_index = 0;
                     !selected_local && local_index < local.graph.count;
                     ++local_index)
                {
                    const auto& candidate = local.graph.nodes[local_index];
                    const int32_t slot = pool.pool_slot_index(
                        candidate.address);
                    if (slot >= 0
                        && (planned_pool_mask
                            & (1u << static_cast<uint32_t>(slot))) == 0
                        && RollbackStageWindAuthorityNodeTypeMatches(
                            owner, candidate))
                        selected_local = &candidate;
                }
                if (selected_local)
                {
                    selected = *selected_local;
                }
                else
                {
                    // A peer may reach the frozen round boundary with fewer
                    // live wind nodes even though its sealed fixed pool has
                    // enough storage. Clone only a same-native-type local
                    // node so process-local gaps stay local, then let the
                    // owner payload and final link stamping provide the
                    // authoritative gameplay state.
                    const RollbackStageWindGraphNode* prototype = nullptr;
                    for (uint32_t local_index = 0;
                         !prototype && local_index < local.graph.count;
                         ++local_index)
                    {
                        if (RollbackStageWindAuthorityNodeTypeMatches(
                                owner, local.graph.nodes[local_index]))
                            prototype = &local.graph.nodes[local_index];
                    }
                    int32_t replacement_slot = -1;
                    for (uint32_t slot = 0;
                         slot < kRollbackStageWindPoolMaxNodes; ++slot)
                    {
                        const uint32_t bit = 1u << slot;
                        if ((planned_pool_mask & bit) == 0
                            && (live_pool.allocated_mask & bit) == 0)
                        {
                            replacement_slot = static_cast<int32_t>(slot);
                            break;
                        }
                    }
                    for (uint32_t slot = 0;
                         replacement_slot < 0
                            && slot < kRollbackStageWindPoolMaxNodes;
                         ++slot)
                    {
                        if ((planned_pool_mask & (1u << slot)) == 0)
                            replacement_slot = static_cast<int32_t>(slot);
                    }
                    if (!prototype || replacement_slot < 0)
                    {
                        report.failure =
                            "stage-wind-authority-pool-node-unmaterializable";
                        return report;
                    }
                    selected = *prototype;
                    selected.address = pool.pool_slot_address(
                        static_cast<uint32_t>(replacement_slot));
                }
            }
            const int32_t selected_pool =
                pool.pool_slot_index(selected.address);
            if (owner_pool_nodes[owner_index] != (selected_pool >= 0))
            {
                report.failure =
                    "stage-wind-authority-node-ownership-mismatch";
                return report;
            }
            if (selected_pool >= 0)
            {
                const uint32_t slot = static_cast<uint32_t>(selected_pool);
                const uint32_t bit = 1u << slot;
                if ((planned_pool_mask & bit) != 0)
                {
                    report.failure =
                        "stage-wind-authority-node-address-duplicate";
                    return report;
                }
                planned_pool_mask |= bit;
                planned_pool.sizes[slot] =
                    static_cast<uint16_t>(owner.bytes);
            }
            selected.vtable = image_base + owner.vtable_rva;
            selected.vtable_rva = owner.vtable_rva;
            selected.bytes = owner.bytes;
            if (!selected.address || !selected.vtable
                || !RollbackStageWindAuthorityCopyNodePayload(
                    owner, selected))
            {
                report.failure =
                    "stage-wind-authority-node-plan-invalid";
                return report;
            }
            authorized.graph.nodes[owner_index] = selected;
        }
        for (uint32_t index = 0; index < node_count; ++index)
        {
            const uintptr_t previous = index == 0
                ? 0 : authorized.graph.nodes[index - 1].address;
            const uintptr_t next = index + 1 < node_count
                ? authorized.graph.nodes[index + 1].address : 0;
            StampRollbackStageWindGraphNodeHeader(
                authorized.graph.nodes[index], authorized.graph.root,
                previous, next);
        }
        if (next_external_index() < local.graph.count)
        {
            report.strict_mismatch_mask |=
                RollbackStageWindAuthorityStrictTopology;
            report.failure =
                "stage-wind-authority-external-node-unmatched";
            return report;
        }
        planned_pool.allocated_mask = planned_pool_mask;
        authorized.graph.pool = planned_pool;
        report.topology_rebuilt = node_count != local.graph.count
            || planned_pool.allocated_mask
                != local.graph.pool.allocated_mask
            || planned_pool.external_freed_mask
                != local.graph.pool.external_freed_mask
            || planned_pool.sizes != local.graph.pool.sizes;
        for (uint32_t index = 0;
             !report.topology_rebuilt && index < node_count; ++index)
        {
            const auto& before = local.graph.nodes[index];
            const auto& after = authorized.graph.nodes[index];
            report.topology_rebuilt = before.address != after.address
                || before.vtable_rva != after.vtable_rva
                || before.bytes != after.bytes;
        }
        authorized.graph.canonical_hash =
            HashRollbackStageWindGraphCanonical(authorized.graph);
        authorized.graph.integrity_hash =
            HashRollbackStageWindGraphIntegrity(authorized.graph);
        authorized.canonical_hash = HashRollbackStageWindCanonical(authorized);
        authorized.integrity_hash = HashRollbackStageWindIntegrity(authorized);
        report.authority_node_hash =
            HashRollbackStageWindGraphNodesCanonical(authorized.graph);
        report.authorized_canonical_hash = authorized.canonical_hash;
        if (!authorized.graph.canonical_hash
            || !authorized.graph.integrity_hash
            || !authorized.canonical_hash || !authorized.integrity_hash
            || HashRollbackStageWindRootDerivedOutputs(authorized.graph)
                != HashRollbackStageWindRootDerivedOutputs(local.graph)
            || BuildRollbackStageWindCanonicalBreakdown(authorized).emitters
                != emitters
            || RollbackHashStageWindAuthorityTopology(authorized.graph)
                != topology
            || report.authority_node_hash != node_hash)
        {
            report.failure = "stage-wind-authority-post-apply-hash-mismatch";
            return report;
        }
        report.ok = true;
        report.failure = "ok";
        return report;
    }

#pragma pack(push, 1)
    struct RollbackStageWindAuthorityMessage
    {
        uint8_t version {kRollbackStageWindAuthorityVersion};
        uint8_t source_player_slot {0};
        uint8_t chunk_index {0};
        uint8_t chunk_count {0};
        uint16_t payload_bytes {0};
        uint16_t total_bytes {0};
        uint32_t round_ordinal {0};
        uint64_t capture_id {0};
        uint64_t session_epoch {0};
        uint64_t round_generation {0};
        uint64_t match_identity_digest {0};
        uint64_t image_hash {0};
        std::array<uint8_t, kRollbackStageWindAuthorityChunkBytes> payload {};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackStageWindAuthorityMessage) == 1076);

    static inline uint8_t RollbackStageWindAuthorityChunkCount(
        uint16_t bytes) noexcept
    {
        return bytes == 0 || bytes > kRollbackStageWindAuthorityImageMaxBytes
            ? 0 : static_cast<uint8_t>((bytes
                + kRollbackStageWindAuthorityChunkBytes - 1)
                / kRollbackStageWindAuthorityChunkBytes);
    }

    static inline uint64_t RollbackStageWindAuthorityCaptureId(
        uint8_t source_player_slot, uint64_t session_epoch,
        uint64_t round_generation, uint32_t round_ordinal,
        uint64_t match_identity_digest, uint64_t image_hash) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(source_player_slot);
        hash.add_scalar(session_epoch);
        hash.add_scalar(round_generation);
        hash.add_scalar(round_ordinal);
        hash.add_scalar(match_identity_digest);
        hash.add_scalar(image_hash);
        return hash.value;
    }

    static inline bool RollbackStageWindAuthorityMessageValid(
        const RollbackStageWindAuthorityMessage& message) noexcept
    {
        const uint8_t chunks = RollbackStageWindAuthorityChunkCount(
            message.total_bytes);
        if (message.version != kRollbackStageWindAuthorityVersion
            || message.source_player_slot >= 2 || chunks == 0
            || chunks > kRollbackStageWindAuthorityMaxChunks
            || message.chunk_count != chunks
            || message.chunk_index >= chunks
            || (message.round_ordinal & 0xFFFF0000u) != 0
            || message.capture_id == 0 || message.session_epoch == 0
            || message.round_generation == 0
            || message.match_identity_digest == 0 || message.image_hash == 0)
            return false;
        const size_t offset = static_cast<size_t>(message.chunk_index)
            * kRollbackStageWindAuthorityChunkBytes;
        const size_t expected = (std::min)(
            kRollbackStageWindAuthorityChunkBytes,
            static_cast<size_t>(message.total_bytes) - offset);
        if (message.payload_bytes != expected
            || message.capture_id != RollbackStageWindAuthorityCaptureId(
                message.source_player_slot, message.session_epoch,
                message.round_generation, message.round_ordinal,
                message.match_identity_digest, message.image_hash))
            return false;
        for (size_t index = expected; index < message.payload.size(); ++index)
        {
            if (message.payload[index] != 0) return false;
        }
        return true;
    }

    static inline bool RollbackBuildStageWindAuthorityMessage(
        const RollbackStageWindAuthorityImage& image,
        uint8_t source_player_slot, uint8_t chunk_index,
        uint64_t session_epoch, uint64_t round_generation,
        uint32_t round_ordinal, uint64_t match_identity_digest,
        RollbackStageWindAuthorityMessage& message) noexcept
    {
        message = {};
        if (image.byte_count == 0 || image.hash == 0
            || RollbackHashStageWindAuthorityBytes(
                image.bytes.data(), image.byte_count) != image.hash)
            return false;
        message.source_player_slot = source_player_slot;
        message.chunk_index = chunk_index;
        message.chunk_count = RollbackStageWindAuthorityChunkCount(
            image.byte_count);
        message.total_bytes = image.byte_count;
        message.round_ordinal = round_ordinal;
        message.session_epoch = session_epoch;
        message.round_generation = round_generation;
        message.match_identity_digest = match_identity_digest;
        message.image_hash = image.hash;
        message.capture_id = RollbackStageWindAuthorityCaptureId(
            source_player_slot, session_epoch, round_generation,
            round_ordinal, match_identity_digest, image.hash);
        if (chunk_index >= message.chunk_count) return false;
        const size_t offset = static_cast<size_t>(chunk_index)
            * kRollbackStageWindAuthorityChunkBytes;
        message.payload_bytes = static_cast<uint16_t>((std::min)(
            kRollbackStageWindAuthorityChunkBytes,
            static_cast<size_t>(image.byte_count) - offset));
        std::memcpy(message.payload.data(), image.bytes.data() + offset,
            message.payload_bytes);
        return RollbackStageWindAuthorityMessageValid(message);
    }

    enum class RollbackStageWindAuthorityInboxDisposition : uint8_t
    {
        Accepted,
        Duplicate,
        Complete,
        Stale,
        Future,
        Conflict,
        Invalid,
    };

    enum class RollbackInitialStageWindAuthorityDisposition : uint8_t
    {
        Accepted,
        Duplicate,
        Complete,
        Invalid,
    };

    enum class RollbackStageWindAuthoritySendDisposition : uint8_t
    {
        Send,
        PeerBaselineObserved,
        Invalid,
    };

    // This is the transport-order state used by the production launch barrier.
    // Keeping it independent of sockets makes retransmission and publication
    // ordering directly testable without pretending that the test is live.
    class RollbackStageWindAuthorityLaunchFlow
    {
    public:
        void reset() noexcept { *this = {}; }

        bool configure_owner(uint8_t chunk_count) noexcept
        {
            reset();
            if (chunk_count == 0
                || chunk_count > kRollbackStageWindAuthorityMaxChunks)
                return false;
            m_chunk_count = chunk_count;
            m_owner_configured = true;
            return true;
        }

        RollbackStageWindAuthoritySendDisposition next_owner_send(
            bool peer_baseline_observed, uint8_t& chunk_index,
            bool& retransmission) const noexcept
        {
            chunk_index = 0;
            retransmission = false;
            if (!m_owner_configured || m_chunk_count == 0
                || m_send_cursor >= m_chunk_count)
                return RollbackStageWindAuthoritySendDisposition::Invalid;
            if (peer_baseline_observed)
                return RollbackStageWindAuthoritySendDisposition::
                    PeerBaselineObserved;
            chunk_index = m_send_cursor;
            retransmission = m_send_cycles != 0;
            return RollbackStageWindAuthoritySendDisposition::Send;
        }

        bool owner_send_committed() noexcept
        {
            if (!m_owner_configured || m_chunk_count == 0
                || m_send_cursor >= m_chunk_count)
                return false;
            ++m_send_cursor;
            if (m_send_cursor != m_chunk_count) return false;
            m_send_cursor = 0;
            ++m_send_cycles;
            m_owner_send_complete = true;
            return true;
        }

        void mark_guest_full_recapture_verified(bool verified) noexcept
        {
            m_guest_full_recapture_verified = verified;
        }

        bool may_publish_baseline(bool local_is_owner) const noexcept
        {
            return local_is_owner ? m_owner_send_complete
                                  : m_guest_full_recapture_verified;
        }

        bool guest_full_recapture_verified() const noexcept
        {
            return m_guest_full_recapture_verified;
        }

    private:
        uint8_t m_chunk_count {0};
        uint8_t m_send_cursor {0};
        uint32_t m_send_cycles {0};
        bool m_owner_configured {false};
        bool m_owner_send_complete {false};
        bool m_guest_full_recapture_verified {false};
    };

    class RollbackStageWindAuthorityInbox
    {
    public:
        void reset() noexcept { *this = {}; }

        bool configure(uint8_t source_player_slot, uint64_t session_epoch,
            uint64_t round_generation, uint32_t round_ordinal,
            uint64_t match_identity_digest) noexcept
        {
            if (source_player_slot >= 2 || session_epoch == 0
                || round_generation == 0
                || (round_ordinal & 0xFFFF0000u) != 0
                || match_identity_digest == 0) return false;
            if (m_configured)
            {
                return m_source_player_slot == source_player_slot
                    && m_session_epoch == session_epoch
                    && m_round_generation == round_generation
                    && m_round_ordinal == round_ordinal
                    && m_match_identity_digest == match_identity_digest;
            }
            m_source_player_slot = source_player_slot;
            m_session_epoch = session_epoch;
            m_round_generation = round_generation;
            m_round_ordinal = round_ordinal;
            m_match_identity_digest = match_identity_digest;
            m_configured = true;
            return true;
        }

        RollbackStageWindAuthorityInboxDisposition accept(
            const RollbackStageWindAuthorityMessage& message) noexcept
        {
            if (!m_configured || !RollbackStageWindAuthorityMessageValid(message)
                || message.source_player_slot != m_source_player_slot
                || message.session_epoch != m_session_epoch
                || message.match_identity_digest != m_match_identity_digest)
                return RollbackStageWindAuthorityInboxDisposition::Invalid;
            if (message.round_generation < m_round_generation
                || (message.round_generation == m_round_generation
                    && message.round_ordinal < m_round_ordinal))
                return RollbackStageWindAuthorityInboxDisposition::Stale;
            if (message.round_generation > m_round_generation
                || (message.round_generation == m_round_generation
                    && message.round_ordinal > m_round_ordinal))
                return RollbackStageWindAuthorityInboxDisposition::Future;
            if ((m_capture_id != 0 && m_capture_id != message.capture_id)
                || (m_image.hash != 0 && m_image.hash != message.image_hash)
                || (m_image.byte_count != 0
                    && m_image.byte_count != message.total_bytes))
                return RollbackStageWindAuthorityInboxDisposition::Conflict;
            m_capture_id = message.capture_id;
            m_image.hash = message.image_hash;
            m_image.byte_count = message.total_bytes;
            m_chunk_count = message.chunk_count;
            const uint32_t bit = uint32_t {1} << message.chunk_index;
            const size_t offset = static_cast<size_t>(message.chunk_index)
                * kRollbackStageWindAuthorityChunkBytes;
            if ((m_received_mask & bit) != 0)
            {
                return std::memcmp(m_image.bytes.data() + offset,
                    message.payload.data(), message.payload_bytes) == 0
                    ? RollbackStageWindAuthorityInboxDisposition::Duplicate
                    : RollbackStageWindAuthorityInboxDisposition::Conflict;
            }
            std::memcpy(m_image.bytes.data() + offset,
                message.payload.data(), message.payload_bytes);
            m_received_mask |= bit;
            if (ready() && RollbackHashStageWindAuthorityBytes(
                    m_image.bytes.data(), m_image.byte_count) != m_image.hash)
                return RollbackStageWindAuthorityInboxDisposition::Conflict;
            return ready()
                ? RollbackStageWindAuthorityInboxDisposition::Complete
                : RollbackStageWindAuthorityInboxDisposition::Accepted;
        }

        bool ready() const noexcept
        {
            return m_configured && m_chunk_count != 0
                && m_received_mask
                    == ((uint32_t {1} << m_chunk_count) - 1u);
        }

        const RollbackStageWindAuthorityImage& image() const noexcept
        {
            return m_image;
        }

        uint64_t round_generation() const noexcept
        {
            return m_round_generation;
        }

        bool configured_for(uint8_t source_player_slot,
            uint64_t session_epoch, uint64_t round_generation,
            uint32_t round_ordinal,
            uint64_t match_identity_digest) const noexcept
        {
            return m_configured
                && m_source_player_slot == source_player_slot
                && m_session_epoch == session_epoch
                && m_round_generation == round_generation
                && m_round_ordinal == round_ordinal
                && m_match_identity_digest == match_identity_digest;
        }

    private:
        uint8_t m_source_player_slot {0};
        uint8_t m_chunk_count {0};
        uint32_t m_received_mask {0};
        uint64_t m_capture_id {0};
        uint64_t m_session_epoch {0};
        uint64_t m_round_generation {0};
        uint32_t m_round_ordinal {0};
        uint64_t m_match_identity_digest {0};
        RollbackStageWindAuthorityImage m_image {};
        bool m_configured {false};
    };

    static inline bool RollbackStageWindAuthorityIsStaleDuringActiveRound(
        bool local_is_owner, uint8_t expected_remote_slot,
        uint64_t session_epoch, uint64_t match_identity_digest,
        uint64_t current_round_generation, uint32_t current_round_ordinal,
        const RollbackStageWindAuthorityMessage& message) noexcept
    {
        return !local_is_owner && expected_remote_slot < 2
            && session_epoch != 0 && match_identity_digest != 0
            && current_round_generation != 0
            && RollbackStageWindAuthorityMessageValid(message)
            && message.source_player_slot == expected_remote_slot
            && message.session_epoch == session_epoch
            && message.match_identity_digest == match_identity_digest
            && (message.round_generation < current_round_generation
                || (message.round_generation == current_round_generation
                    && message.round_ordinal <= current_round_ordinal));
    }

    static inline RollbackInitialStageWindAuthorityDisposition
    RollbackAcceptInitialStageWindAuthorityBeforeBoundary(
        RollbackStageWindAuthorityInbox& inbox,
        bool session_contract_ready, bool local_is_owner,
        uint8_t expected_remote_slot, uint64_t session_epoch,
        const RollbackStageWindAuthorityMessage& message) noexcept
    {
        if (!session_contract_ready || local_is_owner
            || expected_remote_slot >= 2 || session_epoch == 0
            || !RollbackStageWindAuthorityMessageValid(message)
            || message.source_player_slot != expected_remote_slot
            || message.session_epoch != session_epoch
            || message.round_generation != 1)
        {
            return RollbackInitialStageWindAuthorityDisposition::Invalid;
        }
        if (inbox.round_generation() != 0
            && inbox.round_generation() != 1)
        {
            return RollbackInitialStageWindAuthorityDisposition::Invalid;
        }
        if (!inbox.configure(message.source_player_slot,
                message.session_epoch, message.round_generation,
                message.round_ordinal, message.match_identity_digest))
        {
            return RollbackInitialStageWindAuthorityDisposition::Invalid;
        }
        switch (inbox.accept(message))
        {
        case RollbackStageWindAuthorityInboxDisposition::Accepted:
            return RollbackInitialStageWindAuthorityDisposition::Accepted;
        case RollbackStageWindAuthorityInboxDisposition::Duplicate:
            return RollbackInitialStageWindAuthorityDisposition::Duplicate;
        case RollbackStageWindAuthorityInboxDisposition::Complete:
            return RollbackInitialStageWindAuthorityDisposition::Complete;
        default:
            return RollbackInitialStageWindAuthorityDisposition::Invalid;
        }
    }
}
