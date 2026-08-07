#pragma once

#include "RollbackMotionPoseResidue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace Horse
{
    struct RollbackReplayRngBaseline
    {
        uint32_t lcg_state {0};
        uint32_t lfsr_index {0};
        std::array<uint8_t, 100> lfsr_state {};
        uint64_t lfsr_hash {0};
        uint32_t gameplay_crt_state {0};
        uint32_t gameplay_crt_seed {0};
        uint32_t gameplay_crt_draw_ordinal {0};
        uint32_t gameplay_crt_warmup_draws {0};
    };

    struct RollbackReplayInputRoundPair
    {
        uint32_t player0_offset {0};
        uint32_t player1_offset {0};
        uint32_t byte_count {0};
        uint32_t frame_count {0};
        uint64_t player0_hash {0};
        uint64_t player1_hash {0};
        RollbackReplayRngBaseline rng_baseline {};
        RollbackMotionPoseResidueSnapshot motion_pose_residue {};
        uint64_t motion_pose_residue_hash {0};
    };

    struct RollbackConsumedInputSidecar
    {
        bool ok {false};
        uint64_t file_bytes {0};
        uint64_t file_hash {0};
        uint64_t payload_bytes {0};
        uint64_t payload_hash {0};
        uint64_t input_frames_p0 {0};
        uint64_t input_frames_p1 {0};
        uint64_t input_hash_p0 {0};
        uint64_t input_hash_p1 {0};
        int32_t stage_index {-1};
        int32_t left_chara_id {-1};
        int32_t right_chara_id {-1};
        uint32_t random_seed {0};
        uint64_t source_build_id {0};
        uint64_t source_schema_id {0};
        std::array<uint8_t, 32> source_replay_sha256 {};
        std::array<uint8_t, 32> source_oracle_sha256 {};
        std::array<uint8_t, 32> bound_artifact_sha256 {};
        const char* failure {"not-run"};
        std::array<std::vector<uint32_t>, 2> player_inputs {};
        std::vector<RollbackReplayInputRoundPair> round_pairs {};
    };

    static inline uint64_t RollbackConsumedInputFnv1a64(
        const void* data, size_t size) noexcept
    {
        // Wire-compatible with ReplayTraceFields and the Python builder.
        uint64_t hash = 1469598103934665603ull;
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; bytes && i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static inline bool RollbackReplayRngBaselineValid(
        const RollbackReplayRngBaseline& baseline) noexcept
    {
        bool any_state = false;
        for (const uint8_t byte : baseline.lfsr_state)
            any_state = any_state || byte != 0;
        return any_state && baseline.lfsr_index <= 25
            && baseline.lfsr_hash != 0
            && baseline.gameplay_crt_seed != 0
            && baseline.gameplay_crt_warmup_draws
                == (baseline.gameplay_crt_seed & 0xfffu)
            && RollbackConsumedInputFnv1a64(
                baseline.lfsr_state.data(), baseline.lfsr_state.size())
                == baseline.lfsr_hash;
    }

    static inline bool RollbackReplayMotionPoseBaselineValid(
        const RollbackMotionPoseResidueSnapshot& baseline,
        uint64_t expected_hash) noexcept
    {
        return baseline.valid == 1
            && ValidateRollbackMotionPoseResidueSnapshot(baseline)
            && expected_hash != 0
            && HashRollbackMotionPoseResidueSnapshot(baseline)
                == expected_hash;
    }

    static inline bool RollbackConsumedInputSidecarHasMagic(
        const std::vector<uint8_t>& bytes) noexcept
    {
        static constexpr char kMagic[8] = {
            'H', 'M', 'R', 'I', 'N', 'P', '4', '\0'
        };
        return bytes.size() >= sizeof(kMagic)
            && std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) == 0;
    }

    static inline bool RollbackConsumedInputSidecarIdentityMatches(
        const RollbackConsumedInputSidecar& sidecar,
        uint64_t expected_build_id,
        uint64_t expected_schema_id) noexcept
    {
        return sidecar.ok && expected_build_id != 0 && expected_schema_id != 0
            && sidecar.source_build_id == expected_build_id
            && sidecar.source_schema_id == expected_schema_id;
    }

    class RollbackConsumedInputSidecarCodec
    {
    public:
        static bool extract(const std::vector<uint8_t>& bytes,
                            RollbackConsumedInputSidecar& out) noexcept
        {
            out = {};
            out.file_bytes = bytes.size();
            out.file_hash = bytes.empty()
                ? 0 : RollbackConsumedInputFnv1a64(bytes.data(), bytes.size());
            if (!RollbackConsumedInputSidecarHasMagic(bytes))
                return fail(out, "consumed-input-sidecar-magic-invalid");
            if (bytes.size() < kHeaderBytes)
                return fail(out, "consumed-input-sidecar-header-truncated");

            const uint32_t version = read_u32(bytes.data() + 8);
            const uint32_t header_bytes = read_u32(bytes.data() + 12);
            const uint32_t round_count = read_u32(bytes.data() + 16);
            const uint32_t flags = read_u32(bytes.data() + 20);
            const uint32_t descriptor_bytes = read_u32(bytes.data() + 152);
            const uint32_t reserved = read_u32(bytes.data() + 156);
            if (version != kVersion || header_bytes != kHeaderBytes
                || descriptor_bytes != kRoundBytes || round_count == 0
                || round_count > kMaximumRounds || flags != 0 || reserved != 0)
            {
                return fail(out, "consumed-input-sidecar-header-invalid");
            }

            out.stage_index = read_i32(bytes.data() + 24);
            out.left_chara_id = read_i32(bytes.data() + 28);
            out.right_chara_id = read_i32(bytes.data() + 32);
            out.random_seed = read_u32(bytes.data() + 36);
            out.source_build_id = read_u64(bytes.data() + 40);
            out.source_schema_id = read_u64(bytes.data() + 48);
            std::memcpy(out.source_replay_sha256.data(), bytes.data() + 56, 32);
            std::memcpy(out.source_oracle_sha256.data(), bytes.data() + 88, 32);
            std::memcpy(out.bound_artifact_sha256.data(), bytes.data() + 120, 32);
            if (out.stage_index < 0 || out.stage_index > 0xFFF
                || out.left_chara_id < 0 || out.left_chara_id > 63
                || out.right_chara_id < 0 || out.right_chara_id > 63
                || out.random_seed == 0 || out.source_build_id == 0
                || out.source_schema_id == 0
                || !digest_nonzero(out.source_replay_sha256)
                || !digest_nonzero(out.source_oracle_sha256)
                || !digest_nonzero(out.bound_artifact_sha256))
            {
                return fail(out, "consumed-input-sidecar-identity-invalid");
            }

            const uint64_t descriptor_end = static_cast<uint64_t>(header_bytes)
                + static_cast<uint64_t>(round_count) * descriptor_bytes;
            if (descriptor_end > bytes.size())
                return fail(out, "consumed-input-sidecar-descriptors-truncated");

            uint64_t expected_size = descriptor_end;
            try
            {
                out.round_pairs.reserve(round_count);
                for (uint32_t round = 0; round < round_count; ++round)
                {
                    const size_t offset = static_cast<size_t>(header_bytes)
                        + static_cast<size_t>(round) * descriptor_bytes;
                    const uint32_t encoded_round = read_u32(bytes.data() + offset);
                    const uint32_t frame_count = read_u32(bytes.data() + offset + 4);
                    const uint64_t hash0 = read_u64(bytes.data() + offset + 8);
                    const uint64_t hash1 = read_u64(bytes.data() + offset + 16);
                    RollbackReplayRngBaseline rng {};
                    rng.lcg_state = read_u32(bytes.data() + offset + 24);
                    rng.lfsr_index = read_u32(bytes.data() + offset + 28);
                    std::memcpy(rng.lfsr_state.data(),
                        bytes.data() + offset + 32, rng.lfsr_state.size());
                    rng.lfsr_hash = read_u64(bytes.data() + offset + 132);
                    rng.gameplay_crt_state =
                        read_u32(bytes.data() + offset + 140);
                    rng.gameplay_crt_seed =
                        read_u32(bytes.data() + offset + 144);
                    rng.gameplay_crt_draw_ordinal =
                        read_u32(bytes.data() + offset + 148);
                    rng.gameplay_crt_warmup_draws =
                        read_u32(bytes.data() + offset + 152);
                    const uint32_t round_reserved =
                        read_u32(bytes.data() + offset + 156);
                    RollbackMotionPoseResidueSnapshot pose {};
                    pose.valid = read_u8(bytes.data() + offset + 160);
                    pose.last_player = read_i8(bytes.data() + offset + 161);
                    const uint16_t pose_reserved =
                        read_u16(bytes.data() + offset + 162);
                    const uint64_t pose_hash =
                        read_u64(bytes.data() + offset + 164);
                    std::memcpy(pose.retained.data(),
                        bytes.data() + offset + 172,
                        pose.retained.size());
                    // V6 sidecars have no storage for the later recovered
                    // nine SolveBonePose cache-reuse decision bytes. Seed the
                    // deterministic no-source/reuse path; native selector
                    // processing overwrites any authored decisions.
                    pose.extra_bone_cache_reuse.fill(1);
                    const uint32_t trailing_reserved =
                        read_u32(bytes.data() + offset + 2156);
                    if (encoded_round != round || frame_count == 0
                        || frame_count > kMaximumFramesPerRound
                        || hash0 == 0 || hash1 == 0 || round_reserved != 0
                        || pose_reserved != 0 || trailing_reserved != 0
                        || !RollbackReplayRngBaselineValid(rng)
                        || !RollbackReplayMotionPoseBaselineValid(
                            pose, pose_hash))
                    {
                        return fail(out, "consumed-input-sidecar-round-invalid");
                    }
                    const uint64_t round_bytes = static_cast<uint64_t>(frame_count)
                        * sizeof(uint32_t) * 2u;
                    if (expected_size > UINT64_MAX - round_bytes)
                        return fail(out, "consumed-input-sidecar-size-overflow");
                    expected_size += round_bytes;
                    RollbackReplayInputRoundPair pair {};
                    pair.byte_count = frame_count
                        * static_cast<uint32_t>(sizeof(uint32_t));
                    pair.frame_count = frame_count;
                    pair.player0_hash = hash0;
                    pair.player1_hash = hash1;
                    pair.rng_baseline = rng;
                    pair.motion_pose_residue = pose;
                    pair.motion_pose_residue_hash = pose_hash;
                    out.round_pairs.push_back(pair);
                }
                if (expected_size != bytes.size())
                    return fail(out, "consumed-input-sidecar-size-mismatch");

                size_t payload_offset = static_cast<size_t>(descriptor_end);
                for (auto& pair : out.round_pairs)
                {
                    pair.player0_offset = static_cast<uint32_t>(payload_offset);
                    for (uint32_t player = 0; player < 2; ++player)
                    {
                        if (player == 1)
                            pair.player1_offset = static_cast<uint32_t>(payload_offset);
                        auto& stream = out.player_inputs[player];
                        const size_t begin = stream.size();
                        stream.reserve(begin + pair.frame_count);
                        for (uint32_t frame = 0; frame < pair.frame_count; ++frame)
                        {
                            const uint32_t input = read_u32(bytes.data() + payload_offset);
                            if (input > kMaximumInput)
                                return fail(out, "consumed-input-sidecar-input-invalid");
                            stream.push_back(input);
                            payload_offset += sizeof(uint32_t);
                        }
                        const uint64_t observed = RollbackConsumedInputFnv1a64(
                            stream.data() + begin,
                            pair.frame_count * sizeof(uint32_t));
                        const uint64_t expected = player == 0
                            ? pair.player0_hash : pair.player1_hash;
                        if (observed != expected)
                            return fail(out, "consumed-input-sidecar-round-hash-mismatch");
                    }
                }
            }
            catch (const std::bad_alloc&)
            {
                return fail(out, "consumed-input-sidecar-allocation-failed");
            }

            out.payload_bytes = bytes.size() - descriptor_end;
            out.payload_hash = RollbackConsumedInputFnv1a64(
                bytes.data() + descriptor_end,
                bytes.size() - static_cast<size_t>(descriptor_end));
            out.input_frames_p0 = out.player_inputs[0].size();
            out.input_frames_p1 = out.player_inputs[1].size();
            out.input_hash_p0 = stream_hash(out.player_inputs[0]);
            out.input_hash_p1 = stream_hash(out.player_inputs[1]);
            out.ok = out.input_frames_p0 != 0
                && out.input_frames_p0 == out.input_frames_p1
                && out.input_hash_p0 != 0 && out.input_hash_p1 != 0;
            out.failure = out.ok ? "ok" : "consumed-input-sidecar-streams-empty";
            return out.ok;
        }

    private:
        static constexpr uint32_t kHeaderBytes = 160;
        static constexpr uint32_t kRoundBytes = 2160;
        static constexpr uint32_t kVersion = 4;
        static constexpr uint32_t kMaximumRounds = 64;
        static constexpr uint32_t kMaximumFramesPerRound = 15000;
        static constexpr uint32_t kMaximumInput = 0x3FFF;

        static uint32_t read_u32(const uint8_t* bytes) noexcept
        {
            uint32_t value = 0;
            std::memcpy(&value, bytes, sizeof(value));
            return value;
        }

        static uint8_t read_u8(const uint8_t* bytes) noexcept
        {
            return *bytes;
        }

        static int8_t read_i8(const uint8_t* bytes) noexcept
        {
            int8_t value = 0;
            std::memcpy(&value, bytes, sizeof(value));
            return value;
        }

        static uint16_t read_u16(const uint8_t* bytes) noexcept
        {
            uint16_t value = 0;
            std::memcpy(&value, bytes, sizeof(value));
            return value;
        }

        static int32_t read_i32(const uint8_t* bytes) noexcept
        {
            int32_t value = 0;
            std::memcpy(&value, bytes, sizeof(value));
            return value;
        }

        static uint64_t read_u64(const uint8_t* bytes) noexcept
        {
            uint64_t value = 0;
            std::memcpy(&value, bytes, sizeof(value));
            return value;
        }

        static bool digest_nonzero(const std::array<uint8_t, 32>& digest) noexcept
        {
            for (uint8_t byte : digest) if (byte != 0) return true;
            return false;
        }

        static uint64_t stream_hash(const std::vector<uint32_t>& values) noexcept
        {
            return values.empty() ? 0 : RollbackConsumedInputFnv1a64(
                values.data(), values.size() * sizeof(uint32_t));
        }

        static bool fail(RollbackConsumedInputSidecar& out,
                         const char* reason) noexcept
        {
            out.failure = reason;
            return false;
        }
    };
}
