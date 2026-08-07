#include "RollbackConsumedInputSidecar.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
    void put_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    void put_u64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    void append_inputs(std::vector<uint8_t>& bytes,
                       const std::vector<uint32_t>& values)
    {
        const size_t begin = bytes.size();
        bytes.resize(begin + values.size() * sizeof(uint32_t));
        std::memcpy(bytes.data() + begin, values.data(),
                    values.size() * sizeof(uint32_t));
    }

    std::vector<uint8_t> make_sidecar()
    {
        constexpr size_t kHeader = 160;
        constexpr size_t kDescriptor = 2160;
        const std::array<std::array<std::vector<uint32_t>, 2>, 2> rounds {{
            {{{0, 0, 0x10, 0x10}, {0, 0x400, 0x400, 0}}},
            {{{0x40, 0x40, 0}, {0, 0x8, 0x8}}},
        }};
        // Fixed vectors produced by the established Python sidecar builder.
        constexpr uint64_t hashes[2][2] = {
            {0x9275583628419763ull, 0x50B9C0D7534AE903ull},
            {0x55570E932179B993ull, 0xBB28C0C4CF8A8213ull},
        };
        std::vector<uint8_t> bytes(kHeader + rounds.size() * kDescriptor, 0);
        const char magic[8] = {'H', 'M', 'R', 'I', 'N', 'P', '4', '\0'};
        std::memcpy(bytes.data(), magic, sizeof(magic));
        put_u32(bytes, 8, 4);
        put_u32(bytes, 12, static_cast<uint32_t>(kHeader));
        put_u32(bytes, 16, static_cast<uint32_t>(rounds.size()));
        put_u32(bytes, 24, 0x311);
        put_u32(bytes, 28, 14);
        put_u32(bytes, 32, 3);
        put_u32(bytes, 36, 0xEFC102DE);
        put_u64(bytes, 40, 0x1135D62F163558E1ull);
        put_u64(bytes, 48, 0x123456789ABCDEF0ull);
        for (size_t offset = 56; offset < 152; ++offset)
            bytes[offset] = static_cast<uint8_t>(offset + 1);
        put_u32(bytes, 152, static_cast<uint32_t>(kDescriptor));
        for (size_t round = 0; round < rounds.size(); ++round)
        {
            const size_t offset = kHeader + round * kDescriptor;
            put_u32(bytes, offset, static_cast<uint32_t>(round));
            put_u32(bytes, offset + 4,
                    static_cast<uint32_t>(rounds[round][0].size()));
            put_u64(bytes, offset + 8, hashes[round][0]);
            put_u64(bytes, offset + 16, hashes[round][1]);
            put_u32(bytes, offset + 24,
                    0x10203040u + static_cast<uint32_t>(round));
            put_u32(bytes, offset + 28,
                    round == 0 ? 25u : 0u);
            for (size_t i = 0; i < 100; ++i)
            {
                bytes[offset + 32 + i] = static_cast<uint8_t>(
                    1u + round * 17u + i);
            }
            put_u64(bytes, offset + 132,
                    Horse::RollbackConsumedInputFnv1a64(
                        bytes.data() + offset + 32, 100));
            const uint32_t gameplay_seed =
                0xEFC102DEu + static_cast<uint32_t>(round);
            put_u32(bytes, offset + 140,
                    0x55667788u + static_cast<uint32_t>(round));
            put_u32(bytes, offset + 144, gameplay_seed);
            put_u32(bytes, offset + 148,
                    9u + static_cast<uint32_t>(round));
            put_u32(bytes, offset + 152, gameplay_seed & 0xfffu);
            Horse::RollbackMotionPoseResidueSnapshot pose {};
            for (size_t i = 0; i < pose.retained.size(); ++i)
            {
                pose.retained[i] = static_cast<uint8_t>(
                    3u + round * 29u + i);
            }
            pose.valid = 1;
            pose.last_player = static_cast<int8_t>(round & 1u);
            bytes[offset + 160] = pose.valid;
            std::memcpy(bytes.data() + offset + 161,
                        &pose.last_player, sizeof(pose.last_player));
            put_u64(bytes, offset + 164,
                    Horse::HashRollbackMotionPoseResidueSnapshot(pose));
            std::memcpy(bytes.data() + offset + 172,
                        pose.retained.data(), pose.retained.size());
        }
        for (const auto& round : rounds)
        {
            append_inputs(bytes, round[0]);
            append_inputs(bytes, round[1]);
        }
        return bytes;
    }
}

int main()
{
    const auto sidecar = make_sidecar();
    Horse::RollbackConsumedInputSidecar script {};
    const auto extract = [](const std::vector<uint8_t>& bytes,
                            Horse::RollbackConsumedInputSidecar& out) {
        return Horse::RollbackConsumedInputSidecarCodec::extract(bytes, out);
    };
    if (!extract(sidecar, script)
        || !script.ok
        || script.source_build_id != 0x1135D62F163558E1ull
        || script.source_schema_id != 0x123456789ABCDEF0ull
        || !Horse::RollbackConsumedInputSidecarIdentityMatches(
            script, 0x1135D62F163558E1ull, 0x123456789ABCDEF0ull)
        || Horse::RollbackConsumedInputSidecarIdentityMatches(
            script, 0x1135D62F163558E1ull, 0x123456789ABCDEEFull)
        || script.random_seed != 0xEFC102DE
        || script.stage_index != 0x311
        || script.left_chara_id != 14
        || script.right_chara_id != 3
        || script.round_pairs.size() != 2
        || script.round_pairs[0].frame_count != 4
        || script.round_pairs[1].frame_count != 3
        || script.round_pairs[0].rng_baseline.lcg_state != 0x10203040u
        || script.round_pairs[0].rng_baseline.lfsr_index != 25
        || script.round_pairs[1].rng_baseline.lfsr_index != 0
        || script.round_pairs[0].rng_baseline.gameplay_crt_state
            != 0x55667788u
        || script.round_pairs[0].rng_baseline.gameplay_crt_seed
            != 0xEFC102DEu
        || script.round_pairs[0].rng_baseline.gameplay_crt_draw_ordinal != 9
        || script.round_pairs[0].motion_pose_residue.valid != 1
        || script.round_pairs[0].motion_pose_residue.last_player != 0
        || script.round_pairs[1].motion_pose_residue.last_player != 1
        || !Horse::RollbackReplayMotionPoseBaselineValid(
            script.round_pairs[0].motion_pose_residue,
            script.round_pairs[0].motion_pose_residue_hash)
        || !Horse::RollbackReplayRngBaselineValid(
            script.round_pairs[0].rng_baseline)
        || !Horse::RollbackReplayRngBaselineValid(
            script.round_pairs[1].rng_baseline)
        || script.player_inputs[0]
            != std::vector<uint32_t>({0, 0, 0x10, 0x10, 0x40, 0x40, 0})
        || script.player_inputs[1]
            != std::vector<uint32_t>({0, 0x400, 0x400, 0, 0, 0x8, 0x8}))
    {
        std::cerr << "valid consumed-input sidecar rejected: "
                  << script.failure << '\n';
        return 1;
    }

    auto corrupt = sidecar;
    corrupt[corrupt.size() - sizeof(uint32_t)] ^= 1;
    if (extract(corrupt, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-round-hash-mismatch") != 0)
    {
        std::cerr << "corrupt sidecar hash accepted\n";
        return 1;
    }

    auto zero_schema = sidecar;
    put_u64(zero_schema, 48, 0);
    if (extract(zero_schema, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-identity-invalid") != 0)
    {
        std::cerr << "zero schema accepted\n";
        return 1;
    }

    auto invalid_stage = sidecar;
    put_u32(invalid_stage, 24, 0x1000);
    if (extract(invalid_stage, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-identity-invalid") != 0)
    {
        std::cerr << "out-of-range packed stage accepted\n";
        return 1;
    }

    auto high_bits = sidecar;
    put_u32(high_bits, 160 + 2 * 2160, 0x4000);
    if (extract(high_bits, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-input-invalid") != 0)
    {
        std::cerr << "high compact-input bit accepted\n";
        return 1;
    }

    auto wrong_round = sidecar;
    put_u32(wrong_round, 160 + 2160, 7);
    if (extract(wrong_round, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-round-invalid") != 0)
    {
        std::cerr << "noncontiguous round index accepted\n";
        return 1;
    }

    auto corrupt_rng = sidecar;
    corrupt_rng[160 + 32] ^= 1;
    if (extract(corrupt_rng, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-round-invalid") != 0)
    {
        std::cerr << "corrupt RNG baseline accepted\n";
        return 1;
    }

    auto invalid_rng_index = sidecar;
    put_u32(invalid_rng_index, 160 + 28, 26);
    if (extract(invalid_rng_index, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-round-invalid") != 0)
    {
        std::cerr << "invalid RNG index accepted\n";
        return 1;
    }

    auto invalid_gameplay_crt_warmup = sidecar;
    put_u32(invalid_gameplay_crt_warmup, 160 + 152, 0);
    if (extract(invalid_gameplay_crt_warmup, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-round-invalid") != 0)
    {
        std::cerr << "invalid gameplay CRT baseline accepted\n";
        return 1;
    }

    auto corrupt_pose = sidecar;
    corrupt_pose[160 + 172] ^= 1;
    if (extract(corrupt_pose, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-round-invalid") != 0)
    {
        std::cerr << "corrupt motion-pose baseline accepted\n";
        return 1;
    }

    auto invalid_pose_owner = sidecar;
    invalid_pose_owner[160 + 161] = 2;
    if (extract(invalid_pose_owner, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-round-invalid") != 0)
    {
        std::cerr << "invalid motion-pose owner accepted\n";
        return 1;
    }

    auto legacy_v3 = sidecar;
    legacy_v3[6] = '3';
    put_u32(legacy_v3, 8, 3);
    if (extract(legacy_v3, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-magic-invalid") != 0)
    {
        std::cerr << "legacy v3 sidecar accepted\n";
        return 1;
    }

    auto truncated = sidecar;
    truncated.pop_back();
    if (extract(truncated, script)
        || std::strcmp(script.failure,
                       "consumed-input-sidecar-size-mismatch") != 0)
    {
        std::cerr << "truncated sidecar accepted\n";
        return 1;
    }

    std::cout << "rollback replay input sidecar selftest: PASS\n";
    return 0;
}
