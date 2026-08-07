#pragma once

#include "ReplayDebugTrace.hpp"
#include "ReplayScrubDiag.hpp"
#include "RollbackStateHash.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <float.h>
#include <string>
#include <xmmintrin.h>

namespace Horse
{
    static inline void AddRollbackFloatingPointEnvironmentTraceFields(
        ReplayTraceFields& fields) noexcept
    {
        unsigned int x87_control = 0;
        const bool x87_readable =
            _controlfp_s(&x87_control, 0, 0) == 0;
        const uint32_t mxcsr = _mm_getcsr();
        fields.hex("fp_mxcsr", mxcsr)
            .hex("fp_mxcsr_control", mxcsr & 0x0000FFC0u)
            .boolean("fp_x87_control_readable", x87_readable)
            .hex("fp_x87_control", x87_control);
    }

    struct RollbackHitCueRootMotionDigest
    {
        static constexpr size_t kSlotSemanticBytes = 96;
        std::array<uint64_t,
            ReplayScrubDiag::kChara_HitCueSlotCount> slot_semantic_hash {};
        std::array<
            std::array<uint8_t, kSlotSemanticBytes>,
            ReplayScrubDiag::kChara_HitCueSlotCount> slot_semantic_bytes {};
        std::array<uint64_t,
            ReplayScrubDiag::kChara_HitCueSlotCount>
                pose_lane_semantic_hash {};
        uint64_t pose_pack_hash {0};
        uint64_t pose_pack_raw_hash {0};
        uint64_t combined_semantic_hash {0};
        bool pose_pack_readable {false};
    };

    static inline RollbackHitCueRootMotionDigest
    BuildRollbackHitCueRootMotionDigest(
        const ReplayScrubDiag::CharaMoveVmSnap& fighter) noexcept
    {
        RollbackHitCueRootMotionDigest result {};
        result.pose_pack_readable = fighter.hit_cue_pose_pack_readable;
        if (result.pose_pack_readable)
        {
            result.pose_pack_raw_hash = RollbackHashBytes(
                fighter.hit_cue_pose_pack.data(),
                fighter.hit_cue_pose_pack.size());
        }

        RollbackHash pose_pack_semantic {};
        for (size_t index = 0;
             index < fighter.hit_cue_pose_lanes.size(); ++index)
        {
            const auto& lane = fighter.hit_cue_pose_lanes[index];
            RollbackHash semantic {};
            semantic.add_scalar(lane.readable);
            semantic.add_scalar(lane.motion_clip_index);
            semantic.add_scalar(lane.sample_frame);
            semantic.add_scalar(lane.active_flags);
            semantic.add_scalar(lane.active_weight);
            semantic.add_scalar(lane.sampler_yaw);
            semantic.add_scalar(lane.sampler_pitch);
            semantic.add_scalar(lane.scale_x);
            semantic.add_scalar(lane.scale_y);
            semantic.add_scalar(lane.scale_z);
            semantic.add_scalar(lane.scale_w);
            result.pose_lane_semantic_hash[index] = semantic.value;
            pose_pack_semantic.add_scalar(
                result.pose_lane_semantic_hash[index]);
        }
        result.pose_pack_hash = pose_pack_semantic.value;

        RollbackHash combined {};
        combined.add_scalar(result.pose_pack_hash);
        for (size_t index = 0;
             index < fighter.hit_cue_slots.size(); ++index)
        {
            const auto& slot = fighter.hit_cue_slots[index];
            RollbackHash semantic {};
            auto& semantic_bytes = result.slot_semantic_bytes[index];
            size_t semantic_cursor = 0;
            const auto add_semantic = [&](
                    const auto& value) noexcept
            {
                semantic.add_scalar(value);
                if (semantic_cursor + sizeof(value)
                    <= semantic_bytes.size())
                {
                    std::memcpy(
                        semantic_bytes.data() + semantic_cursor,
                        &value, sizeof(value));
                }
                semantic_cursor += sizeof(value);
            };
            add_semantic(slot.active_cue);
            add_semantic(slot.multiplier_index);
            add_semantic(slot.pose_mode);
            add_semantic(slot.frame_counter);
            add_semantic(slot.loop_flag);
            add_semantic(slot.end_reached);
            add_semantic(slot.node_frame);
            add_semantic(slot.node_segment_end);
            add_semantic(slot.node_loop_span);
            add_semantic(slot.node_prev_frame);
            add_semantic(slot.weight_gate);
            add_semantic(slot.node_blend);
            add_semantic(slot.node_blend_target);
            add_semantic(slot.node_blend_step);
            add_semantic(slot.node_blend_rate);
            add_semantic(slot.node_blend_timer);
            add_semantic(slot.cached_local_x);
            add_semantic(slot.cached_local_y);
            add_semantic(slot.cached_local_z);
            add_semantic(slot.cached_local_w);
            add_semantic(slot.cached_world_x);
            add_semantic(slot.cached_world_y);
            add_semantic(slot.cached_world_z);
            add_semantic(slot.cached_world_w);
            add_semantic(slot.lane_gate);
            add_semantic(slot.lane_rate);
            add_semantic(slot.cache_readable);
            add_semantic(slot.lane_readable);
            if (semantic_cursor != semantic_bytes.size())
            {
                semantic.value = 0;
            }
            result.slot_semantic_hash[index] = semantic.value;
            combined.add_scalar(result.slot_semantic_hash[index]);
        }
        result.combined_semantic_hash = combined.value;
        return result;
    }

    static inline void AddRollbackHitCueRootMotionTraceFields(
        ReplayTraceFields& fields,
        const char* prefix,
        const ReplayScrubDiag::CharaMoveVmSnap& fighter) noexcept
    {
        const std::string player(prefix ? prefix : "p");
        const auto key = [&player](const char* suffix) {
            return player + suffix;
        };
        const RollbackHitCueRootMotionDigest digest =
            BuildRollbackHitCueRootMotionDigest(fighter);
        const auto matrix_float = [](
                const std::array<uint8_t,
                    ReplayScrubDiag::kBoneMatrixBytes>& matrix,
                size_t offset) noexcept {
            float value = 0.0f;
            if (offset + sizeof(value) <= matrix.size())
                std::memcpy(&value, matrix.data() + offset, sizeof(value));
            return value;
        };
        const auto matrix_hash = [](
                const std::array<uint8_t,
                    ReplayScrubDiag::kBoneMatrixBytes>& matrix) noexcept {
            return RollbackHashBytes(matrix.data(), matrix.size());
        };
        fields.hex(key("_root_motion_semantic_hash").c_str(),
                digest.combined_semantic_hash)
            .boolean(key("_hit_cue_pose_pack_readable").c_str(),
                digest.pose_pack_readable)
            .hex(key("_hit_cue_pose_pack_hash").c_str(),
                digest.pose_pack_hash)
            .hex(key("_hit_cue_pose_pack_raw_hash").c_str(),
                digest.pose_pack_raw_hash)
            .boolean(key("_root_bone_matrices_readable").c_str(),
                fighter.root_motion_bone.readable)
            .hex(key("_root_bone_current_hash").c_str(),
                fighter.root_motion_bone.readable
                    ? matrix_hash(fighter.root_motion_bone.current) : 0)
            .hex(key("_root_bone_previous_hash").c_str(),
                fighter.root_motion_bone.readable
                    ? matrix_hash(fighter.root_motion_bone.previous) : 0);
        for (size_t word = 0; word < 16; ++word)
        {
            const std::string suffix =
                "_m" + std::to_string(word / 4)
                + std::to_string(word % 4);
            fields.real(
                    key(("_root_bone_current" + suffix).c_str()).c_str(),
                    matrix_float(
                        fighter.root_motion_bone.current,
                        word * sizeof(float)))
                .real(
                    key(("_root_bone_previous" + suffix).c_str()).c_str(),
                    matrix_float(
                        fighter.root_motion_bone.previous,
                        word * sizeof(float)));
        }
        for (size_t index = 0;
             index < fighter.hit_cue_pose_lanes.size(); ++index)
        {
            const auto& lane = fighter.hit_cue_pose_lanes[index];
            const std::string lane_prefix =
                player + "_hitcue_pose_lane" + std::to_string(index);
            fields.hex((lane_prefix + "_semantic_hash").c_str(),
                    digest.pose_lane_semantic_hash[index])
                .boolean((lane_prefix + "_readable").c_str(),
                    lane.readable)
                .hex((lane_prefix + "_motion_bank_identity").c_str(),
                    lane.motion_bank_identity)
                .integer((lane_prefix + "_motion_clip_index").c_str(),
                    lane.motion_clip_index)
                .real((lane_prefix + "_sample_frame").c_str(),
                    lane.sample_frame)
                .hex((lane_prefix + "_active_flags").c_str(),
                    lane.active_flags)
                .real((lane_prefix + "_active_weight").c_str(),
                    lane.active_weight)
                .real((lane_prefix + "_sampler_yaw").c_str(),
                    lane.sampler_yaw)
                .real((lane_prefix + "_sampler_pitch").c_str(),
                    lane.sampler_pitch)
                .real((lane_prefix + "_scale_x").c_str(),
                    lane.scale_x)
                .real((lane_prefix + "_scale_y").c_str(),
                    lane.scale_y)
                .real((lane_prefix + "_scale_z").c_str(),
                    lane.scale_z)
                .real((lane_prefix + "_scale_w").c_str(),
                    lane.scale_w);
        }
        for (size_t index = 0;
             index < fighter.hit_cue_slots.size(); ++index)
        {
            const auto& slot = fighter.hit_cue_slots[index];
            const std::string slot_prefix =
                player + "_hitcue" + std::to_string(index);
            std::array<
                char,
                RollbackHitCueRootMotionDigest::kSlotSemanticBytes * 2 + 1>
                    semantic_hex {};
            static constexpr char kHex[] = "0123456789ABCDEF";
            for (size_t byte_index = 0;
                 byte_index < digest.slot_semantic_bytes[index].size();
                 ++byte_index)
            {
                const uint8_t value =
                    digest.slot_semantic_bytes[index][byte_index];
                semantic_hex[byte_index * 2] = kHex[value >> 4u];
                semantic_hex[byte_index * 2 + 1] =
                    kHex[value & 0x0Fu];
            }
            fields.hex((slot_prefix + "_semantic_hash").c_str(),
                    digest.slot_semantic_hash[index])
                .string((slot_prefix + "_semantic_bytes").c_str(),
                    semantic_hex.data())
                .integer((slot_prefix + "_active_cue").c_str(),
                    slot.active_cue)
                .integer((slot_prefix + "_multiplier_index").c_str(),
                    slot.multiplier_index)
                .integer((slot_prefix + "_pose_mode").c_str(),
                    slot.pose_mode)
                .uinteger((slot_prefix + "_frame_counter").c_str(),
                    slot.frame_counter)
                .integer((slot_prefix + "_loop_flag").c_str(),
                    slot.loop_flag)
                .integer((slot_prefix + "_end_reached").c_str(),
                    slot.end_reached)
                .real((slot_prefix + "_node_frame").c_str(),
                    slot.node_frame)
                .real((slot_prefix + "_node_segment_end").c_str(),
                    slot.node_segment_end)
                .real((slot_prefix + "_node_loop_span").c_str(),
                    slot.node_loop_span)
                .real((slot_prefix + "_node_prev_frame").c_str(),
                    slot.node_prev_frame)
                .real((slot_prefix + "_weight_gate").c_str(),
                    slot.weight_gate)
                .real((slot_prefix + "_node_blend").c_str(),
                    slot.node_blend)
                .real((slot_prefix + "_node_blend_target").c_str(),
                    slot.node_blend_target)
                .real((slot_prefix + "_node_blend_step").c_str(),
                    slot.node_blend_step)
                .real((slot_prefix + "_node_blend_rate").c_str(),
                    slot.node_blend_rate)
                .real((slot_prefix + "_node_blend_timer").c_str(),
                    slot.node_blend_timer)
                .real((slot_prefix + "_cached_local_x").c_str(),
                    slot.cached_local_x)
                .real((slot_prefix + "_cached_local_y").c_str(),
                    slot.cached_local_y)
                .real((slot_prefix + "_cached_local_z").c_str(),
                    slot.cached_local_z)
                .real((slot_prefix + "_cached_local_w").c_str(),
                    slot.cached_local_w)
                .real((slot_prefix + "_cached_world_x").c_str(),
                    slot.cached_world_x)
                .real((slot_prefix + "_cached_world_y").c_str(),
                    slot.cached_world_y)
                .real((slot_prefix + "_cached_world_z").c_str(),
                    slot.cached_world_z)
                .real((slot_prefix + "_cached_world_w").c_str(),
                    slot.cached_world_w)
                .boolean((slot_prefix + "_cache_readable").c_str(),
                    slot.cache_readable)
                .boolean((slot_prefix + "_lane_readable").c_str(),
                    slot.lane_readable)
                .hex((slot_prefix + "_lane_gate").c_str(),
                    slot.lane_gate)
                .real((slot_prefix + "_lane_rate").c_str(),
                    slot.lane_rate);
        }
    }

    struct RollbackRoundResultCinematicEvidence
    {
        static constexpr uintptr_t kActiveSessionDataPtrRva = 0x4843F00;
        static constexpr uintptr_t kBlockInteractiveOpsRva = 0x48463F8;
        static constexpr uintptr_t kStateOffset = 0xAA120;
        static constexpr uintptr_t kRingCountOffset = 0x468;
        static constexpr uintptr_t kRingCursorOffset = 0x46C;
        static constexpr uintptr_t kRingTagsOffset = 0x470;
        static constexpr uintptr_t kCurrentFrameOffset = 0x484;
        static constexpr uintptr_t kPalette0StateOffset = 0xF0518;
        static constexpr uintptr_t kPalette0FrameOffset = 0xF051C;
        static constexpr uintptr_t kPalette1StateOffset = 0xF0528;
        static constexpr uintptr_t kPalette1FrameOffset = 0xF052C;
        static constexpr uintptr_t kFrameCounterOffset = 0xF0598;

        uintptr_t session_identity {0};
        int32_t state {-1};
        int32_t trigger {-1};
        int32_t aux_trigger {-1};
        int32_t head_frame_counter {-1};
        int32_t ring_count {-1};
        int32_t ring_cursor {-1};
        std::array<int32_t, 5> ring_tags {{
            -1, -1, -1, -1, -1}};
        int32_t current_frame {-1};
        int32_t palette_state[2] {-1, -1};
        int32_t palette_frame[2] {-1, -1};
        int32_t cinematic_frame_counter {-1};
        uint32_t block_interactive_ops {0xFFFFFFFFu};
        uint64_t phase_hash {0};
        bool readable {false};
    };

    static inline uint64_t HashRollbackRoundResultCinematicEvidence(
        const RollbackRoundResultCinematicEvidence& evidence) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(evidence.state);
        hash.add_scalar(evidence.trigger);
        hash.add_scalar(evidence.aux_trigger);
        hash.add_scalar(evidence.head_frame_counter);
        hash.add_scalar(evidence.ring_count);
        hash.add_scalar(evidence.ring_cursor);
        for (int32_t tag : evidence.ring_tags)
            hash.add_scalar(tag);
        hash.add_scalar(evidence.current_frame);
        hash.add_scalar(evidence.palette_state[0]);
        hash.add_scalar(evidence.palette_frame[0]);
        hash.add_scalar(evidence.palette_state[1]);
        hash.add_scalar(evidence.palette_frame[1]);
        hash.add_scalar(evidence.cinematic_frame_counter);
        hash.add_scalar(evidence.block_interactive_ops);
        return hash.value;
    }

    static inline bool CaptureRollbackRoundResultCinematicEvidence(
        uintptr_t image_base,
        RollbackRoundResultCinematicEvidence& evidence) noexcept
    {
        evidence = {};
        if (!image_base)
            return false;
        void* session = nullptr;
        if (!SafeReadPtr(reinterpret_cast<const void*>(
                image_base
                    + RollbackRoundResultCinematicEvidence::
                        kActiveSessionDataPtrRva),
                &session)
            || !session)
        {
            return false;
        }
        evidence.session_identity =
            reinterpret_cast<uintptr_t>(session);
        const uintptr_t state = evidence.session_identity
            + RollbackRoundResultCinematicEvidence::kStateOffset;
        const bool ok =
            SafeReadBytes(reinterpret_cast<const void*>(state + 0x00),
                &evidence.state, sizeof(evidence.state))
            && SafeReadBytes(reinterpret_cast<const void*>(state + 0x04),
                &evidence.trigger, sizeof(evidence.trigger))
            && SafeReadBytes(reinterpret_cast<const void*>(state + 0x08),
                &evidence.aux_trigger, sizeof(evidence.aux_trigger))
            && SafeReadBytes(reinterpret_cast<const void*>(state + 0x0C),
                &evidence.head_frame_counter,
                sizeof(evidence.head_frame_counter))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    state + RollbackRoundResultCinematicEvidence::
                        kRingCountOffset),
                &evidence.ring_count, sizeof(evidence.ring_count))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    state + RollbackRoundResultCinematicEvidence::
                        kRingCursorOffset),
                &evidence.ring_cursor, sizeof(evidence.ring_cursor))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    state + RollbackRoundResultCinematicEvidence::
                        kRingTagsOffset),
                evidence.ring_tags.data(),
                sizeof(evidence.ring_tags))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    state + RollbackRoundResultCinematicEvidence::
                        kCurrentFrameOffset),
                &evidence.current_frame, sizeof(evidence.current_frame))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    state + RollbackRoundResultCinematicEvidence::
                        kPalette0StateOffset),
                &evidence.palette_state[0],
                sizeof(evidence.palette_state[0]))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    state + RollbackRoundResultCinematicEvidence::
                        kPalette0FrameOffset),
                &evidence.palette_frame[0],
                sizeof(evidence.palette_frame[0]))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    state + RollbackRoundResultCinematicEvidence::
                        kPalette1StateOffset),
                &evidence.palette_state[1],
                sizeof(evidence.palette_state[1]))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    state + RollbackRoundResultCinematicEvidence::
                        kPalette1FrameOffset),
                &evidence.palette_frame[1],
                sizeof(evidence.palette_frame[1]))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    state + RollbackRoundResultCinematicEvidence::
                        kFrameCounterOffset),
                &evidence.cinematic_frame_counter,
                sizeof(evidence.cinematic_frame_counter))
            && SafeReadBytes(reinterpret_cast<const void*>(
                    image_base
                        + RollbackRoundResultCinematicEvidence::
                            kBlockInteractiveOpsRva),
                &evidence.block_interactive_ops,
                sizeof(evidence.block_interactive_ops));
        evidence.readable = ok;
        if (ok)
        {
            evidence.phase_hash =
                HashRollbackRoundResultCinematicEvidence(evidence);
        }
        return ok;
    }

    static inline void AddRollbackRoundResultCinematicTraceFields(
        ReplayTraceFields& fields,
        const RollbackRoundResultCinematicEvidence& evidence) noexcept
    {
        fields.boolean("round_result_cinematic_readable",
                evidence.readable)
            .hex("round_result_cinematic_phase_hash",
                evidence.phase_hash)
            .integer("round_result_cinematic_state",
                evidence.state)
            .integer("round_result_cinematic_trigger",
                evidence.trigger)
            .integer("round_result_cinematic_aux_trigger",
                evidence.aux_trigger)
            .integer("round_result_cinematic_head_frame_counter",
                evidence.head_frame_counter)
            .integer("round_result_cinematic_ring_count",
                evidence.ring_count)
            .integer("round_result_cinematic_ring_cursor",
                evidence.ring_cursor)
            .integer("round_result_cinematic_current_frame",
                evidence.current_frame)
            .integer("round_result_cinematic_palette0_state",
                evidence.palette_state[0])
            .integer("round_result_cinematic_palette0_frame",
                evidence.palette_frame[0])
            .integer("round_result_cinematic_palette1_state",
                evidence.palette_state[1])
            .integer("round_result_cinematic_palette1_frame",
                evidence.palette_frame[1])
            .integer("round_result_cinematic_frame_counter",
                evidence.cinematic_frame_counter)
            .hex("round_result_cinematic_block_interactive_ops",
                evidence.block_interactive_ops);
        for (size_t index = 0; index < evidence.ring_tags.size(); ++index)
        {
            const std::string key =
                "round_result_cinematic_ring_tag_"
                + std::to_string(index);
            fields.integer(key.c_str(), evidence.ring_tags[index]);
        }
    }
}
