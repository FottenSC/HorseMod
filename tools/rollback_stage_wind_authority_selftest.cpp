#include "RollbackStageWindAuthority.hpp"
#include "RollbackCarriedStateTransaction.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    void restamp_graph_headers(Horse::RollbackStageWindSnapshot& snapshot)
    {
        auto& graph = snapshot.graph;
        for (uint32_t index = 0; index < graph.count; ++index)
        {
            const uintptr_t previous = index == 0
                ? 0 : graph.nodes[index - 1].address;
            const uintptr_t next = index + 1 < graph.count
                ? graph.nodes[index + 1].address : 0;
            Horse::StampRollbackStageWindGraphNodeHeader(
                graph.nodes[index], graph.root, previous, next);
        }
    }

    Horse::RollbackStageWindSnapshot make_snapshot(bool owner)
    {
        Horse::RollbackStageWindSnapshot snapshot {};
        snapshot.output_active = owner ? 1u : 0u;
        for (uint32_t index = 0;
             index < snapshot.combined_rng_state.size(); ++index)
        {
            snapshot.combined_rng_state[index] =
                (owner ? 0xA1000000u : 0xB2000000u) + index;
        }
        snapshot.sentinel = owner ? 0x100000 : 0x110000;
        snapshot.count = 1;
        snapshot.emitters[0].list_node =
            owner ? 0x100100 : 0x110100;
        snapshot.emitters[0].emitter =
            owner ? 0x100200 : 0x110200;
        snapshot.emitters[0].data.fill(owner ? 0x31 : 0x41);

        auto& graph = snapshot.graph;
        graph.valid = true;
        graph.root = 0x200000;
        graph.count = 5;
        graph.root_state.active_bank = owner ? 0u : 1u;
        graph.root_state.pending_count = 0;
        graph.root_state.schedule_state = -1;
        graph.root_state.effect_pair_scheduled = 0;
        graph.root_state.scene_tick = owner ? 3.5f : 7.5f;
        for (size_t index = 0;
             index < graph.root_state.output_forces.size(); ++index)
            graph.root_state.output_forces[index] =
                (owner ? 1.0f : -1.0f)
                + static_cast<float>(index) * 0.25f;

        constexpr std::array<uint32_t, 5> vtables {
            Horse::kRollbackStageWindRingInVtableRva,
            Horse::kRollbackStageWindRingOutVtableRva,
            Horse::kRollbackStageWindParallelVtableRva,
            Horse::kRollbackStageWindParallelVtableRva,
            Horse::kRollbackStageWindShockWaveVtableRva,
        };
        constexpr std::array<uint32_t, 5> sizes {
            0x1E0, 0x130, 0x130, 0x130, 0x180,
        };
        for (uint32_t index = 0; index < graph.count; ++index)
        {
            auto& node = graph.nodes[index];
            node.address = 0x300000 + index * 0x1000;
            node.vtable_rva = vtables[index];
            node.vtable = 0x140000000ull + node.vtable_rva;
            node.bytes = sizes[index];
            node.data.fill(static_cast<uint8_t>(0x20 + index));
            if (owner)
            {
                for (uint32_t offset = 0x30; offset < node.bytes; ++offset)
                    node.data[offset] = static_cast<uint8_t>(
                        0x70 + index + (offset & 7u));
            }
            else
            {
                // Process-local gaps remain guest-owned. Canonical ranges
                // deliberately differ and must be replaced by authority.
                for (uint32_t offset = 0x30; offset < node.bytes; ++offset)
                    node.data[offset] = static_cast<uint8_t>(
                        0x40 + index + (offset & 3u));
                if (node.bytes == 0x130)
                    std::memset(node.data.data() + 0xE0, 0xDD, 0x40);
            }
        }
        restamp_graph_headers(snapshot);
        graph.pool.allocated_mask = 0;
        graph.pool.external_freed_mask = 0;
        graph.canonical_hash =
            Horse::HashRollbackStageWindGraphCanonical(graph);
        graph.integrity_hash =
            Horse::HashRollbackStageWindGraphIntegrity(graph);
        snapshot.canonical_hash =
            Horse::HashRollbackStageWindCanonical(snapshot);
        snapshot.integrity_hash =
            Horse::HashRollbackStageWindIntegrity(snapshot);
        return snapshot;
    }

    void seed_fighter_outputs(const Horse::RollbackStageWindSnapshot& snapshot,
        std::vector<uint8_t>& fighter0, std::vector<uint8_t>& fighter1)
    {
        std::memcpy(fighter0.data()
                + Horse::kRollbackStageWindFighterSliceOffset,
            snapshot.graph.root_state.output_forces.data() + 4,
            4 * sizeof(float));
        std::memcpy(fighter1.data()
                + Horse::kRollbackStageWindFighterSliceOffset,
            snapshot.graph.root_state.output_forces.data() + 8,
            4 * sizeof(float));
    }

    void rehash(Horse::RollbackStageWindSnapshot& snapshot)
    {
        snapshot.graph.canonical_hash =
            Horse::HashRollbackStageWindGraphCanonical(snapshot.graph);
        snapshot.graph.integrity_hash =
            Horse::HashRollbackStageWindGraphIntegrity(snapshot.graph);
        snapshot.canonical_hash =
            Horse::HashRollbackStageWindCanonical(snapshot);
        snapshot.integrity_hash =
            Horse::HashRollbackStageWindIntegrity(snapshot);
    }

    bool semantic_node_bytes_equal(
        const Horse::RollbackStageWindGraphNode& left,
        const Horse::RollbackStageWindGraphNode& right)
    {
        if (left.vtable_rva != right.vtable_rva
            || left.bytes != right.bytes)
            return false;
        const Horse::RollbackStageWindSemanticRanges ranges =
            Horse::RollbackStageWindNodeSemanticRanges(
                left.vtable_rva, left.bytes);
        if (ranges.count == 0) return false;
        for (uint32_t index = 0; index < ranges.count; ++index)
        {
            const auto range = ranges.values[index];
            if (std::memcmp(left.data.data() + range.offset,
                    right.data.data() + range.offset,
                    range.bytes) != 0)
                return false;
        }
        return true;
    }

    bool bind_parallel_pool_node(Horse::RollbackStageWindSnapshot& snapshot,
        Horse::RollbackStageWindAllocationPool& pool)
    {
        for (uint32_t index : std::array<uint32_t, 4> {0, 1, 2, 4})
            if (!pool.track_initial_node(snapshot.graph.nodes[index].address))
                return false;
        pool.seal();
        void* node = pool.allocate(0x130);
        if (!node) return false;
        snapshot.graph.nodes[3].address =
            reinterpret_cast<uintptr_t>(node);
        restamp_graph_headers(snapshot);
        snapshot.graph.pool = pool.capture_state();
        rehash(snapshot);
        return snapshot.canonical_hash != 0 && snapshot.integrity_hash != 0;
    }

    bool make_maximum_snapshot(
        bool owner,
        Horse::RollbackStageWindAllocationPool& pool,
        Horse::RollbackStageWindSnapshot& snapshot)
    {
        snapshot = make_snapshot(owner);
        snapshot.count = static_cast<uint32_t>(
            Horse::kRollbackStageWindEmitterMaxCount);
        for (uint32_t index = 0; index < snapshot.count; ++index)
        {
            auto& emitter = snapshot.emitters[index];
            emitter.list_node = (owner ? 0x900000 : 0xA00000)
                + index * 0x100;
            emitter.emitter = emitter.list_node + 0x80;
            emitter.data.fill(static_cast<uint8_t>(
                (owner ? 0x60 : 0x20) + (index & 0x1F)));
        }
        auto& graph = snapshot.graph;
        graph.nodes = {};
        graph.count = static_cast<uint32_t>(
            Horse::kRollbackStageWindGraphMaxNodes);
        for (uint32_t index = 0;
             index < Horse::kRollbackStageWindExternalMaxNodes; ++index)
        {
            auto& node = graph.nodes[index];
            node.address = (owner ? 0x500000 : 0x700000)
                + index * 0x1000;
            if (!pool.track_initial_node(node.address)) return false;
        }
        pool.seal();
        for (uint32_t index = 0; index < graph.count; ++index)
        {
            auto& node = graph.nodes[index];
            if (index >= Horse::kRollbackStageWindExternalMaxNodes)
            {
                node.address = reinterpret_cast<uintptr_t>(
                    pool.allocate(0x1E0));
                if (!node.address) return false;
            }
            node.vtable_rva = Horse::kRollbackStageWindRingInVtableRva;
            node.vtable = 0x140000000ull + node.vtable_rva;
            node.bytes = 0x1E0;
            node.data.fill(static_cast<uint8_t>(
                (owner ? 0x80 : 0x40) + (index & 0x1F)));
        }
        restamp_graph_headers(snapshot);
        graph.pool = pool.capture_state();
        rehash(snapshot);
        return pool.allocated_count()
                == Horse::kRollbackStageWindPoolMaxNodes
            && snapshot.canonical_hash != 0
            && snapshot.integrity_hash != 0;
    }
}

int main()
{
    const uint32_t all_preflight_failures =
        Horse::RollbackCarriedStatePreflightFailureMask(
            false, false, false, false, false, false, false);
    if (all_preflight_failures
        != (Horse::RollbackCarriedStatePreflightSecondary
            | Horse::RollbackCarriedStatePreflightMotion
            | Horse::RollbackCarriedStatePreflightWindCapture
            | Horse::RollbackCarriedStatePreflightWindTopology
            | Horse::RollbackCarriedStatePreflightWindFighterOutputs
            | Horse::RollbackCarriedStatePreflightWindCanonical
            | Horse::RollbackCarriedStatePreflightWindIntegrity)
        || Horse::RollbackCarriedStatePreflightFailureMask(
            true, true, true, true, true, true, true) != 0)
        return 28;

    auto owner = make_snapshot(true);
    auto guest = make_snapshot(false);
    constexpr uintptr_t image_base = 0x140000000ull;
    Horse::RollbackStageWindAllocationPool owner_pool {};
    Horse::RollbackStageWindAllocationPool guest_pool {};
    if (!bind_parallel_pool_node(owner, owner_pool)
        || !bind_parallel_pool_node(guest, guest_pool))
        return 29;
    if (!owner.canonical_hash || !owner.integrity_hash
        || !guest.canonical_hash || !guest.integrity_hash
        || owner.canonical_hash == guest.canonical_hash)
    {
        std::cerr << "initial authority fixture hash failure"
                  << " owner_canonical=" << owner.canonical_hash
                  << " owner_integrity=" << owner.integrity_hash
                  << " guest_canonical=" << guest.canonical_hash
                  << " guest_integrity=" << guest.integrity_hash
                  << '\n';
        return 1;
    }

    std::vector<uint8_t> fighter0(
        Horse::kRollbackStageWindFighterSliceOffset + 0x40);
    std::vector<uint8_t> fighter1(
        Horse::kRollbackStageWindFighterSliceOffset + 0x40);
    seed_fighter_outputs(guest, fighter0, fighter1);

    Horse::RollbackStageWindAllocationPool maximum_owner_pool {};
    Horse::RollbackStageWindAllocationPool maximum_guest_pool {};
    Horse::RollbackStageWindSnapshot maximum_owner {};
    Horse::RollbackStageWindSnapshot maximum_guest {};
    if (!make_maximum_snapshot(
            true, maximum_owner_pool, maximum_owner)
        || !make_maximum_snapshot(
            false, maximum_guest_pool, maximum_guest))
        return 42;
    Horse::RollbackStageWindAuthorityImage maximum_image {};
    if (!Horse::RollbackBuildStageWindAuthorityImage(
            maximum_owner, maximum_owner_pool, maximum_image)
        || Horse::RollbackStageWindAuthorityChunkCount(
            maximum_image.byte_count)
            != Horse::kRollbackStageWindAuthorityMaxChunks)
        return 42;
    seed_fighter_outputs(maximum_guest, fighter0, fighter1);
    Horse::RollbackStageWindSnapshot maximum_authorized {};
    const auto maximum_applied =
        Horse::RollbackApplyStageWindAuthorityImage(
            maximum_image, maximum_guest, image_base,
            maximum_guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()),
            maximum_authorized);
    if (!maximum_applied.ok
        || maximum_authorized.graph.count
            != Horse::kRollbackStageWindGraphMaxNodes
        || maximum_authorized.graph.count != 48
        || maximum_authorized.canonical_hash
            != maximum_owner.canonical_hash
        || !Horse::RollbackStageWindTopologyAndOwnershipMatch(
            maximum_guest, maximum_authorized))
        return 42;

    Horse::RollbackStageWindAuthorityImage image {};
    auto stale_owner_pool_state = owner;
    stale_owner_pool_state.graph.pool.allocated_mask = 0;
    stale_owner_pool_state.graph.pool.sizes.fill(0);
    rehash(stale_owner_pool_state);
    if (Horse::RollbackBuildStageWindAuthorityImage(
            stale_owner_pool_state, owner_pool, image))
        return 36;
    if (!Horse::RollbackBuildStageWindAuthorityImage(
            owner, owner_pool, image)
        || image.byte_count <= Horse::kRollbackStageWindAuthorityChunkBytes
        || Horse::RollbackStageWindAuthorityChunkCount(image.byte_count) != 2)
        return 2;
    if (Horse::kRollbackStageWindAuthorityVersion != 12)
        return 43;
    const size_t first_node_payload =
        Horse::kRollbackStageWindAuthorityHeaderBytes
        + owner.count * Horse::kRollbackStageWindEmitterMutableBytes
        + Horse::kRollbackStageWindAuthorityNodeHeaderBytes;
    const size_t second_node_payload =
        first_node_payload + (0x1E0 - 0x30)
        + Horse::kRollbackStageWindAuthorityNodeHeaderBytes;
    const size_t third_node_payload =
        second_node_payload + (0x130 - 0x30)
        + Horse::kRollbackStageWindAuthorityNodeHeaderBytes;
    const size_t fourth_node_payload =
        third_node_payload + (0x130 - 0x30)
        + Horse::kRollbackStageWindAuthorityNodeHeaderBytes;
    const size_t fifth_node_payload =
        fourth_node_payload + (0x130 - 0x30)
        + Horse::kRollbackStageWindAuthorityNodeHeaderBytes;
    const size_t first_emitter_payload =
        Horse::kRollbackStageWindAuthorityHeaderBytes;
    for (size_t offset = 32; offset < 40; ++offset)
        if (image.bytes[offset] != 0)
            return 53;
    for (size_t offset = 64; offset < 112; ++offset)
        if (image.bytes[offset] != 0)
            return 53;
    for (uint32_t native_offset :
        std::array<uint32_t, 2> {0x6C, 0x7C})
    {
        if (image.bytes[first_emitter_payload
                + native_offset
                - Horse::kRollbackStageWindEmitterMutableOffset] != 0)
            return 44;
    }
    for (size_t index = 0; index < 12; ++index)
        if (image.bytes[first_node_payload + 4 + index] != 0)
            return 44;
    const std::array<size_t, 5> node_payload_offsets {
        first_node_payload, second_node_payload, third_node_payload,
        fourth_node_payload, fifth_node_payload,
    };
    for (const size_t payload : node_payload_offsets)
    {
        for (uint32_t offset = 0x40; offset < 0x60; ++offset)
        {
            if (image.bytes[payload + offset - 0x30] != 0)
                return 53;
        }
    }
    if (image.bytes[second_node_payload + 0x12C - 0x30] != 0)
        return 44;
    if (image.bytes[fifth_node_payload + 0x12C - 0x30] != 0)
        return 54;
    for (uint32_t offset = 0x110; offset < 0x120; ++offset)
    {
        if (image.bytes[fifth_node_payload + offset - 0x30] != 0)
            return 49;
    }
    for (uint32_t offset = 0xE4; offset < 0xF0; ++offset)
    {
        if (image.bytes[fifth_node_payload + offset - 0x30] != 0)
            return 51;
    }

    const uint32_t guest_raw_bank = guest.graph.root_state.active_bank;
    const auto guest_gap = guest.graph.nodes[1].data[0xE0];
    const auto guest_parallel_angle_residue =
        guest.graph.nodes[1].data[0x12C];
    const auto guest_shock_wave_angle_residue =
        guest.graph.nodes[4].data[0x12C];
    const auto guest_emitter_residue = guest.emitters[0].data;
    const auto guest_root_output_forces =
        guest.graph.root_state.output_forces;
    std::array<std::array<uint8_t, 0x20>, 5>
        guest_node_force_outputs {};
    for (size_t index = 0; index < guest_node_force_outputs.size(); ++index)
    {
        std::memcpy(guest_node_force_outputs[index].data(),
            guest.graph.nodes[index].data.data() + 0x40,
            guest_node_force_outputs[index].size());
    }
    std::array<uint8_t, 12> guest_allocator_residue {};
    std::memcpy(guest_allocator_residue.data(),
        guest.graph.nodes[4].data.data() + 0x34,
        guest_allocator_residue.size());
    std::array<uint8_t, 16> guest_shock_wave_residue {};
    std::memcpy(guest_shock_wave_residue.data(),
        guest.graph.nodes[4].data.data() + 0x110,
        guest_shock_wave_residue.size());
    std::array<uint8_t, 12> guest_shock_wave_motion_residue {};
    std::memcpy(guest_shock_wave_motion_residue.data(),
        guest.graph.nodes[4].data.data() + 0xE4,
        guest_shock_wave_motion_residue.size());
    constexpr std::array<uint32_t, 10> ring_in_residue_offsets {
        0xF4, 0x10C, 0x11C, 0x120, 0x134,
        0x138, 0x144, 0x14C, 0x15C, 0x160,
    };
    std::array<uint8_t, ring_in_residue_offsets.size()>
        guest_ring_in_residue {};
    for (size_t index = 0;
         index < ring_in_residue_offsets.size(); ++index)
    {
        guest_ring_in_residue[index] =
            guest.graph.nodes[0].data[ring_in_residue_offsets[index]];
    }
    const uintptr_t guest_root = guest.graph.root;
    const uintptr_t guest_node = guest.graph.nodes[0].address;
    Horse::RollbackStageWindSnapshot authorized {};
    auto applied = Horse::RollbackApplyStageWindAuthorityImage(
        image, guest, image_base, guest_pool,
        reinterpret_cast<uintptr_t>(fighter0.data()),
        reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (!applied.ok || authorized.output_active != owner.output_active
        || authorized.combined_rng_state != owner.combined_rng_state
        || authorized.combined_rng_state == guest.combined_rng_state
        || authorized.emitters[0].data == guest.emitters[0].data
        || authorized.emitters[0].data[
                0x6C - Horse::kRollbackStageWindEmitterMutableOffset]
            != guest_emitter_residue[
                0x6C - Horse::kRollbackStageWindEmitterMutableOffset]
        || authorized.emitters[0].data[
                0x7C - Horse::kRollbackStageWindEmitterMutableOffset]
            != guest_emitter_residue[
                0x7C - Horse::kRollbackStageWindEmitterMutableOffset]
        || authorized.emitters[0].list_node
            != guest.emitters[0].list_node
        || authorized.emitters[0].emitter != guest.emitters[0].emitter
        || authorized.canonical_hash != owner.canonical_hash
        || authorized.graph.root_state.scene_tick
            != owner.graph.root_state.scene_tick
        || authorized.graph.root_state.output_forces
            != guest_root_output_forces
        || authorized.graph.root_state.active_bank != guest_raw_bank
        || authorized.graph.nodes[1].data[0xE0] != guest_gap
        || authorized.graph.nodes[1].data[0x12C]
            != guest_parallel_angle_residue
        || authorized.graph.nodes[4].data[0x12C]
            != guest_shock_wave_angle_residue
        || authorized.graph.pool.allocated_mask
            != guest.graph.pool.allocated_mask
        || authorized.graph.pool.sizes != guest.graph.pool.sizes
        || authorized.graph.root != guest_root
        || authorized.graph.nodes[0].address != guest_node
        || !semantic_node_bytes_equal(
            authorized.graph.nodes[4], owner.graph.nodes[4])
        || std::memcmp(authorized.graph.nodes[4].data.data() + 0x34,
            guest_allocator_residue.data(),
            guest_allocator_residue.size()) != 0
        || std::memcmp(authorized.graph.nodes[4].data.data() + 0x110,
            guest_shock_wave_residue.data(),
            guest_shock_wave_residue.size()) != 0
        || std::memcmp(authorized.graph.nodes[4].data.data() + 0xE4,
            guest_shock_wave_motion_residue.data(),
            guest_shock_wave_motion_residue.size()) != 0
        || !Horse::RollbackStageWindTopologyAndOwnershipMatch(
            guest, authorized))
        return 3;
    for (size_t index = 0; index < guest_node_force_outputs.size(); ++index)
    {
        if (std::memcmp(authorized.graph.nodes[index].data.data() + 0x40,
                guest_node_force_outputs[index].data(),
                guest_node_force_outputs[index].size()) != 0)
            return 53;
    }
    for (size_t index = 0;
         index < ring_in_residue_offsets.size(); ++index)
    {
        if (authorized.graph.nodes[0].data[
                ring_in_residue_offsets[index]]
            != guest_ring_in_residue[index])
            return 46;
    }

    auto nonzero_reserved = image;
    nonzero_reserved.bytes[first_node_payload + 4] = 1;
    nonzero_reserved.hash = Horse::RollbackHashStageWindAuthorityBytes(
        nonzero_reserved.bytes.data(), nonzero_reserved.byte_count);
    const auto nonzero_reserved_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            nonzero_reserved, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (nonzero_reserved_report.ok
        || std::string_view(nonzero_reserved_report.failure)
            != "stage-wind-authority-node-topology-mismatch")
        return 45;
    auto nonzero_root_output_hash = image;
    nonzero_root_output_hash.bytes[32] = 1;
    nonzero_root_output_hash.hash =
        Horse::RollbackHashStageWindAuthorityBytes(
            nonzero_root_output_hash.bytes.data(),
            nonzero_root_output_hash.byte_count);
    const auto nonzero_root_output_hash_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            nonzero_root_output_hash, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (nonzero_root_output_hash_report.ok
        || std::string_view(nonzero_root_output_hash_report.failure)
            != "stage-wind-authority-wire-header-invalid")
        return 53;
    auto nonzero_root_output_force = image;
    nonzero_root_output_force.bytes[64] = 1;
    nonzero_root_output_force.hash =
        Horse::RollbackHashStageWindAuthorityBytes(
            nonzero_root_output_force.bytes.data(),
            nonzero_root_output_force.byte_count);
    const auto nonzero_root_output_force_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            nonzero_root_output_force, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (nonzero_root_output_force_report.ok
        || std::string_view(nonzero_root_output_force_report.failure)
            != "stage-wind-authority-wire-header-invalid")
        return 53;
    auto nonzero_node_force = image;
    nonzero_node_force.bytes[first_node_payload + 0x40 - 0x30] = 1;
    nonzero_node_force.hash =
        Horse::RollbackHashStageWindAuthorityBytes(
            nonzero_node_force.bytes.data(),
            nonzero_node_force.byte_count);
    const auto nonzero_node_force_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            nonzero_node_force, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (nonzero_node_force_report.ok
        || std::string_view(nonzero_node_force_report.failure)
            != "stage-wind-authority-node-topology-mismatch")
        return 53;
    auto nonzero_parallel_angle_residue = image;
    nonzero_parallel_angle_residue.bytes[
        second_node_payload + 0x12C - 0x30] = 1;
    nonzero_parallel_angle_residue.hash =
        Horse::RollbackHashStageWindAuthorityBytes(
            nonzero_parallel_angle_residue.bytes.data(),
            nonzero_parallel_angle_residue.byte_count);
    const auto nonzero_parallel_angle_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            nonzero_parallel_angle_residue, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (nonzero_parallel_angle_report.ok
        || std::string_view(nonzero_parallel_angle_report.failure)
            != "stage-wind-authority-node-topology-mismatch")
        return 48;
    auto nonzero_shock_wave_angle_residue = image;
    nonzero_shock_wave_angle_residue.bytes[
        fifth_node_payload + 0x12C - 0x30] = 1;
    nonzero_shock_wave_angle_residue.hash =
        Horse::RollbackHashStageWindAuthorityBytes(
            nonzero_shock_wave_angle_residue.bytes.data(),
            nonzero_shock_wave_angle_residue.byte_count);
    const auto nonzero_shock_wave_angle_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            nonzero_shock_wave_angle_residue, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (nonzero_shock_wave_angle_report.ok
        || std::string_view(nonzero_shock_wave_angle_report.failure)
            != "stage-wind-authority-node-topology-mismatch")
        return 54;
    auto nonzero_shock_wave_residue = image;
    nonzero_shock_wave_residue.bytes[
        fifth_node_payload + 0x114 - 0x30] = 1;
    nonzero_shock_wave_residue.hash =
        Horse::RollbackHashStageWindAuthorityBytes(
            nonzero_shock_wave_residue.bytes.data(),
            nonzero_shock_wave_residue.byte_count);
    const auto nonzero_shock_wave_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            nonzero_shock_wave_residue, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (nonzero_shock_wave_report.ok
        || std::string_view(nonzero_shock_wave_report.failure)
            != "stage-wind-authority-node-topology-mismatch")
        return 50;
    auto nonzero_shock_wave_motion_residue = image;
    nonzero_shock_wave_motion_residue.bytes[
        fifth_node_payload + 0xE8 - 0x30] = 1;
    nonzero_shock_wave_motion_residue.hash =
        Horse::RollbackHashStageWindAuthorityBytes(
            nonzero_shock_wave_motion_residue.bytes.data(),
            nonzero_shock_wave_motion_residue.byte_count);
    const auto nonzero_shock_wave_motion_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            nonzero_shock_wave_motion_residue, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (nonzero_shock_wave_motion_report.ok
        || std::string_view(nonzero_shock_wave_motion_report.failure)
            != "stage-wind-authority-node-topology-mismatch")
        return 52;

    auto nonzero_emitter_reserved = image;
    nonzero_emitter_reserved.bytes[
        first_emitter_payload
            + 0x6C
            - Horse::kRollbackStageWindEmitterMutableOffset] = 1;
    nonzero_emitter_reserved.hash =
        Horse::RollbackHashStageWindAuthorityBytes(
            nonzero_emitter_reserved.bytes.data(),
            nonzero_emitter_reserved.byte_count);
    const auto nonzero_emitter_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            nonzero_emitter_reserved, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (nonzero_emitter_report.ok
        || std::string_view(nonzero_emitter_report.failure)
            != "stage-wind-authority-emitter-payload-invalid")
        return 47;

    const auto preserved_output = authorized;
    if (!guest_pool.intercept_free(reinterpret_cast<void*>(
            guest.graph.nodes[3].address)))
        return 39;
    const auto stale_guest_pool_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            image, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (stale_guest_pool_report.ok
        || std::string_view(stale_guest_pool_report.failure)
            != "stage-wind-authority-local-pool-state-stale"
        || authorized.integrity_hash != preserved_output.integrity_hash
        || authorized.graph.integrity_hash
            != preserved_output.graph.integrity_hash
        || guest_pool.allocated_count() != 0
        || !guest_pool.restore_state(guest.graph.pool))
        return 39;

    Horse::RollbackStageWindAllocationPool fewer_pool {};
    auto guest_fewer = make_snapshot(false);
    for (uint32_t index : std::array<uint32_t, 4> {0, 1, 2, 4})
        if (!fewer_pool.track_initial_node(
                guest_fewer.graph.nodes[index].address))
            return 30;
    fewer_pool.seal();
    guest_fewer.graph.nodes[3] = guest_fewer.graph.nodes[4];
    guest_fewer.graph.count = 4;
    restamp_graph_headers(guest_fewer);
    guest_fewer.graph.pool = fewer_pool.capture_state();
    rehash(guest_fewer);
    const auto fewer_report = Horse::RollbackApplyStageWindAuthorityImage(
        image, guest_fewer, image_base, fewer_pool,
        reinterpret_cast<uintptr_t>(fighter0.data()),
        reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (!fewer_report.ok || !fewer_report.topology_rebuilt
        || fewer_report.owner_node_count != 5
        || fewer_report.local_node_count != 4
        || authorized.graph.count != owner.graph.count
        || authorized.graph.nodes[3].address
            != fewer_pool.pool_slot_address(0)
        || authorized.graph.pool.allocated_mask != 1u
        || authorized.graph.pool.sizes[0] != 0x130
        || authorized.canonical_hash != owner.canonical_hash
        || fewer_pool.allocated_count() != 0)
        return 30;

    auto guest_reordered = guest;
    std::swap(guest_reordered.graph.nodes[2],
        guest_reordered.graph.nodes[3]);
    restamp_graph_headers(guest_reordered);
    rehash(guest_reordered);
    const auto reordered_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            image, guest_reordered, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (!reordered_report.ok || !reordered_report.topology_rebuilt
        || authorized.graph.count != owner.graph.count
        || authorized.graph.nodes[2].address
            != guest.graph.nodes[2].address
        || authorized.graph.nodes[3].address
            != guest.graph.nodes[3].address
        || authorized.canonical_hash != owner.canonical_hash)
        return 37;

    Horse::RollbackStageWindAllocationPool wrong_type_pool {};
    auto guest_wrong_pool_type = make_snapshot(false);
    for (uint32_t index : std::array<uint32_t, 4> {0, 1, 2, 4})
        if (!wrong_type_pool.track_initial_node(
                guest_wrong_pool_type.graph.nodes[index].address))
            return 38;
    wrong_type_pool.seal();
    void* wrong_type_node = wrong_type_pool.allocate(0x180);
    if (!wrong_type_node) return 38;
    guest_wrong_pool_type.graph.nodes[3].address =
        reinterpret_cast<uintptr_t>(wrong_type_node);
    guest_wrong_pool_type.graph.nodes[3].vtable_rva =
        Horse::kRollbackStageWindShockWaveVtableRva;
    guest_wrong_pool_type.graph.nodes[3].vtable = image_base
        + guest_wrong_pool_type.graph.nodes[3].vtable_rva;
    guest_wrong_pool_type.graph.nodes[3].bytes = 0x180;
    restamp_graph_headers(guest_wrong_pool_type);
    guest_wrong_pool_type.graph.pool = wrong_type_pool.capture_state();
    rehash(guest_wrong_pool_type);
    const auto wrong_pool_type_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            image, guest_wrong_pool_type, image_base, wrong_type_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (!wrong_pool_type_report.ok
        || !wrong_pool_type_report.topology_rebuilt
        || authorized.graph.nodes[3].address
            != wrong_type_pool.pool_slot_address(1)
        || authorized.graph.pool.allocated_mask != 2u
        || authorized.graph.pool.sizes[1] != 0x130
        || authorized.canonical_hash != owner.canonical_hash
        || wrong_type_pool.allocated_count() != 1)
        return 38;

    Horse::RollbackStageWindAllocationPool unique_owner_pool {};
    auto unique_owner = make_snapshot(true);
    for (uint32_t index : std::array<uint32_t, 4> {0, 1, 2, 3})
        if (!unique_owner_pool.track_initial_node(
                unique_owner.graph.nodes[index].address))
            return 40;
    unique_owner_pool.seal();
    void* unique_owner_node = unique_owner_pool.allocate(0x180);
    if (!unique_owner_node) return 40;
    unique_owner.graph.nodes[4].address =
        reinterpret_cast<uintptr_t>(unique_owner_node);
    restamp_graph_headers(unique_owner);
    unique_owner.graph.pool = unique_owner_pool.capture_state();
    rehash(unique_owner);
    Horse::RollbackStageWindAuthorityImage unique_image {};
    if (!Horse::RollbackBuildStageWindAuthorityImage(
            unique_owner, unique_owner_pool, unique_image))
        return 40;

    Horse::RollbackStageWindAllocationPool no_prototype_pool {};
    auto no_prototype = make_snapshot(false);
    no_prototype.graph.count = 4;
    for (uint32_t index = 0; index < no_prototype.graph.count; ++index)
        if (!no_prototype_pool.track_initial_node(
                no_prototype.graph.nodes[index].address))
            return 40;
    no_prototype_pool.seal();
    restamp_graph_headers(no_prototype);
    no_prototype.graph.pool = no_prototype_pool.capture_state();
    rehash(no_prototype);
    const auto no_prototype_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            unique_image, no_prototype, image_base, no_prototype_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (no_prototype_report.ok
        || std::string_view(no_prototype_report.failure)
            != "stage-wind-authority-pool-node-unmaterializable"
        || no_prototype_pool.allocated_count() != 0)
        return 40;

    Horse::RollbackStageWindAllocationPool more_pool {};
    auto guest_more = make_snapshot(false);
    if (!bind_parallel_pool_node(guest_more, more_pool)) return 31;
    auto* extra_pool_node = static_cast<uint8_t*>(
        more_pool.allocate(0x130));
    if (!extra_pool_node) return 32;
    guest_more.graph.nodes[5] = guest_more.graph.nodes[3];
    guest_more.graph.nodes[5].address =
        reinterpret_cast<uintptr_t>(extra_pool_node);
    guest_more.graph.count = 6;
    restamp_graph_headers(guest_more);
    guest_more.graph.pool = more_pool.capture_state();
    rehash(guest_more);
    const auto more_report = Horse::RollbackApplyStageWindAuthorityImage(
        image, guest_more, image_base, more_pool,
        reinterpret_cast<uintptr_t>(fighter0.data()),
        reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (!more_report.ok || !more_report.topology_rebuilt
        || more_report.owner_node_count != 5
        || more_report.local_node_count != 6
        || authorized.graph.pool.allocated_mask
            != owner.graph.pool.allocated_mask
        || authorized.graph.pool.sizes != owner.graph.pool.sizes
        || authorized.canonical_hash != owner.canonical_hash
        || more_pool.allocated_count() != 2)
        return 33;

    auto wrong_emitter_count = guest;
    wrong_emitter_count.count = 0;
    rehash(wrong_emitter_count);
    const auto emitter_count_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            image, wrong_emitter_count, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (emitter_count_report.ok
        || std::string_view(emitter_count_report.failure)
            != "stage-wind-authority-emitter-count-mismatch"
        || emitter_count_report.header_mismatch_mask
            != Horse::RollbackStageWindAuthorityHeaderEmitterCount
        || emitter_count_report.owner_emitter_count != 1
        || emitter_count_report.local_emitter_count != 0)
        return 34;

    auto old_version = image;
    const uint16_t stale_version =
        Horse::kRollbackStageWindAuthorityVersion - 1;
    std::memcpy(old_version.bytes.data() + 4, &stale_version,
        sizeof(stale_version));
    old_version.hash = Horse::RollbackHashStageWindAuthorityBytes(
        old_version.bytes.data(), old_version.byte_count);
    const auto old_version_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            old_version, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (old_version_report.ok
        || old_version_report.header_mismatch_mask
            != Horse::RollbackStageWindAuthorityHeaderVersion)
        return 35;
    const auto authorized_good = authorized;
    auto trailing_image = image;
    trailing_image.bytes[trailing_image.byte_count++] = 0xA5;
    trailing_image.hash = Horse::RollbackHashStageWindAuthorityBytes(
        trailing_image.bytes.data(), trailing_image.byte_count);
    if (Horse::RollbackApplyStageWindAuthorityImage(
            trailing_image, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized).ok)
        return 27;

    auto bad_emitter_image = image;
    bad_emitter_image.bytes[
        Horse::kRollbackStageWindAuthorityHeaderBytes] ^= 1;
    bad_emitter_image.hash = Horse::RollbackHashStageWindAuthorityBytes(
        bad_emitter_image.bytes.data(), bad_emitter_image.byte_count);
    const auto bad_emitter_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            bad_emitter_image, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (bad_emitter_report.ok
        || std::string_view(bad_emitter_report.failure)
            != "stage-wind-authority-emitter-hash-mismatch"
        || bad_emitter_report.strict_mismatch_mask != 0)
        return 4;

    auto bad_topology = guest;
    bad_topology.graph.nodes[0].vtable_rva =
        Horse::kRollbackStageWindShockWaveVtableRva;
    bad_topology.graph.nodes[0].vtable = 0x140000000ull
        + bad_topology.graph.nodes[0].vtable_rva;
    bad_topology.graph.nodes[0].bytes = 0x180;
    restamp_graph_headers(bad_topology);
    bad_topology.graph.canonical_hash =
        Horse::HashRollbackStageWindGraphCanonical(bad_topology.graph);
    bad_topology.graph.integrity_hash =
        Horse::HashRollbackStageWindGraphIntegrity(bad_topology.graph);
    bad_topology.canonical_hash =
        Horse::HashRollbackStageWindCanonical(bad_topology);
    bad_topology.integrity_hash =
        Horse::HashRollbackStageWindIntegrity(bad_topology);
    const auto bad_topology_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            image, bad_topology, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized);
    if (bad_topology_report.ok
        || std::string_view(bad_topology_report.failure)
            != "stage-wind-authority-external-node-mismatch"
        || bad_topology_report.strict_mismatch_mask
            != Horse::RollbackStageWindAuthorityStrictTopology)
        return 5;

    fighter0[Horse::kRollbackStageWindFighterSliceOffset] ^= 1;
    if (Horse::RollbackApplyStageWindAuthorityImage(
            image, guest, image_base, guest_pool,
            reinterpret_cast<uintptr_t>(fighter0.data()),
            reinterpret_cast<uintptr_t>(fighter1.data()), authorized).ok)
        return 6;
    fighter0[Horse::kRollbackStageWindFighterSliceOffset] ^= 1;

    constexpr uint64_t session = 0x12345678;
    constexpr uint64_t match = 0x87654321;
    Horse::RollbackStageWindAuthorityMessage chunk0 {};
    Horse::RollbackStageWindAuthorityMessage chunk1 {};
    if (!Horse::RollbackBuildStageWindAuthorityMessage(
            image, 0, 0, session, 1, 0, match, chunk0)
        || !Horse::RollbackBuildStageWindAuthorityMessage(
            image, 0, 1, session, 1, 0, match, chunk1))
        return 7;

    auto malformed = chunk0;
    malformed.capture_id ^= 1;
    if (Horse::RollbackStageWindAuthorityMessageValid(malformed)) return 8;
    malformed = chunk0;
    malformed.source_player_slot = 2;
    if (Horse::RollbackStageWindAuthorityMessageValid(malformed)) return 9;
    malformed = chunk0;
    malformed.session_epoch ^= 1;
    if (Horse::RollbackStageWindAuthorityMessageValid(malformed)) return 10;
    malformed = chunk0;
    malformed.match_identity_digest ^= 1;
    if (Horse::RollbackStageWindAuthorityMessageValid(malformed)) return 11;
    malformed = chunk0;
    malformed.total_bytes = 0;
    if (Horse::RollbackStageWindAuthorityMessageValid(malformed)) return 12;
    malformed = chunk0;
    --malformed.chunk_count;
    if (Horse::RollbackStageWindAuthorityMessageValid(malformed)) return 13;
    malformed = chunk1;
    malformed.payload[malformed.payload_bytes] = 1;
    if (Horse::RollbackStageWindAuthorityMessageValid(malformed)) return 14;

    Horse::RollbackStageWindAuthorityLaunchFlow owner_flow {};
    uint8_t next_chunk = UINT8_MAX;
    bool retransmission = false;
    if (!owner_flow.configure_owner(2)
        || owner_flow.may_publish_baseline(true)
        || owner_flow.next_owner_send(false, next_chunk, retransmission)
            != Horse::RollbackStageWindAuthoritySendDisposition::Send
        || next_chunk != 0 || retransmission
        || owner_flow.owner_send_committed()
        || owner_flow.may_publish_baseline(true)
        || owner_flow.next_owner_send(false, next_chunk, retransmission)
            != Horse::RollbackStageWindAuthoritySendDisposition::Send
        || next_chunk != 1 || retransmission
        || !owner_flow.owner_send_committed()
        || !owner_flow.may_publish_baseline(true)
        || owner_flow.next_owner_send(false, next_chunk, retransmission)
            != Horse::RollbackStageWindAuthoritySendDisposition::Send
        || next_chunk != 0 || !retransmission
        || owner_flow.owner_send_committed()
        || owner_flow.next_owner_send(false, next_chunk, retransmission)
            != Horse::RollbackStageWindAuthoritySendDisposition::Send
        || next_chunk != 1 || !retransmission
        || !owner_flow.owner_send_committed()
        || owner_flow.next_owner_send(false, next_chunk, retransmission)
            != Horse::RollbackStageWindAuthoritySendDisposition::Send
        || next_chunk != 0 || !retransmission
        || owner_flow.next_owner_send(true, next_chunk, retransmission)
            != Horse::RollbackStageWindAuthoritySendDisposition::
                PeerBaselineObserved)
        return 15;
    Horse::RollbackStageWindAuthorityLaunchFlow guest_flow {};
    if (guest_flow.may_publish_baseline(false)) return 16;
    guest_flow.mark_guest_full_recapture_verified(false);
    if (guest_flow.may_publish_baseline(false)) return 17;
    const bool full_recapture_matches = authorized_good.canonical_hash
            == owner.canonical_hash
        && authorized_good.integrity_hash != 0;
    guest_flow.mark_guest_full_recapture_verified(full_recapture_matches);
    if (!guest_flow.may_publish_baseline(false)) return 18;

    Horse::RollbackStageWindAuthorityInbox inbox {};
    if (!inbox.configure(0, session, 1, 0, match)) return 19;
    Horse::RollbackStageWindAuthorityMessage wrong_binding {};
    if (!Horse::RollbackBuildStageWindAuthorityMessage(
            image, 1, 0, session, 1, 0, match, wrong_binding)
        || inbox.accept(wrong_binding)
            != Horse::RollbackStageWindAuthorityInboxDisposition::Invalid
        || !Horse::RollbackBuildStageWindAuthorityMessage(
            image, 0, 0, session + 1, 1, 0, match, wrong_binding)
        || inbox.accept(wrong_binding)
            != Horse::RollbackStageWindAuthorityInboxDisposition::Invalid
        || !Horse::RollbackBuildStageWindAuthorityMessage(
            image, 0, 0, session, 1, 0, match + 1, wrong_binding)
        || inbox.accept(wrong_binding)
            != Horse::RollbackStageWindAuthorityInboxDisposition::Invalid)
        return 20;
    if (inbox.accept(chunk1)
            != Horse::RollbackStageWindAuthorityInboxDisposition::Accepted
        || inbox.accept(chunk1)
            != Horse::RollbackStageWindAuthorityInboxDisposition::Duplicate
        || inbox.accept(chunk0)
            != Horse::RollbackStageWindAuthorityInboxDisposition::Complete
        || !inbox.ready() || inbox.image().hash != image.hash
        || std::memcmp(inbox.image().bytes.data(), image.bytes.data(),
            image.byte_count) != 0)
        return 21;

    Horse::RollbackStageWindAuthorityImage maximum_chunk_image {};
    maximum_chunk_image.byte_count =
        static_cast<uint16_t>(
            (Horse::kRollbackStageWindAuthorityMaxChunks - 1)
            * Horse::kRollbackStageWindAuthorityChunkBytes + 1);
    for (uint16_t index = 0;
         index < maximum_chunk_image.byte_count; ++index)
        maximum_chunk_image.bytes[index] =
            static_cast<uint8_t>((index * 17u) & 0xFFu);
    maximum_chunk_image.hash = Horse::RollbackHashStageWindAuthorityBytes(
        maximum_chunk_image.bytes.data(), maximum_chunk_image.byte_count);
    Horse::RollbackStageWindAuthorityInbox maximum_chunk_inbox {};
    if (Horse::RollbackStageWindAuthorityChunkCount(
            maximum_chunk_image.byte_count)
            != Horse::kRollbackStageWindAuthorityMaxChunks
        || !maximum_chunk_inbox.configure(0, session, 1, 0, match))
        return 41;
    for (uint8_t index = 0;
         index < Horse::kRollbackStageWindAuthorityMaxChunks; ++index)
    {
        Horse::RollbackStageWindAuthorityMessage message {};
        if (!Horse::RollbackBuildStageWindAuthorityMessage(
                maximum_chunk_image, 0, index,
                session, 1, 0, match, message))
            return 41;
        const auto disposition = maximum_chunk_inbox.accept(message);
        if (disposition
            != (index + 1 == Horse::kRollbackStageWindAuthorityMaxChunks
                ? Horse::RollbackStageWindAuthorityInboxDisposition::Complete
                : Horse::RollbackStageWindAuthorityInboxDisposition::Accepted))
            return 41;
    }
    if (!maximum_chunk_inbox.ready()
        || maximum_chunk_inbox.image().hash != maximum_chunk_image.hash
        || std::memcmp(maximum_chunk_inbox.image().bytes.data(),
            maximum_chunk_image.bytes.data(),
            maximum_chunk_image.byte_count) != 0)
        return 41;

    auto conflicting = chunk1;
    conflicting.payload[0] ^= 1;
    if (inbox.accept(conflicting)
        != Horse::RollbackStageWindAuthorityInboxDisposition::Conflict)
        return 22;

    Horse::RollbackStageWindAuthorityInbox early {};
    if (Horse::RollbackAcceptInitialStageWindAuthorityBeforeBoundary(
            early, true, false, 0, session, chunk1)
            != Horse::RollbackInitialStageWindAuthorityDisposition::Accepted
        || Horse::RollbackAcceptInitialStageWindAuthorityBeforeBoundary(
            early, true, false, 0, session, chunk0)
            != Horse::RollbackInitialStageWindAuthorityDisposition::Complete)
        return 23;

    Horse::RollbackStageWindAuthorityInbox generation2 {};
    if (!generation2.configure(0, session, 2, 1, match)
        || generation2.accept(chunk0)
            != Horse::RollbackStageWindAuthorityInboxDisposition::Stale)
        return 24;
    Horse::RollbackStageWindAuthorityMessage future {};
    if (!Horse::RollbackBuildStageWindAuthorityMessage(
            image, 0, 0, session, 3, 2, match, future)
        || generation2.accept(future)
            != Horse::RollbackStageWindAuthorityInboxDisposition::Future)
        return 25;

    if (!Horse::RollbackStageWindAuthorityIsStaleDuringActiveRound(
            false, 0, session, match, 2, 1, chunk0)
        || Horse::RollbackStageWindAuthorityIsStaleDuringActiveRound(
            true, 0, session, match, 2, 1, chunk0))
        return 26;

    std::cout << "rollback stage wind authority self-test passed\n";
    return 0;
}
