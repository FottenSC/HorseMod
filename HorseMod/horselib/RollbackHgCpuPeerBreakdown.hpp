#pragma once

#include "RollbackMotionBankCanonical.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Horse
{
    static constexpr size_t kRollbackHgCpuPeerCharaChunkBytes = 0x1000;
    static constexpr size_t kRollbackHgCpuPeerCharaChunkCount = 8;
    static constexpr size_t kRollbackHgCpuKHitListCount = 3;
    static constexpr size_t kRollbackHgCpuKHitPayloadLaneCount = 4;
    static constexpr size_t kRollbackHgCpuCurrentMatrixPartitionCount = 3;

    struct RollbackHgCpuPeerBreakdown
    {
        uint64_t chara_stream_hash[2] {};
        uint64_t chara_chunk_hash[2][kRollbackHgCpuPeerCharaChunkCount] {};
        uint64_t khit_hash[2] {};
        // Diagnostic subdivisions of the exact native KHit writer stream.
        // list_hash covers the whole logical list, source_matrix_hash covers
        // the exact current primary-bank matrices referenced by that list,
        // and payload_lane_hash divides the class-specific world-geometry
        // payload into four 16-byte lanes.
        // These participate only in peer diagnostics; khit_hash remains the
        // canonical mismatch decision.
        uint64_t khit_list_hash[2][kRollbackHgCpuKHitListCount] {};
        uint64_t khit_source_matrix_hash
            [2][kRollbackHgCpuKHitListCount] {};
        uint64_t khit_payload_lane_hash
            [2][kRollbackHgCpuKHitListCount]
            [kRollbackHgCpuKHitPayloadLaneCount] {};
        uint64_t motion_slot_hash {0};
        // Diagnostic-only current-primary hashes. The base builder uses
        // collision ranges 16..23, bone 24, and bone 25. Production may
        // multiplex otherwise-unused P1 fields with exact producer-input
        // hashes after a P2-only issue is isolated. None are authority.
        uint64_t motion_current_partition_hash
            [2][kRollbackHgCpuCurrentMatrixPartitionCount] {};
        uint64_t motion_provider_hash[2][kRollbackMotionBankCount] {};
        // Combined pointer-free hash of the Enshutsu/clip scheduler and the
        // secondary animation-notify stack it feeds. The field width/name is
        // retained to preserve the peer diagnostic packet layout.
        uint64_t secondary_event_hash {0};
        uint64_t timer_shape_hash {0};
        uint64_t skeleton_shape_hash {0};
        uint64_t motion_decode_scratch_hash {0};
        uint64_t motion_pose_residue_hash {0};
        uint32_t effective_bytes {0};
        uint16_t khit_node_count[2] {};
        uint16_t khit_source_bone_min
            [2][kRollbackHgCpuKHitListCount] {};
        uint16_t khit_source_bone_max
            [2][kRollbackHgCpuKHitListCount] {};
        uint8_t motion_provider_age[2][kRollbackMotionBankCount] {};
        uint8_t reserved[4] {};
    };
    static_assert(sizeof(RollbackHgCpuPeerBreakdown) == 616);
    static_assert(std::is_trivially_copyable_v<RollbackHgCpuPeerBreakdown>);
}
